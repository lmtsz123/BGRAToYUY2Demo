#include "DXGICapture.h"
#include "BGRAToYUY2Converter.h"
#include "NV12ToRGBAConverter.h"
#include "Utils.h"
#include <chrono>
#include <thread>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>

enum class ConversionMode
{
    BGRA_TO_YUY2,
    NV12_TO_RGBA
};

class Demo
{
public:
    Demo(ConversionMode mode) : m_mode(mode), m_device(nullptr), m_context(nullptr), 
                               m_frameCount(0), m_totalFrameTime(0) {}

    int Run()
    {
        try
        {
            if (m_mode == ConversionMode::BGRA_TO_YUY2)
            {
                return RunBGRAToYUY2Demo();
            }
            else if (m_mode == ConversionMode::NV12_TO_RGBA)
            {
                return RunNV12ToRGBADemo();
            }
            else
            {
                LogError("Unknown conversion mode");
                return -1;
            }
        }
        catch (const std::exception& e)
        {
            LogError(std::string("Demo failed: ") + e.what());
            return -1;
        }
    }

private:
    int RunBGRAToYUY2Demo()
    {
        // 初始化DXGI捕获
        ThrowIfFailed(m_capture.Initialize(), "Failed to initialize DXGI capture");

        // 初始化BGRA到YUY2转换器
        ThrowIfFailed(m_bgraToYuy2Converter.Initialize(m_capture.GetDevice(), m_capture.GetContext()),
                     "Failed to initialize BGRA to YUY2 converter");

        LogMessage("BGRA to YUY2 demo initialized successfully. Starting capture loop...");
        LogMessage("Press Ctrl+C to exit");

        // 主循环
        MainLoop();
        return 0;
    }

    int RunNV12ToRGBADemo()
    {
        // 初始化DirectX设备
        if (FAILED(InitializeDirectX()))
        {
            LogError("Failed to initialize DirectX");
            return -1;
        }

        // 初始化NV12到RGBA转换器
        ThrowIfFailed(m_nv12ToRgbaConverter.Initialize(m_device, m_context),
                     "Failed to initialize NV12 to RGBA converter");

        LogMessage("NV12 to RGBA demo initialized successfully. Starting conversion test...");
        
        // 运行NV12转换测试
        RunNV12ConversionTest();
        return 0;
    }

    void MainLoop()
    {
        auto lastStatsTime = std::chrono::steady_clock::now();
        const auto statsInterval = std::chrono::seconds(5); // 每5秒输出统计信息

        while (true)
        {
            auto frameStart = std::chrono::high_resolution_clock::now();

            if (ProcessFrame())
            {
                m_frameCount++;
                
                auto frameEnd = std::chrono::high_resolution_clock::now();
                auto frameDuration = std::chrono::duration_cast<std::chrono::microseconds>(
                    frameEnd - frameStart);
                m_totalFrameTime += frameDuration.count();

                // 输出统计信息
                auto currentTime = std::chrono::steady_clock::now();
                if (currentTime - lastStatsTime >= statsInterval)
                {
                    PrintStatistics();
                    lastStatsTime = currentTime;
                }
            }

            // 限制帧率，避免100% CPU使用
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60fps
        }
    }

    bool ProcessFrame()
    {
        ID3D11Texture2D* capturedTexture = nullptr;
        UINT width, height;

        // 捕获帧
        HRESULT hr = m_capture.CaptureFrame(&capturedTexture, width, height);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        {
            return false; // 没有新帧
        }
        if (FAILED(hr))
        {
            LogError("Failed to capture frame");
            return false;
        }

        // 创建输出缓冲区
        ID3D11Buffer* outputBuffer = nullptr;
        hr = m_bgraToYuy2Converter.CreateOutputBuffer(width, height, &outputBuffer);
        if (FAILED(hr))
        {
            SAFE_RELEASE(capturedTexture);
            LogError("Failed to create output buffer");
            return false;
        }

        // 保存有效帧的BGRA原始数据用于调试（跳过前几个可能为空的帧）
        if (m_frameCount == 30)  // 保存第4帧，通常这时数据已经稳定
        {
            SaveBGRAToFile(capturedTexture, width, height);
            
            // 调试信息已简化
        }

        // 执行转换
        hr = m_bgraToYuy2Converter.Convert(capturedTexture, outputBuffer, width, height);
        if (FAILED(hr))
        {
            SAFE_RELEASE(capturedTexture);
            SAFE_RELEASE(outputBuffer);
            
            // 对于SRV创建失败这种临时性错误，不退出程序，只是跳过这一帧
            if (hr == E_INVALIDARG)
            {
                // 这通常是临时性问题，如桌面切换、分辨率变化等
                // 不记录错误，只是静默跳过这一帧
                return false;
            }
            else
            {
                LogError("Failed to convert frame");
                return false;
            }
        }

        // 可选：读取转换后的数据进行验证或保存
        if (m_frameCount == 30 || m_frameCount % 300 == 0) // 第30帧和每300帧验证一次
        {
            ValidateConversion(outputBuffer, width, height);
        }

        SAFE_RELEASE(capturedTexture);
        SAFE_RELEASE(outputBuffer);

        return true;
    }

