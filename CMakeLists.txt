cmake_minimum_required(VERSION 3.22)
project(KVRaftCpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_BUILD_TYPE "Debug")

# 可执行文件的输出路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
# 库文件的输出路径
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 设置编译头文件的搜索路径
include_directories(${PROJECT_SOURCE_DIR}/src/common/include)
