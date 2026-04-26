cmake_minimum_required(VERSION 3.20)

project(AlgalBloomModeling LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/output")

file(GLOB PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(algal_bloom ${PROJECT_SOURCES})

target_include_directories(algal_bloom PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

find_package(OpenCV REQUIRED)
find_package(GDAL REQUIRED)

target_link_libraries(algal_bloom PRIVATE
    ${OpenCV_LIBS}
    ${GDAL_LIBRARIES}
)

if(OpenCV_INCLUDE_DIRS)
    target_include_directories(algal_bloom PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()

if(GDAL_INCLUDE_DIRS)
    target_include_directories(algal_bloom PRIVATE ${GDAL_INCLUDE_DIRS})
endif()

if(MSVC)
    target_compile_options(algal_bloom PRIVATE /utf-8)
    target_compile_definitions(algal_bloom PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()
