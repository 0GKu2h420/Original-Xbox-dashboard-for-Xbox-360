#include "StdAfx.h"
#include <vector>
#include <string>
#include <xaudio2.h>
#include <process.h>
#include <stdio.h>

// Direct3D 9 FVF Constants for Xbox 360 compatibility if not in headers
#ifndef D3DFVF_XYZRHW
#define D3DFVF_XYZRHW 0x004
#endif

// Direct3D Globals
LPDIRECT3D9         g_pD3D       = NULL;
LPDIRECT3DDEVICE9    g_pd3dDevice = NULL;

// Direct3D Texture Pointers
LPDIRECT3DTEXTURE9    g_pTexCellWall   = NULL;
LPDIRECT3DTEXTURE9    g_pTexGameHilite = NULL;
LPDIRECT3DTEXTURE9    g_pTexXboxLogo   = NULL;

// HLSL Shader Globals
LPDIRECT3DVERTEXSHADER9 g_pVertexShader = NULL;
LPDIRECT3DPIXELSHADER9  g_pPixelShader  = NULL;
LPDIRECT3DVERTEXDECLARATION9 g_pVertexDecl = NULL;

// Particle System Globals
const int             MAX_PARTICLES = 100;
struct PARTICLE {
    D3DXVECTOR3 pos;
    D3DXVECTOR3 vel;
    float         scale;
    float         alpha;
};
PARTICLE              g_Particles[MAX_PARTICLES];
LPDIRECT3DVERTEXBUFFER9 g_pParticleVB = NULL;

// Background 3D Cell Blob Mesh Globals
LPDIRECT3DVERTEXBUFFER9 g_pBlobVB = NULL;
LPDIRECT3DINDEXBUFFER9  g_pBlobIB = NULL;
DWORD                 g_dwBlobVertexCount = 0;
DWORD                 g_dwBlobIndexCount  = 0;

// XAudio2 Globals
IXAudio2*             g_pXAudio2          = NULL;
IXAudio2MasteringVoice* g_pMasteringVoice = NULL;

// Active audio buffers
std::vector<BYTE*>    g_ActiveAudioBuffers;

// Input, Animation & Network State Tracking
DWORD                 g_dwLastButtonState = 0;
float                 g_fNavDebounceTimer = 0.0f;
float                 g_fAnimTime         = 0.0f;

// Network & Async State
bool                  g_bIsFetchingNetData = false;
std::string           g_NetResponseStatus  = "xbGuard Active: Ready";
CRITICAL_SECTION      g_NetCritSection;

// UI Screen Vertex Structure using raw transformed pixel coordinates (-0.5 offset standard for DX9)
struct SCREENVERTEX {
    float x, y, z, rhw;
    DWORD color;
    float u, v;
};
#define D3DFVF_SCREENVERTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

// Standard 3D Mesh Vertex Structure
struct MESHVERTEX {
    D3DXVECTOR3 pos;
    D3DXVECTOR3 normal;
    DWORD         color;
};
#define D3DFVF_MESHVERTEX (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE)

// Menu State Machine
enum MenuState {
    MENU_MAIN = 0,
    MENU_MEMORY,
    MENU_MUSIC,
    MENU_LIVE,
    MENU_SETTINGS
};

MenuState             g_CurrentState      = MENU_MAIN;
int                   g_nSelectedMenuItem = 0;
const int             g_nTotalMenuItems   = 4;
int                   g_nSubMenuIndex     = 0;

// Audio timing structure based on default.xap
struct PeriodicAudioGroup {
    float fTimer;
    float fBasePeriod;
    int   nNoiseRange;
    float fVolume;
};

PeriodicAudioGroup g_SteamGroup  = { 60.0f,  60.0f, 20, 0.80f };
PeriodicAudioGroup g_VoicesGroup = { 120.0f, 120.0f, 30, 0.80f };
PeriodicAudioGroup g_CommGroup   = { 45.0f,  45.0f, 15, 0.85f };

// HLSL Shader Strings with Matrix Transformation Constant c0
const char* g_szVSCode = 
"float4x4 g_matWVP : register(c0);\n"
"struct VS_INPUT { float4 Pos : POSITION0; float3 Normal : NORMAL0; float4 Color : COLOR0; };\n"
"struct VS_OUTPUT { float4 Pos : POSITION0; float4 Color : COLOR0; };\n"
"VS_OUTPUT main(VS_INPUT input) {\n"
"    VS_OUTPUT output;\n"
"    output.Pos = mul(input.Pos, g_matWVP);\n"
"    output.Color = input.Color;\n"
"    return output;\n"
"}\n";

const char* g_szPSCode = 
"struct PS_INPUT { float4 Color : COLOR0; };\n"
"float4 main(PS_INPUT input) : COLOR0 {\n"
"    return input.Color;\n"
"}\n";

