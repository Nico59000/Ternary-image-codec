// ============================================================================
//  File: include/io_image_tiff_exr.hpp
//  Optional TIFF and/or OpenEXR loader. Guarded per library.
// ============================================================================
#pragma once
#include <string>
#include "io_image.hpp"

#if defined(T3_HAVE_TIFF) || defined(T3_HAVE_OPENEXR)
bool load_tiff_or_exr_to_rgb(const std::string& path, ImageU8& out);
#else
inline bool load_tiff_or_exr_to_rgb(const std::string&, ImageU8&)
{
    return false;
}
#endif
