#include "BGRAToYUY2Converter.h"
#include <d3dcompiler.h>
#include <fstream>
#include <vector>

BGRAToYUY2Converter::BGRAToYUY2Converter()
    : m_device(nullptr)
    , m_context(nullptr)
    , m_computeShader(nullptr)
    , m_constantBuffer(nullptr)
    , m_initialized(false)
    , m_lastLogTime(std::chrono::steady_clock::now())
{
}

BGRAToYUY2Converter::~BGRAToYUY2Converter()
{
    Cleanup();
}

HRESULT BGRAToYUY2Converter::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!device || !context)
        return E_INVALIDARG;

    m_device = device;
    m_context = context;
    m_device->AddRef();
    m_context->AddRef();

    try
    {
        ThrowIfFailed(CompileShader(), "Failed to compile shader");

        // 创建常量缓冲区
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(ConversionParams);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        ThrowIfFailed(m_device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer),
                     "Failed to create constant buffer");

        m_initialized = true;
        LogMessage("BGRA to YUY2 converter initialized successfully");
        return S_OK;
    }
    catch (const std::exception& e)
    {
        LogError(std::string("Converter initialization failed: ") + e.what());
        Cleanup();
        return E_FAIL;
    }
}

HRESULT BGRAToYUY2Converter::CompileShader()
{
    // 读取shader文件
    std::ifstream shaderFile("shaders/BGRAToYUY2.hlsl");
    if (!shaderFile.is_open())
    {
        LogError("Cannot open shader file: shaders/BGRAToYUY2.hlsl");
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
        "BGRAToYUY2.hlsl",
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
            LogError(std::string("Shader compilation error: ") + 
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

HRESULT BGRAToYUY2Converter::CreateOutputBuffer(UINT width, UINT height, ID3D11Buffer** outBuffer)
{
    UINT yuy2Size = ((width + 1) / 2) * height * 4; // YUY2格式大小

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = yuy2Size;
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    bufferDesc.StructureByteStride = 0; // 原始缓冲区不需要结构大小

    HRESULT hr = m_device->CreateBuffer(&bufferDesc, nullptr, outBuffer);
    if (FAILED(hr))
    {
        LogError("Failed to create output buffer");
    }

    return hr;
}

HRESULT BGRAToYUY2Converter::Convert(ID3D11Texture2D* inputTexture, ID3D11Buffer* outputBuffer,
                                    UINT width, UINT height)
{
    if (!m_initialized || !inputTexture || !outputBuffer)
        return E_INVALIDARG;

    try
    {
        // 验证输入纹理的有效性
        if (!inputTexture)
        {
            LogError("Input texture is null");
            return E_INVALIDARG;
        }

        // 获取纹理描述以验证格式
        D3D11_TEXTURE2D_DESC texDesc;
        inputTexture->GetDesc(&texDesc);
        
        // 验证纹理格式和属性 - 支持常见的桌面格式
        if (texDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && 
            texDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
            texDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
            texDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        {
            LogError("Unsupported texture format: " + std::to_string(texDesc.Format) + 
                    ". Supported formats: BGRA8_UNORM(87), RGBA8_UNORM(28), BGRA8_SRGB(91), RGBA8_SRGB(29)");
            return E_INVALIDARG;
        }

        // 创建输入纹理的SRV
        ID3D11ShaderResourceView* inputSRV = nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        
        // 使用与纹理相同的格式，但转换为非SRGB版本以便计算
        if (texDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
            srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        else if (texDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        else
            srvDesc.Format = texDesc.Format; // 使用原始格式
            
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        HRESULT srvResult = m_device->CreateShaderResourceView(inputTexture, &srvDesc, &inputSRV);
        if (FAILED(srvResult))
        {
            LogError("Failed to create input SRV. Texture may be in invalid state. HRESULT: 0x" + 
                    std::to_string(srvResult));
            return srvResult;
        }

        // 创建输出缓冲区的UAV
        ID3D11UnorderedAccessView* outputUAV = nullptr;
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = (((width + 1) / 2) * height * 4) / 4; // 32位元素数量
        uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;

        ThrowIfFailed(m_device->CreateUnorderedAccessView(outputBuffer, &uavDesc, &outputUAV),
                     "Failed to create output UAV");

        // 更新常量缓冲区
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        ThrowIfFailed(m_context->Map(m_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource),
                     "Failed to map constant buffer");

        ConversionParams* params = (ConversionParams*)mappedResource.pData;
        params->ImageWidth = width;
        params->ImageHeight = height;
        params->OutputStride = ((width + 1) / 2) * 4;
        params->Padding = 0;

        m_context->Unmap(m_constantBuffer, 0);

        // 设置Compute Shader管线
        m_context->CSSetShader(m_computeShader, nullptr, 0);
        m_context->CSSetShaderResources(0, 1, &inputSRV);
        m_context->CSSetUnorderedAccessViews(0, 1, &outputUAV, nullptr);
        m_context->CSSetConstantBuffers(0, 1, &m_constantBuffer);

        // 计算调度参数
        UINT dispatchX = (width + 31) / 32;  // 每个线程处理2个像素，16*2=32
        UINT dispatchY = (height + 15) / 16;

        // 执行Compute Shader
        m_context->Dispatch(dispatchX, dispatchY, 1);

        // 清理管线状态
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        m_context->CSSetShaderResources(0, 1, &nullSRV);
        m_context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

        inputSRV->Release();
        outputUAV->Release();

        // 每10秒输出一次成功日志
        auto currentTime = std::chrono::steady_clock::now();
        auto timeDiff = std::chrono::duration_cast<std::chrono::seconds>(currentTime - m_lastLogTime);
        if (timeDiff.count() >= 10)
        {
            LogMessage("Conversion completed successfully");
            m_lastLogTime = currentTime;
        }
        
        return S_OK;
    }
    catch (const std::exception& e)
    {
        LogError(std::string("Conversion failed: ") + e.what());
        return E_FAIL;
    }
}

HRESULT BGRAToYUY2Converter::ReadOutputBuffer(ID3D11Buffer* buffer, UINT width, UINT height,
                                             BYTE** outData, UINT& dataSize)
{
    if (!buffer || !outData)
        return E_INVALIDARG;

    dataSize = ((width + 1) / 2) * height * 4;

    // 创建staging buffer用于CPU读取
    D3D11_BUFFER_DESC stagingDesc = {};
    stagingDesc.ByteWidth = dataSize;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ID3D11Buffer* stagingBuffer = nullptr;
    HRESULT hr = m_device->CreateBuffer(&stagingDesc, nullptr, &stagingBuffer);
    if (FAILED(hr))
        return hr;

    // 复制数据到staging buffer
    m_context->CopyResource(stagingBuffer, buffer);

    // 映射并读取数据
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = m_context->Map(stagingBuffer, 0, D3D11_MAP_READ, 0, &mappedResource);
    
    if (SUCCEEDED(hr))
    {
        *outData = new BYTE[dataSize];
        memcpy(*outData, mappedResource.pData, dataSize);
        m_context->Unmap(stagingBuffer, 0);
    }

    stagingBuffer->Release();
    return hr;
}

void BGRAToYUY2Converter::Cleanup()
{
    SAFE_RELEASE(m_constantBuffer);
    SAFE_RELEASE(m_computeShader);
    SAFE_RELEASE(m_context);
    SAFE_RELEASE(m_device);
    m_initialized = false;
}