
#include "UnrealSandboxTerrainPrivatePCH.h"
#include "SandboxVoxeldata.h"

#include "Transvoxel.h"

#include <cmath>
#include <vector>
#include <mutex>

#include <iterator>

//#include <unordered_map>
#include <map>


//====================================================================================
	
static FORCEINLINE FVector clcNormal(FVector &p1, FVector &p2, FVector &p3) {
    float A = p1.Y * (p2.Z - p3.Z) + p2.Y * (p3.Z - p1.Z) + p3.Y * (p1.Z - p2.Z);
    float B = p1.Z * (p2.X - p3.X) + p2.Z * (p3.X - p1.X) + p3.Z * (p1.X - p2.X);
    float C = p1.X * (p2.Y - p3.Y) + p2.X * (p3.Y - p1.Y) + p3.X * (p1.Y - p2.Y);
    // float D = -(p1.x * (p2.y * p3.z - p3.y * p2.z) + p2.x * (p3.y * p1.z - p1.y * p3.z) + p3.x * (p1.y * p2.z - p2.y
    // * p1.z));

    FVector n(A, B, C);
    n.Normalize();
    return n;
}

//####################################################################################################################################
//
//	VoxelMeshExtractor
//
//####################################################################################################################################

//#define FORCEINLINE FORCENOINLINE  //debug

class VoxelMeshExtractor {

private:

	TMeshLodSection &mesh_data;
	const TVoxelData &voxel_data;
	const TVoxelDataParam voxel_data_param;

	typedef struct PointAddr {
		int x = 0;
		int y = 0;
		int z = 0;

		PointAddr(const int x0, const int y0, const int z0) : x(x0), y(y0), z(z0) { }
		PointAddr() { }


		FORCEINLINE const PointAddr operator-(const PointAddr& rv) const {
			return PointAddr(x - rv.x, y - rv.y, z - rv.z);
		}

		FORCEINLINE const PointAddr operator+(const PointAddr& rv) const {
			return PointAddr(x + rv.x, y + rv.y, z + rv.z);
		}

		FORCEINLINE const PointAddr operator/(int val) const {
			return PointAddr(x / val, y / val, z / val);
		}
		FORCEINLINE const PointAddr operator*(float val) const {
			return PointAddr(x * val, y * val, z * val);
		}

		FORCEINLINE void operator = (const PointAddr &a) {
			x = a.x; y = a.y; z = a.z;
		}

		FORCEINLINE bool operator==(const PointAddr& in) const {
			return (this->x == in.x) && (this->y == in.y) && (this->z == in.z);
		}

	} PointAddr;


	struct Point {
		PointAddr adr;
		FVector pos;
		float density;
		unsigned short material_id;
	};

	struct TmpPoint {
		FVector v;
		unsigned short matId;
	};

	class MeshHandler {

	private:
		FProcMeshSection* generalMeshSection;
		VoxelMeshExtractor* extractor;

		TMeshContainer* meshMatContainer;

		TMaterialSectionMap* materialSectionMapPtr;
		TMaterialTransitionSectionMap* materialTransitionSectionMapPtr;

		// transition material
		unsigned short transitionMaterialIndex = 0;
		TMap<FString, unsigned short> transitionMaterialDict;

		int triangleCount = 0;

		int vertexGeneralIndex = 0;

	public:

		struct VertexInfo {
			FVector normal;

			std::map<unsigned short, int32> indexInMaterialSectionMap;
			std::map<unsigned short, int32> indexInMaterialTransitionSectionMap;

			int vertexIndex = 0;
		};

		TMap<FVector, VertexInfo> vertexInfoMap;

		MeshHandler(VoxelMeshExtractor* e, FProcMeshSection* s, TMeshContainer* mc) :
			extractor(e), generalMeshSection(s), meshMatContainer(mc) {
			materialSectionMapPtr = &meshMatContainer->MaterialSectionMap;
			materialTransitionSectionMapPtr = &meshMatContainer->MaterialTransitionSectionMap;
		}


	private:

