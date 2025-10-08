// ============================================================================
//  File: src/io_tiff_exr.cpp — TIFF/EXR adapters (optionnels) (DOC+)
// ============================================================================

#include "io_tiff_exr.hpp"
#include "io_image.hpp"

#include <cstring>
#include <algorithm>

using namespace TernaryIO;

static void setErr(std::string* e, const char* msg)
{
    if(e) *e = msg;
}

// ---------------- TIFF backend
#if defined(TERNARY_USE_TIFF)
#include <tiffio.h>
static bool load_tiff_rgb(const std::string& path, ImageU8& out, std::string* err)
{
    TIFF* tif = TIFFOpen(path.c_str(), "rb");
    if(!tif)
    {
        setErr(err,"libtiff: open failed");
        return false;
    }
    uint32_t w,h;
    uint16_t spp, bps, photo;
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
    TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bps);
    TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photo);
    if(spp<3 || bps!=8)
    {
        setErr(err,"libtiff: unsupported format");
        TIFFClose(tif);
        return false;
    }
    out.w=(int)w;
    out.h=(int)h;
    out.c=3;
    out.data.resize((size_t)w*h*3);
    std::vector<uint8_t> scan((size_t)TIFFScanlineSize(tif));
    for(uint32_t y=0; y<h; ++y)
    {
        if(TIFFReadScanline(tif, scan.data(), y, 0)<0)
        {
            setErr(err,"libtiff: read scanline failed");
            TIFFClose(tif);
            return false;
        }
        uint8_t* dp=&out.data[(size_t)y*w*3];
        if(spp==3) std::memcpy(dp, scan.data(), (size_t)w*3);
        else if(spp>=4)
        {
            for(uint32_t x=0; x<w; ++x)
            {
                dp[x*3+0]=scan[x*spp+0];
                dp[x*3+1]=scan[x*spp+1];
                dp[x*3+2]=scan[x*spp+2];
            }
        }
    }
    TIFFClose(tif);
    return true;
}
static bool save_tiff_rgb(const std::string& path, const ImageU8& in, std::string* err)
{
    TIFF* tif = TIFFOpen(path.c_str(), "wb");
    if(!tif)
    {
        setErr(err,"libtiff: create failed");
        return false;
    }
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH,  (uint32_t)in.w);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, (uint32_t)in.h);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    tsize_t linebytes = (tsize_t)in.w*3;
    std::vector<uint8_t> buf((size_t)linebytes);
    for(int y=0; y<in.h; ++y)
    {
        std::memcpy(buf.data(), &in.data[(size_t)y*in.w*3], (size_t)linebytes);
        if(TIFFWriteScanline(tif, buf.data(), y, 0)<0)
        {
            setErr(err,"libtiff: write scanline failed");
            TIFFClose(tif);
            return false;
        }
    }
    TIFFClose(tif);
    return true;
}
#endif

// ---------------- EXR backend
#if defined(TERNARY_USE_TINYEXR)
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"
static bool load_exr_rgb(const std::string& path, ImageU8& out, std::string* err)
{
    float* outRGBA = nullptr;
    int w=0,h=0;
    const char* emsg=nullptr;
    int ret = LoadEXR(&outRGBA, &w, &h, path.c_str(), &emsg);
    if(ret!=TINYEXR_SUCCESS)
    {
        setErr(err, emsg?emsg:"tinyexr: load failed");
        FreeEXRErrorMessage(emsg);
        return false;
    }
    out.w=w;
    out.h=h;
    out.c=3;
    out.data.resize((size_t)w*h*3);
    for(int i=0; i<w*h; ++i)
    {
        float r=outRGBA[i*4+0], g=outRGBA[i*4+1], b=outRGBA[i*4+2];
        out.data[i*3+0] = (uint8_t)std::clamp<int>((int)std::lround(r*255.0f), 0, 255);
        out.data[i*3+1] = (uint8_t)std::clamp<int>((int)std::lround(g*255.0f), 0, 255);
        out.data[i*3+2] = (uint8_t)std::clamp<int>((int)std::lround(b*255.0f), 0, 255);
    }
    free(outRGBA);
    return true;
}
static bool save_exr_rgb(const std::string& path, const ImageU8& in, std::string* err)
{
    std::vector<float> rgba((size_t)in.w*in.h*4, 1.0f);
    for(int i=0; i<in.w*in.h; ++i)
    {
        rgba[i*4+0] = in.data[i*3+0] / 255.0f;
        rgba[i*4+1] = in.data[i*3+1] / 255.0f;
        rgba[i*4+2] = in.data[i*3+2] / 255.0f;
    }
    const char* emsg=nullptr;
    int ret = SaveEXR(rgba.data(), in.w, in.h, 4, /*save as FP32*/1, path.c_str(), &emsg);
    if(ret!=TINYEXR_SUCCESS)
    {
        setErr(err, emsg?emsg:"tinyexr: save failed");
        FreeEXRErrorMessage(emsg);
        return false;
    }
    return true;
}
#endif

