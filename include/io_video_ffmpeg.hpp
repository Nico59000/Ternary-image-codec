// ============================================================================
//  File: include/io_video_ffmpeg.hpp
//  Minimal FFmpeg bridge (encode PNG sequence to MP4/Matroska as a demo).
//  Used by src/io_video_ffmpeg.cpp and main_video_t3v.cpp.
// ============================================================================
#pragma once
#include <string>

bool ffmpeg_encode_png_sequence_to_video(const std::string& first_png_pattern,
        const std::string& out_video_path,
        int fps_num, int fps_den,
        const std::string& codec_name /* e.g., "libx264" */);
