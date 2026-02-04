cmake_minimum_required(VERSION 3.5)
set(PARTITION_CSV_PATH "${CMAKE_SOURCE_DIR}/partitions.csv")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(secure-ota-esp32)

