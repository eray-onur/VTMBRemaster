#include "openbl/media/video.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace openbl::media {

struct VideoDecoder::Impl {
    std::vector<std::byte> data;
    std::size_t pos = 0;
    AVIOContext* io = nullptr;
    AVFormatContext* fmt = nullptr;
    AVCodecContext* video = nullptr;
    AVCodecContext* audio = nullptr;
    int video_stream = -1, audio_stream = -1;
    SwsContext* sws = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    std::vector<std::int16_t> samples;
    bool draining = false;

    static int read(void* opaque, std::uint8_t* buf, int size) {
        auto* self = static_cast<Impl*>(opaque);
        const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(size), self->data.size() - self->pos);
        if (n == 0) {
            return AVERROR_EOF;
        }
        std::memcpy(buf, self->data.data() + self->pos, n);
        self->pos += n;
        return static_cast<int>(n);
    }
    static std::int64_t seek(void* opaque, std::int64_t offset, int whence) {
        auto* self = static_cast<Impl*>(opaque);
        const auto size = static_cast<std::int64_t>(self->data.size());
        if (whence == AVSEEK_SIZE) {
            return size;
        }
        std::int64_t target = offset;
        switch (whence & ~AVSEEK_FORCE) {
        case SEEK_CUR: target += static_cast<std::int64_t>(self->pos); break;
        case SEEK_END: target += size; break;
        default: break;
        }
        if (target < 0 || target > size) {
            return -1;
        }
        self->pos = static_cast<std::size_t>(target);
        return target;
    }

    AVCodecContext* open_codec(int stream) {
        const AVCodecParameters* par = fmt->streams[stream]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(par->codec_id);
        if (!codec) {
            return nullptr;
        }
        AVCodecContext* ctx = avcodec_alloc_context3(codec);
        if (!ctx || avcodec_parameters_to_context(ctx, par) < 0 || avcodec_open2(ctx, codec, nullptr) < 0) {
            avcodec_free_context(&ctx);
            return nullptr;
        }
        return ctx;
    }

    void take_audio_frame() {
        if (!swr) {
            AVChannelLayout stereo_or_mono;
            av_channel_layout_default(&stereo_or_mono, audio->ch_layout.nb_channels);
            if (swr_alloc_set_opts2(&swr, &stereo_or_mono, AV_SAMPLE_FMT_S16, audio->sample_rate, &audio->ch_layout,
                                    audio->sample_fmt, audio->sample_rate, 0, nullptr) < 0 ||
                swr_init(swr) < 0) {
                throw std::runtime_error("FFmpeg: cannot set up audio conversion");
            }
        }
        const int channels = audio->ch_layout.nb_channels;
        const int max_out = swr_get_out_samples(swr, frame->nb_samples);
        const std::size_t base = samples.size();
        samples.resize(base + static_cast<std::size_t>(max_out * channels));
        auto* out = reinterpret_cast<std::uint8_t*>(samples.data() + base);
        const int got = swr_convert(swr, &out, max_out, const_cast<const std::uint8_t**>(frame->extended_data),
                                    frame->nb_samples);
        samples.resize(base + static_cast<std::size_t>(std::max(got, 0) * channels));
    }
};