// Forward Declarations
HRESULT InitD3D();
HRESULT InitTexturesFromBinStream();
HRESULT LoadTextureFromBinStream(LPCSTR szBinPath, DWORD dwOccurrenceIndex, LPDIRECT3DTEXTURE9* ppTexture);
HRESULT InitMeshGeometry();
HRESULT InitShaders();
HRESULT InitParticles();
HRESULT InitXAudio2();
void UpdateParticles(float fDeltaTime);
void RenderParticles();
void RenderDashboardMesh();
void DrawTexturedRect(float x, float y, float w, float h, LPDIRECT3DTEXTURE9 pTexture, D3DCOLOR color = D3DCOLOR_XRGB(255, 255, 255));
void Draw2DRect(float x, float y, float w, float h, D3DCOLOR color);
void DrawMenuSelectionIndicators();
void DrawSubMenuLayouts();
void PlayWavFile(LPCSTR szPath, float fVolume);
void ProcessInput(float fDeltaTime);
void UpdateAudio(float fDeltaTime);
void Render();
unsigned int __stdcall NetworkWorkerThread(void* pParam);

inline UINT16 Swap16(UINT16 val) {
    return (val << 8) | (val >> 8);
}

HRESULT InitD3D()
{
    g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_pD3D) return E_FAIL;

    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.BackBufferWidth         = 1280;
    d3dpp.BackBufferHeight        = 720;
    d3dpp.BackBufferFormat        = D3DFMT_A8R8G8B8;
    d3dpp.FrontBufferFormat       = D3DFMT_LE_X8R8G8B8;
    d3dpp.MultiSampleType         = D3DMULTISAMPLE_NONE;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D24S8;
    d3dpp.SwapEffect              = D3DSWAPEFFECT_DISCARD;
    d3dpp.PresentationInterval   = D3DPRESENT_INTERVAL_ONE;

    if (FAILED(g_pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                    &d3dpp, &g_pd3dDevice)))
        return E_FAIL;

    if (FAILED(InitShaders())) return E_FAIL;
    if (FAILED(InitParticles())) return E_FAIL;

    InitTexturesFromBinStream();
    InitMeshGeometry();

    InitializeCriticalSection(&g_NetCritSection);

    return S_OK;
}

HRESULT LoadTextureFromBinStream(LPCSTR szBinPath, DWORD dwOccurrenceIndex, LPDIRECT3DTEXTURE9* ppTexture)
{
    char fullPath[256];
    sprintf_s(fullPath, 256, "usb:\\%s", szBinPath);

    FILE* pFile = NULL;
    fopen_s(&pFile, fullPath, "rb");
    if (!pFile)
    {
        sprintf_s(fullPath, 256, "game:\\%s", szBinPath);
        fopen_s(&pFile, fullPath, "rb");
        if (!pFile) return E_FAIL;
    }

    fseek(pFile, 0, SEEK_END);
    long dwFileSize = ftell(pFile);
    rewind(pFile);

    BYTE* pBuffer = new BYTE[dwFileSize];
    size_t dwBytesRead = fread(pBuffer, 1, dwFileSize, pFile);
    fclose(pFile);

    DWORD dwFoundCount = 0;
    DWORD dwTargetOffset = 0;
    bool bFound = false;

    for (DWORD i = 0; i < (DWORD)dwFileSize - 128; i++)
    {
        if (pBuffer[i] == 'D' && pBuffer[i+1] == 'D' && pBuffer[i+2] == 'S' && pBuffer[i+3] == ' ')
        {
            if (dwFoundCount == dwOccurrenceIndex)
            {
                dwTargetOffset = i;
                bFound = true;
                break;
            }
            dwFoundCount++;
        }
    }

    HRESULT hr = E_FAIL;
    if (bFound)
    {
        hr = D3DXCreateTextureFromFileInMemoryEx(
            g_pd3dDevice, 
            pBuffer + dwTargetOffset, 
            dwFileSize - dwTargetOffset,
            D3DX_DEFAULT,
            D3DX_DEFAULT,
            D3DX_DEFAULT,
            0,
            D3DFMT_UNKNOWN,
            D3DPOOL_MANAGED,
            D3DX_FILTER_NONE,
            D3DX_FILTER_NONE,
            0,
            NULL,
            NULL,
            ppTexture
        );
    }

    delete[] pBuffer;
    return hr;
}

