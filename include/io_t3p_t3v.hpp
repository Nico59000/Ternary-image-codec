// ============================================================================
//  File: include/io_t3p_t3v.hpp — Simple T3P/T3V containers (DOC+)
//  Project: Ternary Image/Video Codec v6
//
//  OBJET
//  -----
//  • .t3p : image RAW ternaire (mots Word27) + méta JSON (route_*).
//  • .t3v : suite d’images (frames) RAW + méta JSON global et par frame.
//  • Lecture sécurisée : callback "approve_meta" (meta-only) avant tout accès payload.
//    - Si approve==false → on NE lit PAS le payload (pas d’exfiltration).
//    - route_phase 0→1→2 gérée côté appelant (security_route_helper), ici on
//      ne fait que passer la méta au callback.
//
//  FORMAT MINIMAL
//  --------------
//  • T3P6 :
//      magic[4]="T3P6", u8 ver=6, u8 sub, u16 w, u16 h,
//      u32 meta_len, u64 words_count, u32 hdr_crc32,
//      meta_json[meta_len], words[words_count]*sizeof(Word27LE), u32 payload_crc32
//  • T3V6 : idem avec frame_count et table d’offsets simple (v6-min).
//
//  NB : Endianness : little-endian pour les champs numériques et Word27.u.
// ============================================================================

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

#include "ternary_image_codec_v6_min.hpp" // Word27, SubwordMode

namespace T3Container {

// Callback meta-only (retourne true si lecture/usage payload autorisé)
using ApproveMetaFn = std::function<bool(const std::string& /*meta_json*/)>;

// ---------------------------- API .t3p (image) ------------------------------
bool t3p_write(const std::string& path,
               SubwordMode sub, int w, int h,
               const std::vector<Word27>& words,
               const std::string& meta_json, // peut contenir route_ttl/phase/etc.
               std::string* err = nullptr);

bool t3p_read_header(const std::string& path,
                     SubwordMode& out_sub, int& out_w, int& out_h,
                     std::string& out_meta_json,
                     uint64_t& out_words_count,
                     std::string* err = nullptr);

// Lecture sécurisée : approve_meta(meta_json) avant d’allouer/charger payload
bool t3p_read_payload(const std::string& path,
                      const ApproveMetaFn& approve_meta,
                      std::vector<Word27>& out_words,
                      std::string* err = nullptr);

// ---------------------------- API .t3v (vidéo) ------------------------------
struct T3VFrameIndex {
    uint64_t offset = 0;   // offset fichier de début du bloc frame
    uint64_t words = 0;    // nombre de mots Word27 dans la frame
    uint32_t meta_len = 0; // longueur méta JSON par frame
};

bool t3v_write(const std::string& path,
               SubwordMode sub, int w, int h,
               const std::vector<std::vector<Word27>>& frames,
               const std::string& meta_json_global,
               const std::vector<std::string>& metas_per_frame, // size==frames.size() ou vide
               std::string* err = nullptr);

bool t3v_read_header(const std::string& path,
                     SubwordMode& out_sub, int& out_w, int& out_h,
                     std::string& out_meta_json_global,
                     uint64_t& out_frame_count,
                     std::vector<T3VFrameIndex>& out_index,
                     std::string* err = nullptr);

// Lecture sécurisée de 1 frame (approve_meta sur méta frame)
bool t3v_read_frame(const std::string& path,
                    uint64_t frame_idx,
                    const ApproveMetaFn& approve_meta,
                    std::vector<Word27>& out_words,
                    std::string* err = nullptr);

} // namespace T3Container
