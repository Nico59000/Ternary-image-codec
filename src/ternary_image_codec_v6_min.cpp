// ============================================================================
//  File: src/ternary_image_codec_v6_min.cpp — Core minimal impl (DOC+)
//  Project: Ternary Image/Video Codec v6
//
//  PORTÉE (v6_min)
//  ---------------
//  • Implémentation *référente minimale* des 4 fonctions publiques :
//      - encode_raw_pixels_to_words(...)
//      - decode_raw_words_to_pixels(...)
//      - encode_raw_pixels_to_words_subword(...)
//      - decode_raw_words_to_pixels_subword(...)
//  • Pas d’ECC/RS/GF(27) ni d’entrelacement 2D ici (à brancher dans une impl. “full”).
//  • Encodage déterministe “13 trits/pixel” basé sur les quantifications :
//      Yq ∈ [0..242]  → 5 trits (3^5 = 243)
//      Cbq ∈ [-40..+40] → offset +40 → [0..80] → 4 trits (3^4 = 81)
//      Crq idem → 4 trits
//      => total 13 trits/pixel  (3^13 = 1 594 323 < 2^21)
//  • Dans cette implémentation, **1 pixel = 1 Word27** :
//      Word27::u stocke un entier 21 bits max représentant le paquet 13 trits
//      (ordre des trits : Y (LSB..MSB, 5 trits), puis Cb (4 trits), puis Cr (4 trits)).
//  • Le paramètre SubwordMode est accepté mais **n’influence pas** le packing v6_min.
//    (La logique S27 “fullscale centré” et l’extraction centrale sont gérées côté I/O.)
//
//  COMPATIBILITÉ
//  -------------
//  • Compatible avec include/ternary_image_codec_v6_min.hpp (types & signatures).
//  • Compatible avec include/io_image.hpp (encode/decode q-stream ↔ words).
//  • Compatible avec io_heif_avif.* et io_tiff_exr.* (mêmes hypothèses I/O).
//
//  LIMITES (volontaires)
//  ---------------------
//  • Espace Word27 non “plein 27-trits” : ici, Word27::u contient l’agrégat 13-trits
//    (<= 21 bits). Le schéma “27 trits/mot” et les profils RS/entrelacement sont
//    réservés à l’implémentation complète (non fournie ici).
// ============================================================================

#include "ternary_image_codec_v6_min.hpp"

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cassert>

// =============== [Section 1] Constantes & helpers base-3 (DOC+) =============

// Nombre de trits par composante (voir en-tête)
static constexpr int TRITS_Y  = 5;  // 3^5 = 243 → couvre 0..242
static constexpr int TRITS_CB = 4;  // 3^4 = 81  → couvre 0..80  (Cbq + 40)
static constexpr int TRITS_CR = 4;  // idem

static constexpr uint32_t POW3_0 = 1u;
static constexpr uint32_t POW3_1 = 3u;
static constexpr uint32_t POW3_2 = 9u;
static constexpr uint32_t POW3_3 = 27u;
static constexpr uint32_t POW3_4 = 81u;
static constexpr uint32_t POW3_5 = 243u;
static constexpr uint32_t POW3_9 = 19683u;      // 3^9
static constexpr uint32_t POW3_13 = 1594323u;   // 3^13 (borne sup pour contrôle)

// Encodage “13 trits/pixel” → entier base 10 (<= 3^13-1).
// Ordre (LSB → MSB en puissance de 3) : Y(5 trits) puis Cb(4) puis Cr(4).
static inline uint32_t pack13_from_quant(const PixelYCbCrQuant& q)
{
    // Clamp défensif (déjà garanti par le pont image)
    uint32_t Y  = (uint32_t)std::clamp<int>(q.Yq, 0, 242);      // 0..242
    uint32_t Cb = (uint32_t)std::clamp<int>(q.Cbq + 40, 0, 80); // -40..40 → 0..80
    uint32_t Cr = (uint32_t)std::clamp<int>(q.Crq + 40, 0, 80);

    // Y occupe 5 trits → poids 3^0..3^4
    // Cb occupe 4 trits → poids 3^5..3^8
    // Cr occupe 4 trits → poids 3^9..3^12
    // Composition : code = Y + 3^5 * (Cb + 3^4 * Cr)
    //              = Y + 243 * (Cb + 81 * Cr)
    uint32_t code = Y + POW3_5 * (Cb + POW3_4 * Cr);
    // bornes : max = 242 + 243*(80 + 81*80) = 1,594,322 < 2^21
    return code;
}

