#include "nvr/media/rtsp_source.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
}

#include <chrono>
#include <cstring>

#include "nvr/common/logging.h"

namespace nvr {

namespace {
uint64_t NowMs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

Codec ToCodec(int av_codec_id) {
  switch (av_codec_id) {
    case AV_CODEC_ID_H264:
      return Codec::kH264;
    case AV_CODEC_ID_HEVC:
      return Codec::kH265;
    case AV_CODEC_ID_AAC:
      return Codec::kAAC;
    default:
      return Codec::kUnknown;
  }
}
}  // namespace

struct RtspSource::Impl {
  AVFormatContext* fmt = nullptr;
  int video_stream = -1;
  int audio_stream = -1;
  int64_t deadline_us = 0;  // interrupt deadline for the current blocking op
};

RtspSource::RtspSource(int channel, std::string url)
    : RtspSource(channel, std::move(url), Options()) {}

RtspSource::RtspSource(int channel, std::string url, Options opts)
    : channel_(channel), url_(std::move(url)), opts_(opts),
      impl_(std::make_unique<Impl>()) {}

RtspSource::~RtspSource() { Stop(); }

bool RtspSource::Start() {
  if (running_.exchange(true)) return true;
  stop_requested_.store(false);
  thread_ = std::thread([this] { RunLoop(); });
  return true;
}

void RtspSource::Stop() {
  stop_requested_.store(true);
  if (thread_.joinable()) thread_.join();
  running_.store(false);
  CloseInput();
}

bool RtspSource::OpenInput() {
  AVDictionary* opts = nullptr;
  av_dict_set(&opts, "rtsp_transport", opts_.tcp ? "tcp" : "udp", 0);
  av_dict_set(&opts, "max_delay", "500000", 0);
  char to[32];
  std::snprintf(to, sizeof(to), "%d", opts_.open_timeout_ms * 1000);
  av_dict_set(&opts, "stimeout", to, 0);  // socket timeout (us)

  impl_->fmt = avformat_alloc_context();
  if (!impl_->fmt) {
    av_dict_free(&opts);
    return false;
  }
  // Interrupt callback lets Stop() / timeouts break blocking I/O.
  impl_->deadline_us = av_gettime_relative() + opts_.open_timeout_ms * 1000LL;
  impl_->fmt->interrupt_callback.opaque = this;
  impl_->fmt->interrupt_callback.callback = [](void* opaque) -> int {
    auto* self = static_cast<RtspSource*>(opaque);
    if (self->stop_requested_.load()) return 1;
    if (self->impl_->deadline_us && av_gettime_relative() > self->impl_->deadline_us)
      return 1;
    return 0;
  };

  int ret = avformat_open_input(&impl_->fmt, url_.c_str(), nullptr, &opts);
  av_dict_free(&opts);
  if (ret < 0) {
    char err[128];
    av_strerror(ret, err, sizeof(err));
    NVR_LOGW("ch%d rtsp open failed: %s (%s)", channel_, err, url_.c_str());
    CloseInput();
    return false;
  }

  impl_->deadline_us = av_gettime_relative() + opts_.open_timeout_ms * 1000LL;
  if (avformat_find_stream_info(impl_->fmt, nullptr) < 0) {
    NVR_LOGW("ch%d rtsp find_stream_info failed", channel_);
    CloseInput();
    return false;
  }

  impl_->video_stream = -1;
  impl_->audio_stream = -1;
  for (unsigned i = 0; i < impl_->fmt->nb_streams; ++i) {
    AVCodecParameters* par = impl_->fmt->streams[i]->codecpar;
    if (par->codec_type == AVMEDIA_TYPE_VIDEO && impl_->video_stream < 0)
      impl_->video_stream = static_cast<int>(i);
    else if (par->codec_type == AVMEDIA_TYPE_AUDIO && impl_->audio_stream < 0)
      impl_->audio_stream = static_cast<int>(i);
  }
  if (impl_->video_stream < 0) {
    NVR_LOGW("ch%d rtsp no video stream", channel_);
    CloseInput();
    return false;
  }
  NVR_LOGI("ch%d rtsp connected: %s (video=%d audio=%d)", channel_,
           url_.c_str(), impl_->video_stream, impl_->audio_stream);
  return true;
}

void RtspSource::CloseInput() {
  if (impl_->fmt) {
    avformat_close_input(&impl_->fmt);
    impl_->fmt = nullptr;
  }
  impl_->video_stream = impl_->audio_stream = -1;
}

void RtspSource::RunLoop() {
  AVPacket* pkt = av_packet_alloc();
  while (!stop_requested_.load()) {
    if (!impl_->fmt && !OpenInput()) {
      // Backoff before reconnecting.
      for (int slept = 0; slept < opts_.reconnect_delay_ms &&
                          !stop_requested_.load(); slept += 50)
        av_usleep(50 * 1000);
      continue;
    }

    impl_->deadline_us = av_gettime_relative() + opts_.read_timeout_ms * 1000LL;
    int ret = av_read_frame(impl_->fmt, pkt);
    if (ret < 0) {
      char err[128];
      av_strerror(ret, err, sizeof(err));
      NVR_LOGW("ch%d rtsp read ended: %s -> reconnect", channel_, err);
      CloseInput();
      continue;
    }

    int idx = pkt->stream_index;
    bool is_video = idx == impl_->video_stream;
    bool is_audio = idx == impl_->audio_stream;
    if (is_video || is_audio) {
      AVStream* st = impl_->fmt->streams[idx];
      Frame f;
      f.channel = channel_;
      f.media_type = is_video ? MediaType::kVideo : MediaType::kAudio;
      f.frame_type = (is_video && (pkt->flags & AV_PKT_FLAG_KEY))
                         ? FrameType::kI : FrameType::kP;
      f.codec = ToCodec(st->codecpar->codec_id);

      double fps = 0.0;
      if (st->avg_frame_rate.den) fps = av_q2d(st->avg_frame_rate);
      f.fps = fps > 0 ? static_cast<int>(fps + 0.5) : 25;

      // Wall-clock arrival time drives the log.txt time field and the global
      // "先来先存" ordering. The stream PTS (relative) is kept separately for
      // future intra-channel A/V sync during playback.
      f.timestamp_ms = NowMs();
      int64_t pts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
      f.pts_ms = (pts != AV_NOPTS_VALUE)
                     ? static_cast<uint64_t>(pts * av_q2d(st->time_base) * 1000.0)
                     : 0;

      f.data.assign(pkt->data, pkt->data + pkt->size);
      EmitMain(f);
    }
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  CloseInput();
}

}  // namespace nvr
