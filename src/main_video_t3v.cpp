// ============================================================================
//  File: src/main_video_t3v.cpp
//  Demo: read images, encode to .t3v frames, export PNGs, and optionally video.
// ============================================================================
#include <cstdio>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include "io_image.hpp"
#include "t3v_io.hpp"
#include "t3v_indexed_io.hpp"
#include "io_video_ffmpeg.hpp"

int main(int argc, char** argv){
    if(argc<4){ std::printf("Usage: %s <in1.png> <in2.png> ... <out.t3v>\n", argv[0]); return 0; }
    std::vector<std::string> inputs; for(int i=1;i<argc-1;++i) inputs.push_back(argv[i]); std::string out=argv[argc-1];
    ActiveWindow aw=centered_window(SubwordMode::S27); FILE* f=t3v_fopen(out,"wb"); t3v_write_header(f, ProfileID::P2_RS26_22, SubwordMode::S27, true, CosetID::C0, std_res_for(SubwordMode::S27).w, std_res_for(SubwordMode::S27).h, aw, 30000,1001,(uint32_t)inputs.size(),1);

    std::vector<uint64_t> offsets; offsets.reserve(inputs.size());
    for(size_t fi=0; fi<inputs.size(); ++fi){ ImageU8 img; if(!load_image_rgb8(inputs[fi],img)){ std::printf("load fail %s\n", inputs[fi].c_str()); return 1; }
        std::vector<PixelYCbCrQuant> q; rgb_to_quant_stream(img,q); std::vector<Word27> raw; encode_raw_pixels_to_words(q,raw);
        EncoderContext e; e.cfg.profile=ProfileID::P2_RS26_22; e.cfg.tile={64,64}; std::vector<Word27> prof; encode_profile_from_raw(raw,prof,e);
        long pos=std::ftell(f); if(pos>=0) offsets.push_back((uint64_t)pos);
        t3v_write_frame(f, prof);
        std::ostringstream name; name<<"frame_"<<std::setw(5)<<std::setfill('0')<<fi<<".png"; words27_to_image(raw,img.w,img.h,name.str());
    }
    t3v_fclose(f);

    // index sidecar
    t3v_index_write(out+".t3vi", (uint32_t)offsets.size(), offsets);

#ifdef T3_FFMPEG_DEMO
    // Optional: build an mp4 from the PNG sequence
    ffmpeg_encode_png_sequence_to_video("frame_%05d.png", "out.mp4", 30000,1001, "libx264");
#endif
    std::printf("Wrote %s with %zu frames.\n", out.c_str(), inputs.size());
    return 0;
}
