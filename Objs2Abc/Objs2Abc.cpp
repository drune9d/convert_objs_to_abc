// Alembic Includes
#include <Alembic/AbcGeom/All.h>
#include <Alembic/AbcCoreOgawa/All.h>
#include <algorithm>
// Other includes
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <Vector>
#include <io.h>
#include <algorithm>

#include <cstdio>

// We include some global mesh data to test with from an external source
// to keep this example code clean.

using namespace std;
using namespace Alembic::AbcGeom; // Contains Abc, AbcCoreAbstract


struct vertice
{
	float x;
	float y;
	float z;
};

struct uv
{
	float u;
	float v;
};



void ReadObj(const std::string& path, std::vector<vertice>& OutVertices, std::vector<uv>& OutUvs, std::vector<vertice>& OutNormals, std::vector<int>& OutFaceVertices, std::vector<int>& OutFaceUvs, std::vector<int>& Outgcounts) {
	std::string s, str, s1, s2, s3;
	std::ifstream inf;
	vertice v;
	vertice normal;
	uv u;
	inf.open(path);

	if (inf.is_open()) {
		while (getline(inf, s)) {
			if (s.empty()) continue;
			if (s[0] == 'v' && s.size() > 1) {
				if (s[1] == 't') {
					std::istringstream in(s);
					in >> s1 >> u.u >> u.v;
					OutUvs.push_back(u);
				}
				else if (s[1] == 'n') {
					std::istringstream in(s);
					in >> s1 >> normal.x >> normal.y >> normal.z;
					OutNormals.push_back(normal);
				}
				else {
					std::istringstream in(s);
					in >> s1 >> v.x >> v.y >> v.z;
					OutVertices.push_back(v);
				}
			}

			if (s[0] == 'f') {
				std::istringstream in(s);
				std::string face;
				std::vector<int> vertexIndices;
				std::vector<int> uvIndices;
				std::vector<int> normalIndices;

				while (in >> face) {
					int vertexIndex, uvIndex, normalIndex;
					
					if (sscanf_s(face.c_str(), "%d/%d/%d", &vertexIndex, &uvIndex, &normalIndex) == 3) {
						vertexIndices.push_back(vertexIndex - 1);
						uvIndices.push_back(uvIndex - 1);
						normalIndices.push_back(normalIndex - 1);
					}
					
					else if (sscanf_s(face.c_str(), "%d/%d", &vertexIndex, &uvIndex) == 2) {
						vertexIndices.push_back(vertexIndex - 1);
						uvIndices.push_back(uvIndex - 1);
					}
				
					else if (sscanf_s(face.c_str(), "%d", &vertexIndex) == 1) {
						vertexIndices.push_back(vertexIndex - 1);
						uvIndices.push_back(-1); 
					}
				}
				for (size_t i = 0; i < vertexIndices.size(); ++i) {
					OutFaceVertices.push_back(vertexIndices[i]);
					OutFaceUvs.push_back(uvIndices.size() > i ? uvIndices[i] : -1);
				}
				
				Outgcounts.push_back(vertexIndices.size());
			}
		}
	}

	inf.close();
}


void getFiles(string path, std::vector<string>& files)
{

	intptr_t hFile = 0;
	struct _finddata_t fileinfo;
	string p;
	if ((hFile = _findfirst(p.assign(path).append("\\*").c_str(), &fileinfo)) != -1)
	{
		do
		{

			if ((fileinfo.attrib & _A_SUBDIR))
			{
				if (strcmp(fileinfo.name, ".") != 0 && strcmp(fileinfo.name, "..") != 0)
					getFiles(p.assign(path).append("\\").append(fileinfo.name), files);
			}
			else
			{
				files.push_back(p.assign(path).append("\\").append(fileinfo.name));
			}
		} while (_findnext(hFile, &fileinfo) == 0);
		_findclose(hFile);
	}
}