HRESULT InitTexturesFromBinStream()
{
    LPCSTR szBinPath = "000900\\Media\\XIP\\mainmenu5.xip.d\\decompressed_stream.bin";

    LoadTextureFromBinStream(szBinPath, 0, &g_pTexCellWall);
    LoadTextureFromBinStream(szBinPath, 1, &g_pTexGameHilite);
    LoadTextureFromBinStream(szBinPath, 2, &g_pTexXboxLogo);

    if (!g_pTexCellWall) {
        if (SUCCEEDED(g_pd3dDevice->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_pTexCellWall, NULL))) {
            D3DLOCKED_RECT rect;
            if (SUCCEEDED(g_pTexCellWall->LockRect(0, &rect, NULL, 0))) {
                DWORD* pBits = (DWORD*)rect.pBits;
                for (int i = 0; i < 64 * 64; i++) pBits[i] = D3DCOLOR_ARGB(255, 20, 180, 40);
                g_pTexCellWall->UnlockRect(0);
            }
        }
    }

    if (!g_pTexGameHilite) {
        if (SUCCEEDED(g_pd3dDevice->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_pTexGameHilite, NULL))) {
            D3DLOCKED_RECT rect;
            if (SUCCEEDED(g_pTexGameHilite->LockRect(0, &rect, NULL, 0))) {
                DWORD* pBits = (DWORD*)rect.pBits;
                for (int i = 0; i < 64 * 64; i++) pBits[i] = D3DCOLOR_ARGB(200, 50, 240, 90);
                g_pTexGameHilite->UnlockRect(0);
            }
        }
    }

    if (!g_pTexXboxLogo) {
        if (SUCCEEDED(g_pd3dDevice->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_pTexXboxLogo, NULL))) {
            D3DLOCKED_RECT rect;
            if (SUCCEEDED(g_pTexXboxLogo->LockRect(0, &rect, NULL, 0))) {
                DWORD* pBits = (DWORD*)rect.pBits;
                for (int i = 0; i < 64 * 64; i++) pBits[i] = D3DCOLOR_ARGB(255, 255, 255, 255);
                g_pTexXboxLogo->UnlockRect(0);
            }
        }
    }

    return S_OK;
}

HRESULT InitMeshGeometry()
{
    MESHVERTEX vertices[] =
    {
        { D3DXVECTOR3(-1.2f, -1.2f, 1.2f), D3DXVECTOR3(0.0f, 0.0f, 1.0f), D3DCOLOR_XRGB(20, 160, 40) },
        { D3DXVECTOR3( 1.2f, -1.2f, 1.2f), D3DXVECTOR3(0.0f, 0.0f, 1.0f), D3DCOLOR_XRGB(40, 220, 60) },
        { D3DXVECTOR3( 1.2f,  1.2f, 1.2f), D3DXVECTOR3(0.0f, 0.0f, 1.0f), D3DCOLOR_XRGB(60, 255, 80) },
        { D3DXVECTOR3(-1.2f,  1.2f, 1.2f), D3DXVECTOR3(0.0f, 0.0f, 1.0f), D3DCOLOR_XRGB(20, 160, 40) },
        { D3DXVECTOR3(-1.2f, -1.2f,-1.2f), D3DXVECTOR3(0.0f, 0.0f,-1.0f), D3DCOLOR_XRGB(10, 80, 20) },
        { D3DXVECTOR3( 1.2f, -1.2f,-1.2f), D3DXVECTOR3(0.0f, 0.0f,-1.0f), D3DCOLOR_XRGB(20, 110, 30) },
        { D3DXVECTOR3( 1.2f,  1.2f,-1.2f), D3DXVECTOR3(0.0f, 0.0f,-1.0f), D3DCOLOR_XRGB(30, 140, 40) },
        { D3DXVECTOR3(-1.2f,  1.2f,-1.2f), D3DXVECTOR3(0.0f, 0.0f,-1.0f), D3DCOLOR_XRGB(10, 80, 20) },
    };
    g_dwBlobVertexCount = 8;

    WORD indices[] =
    {
        0, 1, 2, 2, 3, 0,
        4, 6, 5, 4, 7, 6,
        4, 5, 1, 4, 1, 0,
        3, 2, 6, 3, 6, 7,
        1, 5, 6, 1, 6, 2,
        4, 0, 3, 4, 3, 7
    };
    g_dwBlobIndexCount = 36;

    if (FAILED(g_pd3dDevice->CreateVertexBuffer(sizeof(vertices), D3DUSAGE_WRITEONLY, 
                                                D3DFVF_MESHVERTEX, D3DPOOL_MANAGED, 
                                                &g_pBlobVB, NULL)))
        return E_FAIL;

    void* pBuffer = NULL;
    g_pBlobVB->Lock(0, 0, &pBuffer, 0);
    memcpy(pBuffer, vertices, sizeof(vertices));
    g_pBlobVB->Unlock();

    if (FAILED(g_pd3dDevice->CreateIndexBuffer(sizeof(indices), D3DUSAGE_WRITEONLY, 
                                               D3DFMT_INDEX16, D3DPOOL_MANAGED, 
                                               &g_pBlobIB, NULL)))
        return E_FAIL;

    g_pBlobIB->Lock(0, 0, &pBuffer, 0);
    memcpy(pBuffer, indices, sizeof(indices));
    g_pBlobIB->Unlock();

    return S_OK;
}

