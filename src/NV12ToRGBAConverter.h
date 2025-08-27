#pragma once
#include "Utils.h"
#include <chrono>

struct NV12ConversionParams
{
    UINT ImageWidth;
    UINT ImageHeight;
    UINT YPlaneStride;    // Y平面的行步长
    UINT UVPlaneStride;   // UV平面的行步长
};

class NV12ToRGBAConverter
{
public:
    NV12ToRGBAConverter();
    ~NV12ToRGBAConverter();

    HRESULT Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    HRESULT Convert(ID3D11Buffer* nv12Buffer, ID3D11Texture2D* outputTexture,
                   UINT width, UINT height);
    HRESULT CreateOutputTexture(UINT width, UINT height, ID3D11Texture2D** outTexture);
    HRESULT CreateNV12InputBuffer(UINT width, UINT height, ID3D11Buffer** outBuffer);
    HRESULT WriteNV12Data(ID3D11Buffer* buffer, const BYTE* yPlaneData, const BYTE* uvPlaneData,
                         UINT width, UINT height);
    void Cleanup();

private:
    HRESULT CompileShader();

    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    ID3D11ComputeShader* m_computeShader;
    ID3D11Buffer* m_constantBuffer;
    bool m_initialized;
    
    // 用于控制日志输出频率
    std::chrono::steady_clock::time_point m_lastLogTime;
};
