#pragma once

#include <vector>
#include <d3dx9.h>

// Mesh vertex structure compatible with Direct3D 9 / Xbox 360 rendering
struct MESHVERTEX {
    D3DXVECTOR3 pos;
    D3DXVECTOR3 normal;
    DWORD       color;
};

#define D3DFVF_MESHVERTEX (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE)

// Function prototype for parsing Wavefront .obj files
bool LoadObjModel(const char* filename, std::vector<MESHVERTEX>& outVertices, std::vector<WORD>& outIndices);