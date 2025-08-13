cmake_minimum_required(VERSION 3.16)
project(BGRAToYUY2Demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows平台DirectX库配置
if(WIN32)
    # DirectX库在Windows SDK中，直接链接即可
    set(DIRECTX_LIBRARIES d3d11.lib dxgi.lib d3dcompiler.lib)
else()
    message(FATAL_ERROR "This project is designed for Windows only")
endif()

# 源文件
set(SOURCES
    src/main.cpp
    src/DXGICapture.cpp
    src/BGRAToYUY2Converter.cpp
)

set(HEADERS
    src/DXGICapture.h
    src/BGRAToYUY2Converter.h
    src/Utils.h
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 链接库
target_link_libraries(${PROJECT_NAME} 
    ${DIRECTX_LIBRARIES}
)

# 设置工作目录和资源复制
set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

# 复制shader文件到输出目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
)

# 设置编译选项
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()