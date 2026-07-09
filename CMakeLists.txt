cmake_minimum_required(VERSION 3.16)
project(nvr_rk3588 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# Target platform selection.
#   -DNVR_PLATFORM=RK3588  -> use real Rockchip MPP/RGA/RKNN (needs SDK)
#   -DNVR_PLATFORM=HOST    -> host build with stubbed HAL (default, for CI/dev)
if(NOT DEFINED NVR_PLATFORM)
  set(NVR_PLATFORM "HOST")
endif()
message(STATUS "NVR_PLATFORM = ${NVR_PLATFORM}")
add_compile_definitions(NVR_PLATFORM_${NVR_PLATFORM})

include_directories(${CMAKE_SOURCE_DIR}/include)

add_subdirectory(src)

option(NVR_BUILD_TESTS "Build unit tests" ON)
if(NVR_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
