// NV12 to RGBA Conversion Compute Shader
// NV12格式：Y平面 + 交错的UV平面 (UVUVUV...)

// 输入NV12数据缓冲区
RWByteAddressBuffer InputBuffer : register(u0);

// 输出RGBA纹理
RWTexture2D<float4> OutputTexture : register(u1);

// 常量缓冲区
cbuffer NV12ConversionParams : register(b0)
{
    uint ImageWidth;     // 图像宽度
    uint ImageHeight;    // 图像高度
    uint YPlaneStride;   // Y平面行步长
    uint UVPlaneStride;  // UV平面行步长
};

// BT.601 YUV到RGB转换矩阵
static const float3x3 YUV_TO_RGB_MATRIX = float3x3(
    1.000f,  0.000f,  1.402f,   // R分量
    1.000f, -0.344f, -0.714f,   // G分量
    1.000f,  1.772f,  0.000f    // B分量
);

// YUV到RGB颜色空间转换函数
float3 YUVToRGB(float y, float u, float v)
{
    // 将YUV值从[16,235]/[16,240]范围转换到[0,1]范围
    y = (y - 16.0f) / 219.0f;
    u = (u - 128.0f) / 224.0f;
    v = (v - 128.0f) / 224.0f;
    
    // 应用YUV到RGB转换矩阵
    float3 yuv = float3(y, u, v);
    float3 rgb = mul(YUV_TO_RGB_MATRIX, yuv);
    
    // 限制到[0,1]范围
    rgb = saturate(rgb);
    
    return rgb;
}

[numthreads(16, 16, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint2 pixelPos = id.xy;
    
    // 边界检查
    if (pixelPos.x >= ImageWidth || pixelPos.y >= ImageHeight)
        return;
    
    // 计算Y平面中的位置
    uint yOffset = pixelPos.y * YPlaneStride + pixelPos.x;
    
    // 读取Y值
    uint yValue = InputBuffer.Load(yOffset);
    float y = (float)yValue;
    
    // 计算UV平面中的位置（UV是2:1采样）
    uint uvX = (pixelPos.x / 2) * 2;  // 确保是偶数位置
    uint uvY = pixelPos.y / 2;
    uint uvPlaneOffset = ImageWidth * ImageHeight;  // UV平面在Y平面之后
    uint uvOffset = uvPlaneOffset + uvY * UVPlaneStride + uvX;
    
    // 读取UV值（NV12格式中UV是交错存储的）
    uint uValue = InputBuffer.Load(uvOffset);
    uint vValue = InputBuffer.Load(uvOffset + 1);
    
    float u = (float)uValue;
    float v = (float)vValue;
    
    // 转换YUV到RGB
    float3 rgb = YUVToRGB(y, u, v);
    
    // 输出RGBA（Alpha设为1.0）
    float4 rgba = float4(rgb, 1.0f);
    OutputTexture[pixelPos] = rgba;
}