		FORCEINLINE void addVertexGeneral(const TmpPoint &point, const FVector& n) {
			const FVector v = point.v;
			VertexInfo& vertexInfo = vertexInfoMap.FindOrAdd(v);

			if (vertexInfo.normal.IsZero()) {
				// new vertex
				vertexInfo.normal = n;

				FProcMeshVertex vertex;
				vertex.Position = v;
				vertex.Normal = n;

				generalMeshSection->ProcIndexBuffer.Add(vertexGeneralIndex);
				generalMeshSection->AddVertex(vertex);
				vertexInfo.vertexIndex = vertexGeneralIndex;

				vertexGeneralIndex++;
			} else {
				// existing vertex
				FVector tmp(vertexInfo.normal);
				tmp += n;
				tmp /= 2;
				vertexInfo.normal = tmp;

				generalMeshSection->ProcIndexBuffer.Add(vertexInfo.vertexIndex);
			}
		}

		FORCEINLINE void addVertexMat(unsigned short matId, const TmpPoint &point, const FVector& n) {
			const FVector& v = point.v;
			VertexInfo& vertexInfo = vertexInfoMap.FindOrAdd(v);

			if (vertexInfo.normal.IsZero()) {
				vertexInfo.normal = n;
			} else {
				FVector tmp(vertexInfo.normal);
				tmp += n;
				tmp /= 2;
				vertexInfo.normal = tmp;
			}

			// get current mat section
			TMeshMaterialSection& matSectionRef = materialSectionMapPtr->FindOrAdd(matId);
			matSectionRef.MaterialId = matId; // update mat id (if case of new section was created by FindOrAdd)

			if (vertexInfo.indexInMaterialSectionMap.find(matId) != vertexInfo.indexInMaterialSectionMap.end()) {
				// vertex exist in mat section
				// just get vertex index and put to index buffer
				int32 vertexIndex = vertexInfo.indexInMaterialSectionMap[matId];
				matSectionRef.MaterialMesh.ProcIndexBuffer.Add(vertexIndex);
			} else { // vertex not exist in mat section
				matSectionRef.MaterialMesh.ProcIndexBuffer.Add(matSectionRef.vertexIndexCounter);

				FProcMeshVertex Vertex;
				Vertex.Position = v;
				Vertex.Normal = vertexInfo.normal;
				Vertex.UV0 = FVector2D(0.f, 0.f);
				Vertex.Color = FColor(0, 0, 0, 0);
				Vertex.Tangent = FProcMeshTangent();
				//Vertex.Tangent.TangentX = FVector(-1, 0, 0); // i dunno how it works but ugly seams between zones are gone. may be someone someday explain me it. lol

				matSectionRef.MaterialMesh.AddVertex(Vertex);

				vertexInfo.indexInMaterialSectionMap[matId] = matSectionRef.vertexIndexCounter;
				matSectionRef.vertexIndexCounter++;
			}
		}

		FORCEINLINE void addVertexMatTransition(std::set<unsigned short>& materialIdSet, unsigned short matId, const TmpPoint &point, const FVector& n) {
			const FVector& v = point.v;
			VertexInfo& vertexInfo = vertexInfoMap.FindOrAdd(v);

			if (vertexInfo.normal.IsZero()) {
				vertexInfo.normal = n;
			}
			else {
				FVector tmp(vertexInfo.normal);
				tmp += n;
				tmp /= 2;
				vertexInfo.normal = tmp;
			}

			// get current mat section
			TMeshMaterialSection& matSectionRef = materialTransitionSectionMapPtr->FindOrAdd(matId);
			matSectionRef.MaterialId = matId; // update mat id (if case of new section was created by FindOrAdd)

			if (vertexInfo.indexInMaterialTransitionSectionMap.find(matId) != vertexInfo.indexInMaterialTransitionSectionMap.end()) {
				// vertex exist in mat section
				// just get vertex index and put to index buffer
				int32 vertexIndex = vertexInfo.indexInMaterialTransitionSectionMap[matId];
				matSectionRef.MaterialMesh.ProcIndexBuffer.Add(vertexIndex);
			} else { // vertex not exist in mat section
				matSectionRef.MaterialMesh.ProcIndexBuffer.Add(matSectionRef.vertexIndexCounter);

				FProcMeshVertex Vertex;
				Vertex.Position = v;
				Vertex.Normal = vertexInfo.normal;
				Vertex.UV0 = FVector2D(0.f, 0.f);
				Vertex.Tangent = FProcMeshTangent();
				//Vertex.Tangent.TangentX = FVector(-1, 0, 0); // i dunno how it works but ugly seams between zones are gone. may be someone someday explain me it. kek

				int i = 0;
				int test = -1;
				for (unsigned short m : materialIdSet) {
					if (m == point.matId) {
						test = i;
						break;
					}

					i++;
				}

				switch (test) {
					case 0:  Vertex.Color = FColor(255,	0,		0,		0); break;
					case 1:  Vertex.Color = FColor(0,	255,	0,		0); break;
					case 2:  Vertex.Color = FColor(0,	0,		255,	0); break;
					default: Vertex.Color = FColor(0,	0,		0,		0); break;
				}

				matSectionRef.MaterialMesh.AddVertex(Vertex);

				vertexInfo.indexInMaterialTransitionSectionMap[matId] = matSectionRef.vertexIndexCounter;
				matSectionRef.vertexIndexCounter++;
			}
		}

