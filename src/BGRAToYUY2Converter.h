#pragma once
#include "Utils.h"
#include <chrono>

struct ConversionParams
{
    UINT ImageWidth;
    UINT ImageHeight;
    UINT OutputStride;
    UINT Padding;
};

class BGRAToYUY2Converter
{
public:
    BGRAToYUY2Converter();
    ~BGRAToYUY2Converter();

    HRESULT Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    HRESULT Convert(ID3D11Texture2D* inputTexture, ID3D11Buffer* outputBuffer,
                   UINT width, UINT height);
    HRESULT CreateOutputBuffer(UINT width, UINT height, ID3D11Buffer** outBuffer);
    HRESULT ReadOutputBuffer(ID3D11Buffer* buffer, UINT width, UINT height, 
                            BYTE** outData, UINT& dataSize);
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