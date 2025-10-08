// ============================================================================
//  File: src/t3dump.cpp — CLI .t3p / .t3v dumper + extract PNG
//  Project: Ternary Image/Video Codec v6
//
//  USAGE EXAMPLES
//  --------------
//   # Info en texte
//   ./t3dump input.t3p
//   ./t3dump input.t3v
//
//   # Rapport JSON
//   ./t3dump input.t3v --json
//
//   # Extraire la frame 0 en PNG
//   ./t3dump input.t3p --extract-png 0 --out out.png
//
//   # Extraire toutes les frames .t3v dans un dossier
//   ./t3dump input.t3v --extract-png all --outdir ./frames
//
//  BUILD (exemples)
//  ----------------
//   g++ -std=c++17 -O2 -Iinclude -Ithird_party \
//       src/compile_stb.cpp src/t3dump.cpp -o t3dump
//
//   # Avec CMake, voir bloc fourni plus bas.
//
//  NOTES
//  -----
//   * CRC-12(0x80F) calculé sur les octets bruts de Word27 (format .t3p/.t3v minimal).
//   * Parité mod 3 ~ somme(byte%3) mod 3 sur les octets bruts (approx rapide).
//   * Extraction PNG utilise words_to_image_subword(...) du pont io_image.hpp.
// ============================================================================

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include <algorithm>

#include "ternary_image_codec_v6_min.hpp" // SubwordMode, StdRes helpers
#include "io_t3p_t3v.hpp"                 // t3p_* / t3v_* (impl minimale fournie)
#include "io_image.hpp"                   // words_to_image_subword(...)

static uint16_t crc12_0x80F(const uint8_t* data, size_t len)
{
    uint16_t poly = 0x80F, crc = 0x000;
    for(size_t i=0; i<len; ++i)
    {
        uint8_t byte=data[i];
        for(int b=7; b>=0; --b)
        {
            uint8_t inb=(byte>>b)&1u, msb=(crc>>11)&1u;
            crc<<=1;
            if(msb^inb) crc^=poly;
            crc&=0x0FFF;
        }
    }
    return (crc & 0x0FFF);
}
static uint8_t approx_parity_mod3(const uint8_t* data, size_t len)
{
    uint32_t s=0;
    for(size_t i=0; i<len; ++i) s += (data[i]%3);
    return (uint8_t)(s%3);
}
static const char* mname(SubwordMode m)
{
    switch(m)
    {
    case SubwordMode::S27:
        return "S27";
    case SubwordMode::S24:
        return "S24";
    case SubwordMode::S21:
        return "S21";
    case SubwordMode::S18:
        return "S18";
    case SubwordMode::S15:
        return "S15";
    default:
        return "UNK";
    }
}
static bool has_suffix(const std::string& s, const char* suf)
{
    if(s.size() < std::strlen(suf)) return false;
    return std::equal(s.end()-std::strlen(suf), s.end(), suf,
                      [](char a,char b)
    {
        return std::tolower(a)==std::tolower(b);
    });
}

struct Args
{
    std::string path;
    bool json=false;
    bool extract=false;
    bool extract_all=false;
    int  idx=0;
    std::string out_png="frame.png";
    std::string outdir=".";
};
static void print_usage(const char* exe)
{
    std::cerr
            << "Usage:\n"
            << "  " << exe << " <file.t3p|file.t3v> [--json]\n"
            << "  " << exe << " <file> --extract-png 0 --out out.png\n"
            << "  " << exe << " <file.t3v> --extract-png all --outdir ./frames\n";
}
static bool parse_args(int argc,char**argv, Args& a)
{
    if(argc<2)
    {
        print_usage(argv[0]);
        return false;
    }
    a.path=argv[1];
    for(int i=2; i<argc; ++i)
    {
        std::string s=argv[i];
        if(s=="--json")
        {
            a.json=true;
        }
        else if(s=="--extract-png" && i+1<argc)
        {
            std::string v=argv[++i];
            if(v=="all")
            {
                a.extract=true;
                a.extract_all=true;
            }
            else
            {
                a.extract=true;
                a.extract_all=false;
                a.idx=std::atoi(v.c_str());
            }
        }
        else if(s=="--out" && i+1<argc)
        {
            a.out_png=argv[++i];
        }
        else if(s=="--outdir" && i+1<argc)
        {
            a.outdir=argv[++i];
        }
    }
    return !a.path.empty();
}

static bool dump_t3p(const Args& A)
{
    SubwordMode sub;
    int w=0,h=0;
    std::vector<Word27> words;
    std::string meta;
    if(!t3p_read(A.path, sub, w, h, words, &meta))
    {
        std::cerr<<"[t3dump] read failed: "<<A.path<<"\n";
        return false;
    }
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(words.data());
    size_t raw_len = words.size()*sizeof(Word27);
    uint16_t crc = crc12_0x80F(raw, raw_len);
    uint8_t  p3  = approx_parity_mod3(raw, raw_len);

    if(A.json)
    {
        std::cout << "{\n"
                  << "  \"t3p\": {\n"
                  << "    \"file\": \""<<A.path<<"\",\n"
                  << "    \"mode\": \""<<mname(sub)<<"\",\n"
                  << "    \"w\": "<<w<<", \"h\": "<<h<<", \"words\": "<<words.size()<<",\n"
                  << "    \"crc12_raw\": \""<< std::hex << std::uppercase << std::setw(3) << std::setfill('0') << (crc&0x0FFF) << std::dec << "\",\n"
                  << "    \"parity3\": "<<(int)p3<<",\n"
                  << "    \"meta_len\": "<<meta.size()<<"\n"
                  << "  }\n}\n";
    }
    else
    {
        std::cout<<"== .t3p ==\n"
                 <<"file: "<<A.path<<"\n"
                 <<"mode: "<<mname(sub)<<"\n"
                 <<"size: "<<w<<" x "<<h<<"\n"
                 <<"words: "<<words.size()<<" (bytes="<<raw_len<<")\n"
                 <<"crc12(raw): 0x"<< std::hex << std::uppercase << std::setw(3) << std::setfill('0') << (crc&0x0FFF) << std::dec << "\n"
                 <<"parity3(raw): "<<(int)p3<<"\n"
                 <<"meta: "<<meta.size()<<" bytes\n";
    }

    if(A.extract)
    {
        int idx = 0; // .t3p -> frame unique
        if(!A.extract_all && A.idx!=0)
        {
            std::cerr<<"[t3dump] .t3p has only frame 0\n";
            return false;
        }
        std::string out = A.extract_all ? (A.outdir+"/frame_0000.png") : A.out_png;
        bool ok = words_to_image_subword(words, sub, w, h, out);
        if(!ok)
        {
            std::cerr<<"[t3dump] PNG write failed: "<<out<<"\n";
            return false;
        }
        if(!A.json) std::cout<<"extracted -> "<<out<<"\n";
    }
    return true;
}