	public:
		FORCEINLINE unsigned short getTransitionMaterialIndex(std::set<unsigned short>& materialIdSet) {
			FString transitionMaterialName = TEXT("");
			FString separator = TEXT("");
			for (unsigned short matId : materialIdSet) {
				transitionMaterialName = FString::Printf(TEXT("%s%s%d"), *transitionMaterialName, *separator, matId);
				separator = TEXT("-");
			}

			if (transitionMaterialDict.Contains(transitionMaterialName)) {
				return transitionMaterialDict[transitionMaterialName];
			} else {
				unsigned short idx = transitionMaterialIndex;
				transitionMaterialDict.Add(transitionMaterialName, idx);
				transitionMaterialIndex++;

				TMeshMaterialTransitionSection& sectionRef = materialTransitionSectionMapPtr->FindOrAdd(idx);
				sectionRef.TransitionName = transitionMaterialName;
				sectionRef.MaterialIdSet = materialIdSet;

				return idx;
			}
		}

		// general mesh without material. used for collision only
		FORCEINLINE void addTriangleGeneral(const FVector& normal, TmpPoint &tmp1, TmpPoint &tmp2, TmpPoint &tmp3) {
			addVertexGeneral(tmp1, normal);
			addVertexGeneral(tmp2, normal);
			addVertexGeneral(tmp3, normal);

			triangleCount++;
		}

		// usual mesh with one material
		FORCEINLINE void addTriangleMat(const FVector& normal, unsigned short matId, TmpPoint &tmp1, TmpPoint &tmp2, TmpPoint &tmp3) {
			addVertexMat(matId, tmp1, normal);
			addVertexMat(matId, tmp2, normal);
			addVertexMat(matId, tmp3, normal);

			triangleCount++;
		}

		// transitional mesh between two or more meshes with different material
		FORCEINLINE void addTriangleMatTransition(const FVector& normal, std::set<unsigned short>& materialIdSet, unsigned short matId, TmpPoint &tmp1, TmpPoint &tmp2, TmpPoint &tmp3) {
			addVertexMatTransition(materialIdSet, matId, tmp1, normal);
			addVertexMatTransition(materialIdSet, matId, tmp2, normal);
			addVertexMatTransition(materialIdSet, matId, tmp3, normal);

			triangleCount++;
		}

	};


	MeshHandler* mainMeshHandler;
	TArray<MeshHandler*> transitionHandlerArray;

public:
	VoxelMeshExtractor(TMeshLodSection &a, const TVoxelData &b, const TVoxelDataParam c) : mesh_data(a), voxel_data(b), voxel_data_param(c) {
		mainMeshHandler = new MeshHandler(this, &a.WholeMesh, &a.RegularMeshContainer);

		for (auto i = 0; i < 6; i++) {
			transitionHandlerArray.Add(new MeshHandler(this, &a.WholeMesh, &a.TransitionPatchArray[i]));
		}
	}

