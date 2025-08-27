#include "NV12ToRGBAConverter.h"
#include <d3dcompiler.h>
#include <fstream>
#include <vector>

NV12ToRGBAConverter::NV12ToRGBAConverter()
    : m_device(nullptr)
    , m_context(nullptr)
    , m_computeShader(nullptr)
    , m_constantBuffer(nullptr)
    , m_initialized(false)
    , m_lastLogTime(std::chrono::steady_clock::now())
{
}

NV12ToRGBAConverter::~NV12ToRGBAConverter()
{
    Cleanup();
}

HRESULT NV12ToRGBAConverter::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!device || !context)
        return E_INVALIDARG;

    m_device = device;
    m_context = context;
    m_device->AddRef();
    m_context->AddRef();

    try
    {
        ThrowIfFailed(CompileShader(), "Failed to compile NV12 to RGBA shader");

        // 创建常量缓冲区
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(NV12ConversionParams);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        ThrowIfFailed(m_device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer),
                     "Failed to create constant buffer");

        m_initialized = true;
        LogMessage("NV12 to RGBA converter initialized successfully");
        return S_OK;
    }
    catch (const std::exception& e)
    {
        LogError(std::string("NV12 converter initialization failed: ") + e.what());
        Cleanup();
        return E_FAIL;
    }
}

