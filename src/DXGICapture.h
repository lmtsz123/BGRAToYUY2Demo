#pragma once
#include "Utils.h"
#include <dxgi1_2.h>
#include <memory>

class DXGICapture
{
public:
    DXGICapture();
    ~DXGICapture();

    HRESULT Initialize();
    HRESULT CaptureFrame(ID3D11Texture2D** outTexture, UINT& width, UINT& height);
    void Cleanup();

    ID3D11Device* GetDevice() const { return m_device; }
    ID3D11DeviceContext* GetContext() const { return m_context; }

private:
    HRESULT CreateD3DDevice();
    HRESULT SetupDuplication();

    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    IDXGIOutputDuplication* m_duplication;
    ID3D11Texture2D* m_stagingTexture;
    
    UINT m_outputWidth;
    UINT m_outputHeight;
    bool m_initialized;
};