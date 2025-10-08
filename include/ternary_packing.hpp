// ============================================================================
//  File: include/ternary_packing.hpp — utilitaires de (dé)paquetage ternaire
//  - Base-243 (3^5) : 5 UTrit → 1 octet [0..242]
//  - Word27 <-> octets (9 octets par word, valeurs 0..26)
//  Remarque: Ces helpers restent en {0,1,2} (interne). Balanced se gère en amont.
// ============================================================================
#pragma once
#include <vector>
#include <cstdint>
#include <array>
#include <cstring>
#include <string>
#include "ternary_image_codec_v6_min.hpp"

namespace tpack {

// -- Base-243 ---------------------------------------------------------------
inline uint8_t pack5_utrits_to_byte(const UTrit* t) {
    // LSD-first: v = t0 + 3*t1 + 9*t2 + 27*t3 + 81*t4
    int v = t[0] + 3*t[1] + 9*t[2] + 27*t[3] + 81*t[4];
    return (uint8_t)v; // 0..242
}
inline void unpack_byte_to5_utrits(uint8_t b, UTrit* t) {
    int v=b;
    for(int i=0;i<5;++i){ t[i]=(UTrit)(v%3); v/=3; }
}

inline void ut_to_base243(const std::vector<UTrit>& in, std::vector<uint8_t>& out){
    out.clear(); size_t i=0; UTrit buf[5]{};
    // header minimal : total_trits (uint32 little-endian)
    uint32_t total = (uint32_t)in.size();
    out.insert(out.end(), (uint8_t*)&total, (uint8_t*)&total + sizeof(uint32_t));
    while(i<in.size()){
        int take=0; for(; take<5 && i<in.size(); ++take) buf[take]=in[i++];
        for(int j=take;j<5;++j) buf[j]=0; // pad
        out.push_back( pack5_utrits_to_byte(buf) );
    }
}

inline bool base243_to_ut(const std::vector<uint8_t>& in, std::vector<UTrit>& out){
    if(in.size()<4) return false;
    uint32_t total=0; std::memcpy(&total, in.data(), 4);
    out.clear(); out.reserve(total);
    size_t idx=4;
    while(idx<in.size() && out.size()<total){
        UTrit t[5]{}; unpack_byte_to5_utrits(in[idx++], t);
        for(int k=0;k<5 && out.size()<total; ++k) out.push_back(t[k]);
    }
    return (out.size()==total);
}

// -- Word27 <-> octets -------------------------------------------------------
inline void words_to_bytes(const std::vector<Word27>& words, std::vector<uint8_t>& out){
    out.clear(); out.reserve(words.size()*9);
    for(const auto& w: words){
        for(int s=0;s<9;++s) out.push_back((uint8_t)(w.sym[s]%27)); // 0..26
    }
}

inline void bytes_to_words(const std::vector<uint8_t>& in, std::vector<Word27>& out){
    out.clear(); if(in.size()%9!=0) return;
    size_t cnt=in.size()/9; out.resize(cnt);
    size_t k=0;
    for(size_t i=0;i<cnt;++i){ for(int s=0;s<9;++s) out[i].sym[s]=(GF27)(in[k++]%27); }
}

} // namespace tpack