HRESULT InitShaders()
{
    LPD3DXBUFFER pCode = NULL;
    LPD3DXBUFFER pErrors = NULL;

    if (SUCCEEDED(D3DXCompileShader(g_szVSCode, (UINT)strlen(g_szVSCode), NULL, NULL, "main", "vs_3_0", 0, &pCode, &pErrors, NULL)))
    {
        g_pd3dDevice->CreateVertexShader((DWORD*)pCode->GetBufferPointer(), &g_pVertexShader);
        pCode->Release();
    }

    if (SUCCEEDED(D3DXCompileShader(g_szPSCode, (UINT)strlen(g_szPSCode), NULL, NULL, "main", "ps_3_0", 0, &pCode, &pErrors, NULL)))
    {
        g_pd3dDevice->CreatePixelShader((DWORD*)pCode->GetBufferPointer(), &g_pPixelShader);
        pCode->Release();
    }

    D3DVERTEXELEMENT9 decl[] =
    {
        { 0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
        { 0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
        D3DDECL_END()
    };

    return g_pd3dDevice->CreateVertexDeclaration(decl, &g_pVertexDecl);
}

HRESULT InitParticles()
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        g_Particles[i].pos = D3DXVECTOR3(((rand() % 2000) / 1000.0f) - 1.0f,
                                         ((rand() % 2000) / 1000.0f) - 1.0f, 0.0f);
        g_Particles[i].vel = D3DXVECTOR3(((rand() % 100) / 10000.0f) - 0.005f,
                                         ((rand() % 100) / 10000.0f) + 0.002f, 0.0f);
        g_Particles[i].scale = ((rand() % 100) / 5000.0f) + 0.01f;
        g_Particles[i].alpha = ((rand() % 100) / 100.0f);
    }

    return S_OK;
}

void UpdateParticles(float fDeltaTime)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        g_Particles[i].pos += g_Particles[i].vel;

        if (g_Particles[i].pos.y > 1.0f)  g_Particles[i].pos.y = -1.0f;
        if (g_Particles[i].pos.x > 1.0f)  g_Particles[i].pos.x = -1.0f;
        if (g_Particles[i].pos.x < -1.0f) g_Particles[i].pos.x = 1.0f;
    }
}

void RenderParticles()
{
    g_pd3dDevice->SetVertexShader(NULL);
    g_pd3dDevice->SetPixelShader(NULL);
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetFVF(D3DFVF_SCREENVERTEX);

    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        float px = (g_Particles[i].pos.x + 1.0f) * 640.0f;
        float py = (1.0f - g_Particles[i].pos.y) * 360.0f;
        float s  = g_Particles[i].scale * 400.0f;

        DWORD col = D3DCOLOR_ARGB(150, 50, 240, 80);

        SCREENVERTEX verts[4] = {
            { px - s - 0.5f, py - s - 0.5f, 0.0f, 1.0f, col, 0.0f, 0.0f },
            { px + s - 0.5f, py - s - 0.5f, 0.0f, 1.0f, col, 1.0f, 0.0f },
            { px - s - 0.5f, py + s - 0.5f, 0.0f, 1.0f, col, 0.0f, 1.0f },
            { px + s - 0.5f, py + s - 0.5f, 0.0f, 1.0f, col, 1.0f, 1.0f }
        };

        g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(SCREENVERTEX));
    }
}

void RenderDashboardMesh()
{
    if (!g_pBlobVB || !g_pBlobIB) return;

    D3DXMATRIX matWorld, matRotY, matRotX;
    D3DXMatrixRotationY(&matRotY, g_fAnimTime * 0.3f);
    D3DXMatrixRotationX(&matRotX, g_fAnimTime * 0.15f);
    matWorld = matRotX * matRotY;

    D3DXMATRIX matView, matProj;
    D3DXVECTOR3 vEye(0.0f, 0.0f, -4.5f), vAt(0.0f, 0.0f, 0.0f), vUp(0.0f, 1.0f, 0.0f);
    D3DXMatrixLookAtLH(&matView, &vEye, &vAt, &vUp);
    D3DXMatrixPerspectiveFovLH(&matProj, D3DX_PI / 4.0f, 1280.0f / 720.0f, 0.1f, 100.0f);

    D3DXMATRIX matWVP = matWorld * matView * matProj;
	D3DXMatrixTranspose(&matWVP, &matWVP);
	g_pd3dDevice->SetVertexShaderConstantF(0, (float*)&matWVP, 4);

    g_pd3dDevice->SetVertexDeclaration(g_pVertexDecl);
    g_pd3dDevice->SetVertexShader(g_pVertexShader);
    g_pd3dDevice->SetPixelShader(g_pPixelShader);

    g_pd3dDevice->SetStreamSource(0, g_pBlobVB, 0, sizeof(MESHVERTEX));
    g_pd3dDevice->SetIndices(g_pBlobIB);

    if (g_pTexCellWall)
        g_pd3dDevice->SetTexture(0, g_pTexCellWall);

    g_pd3dDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, g_dwBlobVertexCount, 0, g_dwBlobIndexCount / 3);
    
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetVertexShader(NULL);
    g_pd3dDevice->SetPixelShader(NULL);
}

