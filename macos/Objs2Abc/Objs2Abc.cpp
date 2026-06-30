// Converts a numbered OBJ frame sequence into an Alembic (.abc) PolyMesh cache.
// Each frame is written with its own full topology, so sequences whose vertex
// and face counts change over time (fracture, fluid, remeshing sims) are
// preserved rather than frozen to the first frame.

// Alembic
#include <Alembic/AbcGeom/All.h>
#include <Alembic/AbcCoreOgawa/All.h>

// Standard library
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <Vector>
#include <io.h>

using namespace std;
using namespace Alembic::AbcGeom; // Contains Abc, AbcCoreAbstract


void getFiles(string path, std::vector<string>& files)
{

	intptr_t hFile = 0;
	struct _finddata_t fileinfo;
	string p;
	// Use forward slashes: these paths are opened directly by std::ifstream
	// (including via its constructor, which is not covered by the compat
	// open() normalization macro), and backslashes are literal on macOS.
	if ((hFile = _findfirst(p.assign(path).append("/*").c_str(), &fileinfo)) != -1)
	{
		do
		{

			if ((fileinfo.attrib & _A_SUBDIR))
			{
				if (strcmp(fileinfo.name, ".") != 0 && strcmp(fileinfo.name, "..") != 0)
					getFiles(p.assign(path).append("/").append(fileinfo.name), files);
			}
			else
			{
				files.push_back(p.assign(path).append("/").append(fileinfo.name));
			}
		} while (_findnext(hFile, &fileinfo) == 0);
		_findclose(hFile);
	}
}

// Full per-frame mesh, already converted to Alembic-ready arrays. Each frame
// carries its own topology so sequences whose vertex/face counts change over
// time (fracture, fluid, remeshing sims) are represented correctly instead of
// being frozen to the first frame's topology.
struct FrameMesh
{
	std::vector<V3f> positions;
	std::vector<int32_t> faceIndices;   // flattened, file (CCW) order
	std::vector<int32_t> faceCounts;    // vertices per face
	std::vector<V2f> uvs;               // face-varying, only if hasUVs
	bool hasUVs = false;
};

// Reads an OBJ frame fully (positions, faces, face-varying UVs) using manual
// char parsing (strtof/strtol) to stay fast on multi-million-face frames.
static FrameMesh ReadFrameMesh(const std::string& path, size_t vHint, size_t fHint)
{
	FrameMesh m;
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		std::cerr << "Warning: could not open frame: " << path << std::endl;
		return m;
	}

	m.positions.reserve(vHint);
	m.faceIndices.reserve(fHint * 3);
	m.faceCounts.reserve(fHint);
	std::vector<V2f> rawUVs;
	std::vector<int> faceUV;            // per face-vertex UV index (0-based), -1 if none
	faceUV.reserve(fHint * 3);
	bool anyMissingUV = false;

	std::string line;
	while (std::getline(file, line)) {
		if (line.size() < 2) continue;
		const char* p = line.c_str();
		char c0 = p[0], c1 = p[1];

		if (c0 == 'v' && c1 == ' ') {
			char* e;
			float x = strtof(p + 2, &e);
			float y = strtof(e, &e);
			float z = strtof(e, nullptr);
			m.positions.push_back(V3f(x, y, z));
		}
		else if (c0 == 'v' && c1 == 't') {
			char* e;
			float u = strtof(p + 3, &e);
			float v = strtof(e, nullptr);
			rawUVs.push_back(V2f(u, v));
		}
		else if (c0 == 'f' && c1 == ' ') {
			const char* q = p + 2;
			int count = 0;
			while (*q) {
				while (*q == ' ' || *q == '\t' || *q == '\r') ++q;
				if (!*q) break;
				char* e;
				long vi = strtol(q, &e, 10);
				if (e == q) { ++q; continue; }  // not a number, skip char
				int uvi = -1;
				if (*e == '/') {
					const char* u = e + 1;
					if (*u != '/') {            // v/vt or v/vt/vn
						char* e2;
						long uv = strtol(u, &e2, 10);
						if (e2 != u) uvi = (int)(uv - 1);
					}
				}
				// advance past the whole token (handles v//vn, v/vt/vn)
				q = e;
				while (*q && *q != ' ' && *q != '\t' && *q != '\r') ++q;
				m.faceIndices.push_back((int32_t)(vi - 1));
				faceUV.push_back(uvi);
				if (uvi < 0) anyMissingUV = true;
				++count;
			}
			if (count > 0) m.faceCounts.push_back(count);
		}
	}

	// Face-varying UVs only when every face corner has a valid UV index.
	if (!rawUVs.empty() && !anyMissingUV) {
		m.uvs.assign(faceUV.size(), V2f(0.0f, 0.0f));
		m.hasUVs = true;
		for (size_t i = 0; i < faceUV.size(); ++i) {
			int idx = faceUV[i];
			if (idx >= 0 && (size_t)idx < rawUVs.size()) {
				m.uvs[i] = rawUVs[idx];
			} else {
				m.hasUVs = false;   // out-of-range reference: drop UVs for safety
				break;
			}
		}
		if (!m.hasUVs) m.uvs.clear();
	}

	return m;
}

