// ============================================================================
//  File: src/minitest_images.cpp — Tests sur image réelle + rapport JSON
//  Usage:
//    ./minitest_images --in input.jpg [--matrix]
//    ./minitest_images --in input.jpg --outer S27 --inner S21
//  Build:
//    g++ -std=c++17 -O2 -Iinclude -Ithird_party src/compile_stb.cpp \
//        src/minitest_images.cpp -o minitest_images
// ============================================================================

#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cstdint>
#include <cstring>

#include "ternary_image_codec_v6_min.hpp"
#include "io_image.hpp"

// CRC12 + parité (mêmes que plus haut)
static uint16_t crc12_0x80F(const uint8_t* data, size_t len){
    uint16_t poly = 0x80F, crc = 0x000;
    for(size_t i=0;i<len;++i){
        uint8_t byte=data[i];
        for(int b=7;b>=0;--b){
            uint8_t inb=(byte>>b)&1u, msb=(crc>>11)&1u;
            crc<<=1; if(msb^inb) crc^=poly; crc&=0x0FFF;
        }
    }
    return crc & 0x0FFF;
}
static uint8_t parity_mod3_unbalanced(const std::vector<uint8_t>& u){
    uint32_t s=0; for(uint8_t t: u) s+=t; return (uint8_t)(s%3);
}

// pack base-243 (5 trits -> 1 octet 0..242) pour signature rapide
static void pack_u_base243(const std::vector<uint8_t>& trits, std::vector<uint8_t>& out){
    out.clear();
    for(size_t i=0;i<trits.size(); i+=5){
        int k=(int)std::min<size_t>(5,trits.size()-i);
        uint32_t v=0,p=1; for(int j=0;j<k;++j){ v += trits[i+j]*p; p*=3; }
        out.push_back((uint8_t)v);
    }
}

static const char* mname(SubwordMode m){
    switch(m){
        case SubwordMode::S27: return "S27";
        case SubwordMode::S24: return "S24";
        case SubwordMode::S21: return "S21";
        case SubwordMode::S18: return "S18";
        case SubwordMode::S15: return "S15";
        default: return "UNK";
    }
}

struct Args {
    std::string in;
    bool matrix=false;
    bool single=false;
    SubwordMode outer=SubwordMode::S27;
    SubwordMode inner=SubwordMode::S21;
};
static bool parse_args(int argc,char**argv, Args& a){
    for(int i=1;i<argc;++i){
        std::string s=argv[i];
        if(s=="--in" && i+1<argc){ a.in=argv[++i]; }
        else if(s=="--matrix"){ a.matrix=true; }
        else if(s=="--outer" && i+1<argc){
            std::string v=argv[++i];
            if(v=="S27") a.outer=SubwordMode::S27;
            else if(v=="S24") a.outer=SubwordMode::S24;
            else if(v=="S21") a.outer=SubwordMode::S21;
            else if(v=="S18") a.outer=SubwordMode::S18;
            else if(v=="S15") a.outer=SubwordMode::S15;
            a.single=true;
        }
        else if(s=="--inner" && i+1<argc){
            std::string v=argv[++i];
            if(v=="S24") a.inner=SubwordMode::S24;
            else if(v=="S21") a.inner=SubwordMode::S21;
            else if(v=="S18") a.inner=SubwordMode::S18;
            else if(v=="S15") a.inner=SubwordMode::S15;
            a.single=true;
        }
    }
    return !a.in.empty() && (a.matrix || a.single);
}

static void trits_signature_from_words(const std::vector<Word27>& words, uint16_t& crc12, uint8_t& parity3){
    // Signature simple sur octets bruts de words (suffisant pour rapport)
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(words.data());
    size_t len = words.size()*sizeof(Word27);
    crc12  = crc12_0x80F(raw, len);
    // Parité mod3 : approx en prenant les octets mod 3
    uint32_t s=0; for(size_t i=0;i<len;++i) s += (raw[i]%3);
    parity3 = (uint8_t)(s%3);
}

static bool encode_direct_rawN_and_dump(const ImageU8& src, SubwordMode sub,
                                        std::string& out_png,
                                        uint16_t& crc12, uint8_t& parity3, size_t& nwords){
    StdRes R=std_res_for(sub);
    ImageU8 work; if(src.w!=R.w || src.h!=R.h) resize_rgb_nn(src, R.w, R.h, work); else work=src;
    std::vector<PixelYCbCrQuant> q; rgb_to_quant_stream(work, q);
    std::vector<Word27> words;
    if(!encode_raw_pixels_to_words_subword(q, sub, words)) return false;
    nwords = words.size();
    trits_signature_from_words(words, crc12, parity3);

    // Decode -> PNG
    std::vector<PixelYCbCrQuant> back;
    if(!decode_raw_words_to_pixels_subword(words, sub, back)) return false;
    ImageU8 img; quant_stream_to_rgb(back, R.w, R.h, img);
    out_png = std::string("out_direct_") + mname(sub) + ".png";
    return save_image_png(out_png, img);
}

