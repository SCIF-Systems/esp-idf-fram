cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    "${CMAKE_CURRENT_LIST_DIR}/../../test"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_fram)