// -------------- Outils communs (idem HEIF/AVIF)

static void rgb_to_quant_stream_local(const ImageU8& rgb, std::vector<PixelYCbCrQuant>& out)
{
    out.clear();
    out.reserve((size_t)rgb.w*rgb.h);
    for(int y=0; y<rgb.h; ++y)
    {
        for(int x=0; x<rgb.w; ++x)
        {
            const uint8_t* p=&rgb.data[(size_t)(y*rgb.w+x)*3];
            uint8_t Y,Cb,Cr;
            rgb_to_ycbcr(p[0],p[1],p[2],Y,Cb,Cr);
            out.push_back( quantize_ycbcr(Y,Cb,Cr) );
        }
    }
}
static void quant_stream_to_rgb_local(const std::vector<PixelYCbCrQuant>& q,int w,int h,ImageU8& out)
{
    out.w=w;
    out.h=h;
    out.c=3;
    out.data.assign((size_t)w*h*3,0);
    size_t idx=0;
    for(int y=0; y<h; ++y)
    {
        for(int x=0; x<w; ++x)
        {
            if(idx>=q.size()) return;
            uint8_t Y,Cb,Cr;
            dequantize_ycbcr(q[idx++],Y,Cb,Cr);
            uint8_t R,G,B;
            ycbcr_to_rgb(Y,Cb,Cr,R,G,B);
            uint8_t* p=&out.data[(size_t)(y*w+x)*3];
            p[0]=R;
            p[1]=G;
            p[2]=B;
        }
    }
}
static void extract_center_q_local(const std::vector<PixelYCbCrQuant>& q_full,
                                   int fullW,int fullH, int subW,int subH,
                                   std::vector<PixelYCbCrQuant>& q_sub)
{
    q_sub.clear();
    q_sub.reserve((size_t)subW*subH);
    const int x0=std::max(0,(fullW-subW)/2);
    const int y0=std::max(0,(fullH-subH)/2);
    for(int y=0; y<subH; ++y)
    {
        int fy=y+y0;
        if(fy<0||fy>=fullH)
        {
            q_sub.resize((size_t)subW*(y+1));
            continue;
        }
        const PixelYCbCrQuant* src=&q_full[(size_t)fy*fullW + (size_t)x0];
        q_sub.insert(q_sub.end(), src, src+subW);
    }
}

