// ============================================================================
//  File: include/io_tiff_exr.hpp — Adapters TIFF/EXR (optionnels) (DOC+)
//  Project: Ternary Image/Video Codec v6
//
//  OBJET
//  -----
//  • Charger/sauver TIFF/EXR <-> vecteur de Word27 (RAW subword).
//  • Cohérence avec io_image.hpp : centrage S27 + fallback direct, et extraction
//    de fenêtre centrale S27 au décodage si nécessaire.
//
//  DÉPENDANCES (compile-time)
//  --------------------------
//  • TERNARY_USE_TIFF   : libtiff
//  • TERNARY_USE_TINYEXR: TinyEXR (header-only)
// ============================================================================

#pragma once
#include <string>
#include <vector>
#include "ternary_image_codec_v6_min.hpp" // Word27, SubwordMode

namespace TernaryIO
{

bool tiff_to_words(const std::string& path,
                   SubwordMode sub, bool centered,
                   std::vector<Word27>& out_words,
                   std::string* err = nullptr);

bool words_to_tiff(const std::string& path,
                   SubwordMode sub, int w, int h,
                   const std::vector<Word27>& words,
                   std::string* err = nullptr);

bool exr_to_words(const std::string& path,
                  SubwordMode sub, bool centered,
                  std::vector<Word27>& out_words,
                  std::string* err = nullptr);

bool words_to_exr(const std::string& path,
                  SubwordMode sub, int w, int h,
                  const std::vector<Word27>& words,
                  std::string* err = nullptr);

} // namespace TernaryIO
