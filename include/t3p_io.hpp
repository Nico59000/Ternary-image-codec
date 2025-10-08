// ============================================================================
//  File: include/t3p_io.hpp — I/O pour flux UTrit en Base-243 (.t3p)
//  Format simple :
//   magic[4]='T3P1' | total_trits[4] | data[...]  (data = bytes base-243)
//  NB: .t3p transporte des UTrit (interne). Pour RAW balanced, convertir en amont.
// ============================================================================
#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <fstream>
#include "ternary_packing.hpp"

namespace t3p {

struct Header { uint32_t magic=0x31503354; /*'T3P1'*/ uint32_t total_trits=0; };

inline bool write(const std::string& path, const std::vector<UTrit>& ut){
    std::vector<uint8_t> payload; tpack::ut_to_base243(ut, payload);
    if(payload.size()<4) return false; // contient déjà total_trits
    // Remplace le compteur local par celui du header standard
    uint32_t total= (uint32_t)ut.size();
    std::memcpy(payload.data(), &total, 4);

    std::ofstream f(path, std::ios::binary); if(!f) return false;
    Header hdr{}; hdr.total_trits=total;
    f.write((const char*)&hdr, sizeof(hdr));
    f.write((const char*)payload.data()+4, (std::streamsize)(payload.size()-4));
    return (bool)f;
}

inline bool read(const std::string& path, std::vector<UTrit>& ut){
    std::ifstream f(path, std::ios::binary); if(!f) return false;
    Header hdr{}; f.read((char*)&hdr, sizeof(hdr)); if(!f) return false;
    if(hdr.magic!=0x31503354) return false; // 'T3P1'
    std::vector<uint8_t> data; data.assign(std::istreambuf_iterator<char>(f), {});
    // recompose buffer avec total_trits en tête, attendu par tpack::base243_to_ut
    std::vector<uint8_t> buf; buf.resize(4); std::memcpy(buf.data(), &hdr.total_trits, 4);
    buf.insert(buf.end(), data.begin(), data.end());
    return tpack::base243_to_ut(buf, ut);
}

} // namespace t3p
