// ============================================================================
//  File: include/t3v_io.hpp — I/O conteneur frames Word27 (.t3v)
//  Format minimal append-friendly :
//   magic[4]='T3V1' | frames[4] | [ frame_i: words[4] | 9*words octets ]
//  Chaque octet ∈ [0..26] représente 1 symbole GF(27).
//  Les super-frames (profilés) peuvent inclure l'entête RS déjà encodé.
// ============================================================================
#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <fstream>
#include "ternary_packing.hpp"

namespace t3v {

struct Header { uint32_t magic=0x31563354; /*'T3V1'*/ uint32_t frames=0; };
struct FrameIndex { uint32_t words=0; uint64_t offset=0; };

inline bool write_single(const std::string& path, const std::vector<Word27>& frame){
    std::vector<uint8_t> bytes; tpack::words_to_bytes(frame, bytes);
    std::ofstream f(path, std::ios::binary); if(!f) return false;
    Header hdr{}; hdr.frames=1; f.write((const char*)&hdr, sizeof(hdr));
    uint32_t words=(uint32_t)frame.size(); f.write((const char*)&words, 4);
    f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
    return (bool)f;
}

inline bool read_all(const std::string& path, std::vector<std::vector<Word27>>& frames){
    std::ifstream f(path, std::ios::binary); if(!f) return false;
    Header hdr{}; f.read((char*)&hdr, sizeof(hdr)); if(!f || hdr.magic!=0x31563354) return false;
    frames.clear(); frames.reserve(hdr.frames);
    for(uint32_t i=0;i<hdr.frames;++i){
        uint32_t words=0; f.read((char*)&words, 4); if(!f) return false;
        std::vector<uint8_t> bytes; bytes.resize((size_t)words*9);
        f.read((char*)bytes.data(), (std::streamsize)bytes.size()); if(!f) return false;
        std::vector<Word27> w; tpack::bytes_to_words(bytes, w);
        frames.push_back(std::move(w));
    }
    return true;
}

} // namespace t3v
