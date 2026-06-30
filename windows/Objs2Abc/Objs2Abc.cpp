// Converts a numbered OBJ frame sequence into an Alembic (.abc) PolyMesh cache.
// Each frame is written with its own full topology, so sequences whose vertex
// and face counts change over time are preserved rather than frozen to frame 0.

#include <Alembic/AbcCoreOgawa/All.h>
#include <Alembic/AbcGeom/All.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace Alembic::AbcGeom;


static bool has_obj_extension(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".obj";
}


void getFiles(const string& path, std::vector<string>& files)
{
    namespace fs = std::filesystem;

    try
    {
        fs::path root(path);
        if (!fs::exists(root))
        {
            std::cerr << "Input directory does not exist: " << path << std::endl;
            return;
        }

        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root))
        {
            std::error_code ec;
            if (entry.is_regular_file(ec) && !ec && has_obj_extension(entry.path()))
            {
                files.push_back(entry.path().string());
            }
        }
    }
    catch (const fs::filesystem_error& exc)
    {
        std::cerr << "Could not enumerate OBJ files in " << path << ": " << exc.what() << std::endl;
    }
}


struct FrameMesh
{
    std::vector<V3f> positions;
    std::vector<int32_t> faceIndices;
    std::vector<int32_t> faceCounts;
    std::vector<V2f> uvs;
    bool hasUVs = false;
};


static FrameMesh ReadFrameMesh(const std::string& path, size_t vHint, size_t fHint)
{
    FrameMesh m;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Warning: could not open frame: " << path << std::endl;
        return m;
    }

    m.positions.reserve(vHint);
    m.faceIndices.reserve(fHint * 3);
    m.faceCounts.reserve(fHint);
    std::vector<V2f> rawUVs;
    std::vector<int> faceUV;
    faceUV.reserve(fHint * 3);
    bool anyMissingUV = false;

    std::string line;
    while (std::getline(file, line))
    {
        if (line.size() < 2)
        {
            continue;
        }

        const char* p = line.c_str();
        char c0 = p[0];
        char c1 = p[1];

        if (c0 == 'v' && c1 == ' ')
        {
            char* e;
            float x = strtof(p + 2, &e);
            float y = strtof(e, &e);
            float z = strtof(e, nullptr);
            m.positions.push_back(V3f(x, y, z));
        }
        else if (c0 == 'v' && c1 == 't')
        {
            char* e;
            float u = strtof(p + 3, &e);
            float v = strtof(e, nullptr);
            rawUVs.push_back(V2f(u, v));
        }
        else if (c0 == 'f' && c1 == ' ')
        {
            const char* q = p + 2;
            int count = 0;
            while (*q)
            {
                while (*q == ' ' || *q == '\t' || *q == '\r')
                {
                    ++q;
                }
                if (!*q)
                {
                    break;
                }

                char* e;
                long vi = strtol(q, &e, 10);
                if (e == q)
                {
                    ++q;
                    continue;
                }

                int uvi = -1;
                if (*e == '/')
                {
                    const char* u = e + 1;
                    if (*u != '/')
                    {
                        char* e2;
                        long uv = strtol(u, &e2, 10);
                        if (e2 != u)
                        {
                            uvi = static_cast<int>(uv - 1);
                        }
                    }
                }

                q = e;
                while (*q && *q != ' ' && *q != '\t' && *q != '\r')
                {
                    ++q;
                }

                m.faceIndices.push_back(static_cast<int32_t>(vi - 1));
                faceUV.push_back(uvi);
                if (uvi < 0)
                {
                    anyMissingUV = true;
                }
                ++count;
            }
            if (count > 0)
            {
                m.faceCounts.push_back(count);
            }
        }
    }

    if (!rawUVs.empty() && !anyMissingUV)
    {
        m.uvs.assign(faceUV.size(), V2f(0.0f, 0.0f));
        m.hasUVs = true;
        for (size_t i = 0; i < faceUV.size(); ++i)
        {
            int idx = faceUV[i];
            if (idx >= 0 && static_cast<size_t>(idx) < rawUVs.size())
            {
                m.uvs[i] = rawUVs[idx];
            }
            else
            {
                m.hasUVs = false;
                break;
            }
        }
        if (!m.hasUVs)
        {
            m.uvs.clear();
        }
    }

    return m;
}


