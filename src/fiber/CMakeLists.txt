# 获取当前源文件所在目录的绝对路径
get_filename_component(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR} ABSOLUTE)

# 将该目录下所有源文件添加到 SRC_LIST 变量中。更常用 file()
aux_source_directory(${SRC_DIR} SRC_LIST)

# 在链接阶段要求链接标准动态加载库
add_library(fiber_lib ${SRC_LIST})
target_link_libraries(fiber_lib -ldl)

# 把动态生成的文件列表放入 cache(好像并不太需要)
set(src_fiber ${SRC_LIST} CACHE INTERNAL "Description of the variable")