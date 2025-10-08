// ============================================================================
//  File: include/video_writer_ffmpeg.hpp — Writer vidéo (FFmpeg, optionnel)
//  MAJ: helper générique de centrage en canevas (pas seulement S27).
// ============================================================================
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <iostream>

#include "ternary_image_codec_v6_min.hpp"
#include "io_image.hpp"

struct FFVideoConfig
{
    std::string codec_name = "libx264";
    int         width = 0;
    int         height = 0;
    double      fps = 25.0;
    int         crf = 23;
    std::string preset = "medium";
    int         gop = 50;
    bool        yuv444 = false;
};
struct FFVideoStats
{
    int frames_written = 0;
    int64_t packets = 0;
};

#ifdef TERNARY_WITH_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

class FFVideoWriter
{
public:
    FFVideoWriter() = default;
    ~FFVideoWriter()
    {
        close();
    }
    bool open(const std::string& out_path, const FFVideoConfig& cfg)
    {
        close();
        cfg_=cfg;
        int ret = avformat_alloc_output_context2(&fmt_, nullptr, nullptr, out_path.c_str());
        if(ret<0 || !fmt_)
        {
            std::cerr<<"[FFVideoWriter] fmt alloc failed\n";
            return false;
        }
        const AVCodec* codec = nullptr;
        if(!cfg.codec_name.empty()) codec = avcodec_find_encoder_by_name(cfg.codec_name.c_str());
        if(!codec) codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if(!codec) codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
        if(!codec)
        {
            std::cerr<<"[FFVideoWriter] no encoder\n";
            return false;
        }
        st_ = avformat_new_stream(fmt_, codec);
        if(!st_) return false;
        ctx_ = avcodec_alloc_context3(codec);
        if(!ctx_) return false;
        ctx_->width=cfg.width;
        ctx_->height=cfg.height;
        ctx_->time_base = AVRational{1, (int)std::max(1.0, cfg.fps)};
        st_->time_base = ctx_->time_base;
        ctx_->pix_fmt = cfg.yuv444 ? AV_PIX_FMT_YUV444P : AV_PIX_FMT_YUV420P;
        ctx_->gop_size = cfg.gop;
        ctx_->framerate = AVRational{ (int)std::max(1.0, cfg.fps), 1 };
        AVDictionary* opts=nullptr;
        if(codec->id==AV_CODEC_ID_H264 || codec->id==AV_CODEC_ID_HEVC || codec->id==AV_CODEC_ID_AV1)
        {
            av_dict_set(&opts,"crf",std::to_string(cfg.crf).c_str(),0);
            av_dict_set(&opts,"preset",cfg.preset.c_str(),0);
            if(codec->id==AV_CODEC_ID_H264) av_dict_set(&opts,"tune","film",0);
        }
        if(fmt_->oformat->flags & AVFMT_GLOBALHEADER) ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if(avcodec_open2(ctx_, codec, &opts) < 0)
        {
            av_dict_free(&opts);
            return false;
        }
        av_dict_free(&opts);
        if(avcodec_parameters_from_context(st_->codecpar, ctx_)<0) return false;
        if(!(fmt_->oformat->flags & AVFMT_NOFILE))
        {
            if(avio_open(&fmt_->pb, out_path.c_str(), AVIO_FLAG_WRITE)<0) return false;
        }
        if(avformat_write_header(fmt_, nullptr)<0) return false;
        sws_ = sws_getContext(cfg.width, cfg.height, AV_PIX_FMT_RGB24,
                              cfg.width, cfg.height, ctx_->pix_fmt,
                              SWS_BILINEAR, nullptr,nullptr,nullptr);
        if(!sws_) return false;
        rgb_=alloc_frame(AV_PIX_FMT_RGB24, cfg.width, cfg.height);
        yuv_=alloc_frame(ctx_->pix_fmt, cfg.width, cfg.height);
        if(!rgb_||!yuv_) return false;
        next_pts_=0;
        stats_= {};
        return true;
    }
    bool add_frame_rgb(const ImageU8& img)
    {
        if(!fmt_||!ctx_||!rgb_||!yuv_||!sws_) return false;
        if(img.w!=ctx_->width || img.h!=ctx_->height || img.c!=3)
        {
            ImageU8 scaled;
            resize_rgb_nn(img, ctx_->width, ctx_->height, scaled);
            return add_frame_rgb(scaled);
        }
        for(int y=0; y<img.h; ++y)
        {
            const uint8_t* src=&img.data[(size_t)y*img.w*3];
            uint8_t* dst=rgb_->data[0]+(size_t)y*rgb_->linesize[0];
            std::copy(src, src+(size_t)img.w*3, dst);
        }
        sws_scale(sws_, &rgb_->data[0], &rgb_->linesize[0], 0, ctx_->height, &yuv_->data[0], &yuv_->linesize[0]);
        yuv_->pts = next_pts_++;
        if(!encode_and_write(yuv_)) return false;
        ++stats_.frames_written;
        return true;
    }
    // RAW-N direct (pas de centrage)
    bool add_frame_words(const std::vector<Word27>& words, SubwordMode sub, int w, int h)
    {
        std::vector<PixelYCbCrQuant> q;
        if(!decode_raw_words_to_pixels_subword(words, sub, q)) return false;
        if((int)q.size()<w*h) return false;
        ImageU8 img;
        quant_stream_to_rgb(q, w, h, img);
        if(img.w!=ctx_->width || img.h!=ctx_->height)
        {
            ImageU8 scaled;
            resize_rgb_nn(img, ctx_->width, ctx_->height, scaled);
            return add_frame_rgb(scaled);
        }
        return add_frame_rgb(img);
    }
    // NOUVEAU : centrage dans le canevas de sortie (générique, pas limité à S27)
    bool add_frame_words_centered_in_canvas(const std::vector<Word27>& words, SubwordMode inner_sub)
    {
        if(inner_sub==SubwordMode::S27)
        {
            StdRes s27=std_res_for(SubwordMode::S27);
            return add_frame_words(words, SubwordMode::S27, s27.w, s27.h);
        }
        StdRes inner=std_res_for(inner_sub);
        std::vector<PixelYCbCrQuant> q;
        if(!decode_raw_words_to_pixels_subword(words, inner_sub, q)) return false;
        if((int)q.size()<inner.w*inner.h) return false;
        ImageU8 innerImg;
        quant_stream_to_rgb(q, inner.w, inner.h, innerImg);
        // centre dans la résolution de sortie
        ImageU8 canvas;
        blit_center_rgb(innerImg, ctx_->width, ctx_->height, canvas);
        return add_frame_rgb(canvas);
    }
    // Compat héritée : S27 spécifique
    bool add_frame_words_centered_in_S27(const std::vector<Word27>& words, SubwordMode inner_sub)
    {
        return add_frame_words_centered_in_canvas(words, inner_sub);
    }

