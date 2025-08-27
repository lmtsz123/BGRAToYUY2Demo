// BGRA to YUY2 Conversion Compute Shader
// YUY2格式：每4个字节存储2个像素 [Y0 U0 Y1 V0]

// 输入BGRA纹理
Texture2D<float4> InputTexture : register(t0);

// 输出YUY2数据缓冲区 - 使用ByteAddressBuffer配合RAW缓冲区
RWByteAddressBuffer OutputBuffer : register(u0);

// 常量缓冲区
cbuffer ConversionParams : register(b0)
{
    uint ImageWidth;     // 图像宽度
    uint ImageHeight;    // 图像高度
    uint OutputStride;   // 输出行步长（字节）
    uint Padding;        // 对齐填充
};

// BT.601颜色转换系数（标准RGB到YUV转换）
// 颜色空间转换函数
float3 RGBToYUV(float3 rgb)
{
    // 确保输入RGB在[0,1]范围内
    rgb = saturate(rgb);
    
    // BT.601 RGB到YUV转换公式
    float Y = 0.299f * rgb.r + 0.587f * rgb.g + 0.114f * rgb.b;
    float U = -0.14713f * rgb.r - 0.28886f * rgb.g + 0.436f * rgb.b;
    float V = 0.615f * rgb.r - 0.51499f * rgb.g - 0.10001f * rgb.b;
    
    // 转换到8位范围：Y:[16,235], UV:[16,240]
    Y = Y * 219.0f + 16.0f;           // Y: [0,1] -> [16,235]
    U = (U + 0.5f) * 224.0f + 16.0f; // U: [-0.5,0.5] -> [16,240]
    V = (V + 0.5f) * 224.0f + 16.0f; // V: [-0.5,0.5] -> [16,240]
    
    // 限制到有效范围
    Y = clamp(Y, 16.0f, 235.0f);
    U = clamp(U, 16.0f, 240.0f);
    V = clamp(V, 16.0f, 240.0f);
    
    return float3(Y, U, V);
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
    
    // 边界检查 - 确保不超出图像范围
    if (pixelPos.x >= ImageWidth || pixelPos.y >= ImageHeight)
        return;
    
    // 读取两个相邻的BGRA像素
    float4 pixel0 = InputTexture.Load(uint3(pixelPos.x, pixelPos.y, 0));
    float4 pixel1 = pixel0; // 默认复制第一个像素
    
    // 处理奇数宽度情况
    if ((pixelPos.x + 1) < ImageWidth)
    {
        pixel1 = InputTexture.Load(uint3(pixelPos.x + 1, pixelPos.y, 0));
    }
    
    // BGRA纹理格式处理
    // 对于DXGI_FORMAT_B8G8R8A8_UNORM，需要交换B和R通道
    float3 rgb0 = float3(pixel0.b, pixel0.g, pixel0.r);  // BGRA -> RGB
    float3 rgb1 = float3(pixel1.b, pixel1.g, pixel1.r);  // BGRA -> RGB
    
    // 移除调试代码，使用真实的输入数据
    
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
    
    // 移除调试代码，使用计算出的YUV值
    
    // 打包成YUY2格式：[Y0 U0 Y1 V0]
    uint packedYUY2 = PackYUY2(y0, u, y1, v);
    
    // 计算输出缓冲区字节偏移
    uint outputIndex = (pixelPos.y * ((ImageWidth + 1) / 2)) + (id.x);
    uint byteOffset = outputIndex * 4; // 每个YUY2像素对占4字节
    
    // 确保索引在有效范围内
    uint maxIndex = ((ImageWidth + 1) / 2) * ImageHeight;
    if (outputIndex < maxIndex) {
        // 使用ByteAddressBuffer的Store方法写入YUV转换后的数据
        OutputBuffer.Store(byteOffset, packedYUY2);
    }
}