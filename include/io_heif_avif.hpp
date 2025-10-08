// ============================================================================
//  File: include/io_heif_avif.hpp — Adapters HEIF/AVIF (optionnels) (DOC+)
//  Project: Ternary Image/Video Codec v6
//
//  OBJET
//  -----
//  • Charger/sauver HEIF/AVIF <-> vecteur de Word27 (RAW subword).
//  • Cohérence avec io_image.hpp : centrage S27 + fallback direct, et extraction
//    de fenêtre centrale au décodage si le cœur renvoie S27.
//
//  DÉPENDANCES (compile-time)
//  --------------------------
//  • TERNARY_USE_LIBHEIF : HEIF/AVIF via libheif (si AV1 enable).
//  • TERNARY_USE_LIBAVIF : AVIF via libavif.
//  (Sinon, ces fonctions retournent false proprement.)
// ============================================================================

#pragma once
#include <string>
#include <vector>
#include "ternary_image_codec_v6_min.hpp" // Word27, SubwordMode, StdRes, std_res_for

namespace TernaryIO
{

/// HEIF -> RAW (subword)
bool heif_to_words(const std::string& path,
                   SubwordMode sub, bool centered,
                   std::vector<Word27>& out_words,
                   std::string* err = nullptr);

/// RAW (subword) -> HEIF
bool words_to_heif(const std::string& path,
                   SubwordMode sub, int w, int h,
                   const std::vector<Word27>& words,
                   std::string* err = nullptr);

/// AVIF -> RAW (subword)
bool avif_to_words(const std::string& path,
                   SubwordMode sub, bool centered,
                   std::vector<Word27>& out_words,
                   std::string* err = nullptr);

/// RAW (subword) -> AVIF
bool words_to_avif(const std::string& path,
                   SubwordMode sub, int w, int h,
                   const std::vector<Word27>& words,
                   std::string* err = nullptr);

} // namespace TernaryIO
