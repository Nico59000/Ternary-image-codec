// ============================================================================
//  File: src/io_image_heif_avif.cpp
//  HEIF/AVIF loader using libheif (if available). See CMake option T3_USE_LIBHEIF.
// ============================================================================
#include "io_image_heif_avif.hpp"
#ifdef T3_HAVE_LIBHEIF
extern "C" {
#include <libheif/heif.h>
}
#include <vector>
#include <cstring>

bool load_heif_avif_to_rgb(const std::string& path, ImageU8& out){
    heif_context* ctx = heif_context_alloc();
    if(heif_context_read_from_file(ctx, path.c_str(), nullptr)!=heif_error_Ok){ heif_context_free(ctx); return false; }
    heif_image_handle* handle=nullptr; heif_context_get_primary_image_handle(ctx,&handle);
    heif_image* img=nullptr; heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);
    int w=heif_image_get_width(img, heif_channel_interleaved);
    int h=heif_image_get_height(img, heif_channel_interleaved);
    const uint8_t* data = heif_image_get_plane_readonly(img, heif_channel_interleaved, nullptr);
    int stride = heif_image_get_stride(img, heif_channel_interleaved);
    out.w=w; out.h=h; out.c=3; out.data.assign((size_t)w*h*3,0);
    for(int y=0;y<h;++y){ std::memcpy(&out.data[(size_t)y*w*3], data + (size_t)y*stride, (size_t)w*3); }
    heif_image_release(img); heif_image_handle_release(handle); heif_context_free(ctx); return true;
}
#endif
