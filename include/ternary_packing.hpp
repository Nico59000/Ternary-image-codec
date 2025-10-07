// ============================================================================
//  File: include/ternary_packing.hpp
//  Packs trit streams (base-3) into bytes using base-243 (5 trits -> 1 byte).
// ============================================================================
#pragma once
#include <vector>
#include <cstdint>
#include "ternary_image_codec_v6_min.hpp" // UTrit

inline void pack_trits_base243(const std::vector<UTrit>& in, std::vector<uint8_t>& out, uint8_t& tail_trits)
{
    out.clear();
    tail_trits=0;
    size_t i=0, N=in.size();
    while(i + 5 <= N)
    {
        uint32_t v=(in[i]%3) + 3*(in[i+1]%3) + 9*(in[i+2]%3) + 27*(in[i+3]%3) + 81*(in[i+4]%3);
        out.push_back((uint8_t)v);
        i+=5;
    }
    size_t left=N-i;
    tail_trits=(uint8_t)left;
    if(left)
    {
        uint32_t v=0,p=1;
        for(size_t k=0; k<left; ++k)
        {
            v += p*(in[i+k]%3);
            p*=3;
        }
        out.push_back((uint8_t)v);
    }
}
inline bool unpack_trits_base243(const uint8_t* data, size_t len, uint8_t tail_trits, std::vector<UTrit>& out)
{
    out.clear();
    if(!data && len) return false;
    if(tail_trits>4) return false;
    size_t full=(len==0?0:(len-(tail_trits?1:0)));
    for(size_t i=0; i<full; ++i)
    {
        uint32_t v=data[i];
        for(int k=0; k<5; ++k)
        {
            out.push_back((UTrit)(v%3));
            v/=3;
        }
    }
    if(tail_trits)
    {
        uint32_t v=data[len-1];
        for(uint8_t k=0; k<tail_trits; ++k)
        {
            out.push_back((UTrit)(v%3));
            v/=3;
        }
    }
    return true;
}