	~VoxelMeshExtractor() {
		delete mainMeshHandler;

		for (MeshHandler* transitionHandler : transitionHandlerArray) {
			delete transitionHandler;
		}
	}
    
private:
	double isolevel = 0.5f;

	FORCEINLINE Point getVoxelpoint(PointAddr adr) {
		return getVoxelpoint(adr.x, adr.y, adr.z);
	}

	FORCEINLINE Point getVoxelpoint(uint8 x, uint8 y, uint8 z) {
		Point vp;
		vp.adr = PointAddr(x,y,z);
		vp.density = getDensity(x, y, z);
		vp.material_id = getMaterial(x, y, z);
		vp.pos = voxel_data.voxelIndexToVector(x, y, z);
		return vp;
	}

	FORCEINLINE float getDensity(int x, int y, int z) {
		int step = voxel_data_param.step();
		if (voxel_data_param.z_cut) {
			FVector p = voxel_data.voxelIndexToVector(x, y, z);
			p += voxel_data.getOrigin();
			if (p.Z > voxel_data_param.z_cut_level) {
				return 0;
			}
		}

		return voxel_data.getDensity(x, y, z);
	}

	FORCEINLINE unsigned short getMaterial(int x, int y, int z) {
		return voxel_data.getMaterial(x, y, z);
	}

	FORCEINLINE FVector vertexInterpolation(FVector p1, FVector p2, float valp1, float valp2) {
		if (std::abs(isolevel - valp1) < 0.00001) {
			return p1;
		}

		if (std::abs(isolevel - valp2) < 0.00001) {
			return p2;
		}

		if (std::abs(valp1 - valp2) < 0.00001) {
			return p1;
		}

		if (valp1 == valp2) {
			return p1;
		}

		float mu = (isolevel - valp1) / (valp2 - valp1);
		return p1 + (p2 - p1) *mu;
	}

	// fast material select for LOD0
	FORCEINLINE void selectMaterialLOD0(struct TmpPoint& tp, Point& point1, Point& point2) {
		if (point1.material_id == point2.material_id) {
			tp.matId = point1.material_id;
			return;
		}

		FVector p1 = point1.pos - tp.v;
		FVector p2 = point2.pos - tp.v;

		if (p1.Size() > p2.Size()) {
			tp.matId = point2.material_id;
		} else {
			tp.matId = point1.material_id;
		}
	}

	// calculate material for LOD1-4
	FORCEINLINE void selectMaterialLODMedium(struct TmpPoint& tp, Point& point1, Point& point2) {
		float mu = (isolevel - point1.density) / (point2.density - point1.density);
		PointAddr tmp = point1.adr + (point2.adr - point1.adr) * mu;
		tp.matId = getMaterial(tmp.x, tmp.y, tmp.z);
	}

	// calculate material for LOD5-6
	FORCEINLINE void selectMaterialLODBig(struct TmpPoint& tp, Point& point1, Point& point2) {
		PointAddr A;
		PointAddr B;

		if (point1.density < isolevel) {
			// point1 - air, point2 - solid
			A = point1.adr;
			B = point2.adr;
		} else {
			// point1 - solid, point2 - air
			A = point2.adr;
			B = point1.adr;
		}

		PointAddr S = B - A;
		if (S.x != 0) S.x = S.x / abs(S.x);
		if (S.y != 0) S.y = S.y / abs(S.y);
		if (S.z != 0) S.z = S.z / abs(S.z);

		// start from air point and find first solid point
		PointAddr tmp = A; 
		while (getDensity(tmp.x, tmp.y, tmp.z) < isolevel) {
			if (tmp == B) break;
			tmp = tmp + S;
		}

		tp.matId = getMaterial(tmp.x, tmp.y, tmp.z);
	}

	FORCEINLINE TmpPoint vertexClc(Point& point1, Point& point2) {
		struct TmpPoint ret;

		ret.v = vertexInterpolation(point1.pos, point2.pos, point1.density, point2.density);

		if (voxel_data_param.lod == 0) {
			selectMaterialLOD0(ret, point1, point2);
		} else if (voxel_data_param.lod > 0 && voxel_data_param.lod < 5) {
			selectMaterialLODMedium(ret, point1, point2);
		} else {
			selectMaterialLODBig(ret, point1, point2);
		}

		return ret;
	}

