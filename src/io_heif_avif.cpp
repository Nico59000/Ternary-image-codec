// ============================================================================
//  File: src/io_heif_avif.cpp — HEIF/AVIF adapters (optionnels) (DOC+)
// ============================================================================

#include "io_heif_avif.hpp"
#include "io_image.hpp" // ImageU8, resize/blit, quant helpers

#include <cstring>
#include <algorithm>

using namespace TernaryIO;

static void setErr(std::string* e, const char* msg)
{
    if(e) *e = msg;
}

// --------- Backends optionnels

#if defined(TERNARY_USE_LIBHEIF)
#include <libheif/heif.h>
static bool load_heif_rgb(const std::string& path, ImageU8& out, std::string* err)
{
    heif_context* ctx = heif_context_alloc();
    if(!ctx)
    {
        setErr(err,"libheif: alloc failed");
        return false;
    }
    heif_error e = heif_context_read_from_file(ctx, path.c_str(), nullptr);
    if(e.code!=heif_error_Ok)
    {
        setErr(err, "libheif: read failed");
        heif_context_free(ctx);
        return false;
    }

    heif_image_handle* handle=nullptr;
    e = heif_context_get_primary_image_handle(ctx, &handle);
    if(e.code!=heif_error_Ok)
    {
        setErr(err,"libheif: no primary image");
        heif_context_free(ctx);
        return false;
    }

    heif_image* img=nullptr;
    heif_decode_options* decopt = heif_decode_options_alloc();
    e = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, decopt);
    heif_decode_options_free(decopt);
    if(e.code!=heif_error_Ok)
    {
        setErr(err,"libheif: decode failed");
        heif_image_handle_release(handle);
        heif_context_free(ctx);
        return false;
    }

    int w = heif_image_get_width(img, heif_channel_interleaved);
    int h = heif_image_get_height(img, heif_channel_interleaved);
    int stride=0;
    const uint8_t* data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
    if(!data)
    {
        setErr(err,"libheif: plane null");
        heif_image_release(img);
        heif_image_handle_release(handle);
        heif_context_free(ctx);
        return false;
    }

    out.w=w;
    out.h=h;
    out.c=3;
    out.data.resize((size_t)w*h*3);
    for(int y=0; y<h; ++y)
    {
        const uint8_t* sp = data + (size_t)y*stride;
        uint8_t* dp = &out.data[(size_t)y*w*3];
        std::memcpy(dp, sp, (size_t)w*3);
    }

    heif_image_release(img);
    heif_image_handle_release(handle);
    heif_context_free(ctx);
    return true;
}

static bool save_heif_rgb(const std::string& path, const ImageU8& in, std::string* err)
{
    heif_context* ctx = heif_context_alloc();
    if(!ctx)
    {
        setErr(err,"libheif: alloc failed");
        return false;
    }

    heif_image* img=nullptr;
    heif_error e = heif_image_create(in.w, in.h, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &img);
    if(e.code!=heif_error_Ok)
    {
        setErr(err,"libheif: image_create failed");
        heif_context_free(ctx);
        return false;
    }

    e = heif_image_add_plane(img, heif_channel_interleaved, in.w, in.h, 24);
    if(e.code!=heif_error_Ok)
    {
        setErr(err,"libheif: add_plane failed");
        heif_image_release(img);
        heif_context_free(ctx);
        return false;
    }

    int stride=0;
    uint8_t* plane = heif_image_get_plane(img, heif_channel_interleaved, &stride);
    if(!plane)
    {
        setErr(err,"libheif: plane null");
        heif_image_release(img);
        heif_context_free(ctx);
        return false;
    }

    for(int y=0; y<in.h; ++y)
    {
        const uint8_t* sp = &in.data[(size_t)y*in.w*3];
        uint8_t* dp = plane + (size_t)y*stride;
        std::memcpy(dp, sp, (size_t)in.w*3);
    }

    heif_encoder* enc=nullptr;
    if(heif_context_get_encoder_for_format(ctx, heif_compression_AV1, &enc) != heif_error_Ok)
    {
        if(heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &enc) != heif_error_Ok)
        {
            setErr(err,"libheif: no encoder (AV1/HEVC)");
            heif_image_release(img);
            heif_context_free(ctx);
            return false;
        }
    }
    heif_encoding_options* encopt = heif_encoding_options_alloc();
    heif_image_handle* handle=nullptr;
    e = heif_context_encode_image(ctx, img, enc, encopt, &handle);
    heif_encoding_options_free(encopt);
    heif_encoder_release(enc);
    if(e.code!=heif_error_Ok)
    {
        setErr(err,"libheif: encode failed");
        heif_image_release(img);
        heif_context_free(ctx);
        return false;
    }

    e = heif_context_write_to_file(ctx, path.c_str());
    heif_image_handle_release(handle);
    heif_image_release(img);
    heif_context_free(ctx);
    if(e.code!=heif_error_Ok)
    {
        setErr(err,"libheif: write failed");
        return false;
    }
    return true;
}
#endif

