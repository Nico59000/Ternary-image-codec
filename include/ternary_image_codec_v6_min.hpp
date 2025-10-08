// ============================================================================
//  File: include/ternary_image_codec_v6_min.hpp — Core types & API (DOC+)
//  Project: Ternary Image/Video Codec v6
//
//  RÔLE
//  -----
//  • Définir les types de base (Word27, PixelYCbCrQuant).
//  • Enum SubwordMode ∈ {S27,S24,S21,S18,S15} et résolutions standard.
//  • Ponts d’API pour le RAW adaptable N (balanced) : encode/decode subword.
//  • Outils balanced↔unbalanced au niveau TRIT (sans exposer le GF(27) interne).
//
//  NOTES
//  -----
//  • Le mappage balanced {-1,0,+1} ↔ unbalanced {0,1,2} est strict :
//        bal→unb:  {-1→0, 0→1, +1→2} ;  unb→bal: {0→-1, 1→0, 2→+1}.
//  • Le cœur ECC/RS/GF(27) reste interne et travaille en unbalanced {0,1,2}.
//  • Les API externes d’images peuvent alimenter/extraire du RAW balanced
//    via cette interface, mais l’impl. interne convertit en unbalanced.
// ============================================================================

#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>

// =============================== Types métier ===============================

struct Word27 {
    // Représentation compacte de 27 trits internes (unbalanced {0,1,2}).
    // Implémentation min : 32 bits L.E. (5 états inutilisés). Le packing réel
    // peut être différent en implémentation, mais l’ABI expose un uint32_t.
    uint32_t u = 0;
};

struct PixelYCbCrQuant {
    // Y: [0..242], Cb/Cr: [-40..+40] (quantification pont I/O)
    uint16_t Yq = 0;
    int16_t  Cbq = 0;
    int16_t  Crq = 0;
};

// =============================== Subword modes ==============================

enum class SubwordMode : uint8_t {
    S27 = 27,  // 8K natif
    S24 = 24,  // 4K
    S21 = 21,  // 1080p
    S18 = 18,  // 720p
    S15 = 15   // qHD-ish
};

struct StdRes { int w=0, h=0; };

inline StdRes std_res_for(SubwordMode m){
    switch(m){
        case SubwordMode::S27: return {7680, 4320}; // 8K UHD
        case SubwordMode::S24: return {3840, 2160}; // 4K UHD
        case SubwordMode::S21: return {1920, 1080}; // 1080p
        case SubwordMode::S18: return {1280,  720}; // 720p
        case SubwordMode::S15: return { 960,  540}; // qHD approx
        default: return {0,0};
    }
}

// ======================== Balanced ↔ Unbalanced (trit) ======================

inline uint8_t trit_bal_to_unb(int8_t t_bal){
    // -1→0, 0→1, +1→2 ; clamp par prudence
    if(t_bal < -1) t_bal = -1;
    if(t_bal > +1) t_bal = +1;
    return (uint8_t)(t_bal + 1);
}
inline int8_t trit_unb_to_bal(uint8_t t_unb){
    // 0→-1, 1→0, 2→+1 ; clamp par prudence
    if(t_unb > 2) t_unb = 1;
    return (int8_t)t_unb - 1;
}

// Helpers pack/unpack (27 trits en un Word27 unbalanced).
// NB: ce packing min utilise 2 bits par trit (inefficace mais simple & stable).
inline void pack27_unbalanced_2b(const uint8_t trits_unb[27], Word27& out){
    uint32_t acc=0; // 27 trits * 2b = 54 bits > 32 : on ne garde ici QUE 16 trits (démo)
// NOTE IMPORTANTE : Cette min-impl est volontairement simple et non-optimisée.
// Dans l’impl. réelle, Word27 doit pouvoir contenir les 27 trits.
// Ici, on encode les 16 premiers pour garder une ABI min. (à remplacer par ta version).
    for(int i=0;i<16;++i){
        uint32_t v = (uint32_t)(trits_unb[i] & 0x3u);
        acc |= (v << (i*2));
    }
    out.u = acc;
}
inline void unpack27_unbalanced_2b(const Word27& in, uint8_t trits_unb_out[27]){
    for(int i=0;i<27;++i) trits_unb_out[i]=1; // init (1=0 en balanced)
    for(int i=0;i<16;++i){
        trits_unb_out[i] = (uint8_t)((in.u >> (i*2)) & 0x3u);
        if(trits_unb_out[i]>2) trits_unb_out[i]=1;
    }
}

// ================================ API Core ==================================
//
// L’implémentation interne (non fournie ici) réalise:
//  • la vectorisation balanced {-1,0,+1} → unbalanced {0,1,2} (et inverse),
//  • le packing Word27 (vrai 27-trits),
//  • l’ECC/RS/GF(27) si activé,
//  • l’entrelacement 2D (si profil actif).
//
// Ces signatures sont utilisées par les ponts I/O (PNG/JPG/HEIF/AVIF/TIFF/EXR, etc)
// et ton module io_image.hpp.
// ----------------------------------------------------------------------------

bool encode_raw_pixels_to_words(const std::vector<PixelYCbCrQuant>& px,
                                std::vector<Word27>& out_words);

bool decode_raw_words_to_pixels(const std::vector<Word27>& in_words,
                                std::vector<PixelYCbCrQuant>& out_px);

bool encode_raw_pixels_to_words_subword(const std::vector<PixelYCbCrQuant>& px,
                                        SubwordMode sub,
                                        std::vector<Word27>& out_words);

bool decode_raw_words_to_pixels_subword(const std::vector<Word27>& in_words,
                                        SubwordMode sub,
                                        std::vector<PixelYCbCrQuant>& out_px);

// ============================= Sanity helpers ===============================

inline bool is_valid_subword(SubwordMode m){
    switch(m){ case SubwordMode::S27: case SubwordMode::S24: case SubwordMode::S21:
               case SubwordMode::S18: case SubwordMode::S15: return true;
               default: return false; }
}

