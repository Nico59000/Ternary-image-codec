// ============================================================================
//  File: include/io_image_heif_avif.hpp
//  Optional HEIF/AVIF loader via libheif. Guard with T3_HAVE_LIBHEIF.
// ============================================================================
#pragma once
#include <string>
#include "io_image.hpp"

#ifdef T3_HAVE_LIBHEIF
bool load_heif_avif_to_rgb(const std::string& path, ImageU8& out);
#else
inline bool load_heif_avif_to_rgb(const std::string&, ImageU8&)
{
    return false;
}
#endif
