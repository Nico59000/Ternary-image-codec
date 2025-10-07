// ============================================================================
//  File: include/io_image.hpp
//  Simple RGB8 loader/saver via stb_image/stb_image_write (embedded prototypes)
//  + YCbCr quantization for the RAW Word27 example conversion.
//  Note: place real stb headers in third_party/ when building the tools.
// ============================================================================
#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "ternary_image_codec_v6_min.hpp" // Word27, PixelYCbCrQuant

struct ImageU8
{
    int w=0,h=0,c=0;
    std::vector<uint8_t> data;
};

extern "C" {
    unsigned char *stbi_load(const char *filename, int *x, int *y, int *comp, int req_comp);
    void stbi_image_free(void *retval_from_stbi_load);
    int stbi_write_png(const char *filename, int w, int h, int comp, const void *data, int stride_in_bytes);
    int stbi_write_jpg(const char *filename, int w, int h, int comp, const void *data, int quality);
}

#ifdef TERNARY_IO_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4996)
#endif
extern "C" {
#include "stb_image.h"
#include "stb_image_write.h"
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif

// ---- RGB <-> YCbCr (BT.601-ish) ----
inline void rgb_to_ycbcr(uint8_t R,uint8_t G,uint8_t B,uint8_t& Y,uint8_t& Cb,uint8_t& Cr)
{
    float r=R,g=G,b=B;
    float y  = 0.299f*r + 0.587f*g + 0.114f*b;
    float cb = -0.168736f*r - 0.331264f*g + 0.5f*b + 128.0f;
    float cr = 0.5f*r - 0.418688f*g - 0.081312f*b + 128.0f;
    Y=(uint8_t)std::clamp<int>((int)std::round(y),0,255);
    Cb=(uint8_t)std::clamp<int>((int)std::round(cb),0,255);
    Cr=(uint8_t)std::clamp<int>((int)std::round(cr),0,255);
}
inline void ycbcr_to_rgb(uint8_t Y,uint8_t Cb,uint8_t Cr,uint8_t& R,uint8_t& G,uint8_t& B)
{
    float y=Y, cb=Cb-128.0f, cr=Cr-128.0f;
    float r = y + 1.402f * cr;
    float g = y - 0.344136f * cb - 0.714136f * cr;
    float b = y + 1.772f * cb;
    R=(uint8_t)std::clamp<int>((int)std::round(r),0,255);
    G=(uint8_t)std::clamp<int>((int)std::round(g),0,255);
    B=(uint8_t)std::clamp<int>((int)std::round(b),0,255);
}

// ---- Quantization (maps to Y∈[0..242], C∈[-40..40]) ----
inline PixelYCbCrQuant quantize_ycbcr(uint8_t Y,uint8_t Cb,uint8_t Cr)
{
    PixelYCbCrQuant q{};
    q.Yq=(uint16_t)std::clamp<int>((int)std::round(Y*(242.0/255.0)),0,242);
    int cb_off=(int)std::round((Cb-128)*(40.0/128.0));
    int cr_off=(int)std::round((Cr-128)*(40.0/128.0));
    q.Cbq=(int16_t)std::clamp(cb_off,-40,40);
    q.Crq=(int16_t)std::clamp(cr_off,-40,40);
    return q;
}
inline void dequantize_ycbcr(const PixelYCbCrQuant& q,uint8_t& Y,uint8_t& Cb,uint8_t& Cr)
{
    Y=(uint8_t)std::clamp<int>((int)std::round(q.Yq*(255.0/242.0)),0,255);
    Cb=(uint8_t)std::clamp<int>((int)std::round(128+q.Cbq*(128.0/40.0)),0,255);
    Cr=(uint8_t)std::clamp<int>((int)std::round(128+q.Crq*(128.0/40.0)),0,255);
}

// ---- Simple image ops ----
inline void resize_rgb_nn(const ImageU8& src,int dstW,int dstH,ImageU8& dst)
{
    dst.w=dstW;
    dst.h=dstH;
    dst.c=3;
    dst.data.assign((size_t)dstW*dstH*3,0);
    if(src.w==0||src.h==0) return;
    for(int y=0; y<dstH; ++y)
    {
        int sy=(int)((y+0.5)*(double)src.h/dstH);
        sy=std::clamp(sy,0,src.h-1);
        for(int x=0; x<dstW; ++x)
        {
            int sx=(int)((x+0.5)*(double)src.w/dstW);
            sx=std::clamp(sx,0,src.w-1);
            const uint8_t* sp=&src.data[(size_t)(sy*src.w+sx)*3];
            uint8_t* dp=&dst.data[(size_t)(y*dstW+x)*3];
            dp[0]=sp[0];
            dp[1]=sp[1];
            dp[2]=sp[2];
        }
    }
}
inline void blit_center_rgb(const ImageU8& src,int canvasW,int canvasH,ImageU8& dst)
{
    dst.w=canvasW;
    dst.h=canvasH;
    dst.c=3;
    dst.data.assign((size_t)canvasW*canvasH*3,0);
    int x0=(canvasW-src.w)/2;
    int y0=(canvasH-src.h)/2;
    x0=std::max(0,x0);
    y0=std::max(0,y0);
    for(int y=0; y<src.h; ++y)
    {
        const uint8_t* sp=&src.data[(size_t)y*src.w*3];
        if(y+y0<0||y+y0>=canvasH) continue;
        uint8_t* dp=&dst.data[(size_t)(y+y0)*canvasW*3 + (size_t)x0*3];
        std::copy(sp, sp+(size_t)src.w*3, dp);
    }
}
inline int pad_even(int w)
{
    return (w%2==0)? w : (w+1);
}

// ---- Disk I/O ----
inline bool load_image_rgb8(const std::string& path, ImageU8& out)
{
    int x=0,y=0,n=0;
    unsigned char* pix=stbi_load(path.c_str(), &x,&y,&n, 3);
    if(!pix) return false;
    out.w=x;
    out.h=y;
    out.c=3;
    out.data.assign(pix, pix+(size_t)x*y*3);
    stbi_image_free(pix);
    return true;
}
inline bool save_image_png(const std::string& path, const ImageU8& img)
{
    return stbi_write_png(path.c_str(), img.w, img.h, 3, img.data.data(), img.w*3)!=0;
}
inline bool save_image_jpg(const std::string& path, const ImageU8& img, int quality=90)
{
    return stbi_write_jpg(path.c_str(), img.w, img.h, 3, img.data.data(), quality)!=0;
}

// ---- Bridge to RAW words ----
inline void rgb_to_quant_stream(const ImageU8& rgb, std::vector<PixelYCbCrQuant>& out)
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
inline void quant_stream_to_rgb(const std::vector<PixelYCbCrQuant>& q,int w,int h,ImageU8& out)
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

// Declaration (defined in core):
inline bool encode_raw_pixels_to_words(const std::vector<PixelYCbCrQuant>& px,std::vector<Word27>& out);
inline bool decode_raw_words_to_pixels(const std::vector<Word27>& in,std::vector<PixelYCbCrQuant>& out);

// Convenience: file -> words (with optional centering to S27 canvas)
inline bool image_to_words27(const std::string& path, std::vector<Word27>& out_words, SubwordMode sub=SubwordMode::S27, bool centered=true)
{
    ImageU8 src;
    if(!load_image_rgb8(path, src)) return false;
    StdRes tgt=(sub==SubwordMode::S27? std_res_for(SubwordMode::S27) : std_res_for(sub));
    ImageU8 work;
    if(src.w!=tgt.w || src.h!=tgt.h)
    {
        resize_rgb_nn(src, tgt.w, tgt.h, work);
    }
    else
    {
        work=src;
    }
    ImageU8 canvas=work;
    if(sub!=SubwordMode::S27 && centered)
    {
        ImageU8 tmp;
        blit_center_rgb(work, std_res_for(SubwordMode::S27).w, std_res_for(SubwordMode::S27).h, tmp);
        canvas.swap(tmp);
    }
    int evenW=pad_even(canvas.w);
    if(evenW!=canvas.w)
    {
        ImageU8 pad=canvas;
        pad.w=evenW;
        pad.data.resize((size_t)evenW*pad.h*3);
        for(int y=0; y<canvas.h; ++y)
        {
            const uint8_t* srcp=&canvas.data[(size_t)y*canvas.w*3];
            uint8_t* dstp=&pad.data[(size_t)y*evenW*3];
            std::copy(srcp, srcp+(size_t)canvas.w*3, dstp);
            uint8_t r=srcp[(canvas.w-1)*3+0], g=srcp[(canvas.w-1)*3+1], b=srcp[(canvas.w-1)*3+2];
            dstp[(evenW-1)*3+0]=r;
            dstp[(evenW-1)*3+1]=g;
            dstp[(evenW-1)*3+2]=b;
        }
        canvas.swap(pad);
    }
    std::vector<PixelYCbCrQuant> q;
    rgb_to_quant_stream(canvas, q);
    return encode_raw_pixels_to_words(q, out_words);
}

inline bool words27_to_image(const std::vector<Word27>& words,int w,int h,const std::string& out_path_png)
{
    std::vector<PixelYCbCrQuant> q;
    if(!decode_raw_words_to_pixels(words, q)) return false;
    if((int)q.size()<w*h) return false;
    ImageU8 img;
    quant_stream_to_rgb(q,w,h,img);
    return save_image_png(out_path_png,img);
}
