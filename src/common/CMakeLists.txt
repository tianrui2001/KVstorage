# 获取当前源文件所在目录的绝对路径
get_filename_component(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)

# 将该目录下所有源文件添加到 SRC_LIST 变量中。更常用 file()
aux_source_directory(${SRC_DIR} SRC_LIST)

# 把动态生成的文件列表放入 cache(好像并不太需要)
set(src_common ${SRC_LIST} CACHE INTERNAL "Description of the variable")