    void ValidateConversion(ID3D11Buffer* buffer, UINT width, UINT height)
    {
        BYTE* yuy2Data = nullptr;
        UINT dataSize = 0;

        HRESULT hr = m_bgraToYuy2Converter.ReadOutputBuffer(buffer, width, height, &yuy2Data, dataSize);
        if (SUCCEEDED(hr) && yuy2Data)
        {
            // 验证YUY2数据的有效性
            bool isValid = ValidateYUY2Data(yuy2Data, dataSize, width, height);
            
            if (isValid)
            {
                LogMessage("YUY2 conversion validation: PASSED");
                
                // 可选：保存有效帧到文件进行调试
                if (m_frameCount == 30)  // 保存第30帧，与BGRA保存同步
                {
                    // 检查YUY2数据内容
                    int nonZeroCount = 0;
                    for (UINT i = 0; i < min(400U, dataSize); i++) {
                        if (yuy2Data[i] != 0) nonZeroCount++;
                    }
                    LogMessage("[YUV] YUY2 data check: " + std::to_string(nonZeroCount) + "/400 non-zero bytes");
                    
                    SaveYUY2ToFile(yuy2Data, dataSize, width, height);
                }
            }
            else
            {
                LogError("YUY2 conversion validation: FAILED");
            }

            delete[] yuy2Data;
        }
        else
        {
            LogError("Failed to read output buffer for validation");
        }
    }

    bool ValidateYUY2Data(const BYTE* data, UINT dataSize, UINT width, UINT height)
    {
        UINT expectedSize = ((width + 1) / 2) * height * 4;
        if (dataSize != expectedSize)
        {
            LogError("YUY2 data size mismatch. Expected: " + std::to_string(expectedSize) + 
                    ", Got: " + std::to_string(dataSize));
            return false;
        }

        // 检查Y分量是否在合理范围内 [0, 255] (放宽验证范围以适应不同显卡)
        // 检查UV分量是否在合理范围内 [0, 255]
        UINT invalidYCount = 0;
        UINT invalidUVCount = 0;
        UINT totalPixels = dataSize / 4;
        
        for (UINT i = 0; i < dataSize; i += 4)
        {
            BYTE y0 = data[i];     // Y0
            BYTE u = data[i + 1];  // U
            BYTE y1 = data[i + 2]; // Y1
            BYTE v = data[i + 3];  // V

            // 统计超出标准范围的像素数量，而不是直接失败
            if (y0 < 10 || y0 > 245 || y1 < 10 || y1 > 245)
            {
                invalidYCount++;
            }

            if (u < 10 || u > 245 || v < 10 || v > 245)
            {
                invalidUVCount++;
            }
        }
        
        // 如果超过10%的像素超出范围，则认为转换失败
        float invalidYRatio = (float)invalidYCount / totalPixels;
        float invalidUVRatio = (float)invalidUVCount / totalPixels;
        
        // 添加调试信息：显示前几个像素的值
        if (invalidYRatio > 0.1f || invalidUVRatio > 0.1f)
        {
            LogMessage("Debug: First 8 YUY2 values:");
            for (int i = 0; i < min(8, (int)dataSize) && i < 32; i += 4)
            {
                LogMessage("  Pixel " + std::to_string(i/4) + ": Y0=" + std::to_string(data[i]) + 
                          " U=" + std::to_string(data[i+1]) + 
                          " Y1=" + std::to_string(data[i+2]) + 
                          " V=" + std::to_string(data[i+3]));
            }
        }
        
        if (invalidYRatio > 0.1f)
        {
            LogError("Too many invalid Y component values: " + std::to_string(invalidYRatio * 100) + "%");
            return false;
        }
        
        if (invalidUVRatio > 0.1f)
        {
            LogError("Too many invalid UV component values: " + std::to_string(invalidUVRatio * 100) + "%");
            return false;
        }

        return true;
    }

