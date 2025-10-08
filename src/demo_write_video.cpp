// ============================================================================
//  File: src/demo_write_video.cpp — Démo writer vidéo (FFmpeg)
//  Build (exemple):
//    g++ -std=c++17 -O2 -DTERNARY_WITH_FFMPEG \
//        src/demo_write_video.cpp \
//        -lavformat -lavcodec -lavutil -lswscale
// ============================================================================
#include <iostream>
#include <vector>
#include "ternary_image_codec_v6_min.hpp"
#include "header_inline_impl.hpp"
#include "video_writer_ffmpeg.hpp"

int main(){
    // Fabrique une séquence synthétique de 60 frames 256x144 en RAW-N S21
    const int W=256, H=144, NFR=60;
    std::vector<std::vector<Word27>> frames;
    frames.reserve(NFR);

    for(int f=0; f<NFR; ++f){
        std::vector<PixelYCbCrQuant> q; q.resize((size_t)W*H);
        for(int y=0;y<H;++y){
            for(int x=0;x<W;++x){
                size_t i=(size_t)y*W+x;
                PixelYCbCrQuant p{};
                p.Yq =(uint16_t)((x+f)%243);
                p.Cbq=(int16_t)(((y-f)%81)-40);
                p.Crq=(int16_t)(((x+y+f)%81)-40);
                q[i]=p;
            }
        }
        std::vector<Word27> w;
        encode_raw_pixels_to_words_subword(q, SubwordMode::S21, w);
        frames.push_back(std::move(w));
    }

    FFVideoConfig cfg;
    cfg.width=W; cfg.height=H; cfg.fps=30.0;
    cfg.codec_name="libx264"; cfg.crf=20; cfg.preset="veryfast";
    cfg.gop=(int)(2*cfg.fps);
    cfg.yuv444=false; // passer à true si besoin (moins compatible)

    FFVideoStats st{};
    if(!write_video_from_words_sequence("demo_s21.mp4", cfg, frames, SubwordMode::S21, W, H, &st)){
        std::cerr << "Video write failed\n"; return 1;
    }
    std::cout << "Wrote demo_s21.mp4, frames="<<st.frames_written<<", packets="<<st.packets<<"\n";
    return 0;
}
