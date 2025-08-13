#include "DXGICapture.h"
#include "BGRAToYUY2Converter.h"
#include "Utils.h"
#include <chrono>
#include <thread>
#include <iomanip>
#include <fstream>

class Demo
{
public:
    Demo() : m_frameCount(0), m_totalFrameTime(0) {}

    int Run()
    {
        try
        {
            // 初始化DXGI捕获
            ThrowIfFailed(m_capture.Initialize(), "Failed to initialize DXGI capture");

            // 初始化转换器
            ThrowIfFailed(m_converter.Initialize(m_capture.GetDevice(), m_capture.GetContext()),
                         "Failed to initialize converter");

            LogMessage("Demo initialized successfully. Starting capture loop...");
            LogMessage("Press Ctrl+C to exit");

            // 主循环
            MainLoop();

            return 0;
        }
        catch (const std::exception& e)
        {
            LogError(std::string("Demo failed: ") + e.what());
            return -1;
        }
    }

private:
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
        hr = m_converter.CreateOutputBuffer(width, height, &outputBuffer);
        if (FAILED(hr))
        {
            SAFE_RELEASE(capturedTexture);
            LogError("Failed to create output buffer");
            return false;
        }

        // 执行转换
        hr = m_converter.Convert(capturedTexture, outputBuffer, width, height);
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
        if (m_frameCount % 300 == 0) // 每300帧验证一次
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

        HRESULT hr = m_converter.ReadOutputBuffer(buffer, width, height, &yuy2Data, dataSize);
        if (SUCCEEDED(hr) && yuy2Data)
        {
            // 验证YUY2数据的有效性
            bool isValid = ValidateYUY2Data(yuy2Data, dataSize, width, height);
            
            if (isValid)
            {
                LogMessage("YUY2 conversion validation: PASSED");
                
                // 可选：保存第一帧到文件进行调试
                if (m_frameCount == 0)
                {
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

        // 检查Y分量是否在有效范围内 [16, 235]
        // 检查UV分量是否在有效范围内 [16, 240]
        for (UINT i = 0; i < dataSize; i += 4)
        {
            BYTE y0 = data[i];     // Y0
            BYTE u = data[i + 1];  // U
            BYTE y1 = data[i + 2]; // Y1
            BYTE v = data[i + 3];  // V

            if (y0 < 16 || y0 > 235 || y1 < 16 || y1 > 235)
            {
                LogError("Invalid Y component value");
                return false;
            }

            if (u < 16 || u > 240 || v < 16 || v > 240)
            {
                LogError("Invalid UV component value");
                return false;
            }
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

    DXGICapture m_capture;
    BGRAToYUY2Converter m_converter;
    UINT m_frameCount;
    long long m_totalFrameTime; // microseconds
};

int main()
{
    LogMessage("BGRA to YUY2 Demo Starting...");
    
    Demo demo;
    return demo.Run();
}