static bool dump_t3v(const Args& A)
{
    SubwordMode sub;
    int w=0,h=0;
    std::vector<std::vector<Word27>> frames;
    double fps=0.0;
    std::string meta;
    if(!t3v_read(A.path, sub, w, h, frames, fps, &meta))
    {
        std::cerr<<"[t3dump] read failed: "<<A.path<<"\n";
        return false;
    }
    size_t total_words=0, total_bytes=0;
    uint16_t crc_glob=0; // on calcule CRC par concat (simple)
    uint8_t  p3_glob=0;
    for(const auto& fr : frames)
    {
        total_words += fr.size();
        total_bytes += fr.size()*sizeof(Word27);
    }
    // CRC/parité sur concat — itératif
    for(const auto& fr : frames)
    {
        const uint8_t* raw = reinterpret_cast<const uint8_t*>(fr.data());
        size_t raw_len = fr.size()*sizeof(Word27);
        uint16_t c = crc12_0x80F(raw, raw_len);
        // Combine simple (XOR) — pour un hash “rapide”
        crc_glob ^= c;
        p3_glob  = (uint8_t)((p3_glob + approx_parity_mod3(raw, raw_len)) % 3);
    }

    if(A.json)
    {
        std::cout << "{\n"
                  << "  \"t3v\": {\n"
                  << "    \"file\": \""<<A.path<<"\",\n"
                  << "    \"mode\": \""<<mname(sub)<<"\",\n"
                  << "    \"w\": "<<w<<", \"h\": "<<h<<", \"frames\": "<<frames.size()<<", \"fps\": "<<fps<<",\n"
                  << "    \"words_total\": "<<total_words<<", \"bytes_total\": "<<total_bytes<<",\n"
                  << "    \"crc12_concat_xor\": \""<< std::hex << std::uppercase << std::setw(3) << std::setfill('0') << (crc_glob&0x0FFF) << std::dec <<"\",\n"
                  << "    \"parity3_sum\": "<<(int)p3_glob<<",\n"
                  << "    \"meta_len\": "<<meta.size()<<"\n"
                  << "  }\n}\n";
    }
    else
    {
        std::cout<<"== .t3v ==\n"
                 <<"file: "<<A.path<<"\n"
                 <<"mode: "<<mname(sub)<<"  fps: "<<fps<<"\n"
                 <<"size: "<<w<<" x "<<h<<"\n"
                 <<"frames: "<<frames.size()<<"\n"
                 <<"words_total: "<<total_words<<"  bytes_total: "<<total_bytes<<"\n"
                 <<"crc12(concat^): 0x"<< std::hex << std::uppercase << std::setw(3) << std::setfill('0') << (crc_glob&0x0FFF) << std::dec << "\n"
                 <<"parity3(sum): "<<(int)p3_glob<<"\n"
                 <<"meta: "<<meta.size()<<" bytes\n";
    }

    if(A.extract)
    {
        if(A.extract_all)
        {
            // toutes les frames
            for(size_t i=0; i<frames.size(); ++i)
            {
                char name[256];
                std::snprintf(name, sizeof(name), "%s/frame_%04zu.png", A.outdir.c_str(), i);
                if(!words_to_image_subword(frames[i], sub, w, h, name))
                {
                    std::cerr<<"[t3dump] PNG write failed: "<<name<<"\n";
                    return false;
                }
            }
            if(!A.json) std::cout<<"extracted "<<frames.size()<<" frames -> "<<A.outdir<<"/frame_####.png\n";
        }
        else
        {
            int idx = std::clamp(A.idx, 0, (int)frames.size()-1);
            std::string out = A.out_png;
            if(!words_to_image_subword(frames[(size_t)idx], sub, w, h, out))
            {
                std::cerr<<"[t3dump] PNG write failed: "<<out<<"\n";
                return false;
            }
            if(!A.json) std::cout<<"extracted frame "<<idx<<" -> "<<out<<"\n";
        }
    }
    return true;
}

int main(int argc,char**argv)
{
    Args A{};
    if(!parse_args(argc,argv,A)) return 2;

    bool ok=false;
    if(has_suffix(A.path, ".t3p")) ok = dump_t3p(A);
    else if(has_suffix(A.path, ".t3v")) ok = dump_t3v(A);
    else
    {
        std::cerr<<"[t3dump] unsupported extension (expect .t3p or .t3v)\n";
        return 2;
    }
    return ok? 0 : 1;
}