    void close()
    {
        if(ctx_) encode_and_write(nullptr);
        if(fmt_)
        {
            av_write_trailer(fmt_);
            if(!(fmt_->oformat->flags & AVFMT_NOFILE) && fmt_->pb) avio_closep(&fmt_->pb);
        }
        if(rgb_) av_frame_free(&rgb_);
        if(yuv_) av_frame_free(&yuv_);
        if(sws_) sws_freeContext(sws_), sws_=nullptr;
        if(ctx_) avcodec_free_context(&ctx_);
        if(fmt_) avformat_free_context(fmt_), fmt_=nullptr;
    }
    const FFVideoStats& stats() const
    {
        return stats_;
    }

private:
    AVFrame* alloc_frame(AVPixelFormat f, int w, int h)
    {
        AVFrame* fr=av_frame_alloc();
        if(!fr) return nullptr;
        fr->format=f;
        fr->width=w;
        fr->height=h;
        if(av_frame_get_buffer(fr, 32)<0)
        {
            av_frame_free(&fr);
            return nullptr;
        }
        return fr;
    }
    bool encode_and_write(AVFrame* f)
    {
        int ret=avcodec_send_frame(ctx_, f);
        if(ret<0 && ret!=AVERROR_EOF)
        {
            std::cerr<<"send_frame fail\n";
            return false;
        }
        AVPacket* pkt=av_packet_alloc();
        if(!pkt) return false;
        while(true)
        {
            ret=avcodec_receive_packet(ctx_, pkt);
            if(ret==AVERROR(EAGAIN)||ret==AVERROR_EOF)
            {
                av_packet_free(&pkt);
                break;
            }
            if(ret<0)
            {
                std::cerr<<"receive_packet fail\n";
                av_packet_free(&pkt);
                return false;
            }
            pkt->stream_index=st_->index;
            av_packet_rescale_ts(pkt, ctx_->time_base, st_->time_base);
            if(av_interleaved_write_frame(fmt_, pkt)<0)
            {
                av_packet_free(&pkt);
                return false;
            }
            ++stats_.packets;
            av_packet_unref(pkt);
        }
        return true;
    }

private:
    FFVideoConfig cfg_{};
    FFVideoStats  stats_{};
    AVFormatContext* fmt_ = nullptr;
    AVCodecContext*  ctx_ = nullptr;
    AVStream*        st_  = nullptr;
    SwsContext*      sws_ = nullptr;
    AVFrame* rgb_ = nullptr;
    AVFrame* yuv_ = nullptr;
    int64_t  next_pts_ = 0;
};