// Fast vertex-position-only reader for animation frames after the first.
// Uses strtof instead of istringstream for significantly faster float parsing.
// reserveHint avoids reallocations since topology is constant across frames.
static std::vector<vertice> ReadObjVerticesOnly(const std::string& path, size_t reserveHint)
{
	std::vector<vertice> vertices;
	vertices.reserve(reserveHint);
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) return vertices;
	std::string line;
	while (std::getline(file, line)) {
		if (line.size() < 3 || line[0] != 'v' || line[1] != ' ') continue;
		const char* p = line.c_str() + 2;
		char* end;
		vertice v;
		v.x = strtof(p, &end);
		v.y = strtof(end, &end);
		v.z = strtof(end, nullptr);
		vertices.push_back(v);
	}
	return vertices;
}

void seq2abc(string inputdir, string ouputfile, float fps, std::string NodeName)
{
	std::vector<vertice> Vertices;
	std::vector<vertice> Normals;
	std::vector<uv> Uvs;
	std::vector<int> FaceUvIndexs;
	std::vector<int> face;
	std::vector<string> filenames;
	std::vector<int> g_counts_array;
	getFiles(inputdir, filenames);
	std::sort(filenames.begin(), filenames.end());

	if (filenames.empty()) {
		std::cerr << "No OBJ files found in: " << inputdir << std::endl;
		return;
	}

	int totalFrames = (int)filenames.size();
	std::cout << "Found " << totalFrames << " OBJ file(s) in: " << inputdir << std::endl;
	if (totalFrames == 1)
		std::cout << "Only one OBJ file was found, so the cache will have a single frame." << std::endl;

	OArchive archive(Alembic::AbcCoreOgawa::WriteArchive(), ouputfile);
	TimeSamplingPtr ts(new TimeSampling(1.0 / fps, 0.0));
	OXform xfobj(archive.getTop(), NodeName, ts);
	OPolyMesh meshyObj(xfobj, NodeName, ts);
	OPolyMeshSchema& mesh = meshyObj.getSchema();

	// Read first frame to establish topology, UVs, and initial positions
	ReadObj(filenames[0], Vertices, Uvs, Normals, face, FaceUvIndexs, g_counts_array);

	std::vector<V3f> verts(Vertices.size());
	for (size_t i = 0; i < Vertices.size(); ++i)
		verts[i] = V3f(Vertices[i].x, Vertices[i].y, Vertices[i].z);

	int32_t g_numIndices2 = (int32_t)face.size();
	int32_t* g_indices2 = new int32_t[g_numIndices2];
	std::copy(face.begin(), face.end(), g_indices2);
	Abc::int32_t* g_counts2 = new Abc::int32_t[g_counts_array.size()];
	std::copy(g_counts_array.begin(), g_counts_array.end(), g_counts2);

	OPolyMeshSchema::Sample mesh_samp(
		V3fArraySample(verts),
		Int32ArraySample(g_indices2, g_numIndices2),
		Int32ArraySample(g_counts2, g_counts_array.size()));

	// Only write a UV set if the OBJ actually contains UV data. Otherwise the
	// array stays uninitialized (Imath V2f does not zero its members), which
	// would emit a garbage, non-deterministic UV map.
	bool hasUVs = !Uvs.empty();
	for (size_t i = 0; hasUVs && i < FaceUvIndexs.size(); ++i) {
		if (FaceUvIndexs[i] == -1) { hasUVs = false; break; }
	}

	std::vector<V2f> FaceUvs;
	if (hasUVs) {
		mesh.setUVSourceName("UVMap");
		FaceUvs.assign(FaceUvIndexs.size(), V2f(0.0f, 0.0f));
		for (size_t i = 0; i < FaceUvIndexs.size(); ++i)
			FaceUvs[i] = V2f(Uvs[FaceUvIndexs[i]].u, Uvs[FaceUvIndexs[i]].v);
		V2fArraySample uvSample(FaceUvs);
		mesh_samp.setUVs(OV2fGeomParam::Sample(uvSample, kFacevaryingScope));
	}

	mesh.set(mesh_samp);
	std::cout << "PROGRESS 1 " << totalFrames << std::endl;

	if (totalFrames > 1) {
		int remaining = totalFrames - 1;
		size_t vertexHint = Vertices.size();

		unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
		numThreads = std::min(numThreads, (unsigned int)remaining);

		// Bounded prefetch pipeline: worker threads read frames ahead while the
		// main thread writes them to Alembic in order. Reading and writing overlap,
		// and at most `window` frames are held in memory at once.
		int window = std::max(2, (int)numThreads * 2);

		std::vector<std::vector<vertice>> slots(remaining);
		std::vector<char> ready(remaining, 0);
		std::mutex m;
		std::condition_variable cv;
		std::atomic<int> nextRead(0);
		int writeIndex = 0;  // guarded by m; frames the writer has consumed

		auto worker = [&]() {
			while (true) {
				int idx = nextRead.fetch_add(1);
				if (idx >= remaining) break;
				// Stay within the memory window of the writer.
				{
					std::unique_lock<std::mutex> lk(m);
					cv.wait(lk, [&] { return idx < writeIndex + window; });
				}
				std::vector<vertice> data = ReadObjVerticesOnly(filenames[idx + 1], vertexHint);
				{
					std::lock_guard<std::mutex> lk(m);
					slots[idx] = std::move(data);
					ready[idx] = 1;
				}
				cv.notify_all();
			}
		};

		std::vector<std::thread> workers;
		workers.reserve(numThreads);
		for (unsigned int t = 0; t < numThreads; ++t)
			workers.emplace_back(worker);

		// Write frames in order — Alembic is not thread-safe.
		for (int i = 0; i < remaining; ++i) {
			std::vector<vertice> fv;
			{
				std::unique_lock<std::mutex> lk(m);
				cv.wait(lk, [&] { return ready[i] != 0; });
				fv = std::move(slots[i]);
				writeIndex = i + 1;
			}
			cv.notify_all();  // let blocked readers advance into the freed window

			for (size_t j = 0; j < fv.size() && j < verts.size(); ++j)
				verts[j] = V3f(fv[j].x, fv[j].y, fv[j].z);
			mesh.set(mesh_samp);
			std::cout << "PROGRESS " << (i + 2) << " " << totalFrames << std::endl;
		}

		for (auto& w : workers) w.join();
	}

	delete[] g_indices2;
	delete[] g_counts2;
}