HRESULT NV12ToRGBAConverter::CompileShader()
{
    // 读取shader文件
    std::ifstream shaderFile("shaders/NV12ToRGBA.hlsl");
    if (!shaderFile.is_open())
    {
        LogError("Cannot open shader file: shaders/NV12ToRGBA.hlsl");
        return E_FAIL;
    }

    std::string shaderSource((std::istreambuf_iterator<char>(shaderFile)),
                            std::istreambuf_iterator<char>());
    shaderFile.close();

    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(
        shaderSource.c_str(),
        shaderSource.size(),
        "NV12ToRGBA.hlsl",
        nullptr,
        nullptr,
        "CSMain",
        "cs_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
        0,
        &shaderBlob,
        &errorBlob
    );

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            LogError(std::string("NV12 shader compilation error: ") + 
                    (char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        return hr;
    }

    hr = m_device->CreateComputeShader(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        nullptr,
        &m_computeShader
    );

    shaderBlob->Release();
    return hr;
}

HRESULT NV12ToRGBAConverter::CreateNV12InputBuffer(UINT width, UINT height, ID3D11Buffer** outBuffer)
{
    // NV12格式大小：Y平面 + UV平面（UV是Y的一半大小）
    UINT yPlaneSize = width * height;
    UINT uvPlaneSize = width * height / 2;  // UV平面是Y平面的一半
    UINT totalSize = yPlaneSize + uvPlaneSize;

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = totalSize;
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    bufferDesc.StructureByteStride = 0;

    HRESULT hr = m_device->CreateBuffer(&bufferDesc, nullptr, outBuffer);
    if (FAILED(hr))
    {
        LogError("Failed to create NV12 input buffer");
    }

    return hr;
}

HRESULT NV12ToRGBAConverter::CreateOutputTexture(UINT width, UINT height, ID3D11Texture2D** outTexture)
{
    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    textureDesc.CPUAccessFlags = 0;
    textureDesc.MiscFlags = 0;

    HRESULT hr = m_device->CreateTexture2D(&textureDesc, nullptr, outTexture);
    if (FAILED(hr))
    {
        LogError("Failed to create output RGBA texture");
    }

    return hr;
}

HRESULT NV12ToRGBAConverter::WriteNV12Data(ID3D11Buffer* buffer, const BYTE* yPlaneData, 
                                          const BYTE* uvPlaneData, UINT width, UINT height)
{
    if (!buffer || !yPlaneData || !uvPlaneData)
        return E_INVALIDARG;

    UINT yPlaneSize = width * height;
    UINT uvPlaneSize = width * height / 2;
    UINT totalSize = yPlaneSize + uvPlaneSize;

    // 创建临时缓冲区来组合Y和UV数据
    std::vector<BYTE> nv12Data(totalSize);
    
    // 复制Y平面数据
    memcpy(nv12Data.data(), yPlaneData, yPlaneSize);
    
    // 复制UV平面数据
    memcpy(nv12Data.data() + yPlaneSize, uvPlaneData, uvPlaneSize);

    // 创建staging buffer用于上传数据
    D3D11_BUFFER_DESC stagingDesc = {};
    stagingDesc.ByteWidth = totalSize;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    stagingDesc.MiscFlags = 0;

    ID3D11Buffer* stagingBuffer = nullptr;
    HRESULT hr = m_device->CreateBuffer(&stagingDesc, nullptr, &stagingBuffer);
    if (FAILED(hr))
        return hr;

    // 映射并写入数据
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = m_context->Map(stagingBuffer, 0, D3D11_MAP_WRITE, 0, &mappedResource);
    
    if (SUCCEEDED(hr))
    {
        memcpy(mappedResource.pData, nv12Data.data(), totalSize);
        m_context->Unmap(stagingBuffer, 0);
        
        // 复制到目标缓冲区
        m_context->CopyResource(buffer, stagingBuffer);
    }

    stagingBuffer->Release();
    return hr;
}

HRESULT NV12ToRGBAConverter::Convert(ID3D11Buffer* nv12Buffer, ID3D11Texture2D* outputTexture,
                                    UINT width, UINT height)
{
    if (!m_initialized || !nv12Buffer || !outputTexture)
        return E_INVALIDARG;

    try
    {
        // 创建输入缓冲区的UAV
        ID3D11UnorderedAccessView* inputUAV = nullptr;
        D3D11_UNORDERED_ACCESS_VIEW_DESC inputUavDesc = {};
        inputUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        inputUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        inputUavDesc.Buffer.FirstElement = 0;
        inputUavDesc.Buffer.NumElements = (width * height * 3 / 2) / 4; // NV12总字节数/4
        inputUavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;

        ThrowIfFailed(m_device->CreateUnorderedAccessView(nv12Buffer, &inputUavDesc, &inputUAV),
                     "Failed to create input UAV");

        // 创建输出纹理的UAV
        ID3D11UnorderedAccessView* outputUAV = nullptr;
        D3D11_UNORDERED_ACCESS_VIEW_DESC outputUavDesc = {};
        outputUavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        outputUavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        outputUavDesc.Texture2D.MipSlice = 0;

        ThrowIfFailed(m_device->CreateUnorderedAccessView(outputTexture, &outputUavDesc, &outputUAV),
                     "Failed to create output UAV");

        // 更新常量缓冲区
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        ThrowIfFailed(m_context->Map(m_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource),
                     "Failed to map constant buffer");

        NV12ConversionParams* params = (NV12ConversionParams*)mappedResource.pData;
        params->ImageWidth = width;
        params->ImageHeight = height;
        params->YPlaneStride = width;        // Y平面每行的字节数
        params->UVPlaneStride = width;       // UV平面每行的字节数

        m_context->Unmap(m_constantBuffer, 0);

        // 设置Compute Shader管线
        m_context->CSSetShader(m_computeShader, nullptr, 0);
        
        ID3D11UnorderedAccessView* uavs[] = { inputUAV, outputUAV };
        m_context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
        m_context->CSSetConstantBuffers(0, 1, &m_constantBuffer);

        // 计算调度参数
        UINT dispatchX = (width + 15) / 16;
        UINT dispatchY = (height + 15) / 16;

        // 执行Compute Shader
        m_context->Dispatch(dispatchX, dispatchY, 1);

        // 清理管线状态
        ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr };
        m_context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);

        inputUAV->Release();
        outputUAV->Release();

        // 每10秒输出一次成功日志
        auto currentTime = std::chrono::steady_clock::now();
        auto timeDiff = std::chrono::duration_cast<std::chrono::seconds>(currentTime - m_lastLogTime);
        if (timeDiff.count() >= 10)
        {
            LogMessage("NV12 to RGBA conversion completed successfully");
            m_lastLogTime = currentTime;
        }
        
        return S_OK;
    }
    catch (const std::exception& e)
    {
        LogError(std::string("NV12 conversion failed: ") + e.what());
        return E_FAIL;
    }
}

void NV12ToRGBAConverter::Cleanup()
{
    SAFE_RELEASE(m_constantBuffer);
    SAFE_RELEASE(m_computeShader);
    SAFE_RELEASE(m_context);
    SAFE_RELEASE(m_device);
    m_initialized = false;
}
