cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_div VERSION 2.0.0)
idf_build_set_property(C_COMPILE_OPTIONS "-Werror=implicit-function-declaration" APPEND)
