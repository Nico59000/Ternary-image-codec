// ============================================================================
//  File: src/minitest_t3containers.cpp — Tests .t3p / .t3v (rapport JSON)
//  Project: Ternary Image/Video Codec v6
//  Build (exemple):
//    g++ -std=c++17 -O2 -Iinclude -Ithird_party \
//        src/minitest_t3containers.cpp -o minitest_t3containers
//  Note: détecte io_t3p_t3v.hpp via __has_include ; si absent => SKIP.
// ============================================================================

#include <cstdint>
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>
#include <random>
#include <algorithm>

#include "ternary_image_codec_v6_min.hpp"
#include "io_image.hpp" // ImageU8 + quant helpers

// ---------- CRC12(0x80F) & Parité ternaire {0,1,2} --------------------------
static uint16_t crc12_0x80F(const uint8_t* data, size_t len){
    uint16_t poly = 0x80F, crc = 0x000;
    for(size_t i=0;i<len;++i){
        uint8_t byte = data[i];
        for(int b=7;b>=0;--b){
            uint8_t inb = (byte>>b)&1u;
            uint8_t msb = (crc>>11)&1u;
            crc <<= 1;
            if(msb ^ inb) crc ^= poly;
            crc &= 0x0FFF;
        }
    }
    return crc & 0x0FFF;
}
static uint8_t parity_mod3_unbalanced(const std::vector<uint8_t>& u){
    uint32_t s=0; for(uint8_t t: u) s+=t; return (uint8_t)(s%3);
}

// ---------- pack base-243 (5 trits {0,1,2} -> 1 octet 0..242) --------------
static void pack_u_base243(const std::vector<uint8_t>& trits, std::vector<uint8_t>& out){
    out.clear();
    size_t n = trits.size();
    for(size_t i=0;i<n;i+=5){
        int k = (int)std::min<size_t>(5, n-i);
        uint32_t val=0, p=1;
        for(int j=0;j<k;++j){ val += (uint32_t)trits[i+j]*p; p*=3; }
        out.push_back((uint8_t)val);
    }
}

// ---------- balanced {-1,0,1} -> unbalanced {0,1,2} ------------------------
static inline uint8_t bal_to_unb(int8_t b){ return (uint8_t)(b+1); }

// ---------- contenu synthétique (damier) ------------------------------------
static void make_rgb_pattern(int w,int h, ImageU8& out){
    out.w=w; out.h=h; out.c=3; out.data.assign((size_t)w*h*3,0);
    for(int y=0;y<h;++y){
        for(int x=0;x<w;++x){
            bool a=((x/8+y/8)%2)==0;
            uint8_t* p=&out.data[(size_t)(y*w+x)*3];
            p[0]=a? 220:30; p[1]=a? 40:210; p[2]=a? 50:230;
        }
    }
}

// ---------- Génère une image RAW-N words -----------------------------------
static bool make_words_for(SubwordMode sub, std::vector<Word27>& out, int& w, int& h){
    StdRes R = std_res_for(sub);
    ImageU8 rgb; make_rgb_pattern(R.w, R.h, rgb);
    std::vector<PixelYCbCrQuant> q; rgb_to_quant_stream(rgb, q);
    w=R.w; h=R.h;
    return encode_raw_pixels_to_words_subword(q, sub, out);
}

// ---------- T3 containers API (détectée) ------------------------------------
#if __has_include("io_t3p_t3v.hpp")
  #include "io_t3p_t3v.hpp"
  #define HAS_T3 1
#else
  #define HAS_T3 0
  // Stubs si le header n'est pas encore ajouté
  inline bool t3p_write(const std::string&, SubwordMode, int, int, const std::vector<Word27>&, const std::string& = {}){ return false; }
  inline bool t3p_read (const std::string&, SubwordMode&, int&, int&, std::vector<Word27>&, std::string* = nullptr){ return false; }
  inline bool t3v_write(const std::string&, SubwordMode, int, int, const std::vector<std::vector<Word27>>&, double, const std::string& = {}){ return false; }
  inline bool t3v_read (const std::string&, SubwordMode&, int&, int&, std::vector<std::vector<Word27>>&, double&, std::string* = nullptr){ return false; }
#endif

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