// Décodage inverse : entier → PixelYCbCrQuant
static inline PixelYCbCrQuant unpack13_to_quant(uint32_t code)
{
    PixelYCbCrQuant out{};
    // Extraire Cr (4 trits) : code = Y + 243*(Cb + 81*Cr)
    uint32_t block = code / POW3_5;     // Cb + 81*Cr
    uint32_t Y     = code % POW3_5;     // 0..242

    uint32_t Cr    = block / POW3_4;    // 0..80
    uint32_t Cb    = block % POW3_4;    // 0..80

    out.Yq  = (uint16_t)std::min<uint32_t>(Y, 242u);
    out.Cbq = (int16_t)std::clamp<int32_t>((int32_t)Cb - 40, -40, 40);
    out.Crq = (int16_t)std::clamp<int32_t>((int32_t)Cr - 40, -40, 40);
    return out;
}

// =========== [Section 2] Stubs balanced/unbalanced (si besoin) ==============
//
// NB: L’implémentation min ne transporte pas explicitement les trits, on stocke
// directement l’agrégat 13-trits dans Word27::u. Les helpers trit_* du .hpp
// suffisent pour la cohérence sémantique (pas utilisés ici).

// ================== [Section 3] Encode/Decode “non-subword” =================

bool encode_raw_pixels_to_words(const std::vector<PixelYCbCrQuant>& px,
                                std::vector<Word27>& out_words)
{
    out_words.clear();
    out_words.reserve(px.size());
    for(const auto& p : px){
        Word27 w{};
        w.u = pack13_from_quant(p); // ≤ ~2^21
        out_words.push_back(w);
    }
    return true;
}

bool decode_raw_words_to_pixels(const std::vector<Word27>& in_words,
                                std::vector<PixelYCbCrQuant>& out_px)
{
    out_px.clear();
    out_px.reserve(in_words.size());
    for(const auto& w : in_words){
        out_px.push_back(unpack13_to_quant(w.u));
    }
    return true;
}

// ============ [Section 4] Encode/Decode “subword” (profil N) ================
//
// v6_min : le paramètre SubwordMode est accepté pour la compatibilité.
// L’empaquetage effectif reste “1 pixel → 1 Word27”. Dans une impl. complète,
// N∈{27,24,21,18,15} piloterait la densité (nombre de pixels/word, coset/pilote,
// balise clairsemée, etc.). Ici, on garde le comportement simple.

static inline bool validate_sub(SubwordMode sub){
    return is_valid_subword(sub);
}

bool encode_raw_pixels_to_words_subword(const std::vector<PixelYCbCrQuant>& px,
                                        SubwordMode sub,
                                        std::vector<Word27>& out_words)
{
    if(!validate_sub(sub)) return false;
    // Impl. min : identique à la version non-subword
    return encode_raw_pixels_to_words(px, out_words);
}

bool decode_raw_words_to_pixels_subword(const std::vector<Word27>& in_words,
                                        SubwordMode sub,
                                        std::vector<PixelYCbCrQuant>& out_px)
{
    if(!validate_sub(sub)) return false;
    // Impl. min : identique à la version non-subword
    return decode_raw_words_to_pixels(in_words, out_px);
}

// ====================== [Section 5] Notes d’évolution =======================
//
// • Pour brancher ECC/RS/GF(27) + entrelacement 2D :
//   - Introduire un buffer trits unbalanced {0,1,2} pour chaque pixel (13 trits),
//     puis appliquer le profil RS (y compris “RS+” si actif) et l’entrelacement.
//   - Définir un vrai packing “27 trits/mot” (Word27 étendu) et utiliser N pour
//     la densité utile/mot (S27→27 trits utiles ; S24→24 ; etc.).
//   - Conserver 1 trit “profil” si nécessaire (coset/pilote) selon la spec.
//
// • Pour l’embed S27 “fullscale centré” : l’I/O appelle encode_subword(...) en
//   lui passant un flux S27 si centrage activé. Notre decode_subword(...) peut
//   donc retourner soit w*h (format cible) soit S27 plein (cas “embed”),
//   ce que gèrent déjà les ponts (extraction centrale).
//
// • Cette v6_min garantit la **cohérence fonctionnelle** avec les modules I/O
//   et les conteneurs .t3p/.t3v fournis, tout en restant simple à auditer.
// ============================================================================