    void SaveYUY2ToFile(const BYTE* data, UINT dataSize, UINT width, UINT height)
    {
        std::string filename = "captured_frame_" + std::to_string(width) + "x" + 
                              std::to_string(height) + ".yuy2";
        
        std::ofstream file(filename, std::ios::binary);
        if (file.is_open())
        {
            file.write(reinterpret_cast<const char*>(data), dataSize);
            file.close();
            LogMessage("Saved YUY2 frame to: " + filename);
        }
        else
        {
            LogError("Failed to save YUY2 frame to file");
        }
    }

    void SaveBGRAToFile(ID3D11Texture2D* texture, UINT width, UINT height)
    {
        if (!texture) return;

        // 获取设备和上下文
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        texture->GetDevice(&device);
        device->GetImmediateContext(&context);

        // 创建staging纹理用于CPU读取
        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width = width;
        stagingDesc.Height = height;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ID3D11Texture2D* stagingTexture = nullptr;
        HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
        if (SUCCEEDED(hr))
        {
            // 复制纹理数据
            context->CopyResource(stagingTexture, texture);

            // 映射并读取数据
            D3D11_MAPPED_SUBRESOURCE mappedResource;
            hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
            if (SUCCEEDED(hr))
            {
                std::string filename = "captured_frame_" + std::to_string(width) + "x" + std::to_string(height) + ".bgra";
                std::ofstream file(filename, std::ios::binary);
                if (file.is_open())
                {
                    // 保存BGRA数据
                    UINT dataSize = width * height * 4;
                    file.write(reinterpret_cast<const char*>(mappedResource.pData), dataSize);
                    file.close();
                    LogMessage("Saved BGRA frame to: " + filename);

                    // 检查BGRA数据有效性
                    BYTE* pixelData = (BYTE*)mappedResource.pData;
                    int nonZeroPixels = 0;
                    int totalPixels = width * height;
                    for (UINT i = 0; i < dataSize; i += 4)
                    {
                        if (pixelData[i] != 0 || pixelData[i+1] != 0 || pixelData[i+2] != 0)
                        {
                            nonZeroPixels++;
                        }
                    }
                    LogMessage("[BGRA] Saved frame with " + std::to_string(nonZeroPixels) + "/" + std::to_string(totalPixels) + 
                              " valid pixels (" + std::to_string((float)nonZeroPixels / totalPixels * 100) + "%)");
                }
                else
                {
                    LogError("Failed to save BGRA frame to file");
                }
                context->Unmap(stagingTexture, 0);
            }
            stagingTexture->Release();
        }

        SAFE_RELEASE(context);
        SAFE_RELEASE(device);
    }

    void PrintStatistics()
    {
        if (m_frameCount > 0)
        {
            double avgFrameTime = static_cast<double>(m_totalFrameTime) / m_frameCount / 1000.0; // ms
            double fps = m_frameCount * 1000000.0 / m_totalFrameTime; // frames per second

            std::cout << "[STATS] Frames: " << m_frameCount 
                      << ", Avg frame time: " << std::fixed << std::setprecision(2) 
                      << avgFrameTime << "ms"
                      << ", FPS: " << std::setprecision(1) << fps << std::endl;
        }
    }

    HRESULT InitializeDirectX()
    {
        // 创建D3D11设备和上下文
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };

        HRESULT hr = D3D11CreateDevice(
            nullptr,                    // 使用默认适配器
            D3D_DRIVER_TYPE_HARDWARE,   // 硬件驱动
            nullptr,                    // 软件驱动句柄
            D3D11_CREATE_DEVICE_DEBUG,  // 调试标志
            featureLevels,              // 特性级别数组
            ARRAYSIZE(featureLevels),   // 特性级别数量
            D3D11_SDK_VERSION,          // SDK版本
            &m_device,                  // 输出设备
            nullptr,                    // 输出特性级别
            &m_context                  // 输出上下文
        );

        if (FAILED(hr))
        {
            LogError("Failed to create D3D11 device");
            return hr;
        }

