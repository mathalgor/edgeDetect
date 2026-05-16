cmake_minimum_required(VERSION 3.21)
project(edgeDetect LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(OpenCV REQUIRED)

add_subdirectory(common)
add_subdirectory(toMultiCanny)
add_subdirectory(cannyToOutline)
add_subdirectory(outlineChooser)
