#pragma once

#include <cstdio>
#include <mutex>

namespace nvr {

enum class LogLevel { kDebug = 0, kInfo, kWarn, kError };

class Logger {
 public:
  static Logger& Instance();

  void SetLevel(LogLevel level) { level_ = level; }
  LogLevel level() const { return level_; }

  void Log(LogLevel level, const char* file, int line, const char* fmt, ...);

 private:
  Logger() = default;
  LogLevel level_ = LogLevel::kInfo;
  std::mutex mu_;
};

}  // namespace nvr

#define NVR_LOG(level, ...) \
  ::nvr::Logger::Instance().Log(level, __FILE__, __LINE__, __VA_ARGS__)

#define NVR_LOGD(...) NVR_LOG(::nvr::LogLevel::kDebug, __VA_ARGS__)
#define NVR_LOGI(...) NVR_LOG(::nvr::LogLevel::kInfo, __VA_ARGS__)
#define NVR_LOGW(...) NVR_LOG(::nvr::LogLevel::kWarn, __VA_ARGS__)
#define NVR_LOGE(...) NVR_LOG(::nvr::LogLevel::kError, __VA_ARGS__)
