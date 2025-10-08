// ============================================================================
//  File: include/t3b_io.hpp
//  Container .t3b: Base-243 packed trits with CRC32 integrity.
// ============================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>
#include "ternary_image_codec_v6_min.hpp"
#include "ternary_packing.hpp"

namespace t3b_detail
{
inline uint32_t crc32_table[256];
inline bool crc_init_done=false;
inline void crc32_init()
{
    if(crc_init_done) return;
    crc_init_done=true;
    const uint32_t poly=0xEDB88320u;
    for(uint32_t i=0; i<256; ++i)
    {
        uint32_t c=i;
        for(int j=0; j<8; ++j)
        {
            c = (c&1)? (poly ^ (c>>1)) : (c>>1);
        }
        crc32_table[i]=c;
    }
}
inline uint32_t crc32(const void* data, size_t len)
{
    crc32_init();
    uint32_t c=0xFFFFFFFFu;
    const uint8_t* p=(const uint8_t*)data;
    for(size_t i=0; i<len; ++i) c = crc32_table[(c ^ p[i]) & 0xFFu] ^ (c>>8);
    return c ^ 0xFFFFFFFFu;
}
}

#pragma pack(push,1)
struct T3BHeaderBin
{
    char magic[4];
    uint8_t version;
    uint8_t subword_code;
    uint8_t centered;
    uint8_t reserved0;
    uint32_t width,height;
    uint32_t trit_count;
    uint8_t tail_trits;
    uint32_t payload_crc32;
    uint32_t header_crc32;
};
#pragma pack(pop)

inline bool t3b_write_file(const std::string& path, SubwordMode sub, bool centered, uint32_t width,uint32_t height, const std::vector<UTrit>& tr)
{
    FILE* f=std::fopen(path.c_str(),"wb");
    if(!f) return false;
    std::vector<uint8_t> packed;
    uint8_t tail=0;
    pack_trits_base243(tr,packed,tail);
    T3BHeaderBin h{};
    std::memcpy(h.magic,"T3B1",4);
    h.version=1;
    h.subword_code=(uint8_t)subword_to_code(sub);
    h.centered=centered?1:0;
    h.width=width;
    h.height=height;
    h.trit_count=(uint32_t)tr.size();
    h.tail_trits=tail;
    h.payload_crc32=t3b_detail::crc32(packed.data(), packed.size());
    h.header_crc32=t3b_detail::crc32(&h, sizeof(T3BHeaderBin)-sizeof(uint32_t));
    bool ok= std::fwrite(&h,sizeof(h),1,f)==1 && (!packed.empty()? std::fwrite(packed.data(),packed.size(),1,f)==1 : true);
    std::fclose(f);
    return ok;
}
inline bool t3b_read_file(const std::string& path, T3BHeaderBin& h, std::vector<UTrit>& tr)
{
    FILE* f=std::fopen(path.c_str(),"rb");
    if(!f) return false;
    if(std::fread(&h,sizeof(h),1,f)!=1)
    {
        std::fclose(f);
        return false;
    }
    if(std::memcmp(h.magic,"T3B1",4)!=0)
    {
        std::fclose(f);
        return false;
    }
    uint32_t hc=t3b_detail::crc32(&h, sizeof(T3BHeaderBin)-sizeof(uint32_t));
    if(hc!=h.header_crc32)
    {
        std::fclose(f);
        return false;
    }
    std::vector<uint8_t> packed;
    packed.resize((size_t)h.trit_count/5 + (h.tail_trits?1:0));
    if(!packed.empty() && std::fread(packed.data(), packed.size(),1,f)!=1)
    {
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    if(t3b_detail::crc32(packed.data(), packed.size())!=h.payload_crc32) return false;
    return unpack_trits_base243(packed.data(), packed.size(), h.tail_trits, tr);
}