	FORCEINLINE void getConrers(int8 (&corner)[8], Point (&d)[8]) {
		for (auto i = 0; i < 8; i++) {
			corner[i] = (d[i].density < isolevel) ? 0 : -127;
		}
	}

	FORCEINLINE PointAddr clcMediumAddr(const PointAddr& adr1, const PointAddr& adr2) {
		return (adr2 - adr1) / 2 + adr1;
	}

	FORCEINLINE void extractRegularCell(Point (&d)[8]) {
		int8 corner[8];
		for (auto i = 0; i < 8; i++) {
			corner[i] = (d[i].density < isolevel) ? -127 : 0;
		}

		unsigned long caseCode = ((corner[0] >> 7) & 0x01)
			| ((corner[1] >> 6) & 0x02)
			| ((corner[2] >> 5) & 0x04)
			| ((corner[3] >> 4) & 0x08)
			| ((corner[4] >> 3) & 0x10)
			| ((corner[5] >> 2) & 0x20)
			| ((corner[6] >> 1) & 0x40)
			| (corner[7] & 0x80);

		if (caseCode == 0) {
			return;
		}

		unsigned int c = regularCellClass[caseCode];
		RegularCellData cd = regularCellData[c];
		std::vector<TmpPoint> vertexList;
		vertexList.reserve(cd.GetTriangleCount() * 3);

		std::set<unsigned short> materialIdSet;

		for (int i = 0; i < cd.GetVertexCount(); i++) {
			const int edgeCode = regularVertexData[caseCode][i];
			const unsigned short v0 = (edgeCode >> 4) & 0x0F;
			const unsigned short v1 = edgeCode & 0x0F;
			struct TmpPoint tp = vertexClc(d[v0], d[v1]);

			materialIdSet.insert(tp.matId);
			vertexList.push_back(tp);
		}

		bool isTransitionMaterialSection = materialIdSet.size() > 1;
		unsigned short transitionMatId = 0;

		// if transition material
		if (isTransitionMaterialSection) {
			transitionMatId = mainMeshHandler->getTransitionMaterialIndex(materialIdSet);
		}

		for (int i = 0; i < cd.GetTriangleCount() * 3; i += 3) {
			TmpPoint tmp1 = vertexList[cd.vertexIndex[i]];
			TmpPoint tmp2 = vertexList[cd.vertexIndex[i + 1]];
			TmpPoint tmp3 = vertexList[cd.vertexIndex[i + 2]];

			// calculate normal
			const FVector n = -clcNormal(tmp1.v, tmp2.v, tmp3.v);

			// add to whole mesh
			mainMeshHandler->addTriangleGeneral(n, tmp1, tmp2, tmp3);

			if (isTransitionMaterialSection) {
				// add transition material section
				mainMeshHandler->addTriangleMatTransition(n, materialIdSet, transitionMatId, tmp1, tmp2, tmp3);
			} else {
				// always one iteration
				for (unsigned short matId : materialIdSet) {
					// add regular material section
					mainMeshHandler->addTriangleMat(n, matId, tmp1, tmp2, tmp3);
				}
			}
		}
	}