static bool encode_centered_canvas_and_dump(const ImageU8& src, SubwordMode outer, SubwordMode inner,
                                            std::string& out_png,
                                            uint16_t& crc12, uint8_t& parity3, size_t& nwords){
    if(outer==SubwordMode::S15) return false;
    StdRes Rout=std_res_for(outer), Rin=std_res_for(inner);
    ImageU8 innerR; resize_rgb_nn(src, Rin.w, Rin.h, innerR);
    ImageU8 canvas; blit_center_rgb(innerR, Rout.w, Rout.h, canvas);
    std::vector<PixelYCbCrQuant> q; rgb_to_quant_stream(canvas, q);
    std::vector<Word27> words;
    if(!encode_raw_pixels_to_words_subword(q, outer, words)) return false;
    nwords = words.size();
    trits_signature_from_words(words, crc12, parity3);

    // Decode -> PNG
    std::vector<PixelYCbCrQuant> back;
    if(!decode_raw_words_to_pixels_subword(words, outer, back)) return false;
    ImageU8 img; quant_stream_to_rgb(back, Rout.w, Rout.h, img);
    out_png = std::string("out_center_") + mname(outer) + "_inner_" + mname(inner) + ".png";
    return save_image_png(out_png, img);
}

int main(int argc,char**argv){
    Args A{};
    if(!parse_args(argc,argv,A)){
        std::cerr<<"Usage:\n  "<<argv[0]<<" --in <image> --matrix\n"
                 <<"  "<<argv[0]<<" --in <image> --outer S27 --inner S21\n";
        return 2;
    }

    ImageU8 src;
    if(!load_image_rgb8(A.in, src)){
        std::cerr<<"cannot load: "<<A.in<<"\n"; return 1;
    }

    std::cout << "{\n  \"minitest_images\": {\n";
    std::cout << "    \"input\":\""<<A.in<<"\",\n";

    bool all_ok=true;

    if(A.matrix){
        std::cout << "    \"direct\": [\n";
        bool f=true;
        for(SubwordMode sub : {SubwordMode::S27,SubwordMode::S24,SubwordMode::S21,SubwordMode::S18,SubwordMode::S15}){
            std::string png; uint16_t crc12=0; uint8_t p3=0; size_t nwords=0;
            bool ok = encode_direct_rawN_and_dump(src, sub, png, crc12, p3, nwords);
            if(!f) std::cout << ",\n"; f=false;
            std::cout << "      {\"mode\":\""<<mname(sub)<<"\",\"ok\":"<<(ok?"true":"false")
                      <<",\"png\":\""<<png<<"\",\"words\":"<<nwords
                      <<",\"crc12_raw\":\""<<std::hex<<std::uppercase<<std::setw(3)<<std::setfill('0')<<(crc12&0x0FFF)<<std::dec
                      <<"\",\"parity3\":"<<(int)p3<<"}";
            all_ok = all_ok && ok;
        }
        std::cout << "\n    ],\n";

        // Matrice canevas glissant (quelques combinaisons)
        struct Pair{SubwordMode outer, inner;};
        std::vector<Pair> P = {
            {SubwordMode::S27,SubwordMode::S24},
            {SubwordMode::S27,SubwordMode::S21},
            {SubwordMode::S24,SubwordMode::S21},
            {SubwordMode::S21,SubwordMode::S18},
            {SubwordMode::S18,SubwordMode::S15}
        };
        std::cout << "    \"centered\": [\n";
        f=true;
        for(const auto& pr : P){
            std::string png; uint16_t crc12=0; uint8_t p3=0; size_t nwords=0;
            bool ok = encode_centered_canvas_and_dump(src, pr.outer, pr.inner, png, crc12, p3, nwords);
            if(!f) std::cout << ",\n"; f=false;
            std::cout << "      {\"outer\":\""<<mname(pr.outer)<<"\",\"inner\":\""<<mname(pr.inner)<<"\",\"ok\":"<<(ok?"true":"false")
                      <<",\"png\":\""<<png<<"\",\"words\":"<<nwords
                      <<",\"crc12_raw\":\""<<std::hex<<std::uppercase<<std::setw(3)<<std::setfill('0')<<(crc12&0x0FFF)<<std::dec
                      <<"\",\"parity3\":"<<(int)p3<<"}";
            all_ok = all_ok && ok;
        }
        std::cout << "\n    ]\n";
    } else {
        // Un seul couple outer/inner
        std::string png; uint16_t crc12=0; uint8_t p3=0; size_t nwords=0;
        bool ok = encode_centered_canvas_and_dump(src, A.outer, A.inner, png, crc12, p3, nwords);
        std::cout << "    \"centered_single\": {\"outer\":\""<<mname(A.outer)<<"\",\"inner\":\""<<mname(A.inner)<<"\",\"ok\":"<<(ok?"true":"false")
                  <<",\"png\":\""<<png<<"\",\"words\":"<<nwords
                  <<",\"crc12_raw\":\""<<std::hex<<std::uppercase<<std::setw(3)<<std::setfill('0')<<(crc12&0x0FFF)<<std::dec
                  <<"\",\"parity3\":"<<(int)p3<<"}\n";
        all_ok = all_ok && ok;
    }

    std::cout << "  ,\"final_status\": "<<(all_ok? "\"PASS\"" : "\"CHECK\"")<<"\n";
    std::cout << "  }\n}\n";
    return all_ok? 0:1;
}