HRESULT InitXAudio2()
{
    if (FAILED(XAudio2Create(&g_pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR)))
        return E_FAIL;

    return g_pXAudio2->CreateMasteringVoice(&g_pMasteringVoice);
}

void PlayWavFile(LPCSTR szPath, float fVolume)
{
    char fullPath[256];
    sprintf_s(fullPath, 256, "usb:\\%s", szPath);

    FILE* pFile = NULL;
    fopen_s(&pFile, fullPath, "rb");
    if (!pFile)
    {
        sprintf_s(fullPath, 256, "game:\\%s", szPath);
        fopen_s(&pFile, fullPath, "rb");
        if (!pFile) return;
    }

    fseek(pFile, 0, SEEK_END);
    long dwFileSize = ftell(pFile);
    rewind(pFile);

    BYTE* pFileData = new BYTE[dwFileSize];
    fread(pFileData, 1, dwFileSize, pFile);
    fclose(pFile);

    if (pFileData[0] != 'R' || pFileData[1] != 'I' || pFileData[2] != 'F' || pFileData[3] != 'F' ||
        pFileData[8] != 'W' || pFileData[9] != 'A' || pFileData[10] != 'V' || pFileData[11] != 'E')
    {
        delete[] pFileData;
        return;
    }

    BYTE* pCurrent = pFileData + 12;
    WAVEFORMATEX wfx = {0};
    bool bFormatFound = false;
    BYTE* pAudioBytes = NULL;
    DWORD dwAudioSize = 0;

    while (pCurrent < (pFileData + dwFileSize - 8))
    {
        DWORD dwChunkSize = pCurrent[4] | (pCurrent[5] << 8) | (pCurrent[6] << 16) | (pCurrent[7] << 24);

        if (pCurrent[0] == 'f' && pCurrent[1] == 'm' && pCurrent[2] == 't' && pCurrent[3] == ' ')
        {
            wfx.wFormatTag     = pCurrent[8]  | (pCurrent[9] << 8);
            wfx.nChannels      = pCurrent[10] | (pCurrent[11] << 8);
            wfx.nSamplesPerSec  = pCurrent[12] | (pCurrent[13] << 8) | (pCurrent[14] << 16) | (pCurrent[15] << 24);
            wfx.nAvgBytesPerSec = pCurrent[16] | (pCurrent[17] << 8) | (pCurrent[18] << 16) | (pCurrent[19] << 24);
            wfx.nBlockAlign     = pCurrent[20] | (pCurrent[21] << 8);
            wfx.wBitsPerSample  = pCurrent[22] | (pCurrent[23] << 8);
            wfx.cbSize          = 0;

            bFormatFound = true;
        }
        else if (pCurrent[0] == 'd' && pCurrent[1] == 'a' && pCurrent[2] == 't' && pCurrent[3] == 'a')
        {
            pAudioBytes = pCurrent + 8;
            dwAudioSize = dwChunkSize;
            break;
        }

        pCurrent += 8 + dwChunkSize;
    }

    if (bFormatFound && pAudioBytes)
    {
        if (wfx.wBitsPerSample == 16)
        {
            UINT16* pSamples = (UINT16*)pAudioBytes;
            DWORD dwSampleCount = dwAudioSize / 2;
            for (DWORD i = 0; i < dwSampleCount; i++)
                pSamples[i] = Swap16(pSamples[i]);
        }

        IXAudio2SourceVoice* pSourceVoice = NULL;
        if (SUCCEEDED(g_pXAudio2->CreateSourceVoice(&pSourceVoice, &wfx)))
        {
            pSourceVoice->SetVolume(fVolume);

            XAUDIO2_BUFFER buffer = {0};
            buffer.AudioBytes = dwAudioSize;
            buffer.pAudioData = pAudioBytes;
            buffer.Flags      = XAUDIO2_END_OF_STREAM;

            pSourceVoice->SubmitSourceBuffer(&buffer);
            pSourceVoice->Start(0);

            g_ActiveAudioBuffers.push_back(pFileData);
            return;
        }
    }

    delete[] pFileData;
}

unsigned int __stdcall NetworkWorkerThread(void* pParam)
{
    EnterCriticalSection(&g_NetCritSection);
    g_NetResponseStatus = "xbGuard Plugin Active: Online Ready";
    g_bIsFetchingNetData = false;
    LeaveCriticalSection(&g_NetCritSection);

    return 0;
}