	FORCEINLINE void extractTransitionCell(int sectionNumber, Point& d0, Point& d2, Point& d6, Point& d8) {
		Point d[14];

		d[0] = d0;
		d[1] = getVoxelpoint(clcMediumAddr(d2.adr, d0.adr));
		d[2] = d2;

		PointAddr a3 = clcMediumAddr(d6.adr, d0.adr);
		PointAddr a5 = clcMediumAddr(d8.adr, d2.adr);

		d[3] = getVoxelpoint(a3);
		d[4] = getVoxelpoint(clcMediumAddr(a5, a3));
		d[5] = getVoxelpoint(a5);

		d[6] = d6;
		d[7] = getVoxelpoint(clcMediumAddr(d8.adr, d6.adr));
		d[8] = d8;

		d[9] = d0;
		d[0xa] = d2;
		d[0xb] = d6;
		d[0xc] = d8;


		for (auto i = 0; i < 9; i++) {
			//mesh_data.DebugPointList.Add(d[i].pos);
		}

		int8 corner[9];
		for (auto i = 0; i < 9; i++) {
			corner[i] = (d[i].density < isolevel) ? -127 : 0;
		}

		static const int caseCodeCoeffs[9] = { 0x01, 0x02, 0x04, 0x80, 0x100, 0x08, 0x40, 0x20, 0x10 };
		static const int charByteSz = sizeof(int8) * 8;

		unsigned long caseCode = 0;
		for (auto ci = 0; ci < 9; ++ci) {
			// add the coefficient only if the value is negative
			caseCode += ((corner[ci] >> (charByteSz - 1)) & 1) * caseCodeCoeffs[ci];
		}

		if (caseCode == 0) {
			return;
		}

		unsigned int classIndex = transitionCellClass[caseCode];

		const bool inverse = (classIndex & 128) != 0;

		TransitionCellData cellData = transitionCellData[classIndex & 0x7F];

		std::vector<TmpPoint> vertexList;
		vertexList.reserve(cellData.GetTriangleCount() * 3);
		std::set<unsigned short> materialIdSet;

		for (int i = 0; i < cellData.GetVertexCount(); i++) {
			const int edgeCode = transitionVertexData[caseCode][i];
			const unsigned short v0 = (edgeCode >> 4) & 0x0F;
			const unsigned short v1 = edgeCode & 0x0F;
			struct TmpPoint tp = vertexClc(d[v0], d[v1]);

			materialIdSet.insert(tp.matId);
			vertexList.push_back(tp);
			//mesh_data.DebugPointList.Add(tp.v);
		}

		bool isTransitionMaterialSection = materialIdSet.size() > 1;
		unsigned short transitionMatId = 0;

		// if transition material
		if (isTransitionMaterialSection) {
			transitionMatId = mainMeshHandler->getTransitionMaterialIndex(materialIdSet);
		}

		for (int i = 0; i < cellData.GetTriangleCount() * 3; i += 3) {
			TmpPoint tmp1 = vertexList[cellData.vertexIndex[i]];
			TmpPoint tmp2 = vertexList[cellData.vertexIndex[i + 1]];
			TmpPoint tmp3 = vertexList[cellData.vertexIndex[i + 2]];

			MeshHandler* meshHandler = transitionHandlerArray[sectionNumber];

			// calculate normal
			FVector n = -clcNormal(tmp1.v, tmp2.v, tmp3.v);

			if(mainMeshHandler->vertexInfoMap.Contains(tmp1.v)) {
				MeshHandler::VertexInfo& vertexInfo = mainMeshHandler->vertexInfoMap.FindOrAdd(tmp1.v);
				n = vertexInfo.normal;
			} else if (mainMeshHandler->vertexInfoMap.Contains(tmp2.v)) {
				MeshHandler::VertexInfo& vertexInfo = mainMeshHandler->vertexInfoMap.FindOrAdd(tmp2.v);
				n = vertexInfo.normal;
			} else if (mainMeshHandler->vertexInfoMap.Contains(tmp3.v)) {
				MeshHandler::VertexInfo& vertexInfo = mainMeshHandler->vertexInfoMap.FindOrAdd(tmp3.v);
				n = vertexInfo.normal;
			}

			if (isTransitionMaterialSection) {
				// add transition material section
				if (inverse) {
					meshHandler->addTriangleMatTransition(n, materialIdSet, transitionMatId, tmp3, tmp2, tmp1);
				} else {
					meshHandler->addTriangleMatTransition(n, materialIdSet, transitionMatId, tmp1, tmp2, tmp3);
				}
			} else {
				// always one iteration
				for (unsigned short matId : materialIdSet) {
					// add regular material section
					if (inverse) {
						meshHandler->addTriangleMat(n, matId, tmp3, tmp2, tmp1);
					} else {
						meshHandler->addTriangleMat(n, matId, tmp1, tmp2, tmp3);
					}
				}
			}
		}
	}

public:
	FORCEINLINE void generateCell(int x, int y, int z) {
		Point d[8];

		int step = voxel_data_param.step();

        d[0] = getVoxelpoint(x, y + step, z);
        d[1] = getVoxelpoint(x, y, z);
        d[2] = getVoxelpoint(x + step, y + step, z);
        d[3] = getVoxelpoint(x + step, y, z);
        d[4] = getVoxelpoint(x, y + step, z + step);
        d[5] = getVoxelpoint(x, y, z + step);
        d[6] = getVoxelpoint(x + step, y + step, z + step);
        d[7] = getVoxelpoint(x + step, y, z + step);

		extractRegularCell(d);

		if (voxel_data_param.bGenerateLOD) {
			if (voxel_data_param.lod > 0) {
				const int e = voxel_data.num() - step - 1;

				if (x == 0) extractTransitionCell(0, d[1], d[0], d[5], d[4]); // X+
				if (x == e) extractTransitionCell(1, d[2], d[3], d[6], d[7]); // X-
				if (y == 0) extractTransitionCell(2, d[3], d[1], d[7], d[5]); // Y-
				if (y == e) extractTransitionCell(3, d[0], d[2], d[4], d[6]); // Y+
				if (z == 0) extractTransitionCell(4, d[3], d[2], d[1], d[0]); // Z-
				if (z == e) extractTransitionCell(5, d[6], d[7], d[4], d[5]); // Z+
			}
		}
    }

};

