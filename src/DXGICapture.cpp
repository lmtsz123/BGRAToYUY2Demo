#include "DXGICapture.h"
#include <dxgi1_2.h>

DXGICapture::DXGICapture()
    : m_device(nullptr)
    , m_context(nullptr)
    , m_duplication(nullptr)
    , m_stagingTexture(nullptr)
    , m_outputWidth(0)
    , m_outputHeight(0)
    , m_initialized(false)
{
}

DXGICapture::~DXGICapture()
{
    Cleanup();
}

HRESULT DXGICapture::Initialize()
{
    try
    {
        ThrowIfFailed(CreateD3DDevice(), "Failed to create D3D device");
        ThrowIfFailed(SetupDuplication(), "Failed to setup duplication");
        
        m_initialized = true;
        LogMessage("DXGI Capture initialized successfully");
        return S_OK;
    }
    catch (const std::exception& e)
    {
        LogError(std::string("Initialization failed: ") + e.what());
        Cleanup();
        return E_FAIL;
    }
}

HRESULT DXGICapture::CreateD3DDevice()
{
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    D3D_FEATURE_LEVEL featureLevel;
    
    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // 默认适配器
        D3D_DRIVER_TYPE_HARDWARE,   // 硬件驱动
        nullptr,                    // 软件驱动模块
        D3D11_CREATE_DEVICE_DEBUG,  // 调试模式
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &m_device,
        &featureLevel,
        &m_context
    );

    if (FAILED(hr))
    {
        // 如果调试模式失败，尝试release模式
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &m_device,
            &featureLevel,
            &m_context
        );
    }

    return hr;
}

HRESULT DXGICapture::SetupDuplication()
{
    // 获取DXGI设备
    IDXGIDevice* dxgiDevice = nullptr;
    HRESULT hr = m_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (FAILED(hr)) return hr;

    // 获取适配器
    IDXGIAdapter* adapter = nullptr;
    hr = dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();
    if (FAILED(hr)) return hr;

    // 获取输出
    IDXGIOutput* output = nullptr;
    hr = adapter->EnumOutputs(0, &output);
    adapter->Release();
    if (FAILED(hr)) return hr;

    // 获取输出描述
    DXGI_OUTPUT_DESC outputDesc;
    output->GetDesc(&outputDesc);
    m_outputWidth = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    m_outputHeight = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

    // 获取输出1接口
    IDXGIOutput1* output1 = nullptr;
    hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
    output->Release();
    if (FAILED(hr)) return hr;

    // 创建桌面复制
    hr = output1->DuplicateOutput(m_device, &m_duplication);
    output1->Release();
    if (FAILED(hr)) return hr;

    // 创建staging纹理用于CPU访问
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = m_outputWidth;
    desc.Height = m_outputHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    hr = m_device->CreateTexture2D(&desc, nullptr, &m_stagingTexture);
    
    LogMessage("Desktop resolution: " + std::to_string(m_outputWidth) + "x" + std::to_string(m_outputHeight));
    
    return hr;
}

HRESULT DXGICapture::CaptureFrame(ID3D11Texture2D** outTexture, UINT& width, UINT& height)
{
    if (!m_initialized || !m_duplication)
        return E_FAIL;

    IDXGIResource* desktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;

    // 获取下一帧
    HRESULT hr = m_duplication->AcquireNextFrame(1000, &frameInfo, &desktopResource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT)
    {
        return hr; // 没有新帧
    }
    if (FAILED(hr))
    {
        LogError("Failed to acquire next frame");
        return hr;
    }

    // 查询纹理接口
    ID3D11Texture2D* acquiredTexture = nullptr;
    hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&acquiredTexture);
    desktopResource->Release();
    
    if (FAILED(hr))
    {
        m_duplication->ReleaseFrame();
        return hr;
    }

    // 创建输出纹理（GPU可访问）
    D3D11_TEXTURE2D_DESC desc;
    acquiredTexture->GetDesc(&desc);
    
    // 修改描述以适应compute shader使用
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    ID3D11Texture2D* outputTexture = nullptr;
    hr = m_device->CreateTexture2D(&desc, nullptr, &outputTexture);
    
    if (SUCCEEDED(hr))
    {
        // 复制纹理数据
        m_context->CopyResource(outputTexture, acquiredTexture);
        
        *outTexture = outputTexture;
        width = desc.Width;
        height = desc.Height;
    }

    acquiredTexture->Release();
    m_duplication->ReleaseFrame();

    return hr;
}

void DXGICapture::Cleanup()
{
    SAFE_RELEASE(m_stagingTexture);
    SAFE_RELEASE(m_duplication);
    SAFE_RELEASE(m_context);
    SAFE_RELEASE(m_device);
    m_initialized = false;
}