// ============================================================================
//  File: src/io_video_ffmpeg.cpp
//  Very small FFmpeg wrapper (system ffmpeg via popen) for demo purposes.
//  This avoids direct linkage complexities. Replace with libav* if desired.
// ============================================================================
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sstream>
#include "io_video_ffmpeg.hpp"

#ifdef _WIN32
  #define POPEN _popen
  #define PCLOSE _pclose
#else
  #define POPEN popen
  #define PCLOSE pclose
#endif

bool ffmpeg_encode_png_sequence_to_video(const std::string& pattern,
                                         const std::string& out_path,
                                         int fps_num, int fps_den,
                                         const std::string& codec){
    // pattern example: "frame_%05d.png"
    std::ostringstream cmd;
    cmd << "ffmpeg -y -hide_banner -loglevel error "
        << "-framerate " << (fps_num/(double)fps_den) << " "
        << "-i '" << pattern << "' "
        << "-pix_fmt yuv420p -c:v " << codec << " '" << out_path << "'";
#if defined(_WIN32)
    FILE* pipe = POPEN(cmd.str().c_str(), "rt");
#else
    FILE* pipe = POPEN(cmd.str().c_str(), "r");
#endif
    if(!pipe) return false;
    char buf[256]; while(fgets(buf,sizeof(buf),pipe)){}
    int rc = PCLOSE(pipe);
    return rc==0;
}