typedef std::shared_ptr<VoxelMeshExtractor> VoxelMeshExtractorPtr;

//####################################################################################################################################

TMeshDataPtr polygonizeCellSubstanceCacheNoLOD(const TVoxelData &vd, const TVoxelDataParam &vdp) {
	TMeshData* mesh_data = new TMeshData();
	VoxelMeshExtractorPtr mesh_extractor_ptr = VoxelMeshExtractorPtr(new VoxelMeshExtractor(mesh_data->MeshSectionLodArray[0], vd, vdp));

	int step = vdp.step();
	for (auto it = vd.substanceCacheLOD[0].cellList.cbegin(); it != vd.substanceCacheLOD[0].cellList.cend(); ++it) {
		int index = *it;

		int x = index / (vd.num() * vd.num());
		int y = (index / vd.num()) % vd.num();
		int z = index % vd.num();

		mesh_extractor_ptr->generateCell(x, y, z);
	}

	mesh_data->CollisionMeshPtr = &mesh_data->MeshSectionLodArray[0].WholeMesh;

	return TMeshDataPtr(mesh_data);
}


TMeshDataPtr polygonizeCellSubstanceCacheLOD(const TVoxelData &vd, const TVoxelDataParam &vdp) {
	TMeshData* mesh_data = new TMeshData();
	static const int max_lod = LOD_ARRAY_SIZE;

	// create mesh extractor for each LOD
	for (auto lod = 0; lod < max_lod; lod++) {
		TVoxelDataParam me_vdp = vdp;
		me_vdp.lod = lod;

		VoxelMeshExtractorPtr mesh_extractor_ptr = VoxelMeshExtractorPtr(new VoxelMeshExtractor(mesh_data->MeshSectionLodArray[lod], vd, me_vdp));

		int step = vdp.step();
		for (auto it = vd.substanceCacheLOD[lod].cellList.cbegin(); it != vd.substanceCacheLOD[lod].cellList.cend(); ++it) {
			int index = *it;

			int x = index / (vd.num() * vd.num());
			int y = (index / vd.num()) % vd.num();
			int z = index % vd.num();

			mesh_extractor_ptr->generateCell(x, y, z);
		}
	}

	mesh_data->CollisionMeshPtr = &mesh_data->MeshSectionLodArray[vdp.collisionLOD].WholeMesh;

	return TMeshDataPtr(mesh_data);
}


TMeshDataPtr polygonizeVoxelGridNoLOD(const TVoxelData &vd, const TVoxelDataParam &vdp) {
	TMeshData* mesh_data = new TMeshData();
	VoxelMeshExtractorPtr mesh_extractor_ptr = VoxelMeshExtractorPtr(new VoxelMeshExtractor(mesh_data->MeshSectionLodArray[0], vd, vdp));

	int step = vdp.step();

	for (auto x = 0; x < vd.num() - step; x += step) {
		for (auto y = 0; y < vd.num() - step; y += step) {
			for (auto z = 0; z < vd.num() - step; z += step) {
				mesh_extractor_ptr->generateCell(x, y, z);
			}
		}
	}

	mesh_data->CollisionMeshPtr = &mesh_data->MeshSectionLodArray[0].WholeMesh;

	return TMeshDataPtr(mesh_data);
}