void ProcessInput(float fDeltaTime)
{
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));

    if (g_fNavDebounceTimer > 0.0f)
        g_fNavDebounceTimer -= fDeltaTime;

    if (XInputGetState(0, &state) == ERROR_SUCCESS)
    {
        DWORD dwButtons = state.Gamepad.wButtons;
        SHORT sThumbY   = state.Gamepad.sThumbLY;

        bool bNavUp   = (dwButtons & XINPUT_GAMEPAD_DPAD_UP)   || (sThumbY > 16000);
        bool bNavDown = (dwButtons & XINPUT_GAMEPAD_DPAD_DOWN) || (sThumbY < -16000);

        if (g_CurrentState == MENU_MAIN)
        {
            if (g_fNavDebounceTimer <= 0.0f)
            {
                if (bNavUp)
                {
                    g_nSelectedMenuItem = (g_nSelectedMenuItem - 1 + g_nTotalMenuItems) % g_nTotalMenuItems;
                    PlayWavFile("Audio\\MainAudio\\Global Scroll Beep.wav", 0.85f);
                    g_fNavDebounceTimer = 0.18f;
                }
                else if (bNavDown)
                {
                    g_nSelectedMenuItem = (g_nSelectedMenuItem + 1) % g_nTotalMenuItems;
                    PlayWavFile("Audio\\MainAudio\\Global Scroll Beep.wav", 0.85f);
                    g_fNavDebounceTimer = 0.18f;
                }
            }

            if ((dwButtons & XINPUT_GAMEPAD_A) && !(g_dwLastButtonState & XINPUT_GAMEPAD_A))
            {
                PlayWavFile("Audio\\MainAudio\\Global A Button Select.wav", 0.92f);

                switch (g_nSelectedMenuItem)
                {
                    case 0:
                        g_CurrentState = MENU_MEMORY;
                        PlayWavFile("Audio\\TransitionAudio\\Games Main Menu In_LR.wav", 0.92f);
                        break;
                    case 1:
                        g_CurrentState = MENU_MUSIC;
                        PlayWavFile("Audio\\TransitionAudio\\Music Main Menu In_LR.wav", 0.92f);
                        break;
                    case 2:
                        g_CurrentState = MENU_LIVE;
                        PlayWavFile("Audio\\TransitionAudio\\Settings Sub Menu In_LR.wav", 0.92f);
                        
                        EnterCriticalSection(&g_NetCritSection);
                        if (!g_bIsFetchingNetData) {
                            g_bIsFetchingNetData = true;
                            g_NetResponseStatus = "Validating with xbGuard...";
                            uintptr_t threadHandle = _beginthreadex(NULL, 0, &NetworkWorkerThread, NULL, 0, NULL);
                            if (threadHandle) CloseHandle((HANDLE)threadHandle);
                        }
                        LeaveCriticalSection(&g_NetCritSection);
                        break;
                    case 3:
                        g_CurrentState = MENU_SETTINGS;
                        PlayWavFile("Audio\\TransitionAudio\\Settings Main Menu In_LR.wav", 0.92f);
                        break;
                }
                g_nSubMenuIndex = 0;
            }
        }
        else
        {
            if (g_fNavDebounceTimer <= 0.0f)
            {
                if (bNavUp)
                {
                    g_nSubMenuIndex = (g_nSubMenuIndex - 1 + 4) % 4;
                    
                    if (g_CurrentState == MENU_MEMORY)
                        PlayWavFile("Audio\\MemoryAudio\\Memory Controller Select.wav", 0.90f);
                    else if (g_CurrentState == MENU_MUSIC)
                        PlayWavFile("Audio\\MusicAudio\\Music CD Select.wav", 0.85f);
                    else if (g_CurrentState == MENU_SETTINGS)
                        PlayWavFile("Audio\\SettingsAudio\\Settings Lang SubMenu Sel.wav", 0.90f);
                    else
                        PlayWavFile("Audio\\MainAudio\\Global Scroll Beep.wav", 0.85f);

                    g_fNavDebounceTimer = 0.18f;
                }
                else if (bNavDown)
                {
                    g_nSubMenuIndex = (g_nSubMenuIndex + 1) % 4;

                    if (g_CurrentState == MENU_MEMORY)
                        PlayWavFile("Audio\\MemoryAudio\\Memory Controller Select.wav", 0.90f);
                    else if (g_CurrentState == MENU_MUSIC)
                        PlayWavFile("Audio\\MusicAudio\\Music CD Select.wav", 0.85f);
                    else if (g_CurrentState == MENU_SETTINGS)
                        PlayWavFile("Audio\\SettingsAudio\\Settings Lang SubMenu Sel.wav", 0.90f);
                    else
                        PlayWavFile("Audio\\MainAudio\\Global Scroll Beep.wav", 0.85f);

                    g_fNavDebounceTimer = 0.18f;
                }
            }

            if ((dwButtons & XINPUT_GAMEPAD_A) && !(g_dwLastButtonState & XINPUT_GAMEPAD_A))
            {
                if (g_CurrentState == MENU_MEMORY)
                    PlayWavFile("Audio\\MemoryAudio\\Memory Games Select.wav", 0.90f);
                else if (g_CurrentState == MENU_MUSIC)
                    PlayWavFile("Audio\\MusicAudio\\Games Info Screen In MSurr.wav", 0.92f);
                else if (g_CurrentState == MENU_SETTINGS)
                    PlayWavFile("Audio\\SettingsAudio\\Settings Parent SubMenu Sel.wav", 0.90f);
            }

            if ((dwButtons & XINPUT_GAMEPAD_B) && !(g_dwLastButtonState & XINPUT_GAMEPAD_B))
            {
                PlayWavFile("Audio\\MainAudio\\Global B Button Back.wav", 0.92f);

                switch (g_CurrentState)
                {
                    case MENU_MEMORY:   PlayWavFile("Audio\\TransitionAudio\\Games Main Menu Out_LR.wav", 0.92f); break;
                    case MENU_MUSIC:    PlayWavFile("Audio\\TransitionAudio\\Music Main Menu Out_LR.wav", 0.92f); break;
                    case MENU_LIVE:     PlayWavFile("Audio\\TransitionAudio\\Settings Sub Menu Out_LR.wav", 0.92f); break;
                    case MENU_SETTINGS: PlayWavFile("Audio\\TransitionAudio\\Settings Main Menu Out_LR.wav", 0.92f); break;
                }

                g_CurrentState = MENU_MAIN;
            }
        }

        g_dwLastButtonState = dwButtons;
    }
}