#else
class FFVideoWriter
{
public:
    bool open(const std::string&, const FFVideoConfig&)
    {
        std::cerr<<"[FFVideoWriter] FFmpeg indisponible\n";
        return false;
    }
    bool add_frame_rgb(const ImageU8&)
    {
        return false;
    }
    bool add_frame_words(const std::vector<Word27>&, SubwordMode, int, int)
    {
        return false;
    }
    bool add_frame_words_centered_in_canvas(const std::vector<Word27>&, SubwordMode)
    {
        return false;
    }
    bool add_frame_words_centered_in_S27(const std::vector<Word27>&, SubwordMode)
    {
        return false;
    }
    void close() {}
    FFVideoStats stats() const
    {
        return {};
    }
};
#endif

// == Helpers haut-niveau =====================================================
inline bool write_video_from_words_sequence(const std::string& out_path,
        const FFVideoConfig& cfg,
        const std::vector<std::vector<Word27>>& frames,
        SubwordMode sub, int w, int h,
        FFVideoStats* out_stats=nullptr)
{
#ifdef TERNARY_WITH_FFMPEG
    FFVideoWriter wr;
    if(!wr.open(out_path, cfg)) return false;
    for(const auto& f: frames)
    {
        if(!wr.add_frame_words(f, sub, w, h))
        {
            wr.close();
            return false;
        }
    }
    if(out_stats) *out_stats = wr.stats();
    wr.close();
    return true;
#else
    (void)out_path;
    (void)cfg;
    (void)frames;
    (void)sub;
    (void)w;
    (void)h;
    (void)out_stats;
    return false;
#endif
}
inline bool write_video_centered_in_canvas_from_rawN_sequence(const std::string& out_path,
        const FFVideoConfig& cfg,
        const std::vector<std::vector<Word27>>& frames,
        SubwordMode inner_sub,
        FFVideoStats* out_stats=nullptr)
{
#ifdef TERNARY_WITH_FFMPEG
    FFVideoWriter wr;
    if(!wr.open(out_path, cfg)) return false;
    for(const auto& f: frames)
    {
        if(!wr.add_frame_words_centered_in_canvas(f, inner_sub))
        {
            wr.close();
            return false;
        }
    }
    if(out_stats) *out_stats = wr.stats();
    wr.close();
    return true;
#else
    (void)out_path;
    (void)cfg;
    (void)frames;
    (void)inner_sub;
    (void)out_stats;
    return false;
#endif
}