int main(){
    std::cout << "{\n  \"t3containers\": {\n";
    std::cout << "    \"available\": " << (HAS_T3? "true":"false") << ",\n";

    bool all_ok = true;

    // --- .t3p: une image par sous-mode -------------------------------------
    std::cout << "    \"t3p\": [\n";
    bool first=true;
    for(SubwordMode sub : {SubwordMode::S27,SubwordMode::S24,SubwordMode::S21,SubwordMode::S18,SubwordMode::S15}){
        int w=0,h=0; std::vector<Word27> words;
        bool ok_gen = make_words_for(sub, words, w, h);

        // sérialisation ternaire pour CRC/parité
        std::vector<int8_t> bal; bal.reserve((size_t)w*h*13);
        std::vector<uint8_t> unb; unb.reserve(bal.size());
        // NB: on ne dispose pas ici des trits balanced par pixel (exposés côté cœur);
        //     on produit une estimation via décodage->re-quant->encode pour la forme.
        //     Pour le rapport conteneur, on dérive la signature des 'words' byte-packés.
        (void)bal;

        // pack des Word27 (approx: on re-packe via trits unbalanced synthétiques)
        // Ici, pour rapport, on prend les bytes de la représentation brute des Word27 :
        const uint8_t* raw_bytes = reinterpret_cast<const uint8_t*>(words.data());
        size_t raw_len = words.size()*sizeof(Word27);
        uint16_t crc12 = crc12_0x80F(raw_bytes, raw_len);

        bool ok_write=false, ok_read=false, ok_eq=false;
        SubwordMode sub_r=SubwordMode::S27; int wr=0,hr=0; std::vector<Word27> words_r;

        if(HAS_T3 && ok_gen){
            std::string path = std::string("test_") + mname(sub) + ".t3p";
            ok_write = t3p_write(path, sub, w, h, words, "{\"gen\":\"minitest\"}");
            if(ok_write){
                ok_read = t3p_read(path, sub_r, wr, hr, words_r, nullptr);
                ok_eq   = ok_read && sub_r==sub && wr==w && hr==h && words_r==words;
            }
        }

        if(!first) std::cout << ",\n";
        first=false;
        std::cout << "      {\"mode\":\"" << mname(sub) << "\","
                  << "\"w\":"<<w<<",\"h\":"<<h
                  << ",\"words\":"<<words.size()
                  << ",\"crc12_raw\":\"" << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << (crc12&0x0FFF) << std::dec << "\""
                  << ",\"write\":" << (ok_write? "true":"false")
                  << ",\"read\":"  << (ok_read? "true":"false")
                  << ",\"equal\":" << (ok_eq? "true":"false")
                  << "}";
        all_ok = all_ok && (!HAS_T3 || (ok_write&&ok_read&&ok_eq));
    }
    std::cout << "\n    ],\n";

    // --- .t3v: petite séquence S21 (3 frames) -------------------------------
    std::cout << "    \"t3v\": {\n";
    bool ok_write=false, ok_read=false, ok_frames=false;
    int w=0,h=0; std::vector<std::vector<Word27>> frames;
    SubwordMode sub = SubwordMode::S21; StdRes R=std_res_for(sub);
    w=R.w; h=R.h;

    for(int i=0;i<3;++i){
        std::vector<Word27> words;
        if(!make_words_for(sub, words, w, h)){ all_ok=false; break; }
        frames.push_back(std::move(words));
    }
    double fps_w=25.0, fps_r=0.0;
    if(HAS_T3){
        ok_write = t3v_write("test_S21.t3v", sub, w, h, frames, fps_w, "{\"seq\":\"minitest\"}");
        std::vector<std::vector<Word27>> back;
        SubwordMode sub_r=SubwordMode::S27; int wr=0,hr=0;
        if(ok_write) ok_read = t3v_read("test_S21.t3v", sub_r, wr, hr, back, fps_r, nullptr);
        ok_frames = ok_read && sub_r==sub && wr==w && hr==h && back.size()==frames.size();
        if(ok_frames){
            for(size_t i=0;i<frames.size();++i) if(back[i]!=frames[i]) { ok_frames=false; break; }
        }
    }
    std::cout << "      \"mode\":\"S21\",\n";
    std::cout << "      \"w\":"<<w<<", \"h\":"<<h<<", \"frames\":"<<frames.size()<<",\n";
    std::cout << "      \"write\":"<<(ok_write? "true":"false")<<", \"read\":"<<(ok_read? "true":"false")<<", \"equal\":"<<(ok_frames? "true":"false")<<",\n";
    std::cout << "      \"fps_w\":"<<fps_w<<", \"fps_r\":"<<fps_r<<"\n";
    std::cout << "    },\n";

    std::cout << "    \"final_status\": " << (all_ok? "\"PASS\"" : "\"CHECK\"") << "\n";
    std::cout << "  }\n}\n";
    return all_ok? 0: 1;
}