void UpdateAudio(float fDeltaTime)
{
    char szPath[256];

    // 1. Steam Vents
    g_SteamGroup.fTimer -= fDeltaTime;
    if (g_SteamGroup.fTimer <= 0.0f)
    {
        int index = (rand() % 7) + 1;
        sprintf_s(szPath, 256, "Audio\\AmbientAudio\\AMB_EC_Steam%d.wav", index);
        PlayWavFile(szPath, g_SteamGroup.fVolume);
        g_SteamGroup.fTimer = g_SteamGroup.fBasePeriod + (float)(rand() % g_SteamGroup.nNoiseRange);
    }

    // 2. Ambient Voices
    g_VoicesGroup.fTimer -= fDeltaTime;
    if (g_VoicesGroup.fTimer <= 0.0f)
    {
        int index = (rand() % 12) + 1;
        sprintf_s(szPath, 256, "Audio\\AmbientAudio\\AMB_EC_Voices%d.wav", index);
        PlayWavFile(szPath, g_VoicesGroup.fVolume);
        g_VoicesGroup.fTimer = g_VoicesGroup.fBasePeriod + (float)(rand() % g_VoicesGroup.nNoiseRange);
    }

    // 3. Comm Chatter
    g_CommGroup.fTimer -= fDeltaTime;
    if (g_CommGroup.fTimer <= 0.0f)
    {
        int index = (rand() % 9) + 1;
        sprintf_s(szPath, 256, "Audio\\AmbientAudio\\comm voice %d.wav", index);
        PlayWavFile(szPath, g_CommGroup.fVolume);
        g_CommGroup.fTimer = g_CommGroup.fBasePeriod + (float)(rand() % g_CommGroup.nNoiseRange);
    }
}

void DrawTexturedRect(float x, float y, float w, float h, LPDIRECT3DTEXTURE9 pTexture, D3DCOLOR color)
{
    SCREENVERTEX vertices[4] =
    {
        { x - 0.5f,     y - 0.5f,     0.0f, 1.0f, color, 0.0f, 0.0f },
        { x + w - 0.5f, y - 0.5f,     0.0f, 1.0f, color, 1.0f, 0.0f },
        { x - 0.5f,     y + h - 0.5f, 0.0f, 1.0f, color, 0.0f, 1.0f },
        { x + w - 0.5f, y + h - 0.5f, 0.0f, 1.0f, color, 1.0f, 1.0f }
    };

    g_pd3dDevice->SetVertexShader(NULL);
    g_pd3dDevice->SetPixelShader(NULL);
    g_pd3dDevice->SetVertexDeclaration(NULL); // Clear out the 3D vertex decl
    g_pd3dDevice->SetTexture(0, pTexture);
    g_pd3dDevice->SetFVF(D3DFVF_SCREENVERTEX);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(SCREENVERTEX));
    g_pd3dDevice->SetTexture(0, NULL);
}

void Draw2DRect(float x, float y, float w, float h, D3DCOLOR color)
{
    DrawTexturedRect(x, y, w, h, NULL, color);
}

