# CMakeList.txt: ffmpeg_sdl 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#
cmake_minimum_required (VERSION 3.8)

project(ffmpeg_sdl)


include_directories("D:/mingw32/ffmpeg/include" )

link_directories("D:/mingw32/ffmpeg/lib")

# 将源代码添加到此项目的可执行文件。
add_executable (ffmpeg_sdl "ffmpeg_sdl.cpp" "ffmpeg_sdl.h")

# TODO: 如有需要，请添加测试并安装目标。



target_link_libraries(ffmpeg_sdl avcodec avdevice avfilter avformat avutil swresample swscale)