void print_usage(const char* name)
{
	std::cout << "\nUsage: " << name << " [options]" << std::endl
		<< "Options:" << std::endl
		<< "  -h, --help     help  \n"
		<< "  -i, --in       obj input dir \n"
		<< "  -o, --out      output abc name \n"
		<< "  -f --fps       abc frame rate \n"
		<< "  -n --name      Node Name \n"
		<< "\n"
		<< std::endl;
}



int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		print_usage(argv[0]);
		return 1;
	}
	string inputdir = "";
	string output = "output.abc";
	string NodeName = "NodeName";
	float fps = 24.0;

	for (int i = 1; i < argc; i++)
	{
		std::string t_arg = std::string(argv[i]);
		if (t_arg == "-h" || t_arg == "--help")
		{
			print_usage(argv[0]);
			return 0;
		}

		// All remaining options require a value argument.
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
			fps = std::stof(argv[++i]);
		}
		else if(t_arg == "-n" || t_arg == "--name")
	    {
			NodeName = argv[++i];
		}

	}

	if (inputdir.empty())
	{
		std::cerr << "No input directory given. Use -i <obj_folder>." << std::endl;
		print_usage(argv[0]);
		return 1;
	}
	
	std::cout << "input dir: " << inputdir << std::endl;
	std::cout << "output abc: " << output << std::endl;
	std::cout << "fps: " << fps << std::endl;
	std::cout << "NodeName: " << NodeName << std::endl;

	// Mesh out
	
	seq2abc(inputdir, output, fps, NodeName);

	return 0;
}