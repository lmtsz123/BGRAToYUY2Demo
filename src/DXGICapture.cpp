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
    
    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // 默认适配器
        D3D_DRIVER_TYPE_HARDWARE,   // 硬件驱动
        nullptr,                    // 软件驱动模块
        createDeviceFlags,          // 条件调试模式
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
    if (FAILED(hr)) 
    {
        LogError("Failed to create desktop duplication. HRESULT: 0x" + std::to_string(hr));
        if (hr == DXGI_ERROR_UNSUPPORTED)
        {
            LogError("Desktop duplication is not supported on this system");
        }
        else if (hr == E_ACCESSDENIED)
        {
            LogError("Access denied. Try running as administrator or check if another application is using desktop duplication");
        }
        else if (hr == DXGI_ERROR_SESSION_DISCONNECTED)
        {
            LogError("Session disconnected. Desktop duplication not available in remote desktop sessions");
        }
        return hr;
    }
    
    LogMessage("Desktop duplication created successfully. Resolution: " + 
              std::to_string(m_outputWidth) + "x" + std::to_string(m_outputHeight));

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
        LogError("Failed to acquire next frame. HRESULT: 0x" + std::to_string(hr));
        
        // 如果是访问被拒绝，可能需要重新初始化
        if (hr == DXGI_ERROR_ACCESS_LOST)
        {
            LogError("Desktop duplication access lost. Trying to reinitialize...");
            Cleanup();
            if (SUCCEEDED(Initialize()))
            {
                LogMessage("Desktop duplication reinitialized successfully");
                return DXGI_ERROR_WAIT_TIMEOUT; // 返回超时，让调用者重试
            }
        }
        return hr;
    }
    
    // 帧信息已移除以减少日志噪音

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
    
    // 强制使用BGRA格式，确保与转换器兼容
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

    ID3D11Texture2D* outputTexture = nullptr;
    hr = m_device->CreateTexture2D(&desc, nullptr, &outputTexture);
    
    if (SUCCEEDED(hr))
    {
        // AMD显卡特殊处理：先复制到staging纹理，再复制到输出纹理
        // 这可以解决AMD驱动的桌面复制问题
        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width = desc.Width;
        stagingDesc.Height = desc.Height;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = desc.Format;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.SampleDesc.Quality = 0;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        
        ID3D11Texture2D* tempStagingTexture = nullptr;
        HRESULT stagingResult = m_device->CreateTexture2D(&stagingDesc, nullptr, &tempStagingTexture);
        // Staging texture creation log removed
        
        if (SUCCEEDED(stagingResult))
        {
            // 先复制到staging纹理
            m_context->CopyResource(tempStagingTexture, acquiredTexture);
            m_context->Flush();
            
            // 检查staging纹理是否有数据
            D3D11_MAPPED_SUBRESOURCE mappedResource;
            HRESULT mapResult = m_context->Map(tempStagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
            
            if (SUCCEEDED(mapResult))
            {
                BYTE* pixelData = (BYTE*)mappedResource.pData;
                bool hasData = false;
                
                // 检查前100个像素是否有非零数据
                for (int i = 0; i < 400 && i < (int)(desc.Width * 4); i++)
                {
                    if (pixelData[i] != 0)
                    {
                        hasData = true;
                        break;
                    }
                }
                
                // 只在有数据时记录
                if (hasData) {
                    LogMessage("[BGRA] Desktop capture successful");
                }
                
                m_context->Unmap(tempStagingTexture, 0);
                
                if (hasData)
                {
                    // 如果staging纹理有数据，从staging复制到输出纹理
                    // 注意：不能直接从staging纹理复制到default纹理，需要先复制到中间纹理
                    
                    // 创建一个中间纹理，格式与staging相同但用法为default
                    D3D11_TEXTURE2D_DESC intermediateDesc = stagingDesc;
                    intermediateDesc.Usage = D3D11_USAGE_DEFAULT;
                    intermediateDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    intermediateDesc.CPUAccessFlags = 0;
                    
                    ID3D11Texture2D* intermediateTexture = nullptr;
                    if (SUCCEEDED(m_device->CreateTexture2D(&intermediateDesc, nullptr, &intermediateTexture)))
                    {
                        // 从staging复制到中间纹理
                        m_context->CopyResource(intermediateTexture, tempStagingTexture);
                        m_context->Flush();
                        
                        // 验证中间纹理是否有数据
                        int verifyNonZero = 0;
                        D3D11_TEXTURE2D_DESC verifyDesc = stagingDesc;
                        ID3D11Texture2D* verifyTexture = nullptr;
                        if (SUCCEEDED(m_device->CreateTexture2D(&verifyDesc, nullptr, &verifyTexture)))
                        {
                            m_context->CopyResource(verifyTexture, intermediateTexture);
                            m_context->Flush();
                            
                            D3D11_MAPPED_SUBRESOURCE verifyMapped;
                            if (SUCCEEDED(m_context->Map(verifyTexture, 0, D3D11_MAP_READ, 0, &verifyMapped)))
                            {
                                BYTE* verifyData = (BYTE*)verifyMapped.pData;
                                for (int i = 0; i < 400; i += 4)
                                {
                                    if (verifyData[i] != 0 || verifyData[i+1] != 0 || verifyData[i+2] != 0)
                                    {
                                        verifyNonZero++;
                                        // 像素详情已移除
                                    }
                                }
                                // 中间纹理验证已移除
                                m_context->Unmap(verifyTexture, 0);
                            }
                            verifyTexture->Release();
                        }
                        
                        // 再从中间纹理复制到输出纹理
                        m_context->CopyResource(outputTexture, intermediateTexture);
                        m_context->Flush();
                        
                        // 检查最终输出纹理
                        D3D11_TEXTURE2D_DESC finalVerifyDesc = stagingDesc;
                        ID3D11Texture2D* finalVerifyTexture = nullptr;
                        if (SUCCEEDED(m_device->CreateTexture2D(&finalVerifyDesc, nullptr, &finalVerifyTexture)))
                        {
                            m_context->CopyResource(finalVerifyTexture, outputTexture);
                            m_context->Flush();
                            
                            D3D11_MAPPED_SUBRESOURCE finalMapped;
                            if (SUCCEEDED(m_context->Map(finalVerifyTexture, 0, D3D11_MAP_READ, 0, &finalMapped)))
                            {
                                BYTE* finalData = (BYTE*)finalMapped.pData;
                                int finalNonZero = 0;
                                for (int i = 0; i < 400; i += 4)
                                {
                                    if (finalData[i] != 0 || finalData[i+1] != 0 || finalData[i+2] != 0)
                                    {
                                        finalNonZero++;
                                    }
                                }
                                m_context->Unmap(finalVerifyTexture, 0);
                                
                                // 如果最终输出纹理还是空的，直接替换为中间纹理
                                if (finalNonZero == 0 && verifyNonZero > 0)
                                {
                                    LogMessage("[BGRA] Texture replacement applied for AMD GPU");
                                    outputTexture->Release();
                                    outputTexture = intermediateTexture;
                                    intermediateTexture->AddRef();
                                    
                                    *outTexture = outputTexture;
                                    outputTexture->AddRef();
                                }
                            }
                            finalVerifyTexture->Release();
                        }
                        
                        intermediateTexture->Release();
                    }
                    else
                    {
                        // 如果中间纹理创建失败，尝试直接复制（可能会失败）
                        m_context->CopyResource(outputTexture, tempStagingTexture);
                        LogMessage("AMD GPU workaround: Direct copy from staging (may fail)");
                    }
                }
                else
                {
                    // 尝试AMD显卡的另一种修复：等待并重试
                    LogMessage("AMD GPU: Trying alternative fix - wait and retry");
                    Sleep(33); // 等待2帧时间
                    
                    // 重新复制
                    m_context->CopyResource(tempStagingTexture, acquiredTexture);
                    m_context->Flush();
                    
                    // 再次检查
                    if (SUCCEEDED(m_context->Map(tempStagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource)))
                    {
                        pixelData = (BYTE*)mappedResource.pData;
                        hasData = false;
                        
                        for (int i = 0; i < 400 && i < (int)(desc.Width * 4); i++)
                        {
                            if (pixelData[i] != 0)
                            {
                                hasData = true;
                                break;
                            }
                        }
                        
                        m_context->Unmap(tempStagingTexture, 0);
                        
                        if (hasData)
                        {
                            // 重试成功，使用中间纹理复制
                            D3D11_TEXTURE2D_DESC retryIntermediateDesc = stagingDesc;
                            retryIntermediateDesc.Usage = D3D11_USAGE_DEFAULT;
                            retryIntermediateDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                            retryIntermediateDesc.CPUAccessFlags = 0;
                            
                            ID3D11Texture2D* retryIntermediateTexture = nullptr;
                            if (SUCCEEDED(m_device->CreateTexture2D(&retryIntermediateDesc, nullptr, &retryIntermediateTexture)))
                            {
                                m_context->CopyResource(retryIntermediateTexture, tempStagingTexture);
                                m_context->Flush();
                                m_context->CopyResource(outputTexture, retryIntermediateTexture);
                                m_context->Flush();
                                retryIntermediateTexture->Release();
                                LogMessage("AMD GPU workaround: Retry successful (via intermediate)!");
                            }
                            else
                            {
                                m_context->CopyResource(outputTexture, tempStagingTexture);
                                LogMessage("AMD GPU workaround: Retry successful (direct copy)!");
                            }
                        }
                        else
                        {
                            m_context->CopyResource(outputTexture, acquiredTexture);
                            LogMessage("AMD GPU: Both attempts failed, using direct copy");
                        }
                    }
                    else
                    {
                        m_context->CopyResource(outputTexture, acquiredTexture);
                        LogMessage("AMD GPU: Retry map failed, using direct copy");
                    }
                }
            }
            else
            {
                // 如果映射失败，直接复制
                m_context->CopyResource(outputTexture, acquiredTexture);
                LogMessage("AMD GPU: Map failed, using direct copy");
            }
            
            tempStagingTexture->Release();
        }
        else
        {
            // 如果创建staging纹理失败，尝试使用原始纹理格式的staging纹理
            LogMessage("AMD GPU: Trying staging texture with original format");
            
            D3D11_TEXTURE2D_DESC originalDesc;
            acquiredTexture->GetDesc(&originalDesc);
            
            D3D11_TEXTURE2D_DESC originalStagingDesc = {};
            originalStagingDesc.Width = originalDesc.Width;
            originalStagingDesc.Height = originalDesc.Height;
            originalStagingDesc.MipLevels = 1;
            originalStagingDesc.ArraySize = 1;
            originalStagingDesc.Format = originalDesc.Format; // 使用原始格式
            originalStagingDesc.SampleDesc.Count = 1;
            originalStagingDesc.SampleDesc.Quality = 0;
            originalStagingDesc.Usage = D3D11_USAGE_STAGING;
            originalStagingDesc.BindFlags = 0;
            originalStagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            originalStagingDesc.MiscFlags = 0;
            
            ID3D11Texture2D* originalStagingTexture = nullptr;
            HRESULT originalStagingResult = m_device->CreateTexture2D(&originalStagingDesc, nullptr, &originalStagingTexture);
            LogMessage("Creating original format staging texture result: 0x" + std::to_string(originalStagingResult));
            
            if (SUCCEEDED(originalStagingResult))
            {
                // 复制到原始格式的staging纹理
                m_context->CopyResource(originalStagingTexture, acquiredTexture);
                m_context->Flush();
                
                // 检查是否有数据
                D3D11_MAPPED_SUBRESOURCE mappedResource;
                if (SUCCEEDED(m_context->Map(originalStagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource)))
                {
                    BYTE* pixelData = (BYTE*)mappedResource.pData;
                    bool hasData = false;
                    
                    for (int i = 0; i < 400 && i < (int)(originalDesc.Width * 4); i++)
                    {
                        if (pixelData[i] != 0)
                        {
                            hasData = true;
                            break;
                        }
                    }
                    
                    LogMessage("Original format staging texture data check: " + std::string(hasData ? "HAS DATA" : "EMPTY"));
                    m_context->Unmap(originalStagingTexture, 0);
                    
                    if (hasData)
                    {
                        // 从原始格式staging纹理复制到输出纹理
                        m_context->CopyResource(outputTexture, originalStagingTexture);
                        LogMessage("AMD GPU workaround: Used original format staging texture");
                    }
                    else
                    {
                        m_context->CopyResource(outputTexture, acquiredTexture);
                        LogMessage("AMD GPU: Original format staging also empty, using direct copy");
                    }
                }
                else
                {
                    m_context->CopyResource(outputTexture, acquiredTexture);
                    LogMessage("AMD GPU: Original format staging map failed, using direct copy");
                }
                
                originalStagingTexture->Release();
            }
            else
            {
                // 如果所有staging纹理都失败，使用直接复制
        m_context->CopyResource(outputTexture, acquiredTexture);
                LogMessage("AMD GPU: All staging texture attempts failed, using direct copy");
            }
        }
        
        // 强制刷新GPU命令
        m_context->Flush();
        
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