template<typename LoaderRGB>
static bool imageFile_to_words_generic(LoaderRGB loader,
                                       const std::string& path,
                                       SubwordMode sub, bool centered,
                                       std::vector<Word27>& out_words,
                                       std::string* err)
{
    ImageU8 src;
    if(!loader(path, src, err)) return false;

    const StdRes tgt = std_res_for(sub);
    ImageU8 work;
    if(src.w!=tgt.w || src.h!=tgt.h)
    {
        resize_rgb_nn(src, tgt.w, tgt.h, work);
    }
    else
    {
        work = src;
    }

    if(centered && sub!=SubwordMode::S27)
    {
        const StdRes big = std_res_for(SubwordMode::S27);
        ImageU8 canvas;
        blit_center_rgb(work, big.w, big.h, canvas);

        std::vector<PixelYCbCrQuant> q_full;
        rgb_to_quant_stream_local(canvas, q_full);
        if( encode_raw_pixels_to_words_subword(q_full, sub, out_words) )
        {
            return true;
        }
        std::vector<PixelYCbCrQuant> q_sub;
        rgb_to_quant_stream_local(work, q_sub);
        return encode_raw_pixels_to_words_subword(q_sub, sub, out_words);
    }

    std::vector<PixelYCbCrQuant> q;
    rgb_to_quant_stream_local(work, q);
    return encode_raw_pixels_to_words_subword(q, sub, out_words);
}

template<typename SaverRGB>
static bool words_to_imageFile_generic(SaverRGB saver,
                                       const std::string& path,
                                       SubwordMode sub, int w,int h,
                                       const std::vector<Word27>& words,
                                       std::string* err)
{
    std::vector<PixelYCbCrQuant> q;
    if(!decode_raw_words_to_pixels_subword(words, sub, q))
    {
        setErr(err,"decode_raw_words_to_pixels_subword failed");
        return false;
    }

    const StdRes big = std_res_for(SubwordMode::S27);
    const size_t need_sub = (size_t)w*h;
    const size_t full_S27 = (size_t)big.w*big.h;

    ImageU8 img;
    if(q.size() == need_sub)
    {
        quant_stream_to_rgb_local(q, w, h, img);
    }
    else if(q.size() == full_S27 && sub!=SubwordMode::S27)
    {
        const StdRes tgt = std_res_for(sub);
        std::vector<PixelYCbCrQuant> q_sub;
        q_sub.reserve(need_sub);
        extract_center_q_local(q, big.w, big.h, tgt.w, tgt.h, q_sub);
        quant_stream_to_rgb_local(q_sub, w, h, img);
    }
    else
    {
        quant_stream_to_rgb_local(q, w, h, img);
    }
    return saver(path, img, err);
}

// ---------------- Implémentations publiques

bool tiff_to_words(const std::string& path, SubwordMode sub, bool centered,
                   std::vector<Word27>& out_words, std::string* err)
{
#if defined(TERNARY_USE_TIFF)
    return imageFile_to_words_generic(load_tiff_rgb, path, sub, centered, out_words, err);
#else
    setErr(err, "TIFF disabled (compile without TERNARY_USE_TIFF)");
    return false;
#endif
}

bool words_to_tiff(const std::string& path, SubwordMode sub, int w, int h,
                   const std::vector<Word27>& words, std::string* err)
{
#if defined(TERNARY_USE_TIFF)
    auto saver = [](const std::string& p, const ImageU8& img, std::string* e)
    {
        return save_tiff_rgb(p, img, e);
    };
    return words_to_imageFile_generic(saver, path, sub, w, h, words, err);
#else
    setErr(err, "TIFF disabled (compile without TERNARY_USE_TIFF)");
    return false;
#endif
}

bool exr_to_words(const std::string& path, SubwordMode sub, bool centered,
                  std::vector<Word27>& out_words, std::string* err)
{
#if defined(TERNARY_USE_TINYEXR)
    return imageFile_to_words_generic(load_exr_rgb, path, sub, centered, out_words, err);
#else
    setErr(err, "EXR disabled (compile without TERNARY_USE_TINYEXR)");
    return false;
#endif
}

bool words_to_exr(const std::string& path, SubwordMode sub, int w, int h,
                  const std::vector<Word27>& words, std::string* err)
{
#if defined(TERNARY_USE_TINYEXR)
    auto saver = [](const std::string& p, const ImageU8& img, std::string* e)
    {
        return save_exr_rgb(p, img, e);
    };
    return words_to_imageFile_generic(saver, path, sub, w, h, words, err);
#else
    setErr(err, "EXR disabled (compile without TERNARY_USE_TINYEXR)");
    return false;
#endif
}
