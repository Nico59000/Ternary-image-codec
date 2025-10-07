// ============================================================================
//  File: src/io_image_tiff_exr.cpp
//  Minimal readers for TIFF (8-bit RGB) and OpenEXR (float RGB -> 8-bit).
//  Each part only compiled if the corresponding library is present.
// ============================================================================
#include "io_image_tiff_exr.hpp"
#include <vector>
#include <cstring>

#ifdef T3_HAVE_TIFF
#include <tiffio.h>
static bool load_tiff_rgb8(const std::string& path, ImageU8& out){
    TIFF* tif = TIFFOpen(path.c_str(), "r"); if(!tif) return false; uint32_t w=0,h=0; uint16_t spp=0,bps=0, photometric=0; TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w); TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h); TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &spp); TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bps); TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric);
    if(spp!=3||bps!=8||photometric!=PHOTOMETRIC_RGB){ TIFFClose(tif); return false; }
    out.w=(int)w; out.h=(int)h; out.c=3; out.data.assign((size_t)w*h*3,0);
    for(uint32_t y=0;y<h;++y){ TIFFReadScanline(tif, &out.data[(size_t)y*w*3], y, 0); }
    TIFFClose(tif); return true;
}
#endif

#ifdef T3_HAVE_OPENEXR
#include <ImfRgbaFile.h>
#include <ImfArray.h>
static bool load_exr_rgb(const std::string& path, ImageU8& out){
    Imf::RgbaInputFile file(path.c_str()); Imath::Box2i dw= file.dataWindow(); int w=dw.max.x-dw.min.x+1; int h=dw.max.y-dw.min.y+1; Imf::Array2D<Imf::Rgba> pix; pix.resizeErase(h,w); file.setFrameBuffer(&pix[0][0]-dw.min.x-dw.min.y*w,1,w); file.readPixels(dw.min.y,dw.max.y);
    out.w=w; out.h=h; out.c=3; out.data.assign((size_t)w*h*3,0);
    for(int y=0;y<h;++y){ for(int x=0;x<w;++x){ Imf::Rgba p=pix[y][x]; auto to8=[&](float v){ int vv=(int)std::round(std::clamp(v,0.0f,1.0f)*255.0f); return (uint8_t)vv; }; uint8_t* d=&out.data[(size_t)(y*w+x)*3]; d[0]=to8(p.r); d[1]=to8(p.g); d[2]=to8(p.b); } }
    return true;
}
#endif

#if defined(T3_HAVE_TIFF) || defined(T3_HAVE_OPENEXR)
bool load_tiff_or_exr_to_rgb(const std::string& path, ImageU8& out){
#ifdef T3_HAVE_TIFF
    if(load_tiff_rgb8(path,out)) return true;
#endif
#ifdef T3_HAVE_OPENEXR
    if(load_exr_rgb(path,out)) return true;
#endif
    return false;
}
#endif
