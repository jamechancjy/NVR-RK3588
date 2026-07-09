set(NVR_CORE_SRCS
  common/crc32.cpp
  common/logging.cpp
  common/base64.cpp
  common/sha1.cpp
  storage/log_index.cpp
  storage/record_file.cpp
  storage/disk_manager.cpp
  storage/storage_engine.cpp
  media/stream_source.cpp
  media/onvif.cpp
  media/sip.cpp
  media/rtp_receiver.cpp
  media/ps_depacketizer.cpp
  media/gb28181_source.cpp
  record/recorder.cpp
)

# Optional FFmpeg for RTSP ingest. When present, RtspSource is compiled and
# NVR_HAVE_FFMPEG is defined; otherwise the RTSP factory returns nullptr.
find_package(PkgConfig QUIET)
set(NVR_FFMPEG_FOUND FALSE)
if(PkgConfig_FOUND)
  pkg_check_modules(FFMPEG IMPORTED_TARGET
    libavformat libavcodec libavutil)
  if(FFMPEG_FOUND)
    set(NVR_FFMPEG_FOUND TRUE)
    list(APPEND NVR_CORE_SRCS media/rtsp_source.cpp media/onvif_source.cpp)
  endif()
endif()
message(STATUS "FFmpeg (RTSP ingest): ${NVR_FFMPEG_FOUND}")

add_library(nvr_core ${NVR_CORE_SRCS})
target_include_directories(nvr_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
target_link_libraries(nvr_core PUBLIC Threads::Threads)

if(NVR_FFMPEG_FOUND)
  target_link_libraries(nvr_core PUBLIC PkgConfig::FFMPEG)
  target_compile_definitions(nvr_core PUBLIC NVR_HAVE_FFMPEG)
endif()

# std::filesystem needs an extra link on some toolchains (e.g. GCC < 9).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
  target_link_libraries(nvr_core PUBLIC stdc++fs)
endif()

add_executable(nvrd main.cpp)
target_link_libraries(nvrd PRIVATE nvr_core)
