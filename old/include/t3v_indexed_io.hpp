// ============================================================================
//  File: include/t3v_indexed_io.hpp
//  Index sidecar for .t3v: allows random access to frames by byte offset.
// ============================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>
#include <cstddef>
#include "t3v_io.hpp"

#pragma pack(push,1)
struct T3VIndexBin
{
    char magic[4];
    uint8_t version;
    uint32_t frame_count;
    uint32_t reserved0;
    uint32_t header_crc32;
};
#pragma pack(pop)

inline bool t3v_index_write(const std::string& idx_path, uint32_t frame_count, const std::vector<uint64_t>& offsets)
{
    FILE* f=std::fopen(idx_path.c_str(),"wb");
    if(!f) return false;
    T3VIndexBin h{};
    std::memcpy(h.magic,"T3VI",4);
    h.version=1;
    h.frame_count=frame_count;
    h.reserved0=0;
    h.header_crc32=t3v_detail::crc32(&h,sizeof(T3VIndexBin)-sizeof(uint32_t));
    bool ok=(std::fwrite(&h,sizeof(h),1,f)==1);
    ok = ok && (!offsets.empty()? std::fwrite(offsets.data(), offsets.size()*sizeof(uint64_t), 1, f)==1 : true);
    std::fclose(f);
    return ok;
}
inline bool t3v_index_read(const std::string& idx_path, T3VIndexBin& h, std::vector<uint64_t>& offsets)
{
    FILE* f=std::fopen(idx_path.c_str(),"rb");
    if(!f) return false;
    if(std::fread(&h,sizeof(h),1,f)!=1)
    {
        std::fclose(f);
        return false;
    }
    if(std::memcmp(h.magic,"T3VI",4)!=0)
    {
        std::fclose(f);
        return false;
    }
    uint32_t hc=t3v_detail::crc32(&h,sizeof(T3VIndexBin)-sizeof(uint32_t));
    if(hc!=h.header_crc32)
    {
        std::fclose(f);
        return false;
    }
    offsets.resize(h.frame_count);
    if(h.frame_count && std::fread(offsets.data(), offsets.size()*sizeof(uint64_t),1,f)!=1)
    {
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    return true;
}

inline bool t3v_scan_and_index(const std::string& t3v_path, const std::string& idx_path)
{
    FILE* f=std::fopen(t3v_path.c_str(),"rb");
    if(!f) return false;
    T3VHeaderBin th{};
    if(!t3v_read_header(f, th))
    {
        std::fclose(f);
        return false;
    }
    std::vector<uint64_t> offs;
    offs.reserve(th.frame_count? th.frame_count : 1024);
    long base=std::ftell(f);
    if(base<0)
    {
        std::fclose(f);
        return false;
    }
    uint64_t pos=(uint64_t)base;
    std::vector<Word27> tmp;
    uint32_t n=0;
    while(true)
    {
        long here=std::ftell(f);
        if(here<0) break;
        pos=(uint64_t)here;
        if(std::fread(&n,sizeof(n),1,f)!=1) break;
        size_t bytes=(size_t)n*9;
        if(bytes)
        {
            if(std::fseek(f,(long)bytes,SEEK_CUR)!=0) break;
        }
        uint32_t crc=0;
        if(std::fread(&crc,sizeof(crc),1,f)!=1) break;
        offs.push_back(pos);
    }
    std::fclose(f);
    return t3v_index_write(idx_path, (uint32_t)offs.size(), offs);
}
