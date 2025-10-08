// ============================================================================
//  File: src/main.cpp
//  CLI: image -> RAW words -> profile encode -> .t3v frame + PNG roundtrip
// ============================================================================
#include <cstdio>
#include <string>
#include <vector>
#include "io_image.hpp"
#include "t3v_io.hpp"

int main(int argc, char** argv){
    if(argc<3){ std::printf("Usage: %s <input.png> <out.t3v>\n", argv[0]); return 0; }
    std::string in=argv[1], out=argv[2];
    ImageU8 img; if(!load_image_rgb8(in,img)){ std::printf("load failed\n"); return 1; }
    std::vector<PixelYCbCrQuant> q; rgb_to_quant_stream(img,q);
    std::vector<Word27> raw; encode_raw_pixels_to_words(q,raw);

    EncoderContext e; e.cfg.profile=ProfileID::P2_RS26_22; e.cfg.tile={64,64}; e.cfg.beacon={83,2,true};
    std::vector<Word27> prof; encode_profile_from_raw(raw,prof,e);

    FILE* f=t3v_fopen(out,"wb"); ActiveWindow aw=centered_window(SubwordMode::S27); t3v_write_header(f, e.cfg.profile, e.cfg.subword, e.cfg.centered, e.cfg.coset, std_res_for(SubwordMode::S27).w, std_res_for(SubwordMode::S27).h, aw, 0,1,1,0);
    t3v_write_frame(f, prof); t3v_fclose(f);

    // Roundtrip PNG for visual check
    std::vector<Word27> prof_in; FILE* fr=t3v_fopen(out,"rb"); T3VHeaderBin hb{}; t3v_read_header(fr,hb); t3v_read_frame(fr, prof_in); t3v_fclose(fr);
    DecoderContext d; std::vector<Word27> raw2; decode_profile_to_raw(prof_in,raw2,d);
    words27_to_image(raw2,img.w,img.h,"roundtrip.png");
    std::printf("OK. wrote %s and roundtrip.png\n", out.c_str());
    return 0;
}
