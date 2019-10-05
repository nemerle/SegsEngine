# This file is included from parent-dir CMakeLists.txt

file(GLOB_RECURSE source_files "${module_dir}/*.cpp")
file(GLOB_RECURSE header_files "${module_dir}/*.h")
file(GLOB_RECURSE qrc_files "${module_dir}/*.qrc")

list(APPEND module_sources ${source_files} ${header_files} ${qrc_files})
list(APPEND module_3rdparty ${PROJECT_SOURCE_DIR}/thirdparty/misc/stb_vorbis.c)