TMeshDataPtr polygonizeVoxelGridWithLOD(const TVoxelData &vd, const TVoxelDataParam &vdp) {
	TMeshData* mesh_data = new TMeshData();
	std::vector<VoxelMeshExtractorPtr> MeshExtractorLod;
	//VoxelMeshExtractorPtr MeshExtractorLod[LOD_ARRAY_SIZE];

	static const int max_lod = LOD_ARRAY_SIZE;

	// create mesh extractor for each LOD
	for (auto lod = 0; lod < max_lod; lod++) {
		TVoxelDataParam me_vdp = vdp;
		me_vdp.lod = lod;
		VoxelMeshExtractorPtr me_ptr = VoxelMeshExtractorPtr(new VoxelMeshExtractor(mesh_data->MeshSectionLodArray[lod], vd, me_vdp));
		MeshExtractorLod.push_back(me_ptr);
	}

	int step = vdp.step();

	for (auto x = 0; x < vd.num() - step; x += step) {
		for (auto y = 0; y < vd.num() - step; y += step) {
			for (auto z = 0; z < vd.num() - step; z += step) {
				// generate mesh for each LOD
				//==================================================================
				for (auto i = 0; i < max_lod; i++) {
					int s = 1 << i;
					if (x % s == 0 && y % s == 0 && z % s == 0) {
						VoxelMeshExtractorPtr me_ptr = MeshExtractorLod[i];
						me_ptr->generateCell(x, y, z);
					}
				}
				//==================================================================
			}
		}
	}

	mesh_data->CollisionMeshPtr = &mesh_data->MeshSectionLodArray[vdp.collisionLOD].WholeMesh;

	return TMeshDataPtr(mesh_data);
}

TMeshDataPtr sandboxVoxelGenerateMesh(const TVoxelData &vd, const TVoxelDataParam &vdp) {
	if (vd.isSubstanceCacheValid()) {
		//for (auto lod = 0; lod < LOD_ARRAY_SIZE; lod++) {
		//	UE_LOG(LogTemp, Warning, TEXT("SubstanceCacheLOD -> %d ---> %f %f %f -> %d elenents"), lod, vd.getOrigin().X, vd.getOrigin().Y, vd.getOrigin().Z, vd.substanceCacheLOD[lod].cellList.size());
		//}

		return vdp.bGenerateLOD ? polygonizeCellSubstanceCacheLOD(vd, vdp) : polygonizeCellSubstanceCacheNoLOD(vd, vdp);
	}

	return vdp.bGenerateLOD ? polygonizeVoxelGridWithLOD(vd, vdp) : polygonizeVoxelGridNoLOD(vd, vdp);
}

// =================================================================
// utils
// =================================================================

extern FVector sandboxConvertVectorToCubeIndex(FVector vec) {
	return sandboxSnapToGrid(vec, 200);
}

extern FVector sandboxSnapToGrid(FVector vec, float grid_range) {
	FVector tmp(vec);
	tmp /= grid_range;
	//FVector tmp2(std::round(tmp.X), std::round(tmp.Y), std::round(tmp.Z));
	FVector tmp2((int)tmp.X, (int)tmp.Y, (int)tmp.Z);
	tmp2 *= grid_range;
	return FVector((int)tmp2.X, (int)tmp2.Y, (int)tmp2.Z);
}

FVector sandboxGridIndex(const FVector& v, int range) {
	FVector tmp(v);

	const int r = range / 2;

	tmp.X = (tmp.X > 0) ? tmp.X + r : tmp.X - r;
	tmp.Y = (tmp.Y > 0) ? tmp.Y + r : tmp.Y - r;
	tmp.Z = (tmp.Z > 0) ? tmp.Z + r : tmp.Z - r;

	tmp /= range;

	return FVector((int)tmp.X, (int)tmp.Y, (int)tmp.Z);
}