static bool seq2abc(string inputdir, string outputfile, float fps, std::string NodeName)
{
    std::vector<string> filenames;
    getFiles(inputdir, filenames);
    std::sort(filenames.begin(), filenames.end(), [](const std::string& a, const std::string& b) {
        std::string lowerA = a;
        std::string lowerB = b;
        std::transform(lowerA.begin(), lowerA.end(), lowerA.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        std::transform(lowerB.begin(), lowerB.end(), lowerB.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return lowerA < lowerB;
    });

    if (filenames.empty())
    {
        std::cerr << "No OBJ files found in: " << inputdir << std::endl;
        return false;
    }

    try
    {
        int totalFrames = static_cast<int>(filenames.size());
        std::cout << "Found " << totalFrames << " OBJ file(s) in: " << inputdir << std::endl;
        if (totalFrames == 1)
        {
            std::cout << "Only one OBJ file was found, so the cache will have a single frame." << std::endl;
        }

        OArchive archive(Alembic::AbcCoreOgawa::WriteArchive(), outputfile);
        TimeSamplingPtr ts(new TimeSampling(1.0 / fps, 0.0));
        OXform xfobj(archive.getTop(), NodeName, ts);
        OPolyMesh meshyObj(xfobj, NodeName, ts);
        OPolyMeshSchema& mesh = meshyObj.getSchema();

        FrameMesh first = ReadFrameMesh(filenames[0], 0, 0);
        size_t vHint = first.positions.size();
        size_t fHint = first.faceCounts.size();
        bool writeUVs = first.hasUVs;
        if (writeUVs)
        {
            mesh.setUVSourceName("UVMap");
        }

        int remaining = totalFrames;
        unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
        numThreads = std::min(numThreads, static_cast<unsigned int>(remaining));
        int window = std::max(2, static_cast<int>(numThreads));

        std::vector<FrameMesh> slots(remaining);
        std::vector<char> ready(remaining, 0);
        std::mutex mtx;
        std::condition_variable cv;
        std::atomic<int> nextRead(1);
        int writeIndex = 0;

        slots[0] = std::move(first);
        ready[0] = 1;

        auto worker = [&]() {
            while (true)
            {
                int idx = nextRead.fetch_add(1);
                if (idx >= remaining)
                {
                    break;
                }

                {
                    std::unique_lock<std::mutex> lk(mtx);
                    cv.wait(lk, [&] { return idx < writeIndex + window; });
                }

                FrameMesh data = ReadFrameMesh(filenames[idx], vHint, fHint);
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    slots[idx] = std::move(data);
                    ready[idx] = 1;
                }
                cv.notify_all();
            }
        };

        std::vector<std::thread> workers;
        workers.reserve(numThreads);
        for (unsigned int t = 0; t < numThreads; ++t)
        {
            workers.emplace_back(worker);
        }

        std::vector<V2f> zeroUVs;
        for (int i = 0; i < remaining; ++i)
        {
            FrameMesh fm;
            {
                std::unique_lock<std::mutex> lk(mtx);
                cv.wait(lk, [&] { return ready[i] != 0; });
                fm = std::move(slots[i]);
                writeIndex = i + 1;
            }
            cv.notify_all();

            OPolyMeshSchema::Sample samp(
                V3fArraySample(fm.positions),
                Int32ArraySample(fm.faceIndices),
                Int32ArraySample(fm.faceCounts));

            if (writeUVs)
            {
                if (fm.hasUVs)
                {
                    samp.setUVs(OV2fGeomParam::Sample(V2fArraySample(fm.uvs), kFacevaryingScope));
                }
                else
                {
                    zeroUVs.assign(fm.faceIndices.size(), V2f(0.0f, 0.0f));
                    samp.setUVs(OV2fGeomParam::Sample(V2fArraySample(zeroUVs), kFacevaryingScope));
                }
            }

            mesh.set(samp);
            std::cout << "PROGRESS " << (i + 1) << " " << totalFrames << std::endl;
        }

        for (auto& w : workers)
        {
            w.join();
        }
    }
    catch (const std::exception& exc)
    {
        std::cerr << "Alembic conversion failed: " << exc.what() << std::endl;
        return false;
    }

    return true;
}


static void print_usage(const char* name)
{
    std::cout << "\nUsage: " << name << " [options]" << std::endl
              << "Options:" << std::endl
              << "  -h, --help     help\n"
              << "  -i, --in       obj input dir\n"
              << "  -o, --out      output abc name\n"
              << "  -f, --fps      abc frame rate\n"
              << "  -n, --name     Node Name\n"
              << std::endl;
}


int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    string inputdir;
    string output = "output.abc";
    string NodeName = "NodeName";
    float fps = 24.0f;

    for (int i = 1; i < argc; i++)
    {
        std::string t_arg = std::string(argv[i]);
        if (t_arg == "-h" || t_arg == "--help")
        {
            print_usage(argv[0]);
            return 0;
        }

        bool needsValue = (t_arg == "-i" || t_arg == "--in" ||
                           t_arg == "-o" || t_arg == "--out" ||
                           t_arg == "-f" || t_arg == "--fps" ||
                           t_arg == "-n" || t_arg == "--name");
        if (needsValue && i + 1 >= argc)
        {
            std::cerr << "Missing value for option: " << t_arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }

        if (t_arg == "-i" || t_arg == "--in")
        {
            inputdir = argv[++i];
        }
        else if (t_arg == "-o" || t_arg == "--out")
        {
            output = argv[++i];
        }
        else if (t_arg == "-f" || t_arg == "--fps")
        {
            try
            {
                fps = std::stof(argv[++i]);
            }
            catch (const std::exception&)
            {
                std::cerr << "Invalid frame rate." << std::endl;
                return 1;
            }
        }
        else if (t_arg == "-n" || t_arg == "--name")
        {
            NodeName = argv[++i];
        }
        else
        {
            std::cerr << "Unknown option: " << t_arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    if (inputdir.empty())
    {
        std::cerr << "No input directory given. Use -i <obj_folder>." << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    if (fps <= 0.0f)
    {
        std::cerr << "Frame rate must be greater than zero." << std::endl;
        return 1;
    }

    std::cout << "input dir: " << inputdir << std::endl;
    std::cout << "output abc: " << output << std::endl;
    std::cout << "fps: " << fps << std::endl;
    std::cout << "NodeName: " << NodeName << std::endl;

    return seq2abc(inputdir, output, fps, NodeName) ? 0 : 1;
}