void seq2abc(string inputdir, string ouputfile, float fps, std::string NodeName)
{
	std::vector<string> filenames;
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

	// Read the first frame up front so we can size reservations and decide,
	// from frame 0, whether this sequence carries UVs. (Doing this on the
	// main thread keeps the UV-source-name set before any sample is written.)
	FrameMesh first = ReadFrameMesh(filenames[0], 0, 0);
	size_t vHint = first.positions.size();
	size_t fHint = first.faceCounts.size();
	bool writeUVs = first.hasUVs;
	if (writeUVs)
		mesh.setUVSourceName("UVMap");

	int remaining = totalFrames;
	unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
	numThreads = std::min(numThreads, (unsigned int)remaining);

	// Bounded prefetch pipeline: worker threads read whole frames ahead while
	// the main thread writes them to Alembic in order. Full frames are large,
	// so keep the in-flight window modest to bound peak memory.
	int window = std::max(2, (int)numThreads);

	std::vector<FrameMesh> slots(remaining);
	std::vector<char> ready(remaining, 0);
	std::mutex mtx;
	std::condition_variable cv;
	std::atomic<int> nextRead(1);   // frame 0 already read on the main thread
	int writeIndex = 0;             // guarded by mtx; frames the writer has consumed

	slots[0] = std::move(first);
	ready[0] = 1;

	auto worker = [&]() {
		while (true) {
			int idx = nextRead.fetch_add(1);
			if (idx >= remaining) break;
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
		workers.emplace_back(worker);

	// Write frames in order — Alembic is not thread-safe. Each frame writes its
	// own full topology, so changing vertex/face counts are preserved.
	std::vector<V2f> zeroUVs;
	for (int i = 0; i < remaining; ++i) {
		FrameMesh fm;
		{
			std::unique_lock<std::mutex> lk(mtx);
			cv.wait(lk, [&] { return ready[i] != 0; });
			fm = std::move(slots[i]);
			writeIndex = i + 1;
		}
		cv.notify_all();  // let blocked readers advance into the freed window

		OPolyMeshSchema::Sample samp(
			V3fArraySample(fm.positions),
			Int32ArraySample(fm.faceIndices),
			Int32ArraySample(fm.faceCounts));

		// Keep the UV param's sample count aligned with the mesh: once UVs are
		// written they must be written every frame. A frame missing UVs (rare,
		// inconsistent input) gets a zero-filled set of the right length.
		if (writeUVs) {
			if (fm.hasUVs) {
				samp.setUVs(OV2fGeomParam::Sample(V2fArraySample(fm.uvs), kFacevaryingScope));
			} else {
				zeroUVs.assign(fm.faceIndices.size(), V2f(0.0f, 0.0f));
				samp.setUVs(OV2fGeomParam::Sample(V2fArraySample(zeroUVs), kFacevaryingScope));
			}
		}

		mesh.set(samp);
		std::cout << "PROGRESS " << (i + 1) << " " << totalFrames << std::endl;
	}

	for (auto& w : workers) w.join();
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