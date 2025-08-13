// BGRA to YUY2 Conversion Compute Shader
// YUY2格式：每4个字节存储2个像素 [Y0 U0 Y1 V0]

// 输入BGRA纹理
Texture2D<float4> InputTexture : register(t0);

// 输出YUY2数据缓冲区
RWStructuredBuffer<uint> OutputBuffer : register(u0);

// 常量缓冲区
cbuffer ConversionParams : register(b0)
{
    uint ImageWidth;     // 图像宽度
    uint ImageHeight;    // 图像高度
    uint OutputStride;   // 输出行步长（字节）
    uint Padding;        // 对齐填充
};

// BT.601颜色转换矩阵常量
static const float3x3 RGB_TO_YUV_MATRIX = float3x3(
     0.299f,  0.587f,  0.114f,   // Y分量
    -0.169f, -0.331f,  0.500f,   // U分量
     0.500f, -0.419f, -0.081f    // V分量
);

// 颜色空间转换函数
float3 RGBToYUV(float3 rgb)
{
    float3 yuv = mul(RGB_TO_YUV_MATRIX, rgb);
    
    // YUV范围调整：Y:[16,235], UV:[16,240]
    yuv.x = yuv.x * 219.0f + 16.0f;      // Y分量
    yuv.yz = yuv.yz * 224.0f + 128.0f;   // UV分量
    
    // 限制到有效范围
    yuv.x = clamp(yuv.x, 16.0f, 235.0f);
    yuv.yz = clamp(yuv.yz, 16.0f, 240.0f);
    
    return yuv;
}

// 将4个8位值打包成一个32位整数
uint PackYUY2(uint y0, uint u, uint y1, uint v)
{
    return (v << 24) | (y1 << 16) | (u << 8) | y0;
}

[numthreads(16, 16, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    // 每个线程处理2个水平相邻的像素
    uint2 pixelPos = uint2(id.x * 2, id.y);
    
    // 边界检查
    if (pixelPos.x >= ImageWidth || pixelPos.y >= ImageHeight)
        return;
    
    // 读取两个相邻的BGRA像素
    float4 pixel0 = InputTexture.Load(uint3(pixelPos.x, pixelPos.y, 0));
    float4 pixel1 = float4(0, 0, 0, 1);
    
    // 处理奇数宽度情况
    bool hasSecondPixel = (pixelPos.x + 1) < ImageWidth;
    if (hasSecondPixel)
    {
        pixel1 = InputTexture.Load(uint3(pixelPos.x + 1, pixelPos.y, 0));
    }
    else
    {
        // 如果没有第二个像素，复制第一个像素
        pixel1 = pixel0;
    }
    
    // BGRA -> RGB转换（注意：pixel0.rgb在HLSL中已经是RGB顺序）
    float3 rgb0 = pixel0.rgb;
    float3 rgb1 = pixel1.rgb;
    
    // 转换到YUV色彩空间
    float3 yuv0 = RGBToYUV(rgb0);
    float3 yuv1 = RGBToYUV(rgb1);
    
    // YUY2格式中UV分量是2:1采样，取两个像素UV的平均值
    float u_avg = (yuv0.y + yuv1.y) * 0.5f;
    float v_avg = (yuv0.z + yuv1.z) * 0.5f;
    
    // 转换为8位整数
    uint y0 = (uint)round(yuv0.x);
    uint y1 = (uint)round(yuv1.x);
    uint u = (uint)round(u_avg);
    uint v = (uint)round(v_avg);
    
    // 打包成YUY2格式：[Y0 U0 Y1 V0]
    uint packedYUY2 = PackYUY2(y0, u, y1, v);
    
    // 计算输出缓冲区索引
    uint outputIndex = (pixelPos.y * ((ImageWidth + 1) / 2)) + (pixelPos.x / 2);
    
    // 写入输出缓冲区
    OutputBuffer[outputIndex] = packedYUY2;
}