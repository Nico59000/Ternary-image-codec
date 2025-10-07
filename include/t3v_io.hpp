// ============================================================================
//  File: include/t3v_io.hpp
//  Container .t3v: simple stream of Word27 frames + header, per-frame CRC32.
//  This header is self-contained and used by tools and the FFmpeg bridge.
// ============================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>
#include <cstring>
#include <string>
#include "ternary_image_codec_v6_min.hpp"

namespace t3v_detail
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
struct T3VHeaderBin
{
    char     magic[4];
    uint8_t  version;     // =1
    uint8_t  file_type;   // 0=image; 1=video
    uint8_t  profile;     // ProfileID
    uint8_t  subword_code;// 0..4
    uint8_t  centered;    // bool
    uint8_t  coset;       // CosetID
    uint32_t width, height;      // canvas (S27 base)
    uint32_t aw_x0, aw_y0, aw_w, aw_h; // active window
    uint32_t fps_num, fps_den;   // optional
    uint32_t frame_count;        // may be 0 until finalize
    uint32_t reserved0;
    uint32_t header_crc32;       // CRC32 of all previous fields
};
#pragma pack(pop)

inline uint8_t subword_to_code(SubwordMode m)
{
    switch(m)
    {
    case SubwordMode::S27:
        return 0;
    case SubwordMode::S24:
        return 1;
    case SubwordMode::S21:
        return 2;
    case SubwordMode::S18:
        return 3;
    case SubwordMode::S15:
        return 4;
    }
    return 0;
}
inline SubwordMode code_to_subword(uint8_t c)
{
    switch(c)
    {
    case 0:
        return SubwordMode::S27;
    case 1:
        return SubwordMode::S24;
    case 2:
        return SubwordMode::S21;
    case 3:
        return SubwordMode::S18;
    case 4:
        return SubwordMode::S15;
    default:
        return SubwordMode::S27;
    }
}

inline bool t3v_write_header(FILE* f, ProfileID prof, SubwordMode sub, bool centered, CosetID coset, uint32_t width,uint32_t height, const ActiveWindow& aw, uint32_t fps_num=0, uint32_t fps_den=1, uint32_t frame_count=1, uint8_t file_type=0)
{
    T3VHeaderBin h{};
    std::memcpy(h.magic,"T3V1",4);
    h.version=1;
    h.file_type=file_type;
    h.profile=(uint8_t)prof;
    h.subword_code=subword_to_code(sub);
    h.centered=centered?1:0;
    h.coset=(uint8_t)coset;
    h.width=width;
    h.height=height;
    h.aw_x0=aw.x0;
    h.aw_y0=aw.y0;
    h.aw_w=aw.w;
    h.aw_h=aw.h;
    h.fps_num=fps_num;
    h.fps_den=fps_den;
    h.frame_count=frame_count;
    h.reserved0=0;
    h.header_crc32 = t3v_detail::crc32(&h, sizeof(T3VHeaderBin)-sizeof(uint32_t));
    return std::fwrite(&h, sizeof(h), 1, f)==1;
}
inline bool t3v_read_header(FILE* f, T3VHeaderBin& h)
{
    if(std::fread(&h, sizeof(h), 1, f)!=1) return false;
    if(std::memcmp(h.magic, "T3V1", 4)!=0) return false;
    uint32_t c = t3v_detail::crc32(&h, sizeof(T3VHeaderBin)-sizeof(uint32_t));
    return (c==h.header_crc32);
}

inline bool t3v_write_frame(FILE* f, const std::vector<Word27>& words)
{
    uint32_t n= (uint32_t)words.size();
    if(std::fwrite(&n, sizeof(n), 1, f)!=1) return false;
    std::vector<uint8_t> buf;
    buf.reserve((size_t)n*9);
    for(const auto& w: words)
    {
        for(int s=0; s<9; ++s) buf.push_back(w.sym[s]%27);
    }
    if(!buf.empty() && std::fwrite(buf.data(), buf.size(), 1, f)!=1) return false;
    uint32_t crc = t3v_detail::crc32(&n, sizeof(n));
    crc = t3v_detail::crc32(buf.data(), buf.size()) ^ (crc*16777619u);
    return std::fwrite(&crc, sizeof(crc), 1, f)==1;
}
inline bool t3v_read_frame(FILE* f, std::vector<Word27>& words)
{
    uint32_t n=0;
    if(std::fread(&n, sizeof(n), 1, f)!=1) return false;
    size_t bytes=(size_t)n*9;
    std::vector<uint8_t> buf(bytes);
    if(bytes && std::fread(buf.data(), bytes, 1, f)!=1) return false;
    uint32_t crc_file=0;
    if(std::fread(&crc_file, sizeof(crc_file), 1, f)!=1) return false;
    uint32_t crc = t3v_detail::crc32(&n, sizeof(n));
    crc = t3v_detail::crc32(buf.data(), buf.size()) ^ (crc*16777619u);
    if(crc!=crc_file) return false;
    words.assign(n, Word27{});
    size_t k=0;
    for(uint32_t i=0; i<n; ++i) for(int s=0; s<9; ++s) words[i].sym[s]=buf[k++];
    return true;
}

inline SubwordMode t3v_header_subword(const T3VHeaderBin& h)
{
    return code_to_subword(h.subword_code);
}
inline ActiveWindow t3v_header_aw(const T3VHeaderBin& h)
{
    return {h.aw_x0,h.aw_y0,h.aw_w,h.aw_h};
}
inline FILE* t3v_fopen(const std::string& path, const char* mode)
{
    return std::fopen(path.c_str(), mode);
}
inline void  t3v_fclose(FILE* f)
{
    if(f) std::fclose(f);
}