void DrawMenuSelectionIndicators()
{
    if (g_pTexXboxLogo)
        DrawTexturedRect(950.0f, 100.0f, 128.0f, 128.0f, g_pTexXboxLogo);

    for (int i = 0; i < g_nTotalMenuItems; i++)
    {
        float yPos = 240.0f + (i * 90.0f);

        if (i == g_nSelectedMenuItem)
        {
            if (g_pTexGameHilite)
            {
                DrawTexturedRect(150.0f, yPos - 5.0f, 420.0f, 60.0f, g_pTexGameHilite);
            }
            else
            {
                Draw2DRect(150.0f, yPos, 400.0f, 50.0f, D3DCOLOR_XRGB(40, 220, 60));
                Draw2DRect(135.0f, yPos, 15.0f, 50.0f, D3DCOLOR_XRGB(180, 255, 180));
            }
        }
        else
        {
            Draw2DRect(150.0f, yPos, 340.0f, 45.0f, D3DCOLOR_XRGB(10, 45, 20));
        }
    }
}

void DrawSubMenuLayouts()
{
    Draw2DRect(100.0f, 100.0f, 1080.0f, 50.0f, D3DCOLOR_XRGB(30, 160, 40));
    Draw2DRect(100.0f, 160.0f, 1080.0f, 480.0f, D3DCOLOR_XRGB(6, 24, 12));

    for (int i = 0; i < 4; i++)
    {
        float yPos = 195.0f + (i * 105.0f);
        D3DCOLOR itemColor = (i == g_nSubMenuIndex) ? D3DCOLOR_XRGB(50, 200, 70) : D3DCOLOR_XRGB(15, 50, 25);
        
        switch (g_CurrentState)
        {
            case MENU_MEMORY:
                Draw2DRect(130.0f, yPos, 450.0f, 75.0f, itemColor);
                Draw2DRect(600.0f, yPos, 540.0f, 75.0f, D3DCOLOR_XRGB(10, 35, 15));
                Draw2DRect(620.0f, yPos + 45.0f, 400.0f, 12.0f, D3DCOLOR_XRGB(80, 240, 100));
                break;

            case MENU_MUSIC:
                Draw2DRect(130.0f, yPos, 1020.0f, 75.0f, itemColor);
                Draw2DRect(155.0f, yPos + 15.0f, 45.0f, 45.0f, D3DCOLOR_XRGB(100, 255, 120));
                break;

            case MENU_LIVE:
                Draw2DRect(130.0f, yPos, 1020.0f, 75.0f, itemColor);
                Draw2DRect(1080.0f, yPos + 20.0f, 35.0f, 35.0f, D3DCOLOR_XRGB(60, 220, 80));
                break;

            case MENU_SETTINGS:
                Draw2DRect(130.0f, yPos, 1020.0f, 75.0f, itemColor);
                Draw2DRect(145.0f, yPos + 12.0f, 8.0f, 50.0f, D3DCOLOR_XRGB(120, 255, 130));
                break;
        }
    }
}

void Render()
{
    if (!g_pd3dDevice) return;

    D3DCOLOR clearColor = D3DCOLOR_XRGB(2, 12, 4);
    switch (g_CurrentState)
    {
        case MENU_MEMORY:   clearColor = D3DCOLOR_XRGB(10, 20, 8);  break;
        case MENU_MUSIC:    clearColor = D3DCOLOR_XRGB(2, 18, 15);  break;
        case MENU_LIVE:     clearColor = D3DCOLOR_XRGB(5, 12, 20);  break;
        case MENU_SETTINGS: clearColor = D3DCOLOR_XRGB(10, 10, 20); break;
    }

    g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clearColor, 1.0f, 0);

    if (SUCCEEDED(g_pd3dDevice->BeginScene()))
    {
        // 1. Render Background 3D Mesh
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
        g_pd3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        RenderDashboardMesh();

        // 2. Render UI Overlays and Particles in 2D screen pixel space
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

        RenderParticles();

        // Ensure world/view matrices are identity or reset for screen-space 2D drawing if needed
        D3DXMATRIX identity;
        D3DXMatrixIdentity(&identity);
        g_pd3dDevice->SetVertexShaderConstantF(0, (float*)&identity, 4);

        if (g_CurrentState == MENU_MAIN)
            DrawMenuSelectionIndicators();
        else
            DrawSubMenuLayouts();

        g_pd3dDevice->EndScene();
    }

    g_pd3dDevice->Present(NULL, NULL, NULL, NULL);
}

void __cdecl main()
{
    if (FAILED(InitD3D())) return;
    if (FAILED(InitXAudio2())) return;

    PlayWavFile("Audio\\AmbientAudio\\AMB_12_HYDROTHUNDER_LR.wav", 0.95f);

    DWORD dwLastTime = GetTickCount();

    while (TRUE)
    {
        DWORD dwCurrentTime = GetTickCount();
        float fDeltaTime = (dwCurrentTime - dwLastTime) / 1000.0f;
        dwLastTime = dwCurrentTime;

        g_fAnimTime += fDeltaTime;

        ProcessInput(fDeltaTime);
        UpdateParticles(fDeltaTime);
        UpdateAudio(fDeltaTime);
        Render();
    }
}