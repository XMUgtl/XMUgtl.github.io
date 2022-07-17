#构建编译环境
cmake_minimum_required(VERSION 2.5)
project(mymuduo)

# mymduduo 最终编译成so动态库，设置动态库的路径，放在根目录的lib 文件夹下面
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
#定义参与编译的源代码文件
aux_source_directory(. SRC_LIST)
# 编译动生成动态库mymuduo
add_library(mymuduo SHARED ${SRC_LIST})