#if defined(TERNARY_USE_LIBAVIF)
#include <avif/avif.h>
static bool load_avif_rgb(const std::string& path, ImageU8& out, std::string* err)
{
    avifRWData raw = AVIF_DATA_EMPTY;
    if(!avifRWDataReadFile(&raw, path.c_str()))
    {
        setErr(err,"libavif: read file failed");
        return false;
    }

    avifDecoder* dec = avifDecoderCreate();
    avifResult r = avifDecoderSetIOMemory(dec, raw.data, raw.size);
    if(r!=AVIF_RESULT_OK)
    {
        setErr(err,"libavif: SetIOMemory failed");
        avifDecoderDestroy(dec);
        avifRWDataFree(&raw);
        return false;
    }
    r = avifDecoderParse(dec);
    if(r!=AVIF_RESULT_OK)
    {
        setErr(err,"libavif: parse failed");
        avifDecoderDestroy(dec);
        avifRWDataFree(&raw);
        return false;
    }
    r = avifDecoderNextImage(dec);
    if(r!=AVIF_RESULT_OK)
    {
        setErr(err,"libavif: decode failed");
        avifDecoderDestroy(dec);
        avifRWDataFree(&raw);
        return false;
    }

    avifImage* img = dec->image;
    out.w = img->width;
    out.h = img->height;
    out.c=3;
    out.data.resize((size_t)out.w*out.h*3);

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, img);
    rgb.format = AVIF_RGB_FORMAT_RGB;
    rgb.depth = 8;
    avifRGBImageAllocatePixels(&rgb);
    r = avifImageYUVToRGB(img, &rgb);
    if(r!=AVIF_RESULT_OK)
    {
        setErr(err,"libavif: YUV->RGB failed");
        avifRGBImageFreePixels(&rgb);
        avifDecoderDestroy(dec);
        avifRWDataFree(&raw);
        return false;
    }

    for(uint32_t y=0; y<rgb.height; ++y)
    {
        std::memcpy(&out.data[(size_t)y*out.w*3], rgb.pixels + (size_t)y*rgb.rowBytes, (size_t)out.w*3);
    }
    avifRGBImageFreePixels(&rgb);
    avifDecoderDestroy(dec);
    avifRWDataFree(&raw);
    return true;
}

static bool save_avif_rgb(const std::string& path, const ImageU8& in, std::string* err)
{
    avifImage* img = avifImageCreate(in.w, in.h, 8, AVIF_PIXEL_FORMAT_YUV444);
    if(!img)
    {
        setErr(err,"libavif: image create failed");
        return false;
    }

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, img);
    rgb.format = AVIF_RGB_FORMAT_RGB;
    rgb.depth = 8;
    avifRGBImageAllocatePixels(&rgb);
    for(int y=0; y<in.h; ++y)
    {
        std::memcpy(rgb.pixels + (size_t)y*rgb.rowBytes, &in.data[(size_t)y*in.w*3], (size_t)in.w*3);
    }
    avifResult r = avifImageRGBToYUV(img, &rgb);
    avifRGBImageFreePixels(&rgb);
    if(r!=AVIF_RESULT_OK)
    {
        setErr(err,"libavif: RGB->YUV failed");
        avifImageDestroy(img);
        return false;
    }

    avifEncoder* enc = avifEncoderCreate();
    enc->speed = 6;
    enc->minQuantizer=20;
    enc->maxQuantizer=32;
    r = avifEncoderWrite(enc, img, path.c_str());
    avifEncoderDestroy(enc);
    avifImageDestroy(img);
    if(r!=AVIF_RESULT_OK)
    {
        setErr(err,"libavif: encode failed");
        return false;
    }
    return true;
}
#endif

// --------- Outils communs RAW <-> ImageU8 (locaux, s’appuient sur io_image.hpp)

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

// --------- Encodage

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

// --------- Décodage

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

// --------- Implémentations publiques

bool heif_to_words(const std::string& path, SubwordMode sub, bool centered,
                   std::vector<Word27>& out_words, std::string* err)
{
#if defined(TERNARY_USE_LIBHEIF)
    return imageFile_to_words_generic(load_heif_rgb, path, sub, centered, out_words, err);
#else
    setErr(err, "HEIF disabled (compile without TERNARY_USE_LIBHEIF)");
    return false;
#endif
}

bool words_to_heif(const std::string& path, SubwordMode sub, int w, int h,
                   const std::vector<Word27>& words, std::string* err)
{
#if defined(TERNARY_USE_LIBHEIF)
    auto saver = [](const std::string& p, const ImageU8& img, std::string* e)
    {
        return save_heif_rgb(p, img, e);
    };
    return words_to_imageFile_generic(saver, path, sub, w, h, words, err);
#else
    setErr(err, "HEIF disabled (compile without TERNARY_USE_LIBHEIF)");
    return false;
#endif
}

bool avif_to_words(const std::string& path, SubwordMode sub, bool centered,
                   std::vector<Word27>& out_words, std::string* err)
{
#if defined(TERNARY_USE_LIBAVIF)
    return imageFile_to_words_generic(load_avif_rgb, path, sub, centered, out_words, err);
#elif defined(TERNARY_USE_LIBHEIF)
    return imageFile_to_words_generic(load_heif_rgb, path, sub, centered, out_words, err);
#else
    setErr(err, "AVIF disabled (compile without TERNARY_USE_LIBAVIF or TERNARY_USE_LIBHEIF)");
    return false;
#endif
}

bool words_to_avif(const std::string& path, SubwordMode sub, int w, int h,
                   const std::vector<Word27>& words, std::string* err)
{
#if defined(TERNARY_USE_LIBAVIF)
    auto saver = [](const std::string& p, const ImageU8& img, std::string* e)
    {
        return save_avif_rgb(p, img, e);
    };
    return words_to_imageFile_generic(saver, path, sub, w, h, words, err);
#elif defined(TERNARY_USE_LIBHEIF)
    auto saver = [](const std::string& p, const ImageU8& img, std::string* e)
    {
        return save_heif_rgb(p, img, e);
    };
    return words_to_imageFile_generic(saver, path, sub, w, h, words, err);
#else
    setErr(err, "AVIF disabled (compile without TERNARY_USE_LIBAVIF or TERNARY_USE_LIBHEIF)");
    return false;
#endif
}
