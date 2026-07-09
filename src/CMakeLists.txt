add_library(nvr_core
  common/crc32.cpp
  common/logging.cpp
  storage/log_index.cpp
  storage/record_file.cpp
  storage/disk_manager.cpp
  storage/storage_engine.cpp
  media/stream_source.cpp
  record/recorder.cpp
)
target_include_directories(nvr_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
target_link_libraries(nvr_core PUBLIC Threads::Threads)

# std::filesystem needs an extra link on some toolchains (e.g. GCC < 9).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
  target_link_libraries(nvr_core PUBLIC stdc++fs)
endif()

add_executable(nvrd main.cpp)
target_link_libraries(nvrd PRIVATE nvr_core)