VideoDecoder::VideoDecoder(std::span<const std::byte> data) : impl_(std::make_unique<Impl>()) {
    Impl& d = *impl_;
    d.data.assign(data.begin(), data.end());
    constexpr int kBuffer = 1 << 16;
    auto* buffer = static_cast<std::uint8_t*>(av_malloc(kBuffer));
    d.io = avio_alloc_context(buffer, kBuffer, 0, &d, &Impl::read, nullptr, &Impl::seek);
    d.fmt = avformat_alloc_context();
    if (!d.io || !d.fmt) {
        throw std::runtime_error("FFmpeg: out of memory");
    }
    d.fmt->pb = d.io;
    if (avformat_open_input(&d.fmt, nullptr, nullptr, nullptr) < 0 || avformat_find_stream_info(d.fmt, nullptr) < 0) {
        throw std::runtime_error("FFmpeg: cannot open the movie");
    }
    d.video_stream = av_find_best_stream(d.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    d.audio_stream = av_find_best_stream(d.fmt, AVMEDIA_TYPE_AUDIO, -1, d.video_stream, nullptr, 0);
    if (d.video_stream < 0 || !(d.video = d.open_codec(d.video_stream))) {
        throw std::runtime_error("FFmpeg: no decodable video stream");
    }
    if (d.audio_stream >= 0) {
        d.audio = d.open_codec(d.audio_stream);
    }
    d.packet = av_packet_alloc();
    d.frame = av_frame_alloc();
}

VideoDecoder::~VideoDecoder() {
    Impl& d = *impl_;
    av_frame_free(&d.frame);
    av_packet_free(&d.packet);
    sws_freeContext(d.sws);
    swr_free(&d.swr);
    avcodec_free_context(&d.video);
    avcodec_free_context(&d.audio);
    avformat_close_input(&d.fmt);
    if (d.io) {
        av_freep(&d.io->buffer);
        avio_context_free(&d.io);
    }
}

std::uint32_t VideoDecoder::width() const { return static_cast<std::uint32_t>(impl_->video->width); }
std::uint32_t VideoDecoder::height() const { return static_cast<std::uint32_t>(impl_->video->height); }
double VideoDecoder::fps() const { return av_q2d(impl_->fmt->streams[impl_->video_stream]->avg_frame_rate); }
std::string VideoDecoder::codec_name() const { return impl_->video->codec->name; }
bool VideoDecoder::has_audio() const { return impl_->audio != nullptr; }
std::uint32_t VideoDecoder::audio_sample_rate() const {
    return impl_->audio ? static_cast<std::uint32_t>(impl_->audio->sample_rate) : 0;
}
std::uint16_t VideoDecoder::audio_channels() const {
    return impl_->audio ? static_cast<std::uint16_t>(impl_->audio->ch_layout.nb_channels) : 0;
}

std::vector<std::int16_t> VideoDecoder::take_audio() {
    return std::exchange(impl_->samples, {});
}

bool VideoDecoder::next(VideoFrame& out) {
    Impl& d = *impl_;
    while (true) {
        const int r = avcodec_receive_frame(d.video, d.frame);
        if (r == 0) {
            const int w = d.frame->width, h = d.frame->height;
            d.sws = sws_getCachedContext(d.sws, w, h, static_cast<AVPixelFormat>(d.frame->format), w, h,
                                         AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            out.width = static_cast<std::uint32_t>(w);
            out.height = static_cast<std::uint32_t>(h);
            out.rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
            std::uint8_t* dst[] = {out.rgba.data()};
            const int stride[] = {w * 4};
            sws_scale(d.sws, d.frame->data, d.frame->linesize, 0, h, dst, stride);
            const AVRational tb = d.fmt->streams[d.video_stream]->time_base;
            out.time = d.frame->best_effort_timestamp == AV_NOPTS_VALUE
                           ? 0.0
                           : static_cast<double>(d.frame->best_effort_timestamp) * av_q2d(tb);
            av_frame_unref(d.frame);
            return true;
        }
        if (r == AVERROR_EOF) {
            return false;
        }
        if (r != AVERROR(EAGAIN)) {
            throw std::runtime_error("FFmpeg: video decoding failed");
        }
        if (d.draining) {
            return false;
        }
        // Feed the next packet.
        const int rr = av_read_frame(d.fmt, d.packet);
        if (rr < 0) {
            d.draining = true;
            avcodec_send_packet(d.video, nullptr);
            continue;
        }
        if (d.packet->stream_index == d.video_stream) {
            avcodec_send_packet(d.video, d.packet);
        } else if (d.audio && d.packet->stream_index == d.audio_stream) {
            if (avcodec_send_packet(d.audio, d.packet) == 0) {
                while (avcodec_receive_frame(d.audio, d.frame) == 0) {
                    d.take_audio_frame();
                    av_frame_unref(d.frame);
                }
            }
        }
        av_packet_unref(d.packet);
    }
}

} // namespace openbl::media