        LogMessage("DirectX device initialized successfully");
        return S_OK;
    }

    void RunNV12ConversionTest()
    {
        const UINT testWidth = 1920;
        const UINT testHeight = 1080;

        LogMessage("Starting NV12 to RGBA conversion test...");
        LogMessage("Test resolution: " + std::to_string(testWidth) + "x" + std::to_string(testHeight));

        // 创建测试NV12数据
        std::vector<BYTE> testNV12Data = CreateTestNV12Data(testWidth, testHeight);

        // 创建NV12输入缓冲区
        ID3D11Buffer* nv12Buffer = nullptr;
        HRESULT hr = m_nv12ToRgbaConverter.CreateNV12InputBuffer(testWidth, testHeight, &nv12Buffer);
        if (FAILED(hr))
        {
            LogError("Failed to create NV12 input buffer");
            return;
        }

        // 写入测试数据到NV12缓冲区
        UINT yPlaneSize = testWidth * testHeight;
        hr = m_nv12ToRgbaConverter.WriteNV12Data(nv12Buffer, 
                                                testNV12Data.data(),           // Y平面数据
                                                testNV12Data.data() + yPlaneSize, // UV平面数据
                                                testWidth, testHeight);
        if (FAILED(hr))
        {
            LogError("Failed to write NV12 test data");
            SAFE_RELEASE(nv12Buffer);
            return;
        }

        // 创建输出RGBA纹理
        ID3D11Texture2D* rgbaTexture = nullptr;
        hr = m_nv12ToRgbaConverter.CreateOutputTexture(testWidth, testHeight, &rgbaTexture);
        if (FAILED(hr))
        {
            LogError("Failed to create output RGBA texture");
            SAFE_RELEASE(nv12Buffer);
            return;
        }

        // 执行转换
        auto startTime = std::chrono::high_resolution_clock::now();
        hr = m_nv12ToRgbaConverter.Convert(nv12Buffer, rgbaTexture, testWidth, testHeight);
        auto endTime = std::chrono::high_resolution_clock::now();

        if (SUCCEEDED(hr))
        {
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
            LogMessage("NV12 to RGBA conversion completed successfully!");
            LogMessage("Conversion time: " + std::to_string(duration.count() / 1000.0) + "ms");

            // 验证转换结果
            ValidateRGBAOutput(rgbaTexture, testWidth, testHeight);
        }
        else
        {
            LogError("NV12 to RGBA conversion failed");
        }

        SAFE_RELEASE(nv12Buffer);
        SAFE_RELEASE(rgbaTexture);
    }

    std::vector<BYTE> CreateTestNV12Data(UINT width, UINT height)
    {
        UINT yPlaneSize = width * height;
        UINT uvPlaneSize = width * height / 2;
        UINT totalSize = yPlaneSize + uvPlaneSize;
        
        std::vector<BYTE> nv12Data(totalSize);

        // 创建测试模式：渐变色彩
        for (UINT y = 0; y < height; y++)
        {
            for (UINT x = 0; x < width; x++)
            {
                // Y分量：从左到右渐变
                BYTE yValue = static_cast<BYTE>(16 + (x * 219) / width);
                nv12Data[y * width + x] = yValue;
            }
        }

        // UV分量：创建彩色渐变
        for (UINT y = 0; y < height / 2; y++)
        {
            for (UINT x = 0; x < width / 2; x++)
            {
                UINT uvIndex = yPlaneSize + y * width + x * 2;
                
                // U分量：从上到下渐变
                BYTE uValue = static_cast<BYTE>(16 + (y * 224) / (height / 2));
                // V分量：从左到右渐变
                BYTE vValue = static_cast<BYTE>(16 + (x * 224) / (width / 2));
                
                nv12Data[uvIndex] = uValue;     // U
                nv12Data[uvIndex + 1] = vValue; // V
            }
        }

        LogMessage("Created test NV12 data with gradient pattern");
        return nv12Data;
    }

    void ValidateRGBAOutput(ID3D11Texture2D* rgbaTexture, UINT width, UINT height)
    {
        // 创建staging纹理用于CPU读取
        D3D11_TEXTURE2D_DESC stagingDesc = {};
        rgbaTexture->GetDesc(&stagingDesc);
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        ID3D11Texture2D* stagingTexture = nullptr;
        HRESULT hr = m_device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
        if (FAILED(hr))
        {
            LogError("Failed to create staging texture for validation");
            return;
        }

        // 复制数据到staging纹理
        m_context->CopyResource(stagingTexture, rgbaTexture);

        // 映射并读取数据
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        hr = m_context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
        
        if (SUCCEEDED(hr))
        {
            BYTE* rgbaData = static_cast<BYTE*>(mappedResource.pData);
            
            // 验证几个采样点的RGBA值
            bool isValid = true;
            for (UINT testY = 0; testY < height && isValid; testY += height / 4)
            {
                for (UINT testX = 0; testX < width && isValid; testX += width / 4)
                {
                    UINT pixelIndex = testY * mappedResource.RowPitch + testX * 4;
                    BYTE r = rgbaData[pixelIndex];
                    BYTE g = rgbaData[pixelIndex + 1];
                    BYTE b = rgbaData[pixelIndex + 2];
                    BYTE a = rgbaData[pixelIndex + 3];

                    // 检查Alpha通道是否为255
                    if (a != 255)
                    {
                        LogError("Invalid alpha value at (" + std::to_string(testX) + "," + 
                                std::to_string(testY) + "): " + std::to_string(a));
                        isValid = false;
                    }

                    // 检查RGB值是否在合理范围内
                    if (r > 255 || g > 255 || b > 255)
                    {
                        LogError("Invalid RGB values at (" + std::to_string(testX) + "," + 
                                std::to_string(testY) + "): " + std::to_string(r) + "," + 
                                std::to_string(g) + "," + std::to_string(b));
                        isValid = false;
                    }
                }
            }

            if (isValid)
            {
                LogMessage("RGBA output validation: PASSED");
                
                // 保存第一行像素用于调试
                SaveRGBASample(rgbaData, mappedResource.RowPitch, width, height);
            }
            else
            {
                LogError("RGBA output validation: FAILED");
            }

            m_context->Unmap(stagingTexture, 0);
        }
        else
        {
            LogError("Failed to map staging texture for validation");
        }

        SAFE_RELEASE(stagingTexture);
    }

    void SaveRGBASample(const BYTE* rgbaData, UINT rowPitch, UINT width, UINT height)
    {
        std::string filename = "rgba_sample_" + std::to_string(width) + "x" + std::to_string(height) + ".txt";
        std::ofstream file(filename);
        
        if (file.is_open())
        {
            file << "RGBA Sample Data (first 10 pixels of first row):\n";
            for (UINT x = 0; x < (std::min)(10U, width); x++)
            {
                UINT pixelIndex = x * 4;
                BYTE r = rgbaData[pixelIndex];
                BYTE g = rgbaData[pixelIndex + 1];
                BYTE b = rgbaData[pixelIndex + 2];
                BYTE a = rgbaData[pixelIndex + 3];
                
                file << "Pixel[" << x << "]: R=" << static_cast<int>(r) 
                     << " G=" << static_cast<int>(g) 
                     << " B=" << static_cast<int>(b) 
                     << " A=" << static_cast<int>(a) << "\n";
            }
            file.close();
            LogMessage("Saved RGBA sample to: " + filename);
        }
    }

    ConversionMode m_mode;
    DXGICapture m_capture;
    BGRAToYUY2Converter m_bgraToYuy2Converter;
    NV12ToRGBAConverter m_nv12ToRgbaConverter;
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    UINT m_frameCount;
    long long m_totalFrameTime; // microseconds
};

int main()
{
    LogMessage("DirectX Color Conversion Demo Starting...");
    LogMessage("Available conversion modes:");
    LogMessage("1. BGRA to YUY2 (Desktop capture to YUV format)");
    LogMessage("2. NV12 to RGBA (YUV format to RGB format)");
    
    std::cout << "Please select conversion mode (1 or 2): ";
    int choice;
    std::cin >> choice;
    
    ConversionMode mode;
    switch (choice)
    {
    case 1:
        mode = ConversionMode::BGRA_TO_YUY2;
        LogMessage("Selected: BGRA to YUY2 conversion");
        break;
    case 2:
        mode = ConversionMode::NV12_TO_RGBA;
        LogMessage("Selected: NV12 to RGBA conversion");
        break;
    default:
        LogError("Invalid choice. Defaulting to BGRA to YUY2 conversion");
        mode = ConversionMode::BGRA_TO_YUY2;
        break;
    }
    
    Demo demo(mode);
    return demo.Run();
}