#include "stdafx.h"
#include "Objloader.h"
#include <fstream>
#include <sstream>

int ParseObjIndex(const std::string& token)
{
    size_t slash = token.find('/');
    if (slash != std::string::npos) {
        return std::stoi(token.substr(0, slash)) - 1;
    }
    return std::stoi(token) - 1;
}

bool LoadObjModel(const char* filename, std::vector<MESHVERTEX>& outVertices, std::vector<WORD>& outIndices)
{
    std::ifstream file(filename);
    if (!file.is_open()) return false;

    std::vector<D3DXVECTOR3> tempPositions;
    std::string line;

    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if (type == "v")
        {
            D3DXVECTOR3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            tempPositions.push_back(pos);
        }
        else if (type == "f")
        {
            std::string v1, v2, v3;
            iss >> v1 >> v2 >> v3;

            int idx1 = ParseObjIndex(v1);
            int idx2 = ParseObjIndex(v2);
            int idx3 = ParseObjIndex(v3);

            MESHVERTEX v;
            v.color = D3DCOLOR_XRGB(40, 200, 50);

            if (idx1 >= 0 && idx1 < (int)tempPositions.size()) {
                v.pos = tempPositions[idx1];
                outVertices.push_back(v);
                outIndices.push_back((WORD)(outVertices.size() - 1));
            }

            if (idx2 >= 0 && idx2 < (int)tempPositions.size()) {
                v.pos = tempPositions[idx2];
                outVertices.push_back(v);
                outIndices.push_back((WORD)(outVertices.size() - 1));
            }

            if (idx3 >= 0 && idx3 < (int)tempPositions.size()) {
                v.pos = tempPositions[idx3];
                outVertices.push_back(v);
                outIndices.push_back((WORD)(outVertices.size() - 1));
            }
        }
    }

    file.close();
    return !outVertices.empty();
}