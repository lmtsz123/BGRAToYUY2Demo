#pragma once
#include <windows.h>
#include <d3d11.h>
#include <iostream>
#include <string>

#define SAFE_RELEASE(p) { if(p) { (p)->Release(); (p) = nullptr; } }

inline void ThrowIfFailed(HRESULT hr, const std::string& message = "")
{
    if (FAILED(hr))
    {
        std::cerr << "HRESULT failed: 0x" << std::hex << hr;
        if (!message.empty())
        {
            std::cerr << " - " << message;
        }
        std::cerr << std::endl;
        throw std::runtime_error("DirectX operation failed");
    }
}

inline void LogMessage(const std::string& message)
{
    std::cout << "[INFO] " << message << std::endl;
}

inline void LogError(const std::string& message)
{
    std::cerr << "[ERROR] " << message << std::endl;
}