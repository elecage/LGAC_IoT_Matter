cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lgac_iot_matter)

idf_build_set_property(COMPILE_OPTIONS "-Wno-error=format-nonliteral" APPEND)
