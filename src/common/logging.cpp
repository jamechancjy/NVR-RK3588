#include "nvr/common/logging.h"

#include <cstdarg>
#include <ctime>

namespace nvr {

Logger& Logger::Instance() {
  static Logger instance;
  return instance;
}

void Logger::Log(LogLevel level, const char* file, int line, const char* fmt,
                 ...) {
  if (level < level_) return;

  static const char* kNames[] = {"D", "I", "W", "E"};
  const char* name = kNames[static_cast<int>(level)];

  char ts[32];
  std::time_t now = std::time(nullptr);
  std::tm tm_buf{};
  localtime_r(&now, &tm_buf);
  std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

  // Keep only the file basename to reduce noise.
  const char* base = file;
  for (const char* p = file; *p; ++p)
    if (*p == '/') base = p + 1;

  std::lock_guard<std::mutex> lock(mu_);
  std::fprintf(stderr, "[%s] %s %s:%d ", ts, name, base, line);
  va_list args;
  va_start(args, fmt);
  std::vfprintf(stderr, fmt, args);
  va_end(args);
  std::fputc('\n', stderr);
}

}  // namespace nvr
