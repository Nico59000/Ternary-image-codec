// ============================================================================
//  File: include/io_image.hpp — Image I/O bridge (DOC+)
//  Project: Ternary Image/Video Codec v6
//
//  OBJET
//  -----
//  • Pont simple entre images RGB8 et flux RAW ternaire (quant YCbCr).
//  • RAW adaptable N (balanced) via SubwordMode ∈ {S27,S24,S21,S18,S15}.
//  • Encodage robuste:
//      - Si centered=true et sub!=S27 → tente "embed" dans canevas S27,
//        puis Fallback en encodage direct du format cible si le cœur ne
//        supporte pas l’embed pour ce sub.
//  • Décodage robuste:
//      - Si le cœur renvoie une S27, extraction de la fenêtre centrale vers
//        la taille (w,h) attendue; sinon usage direct.
//
//  REMARQUES
//  ---------
//  • La logique balanced/unbalanced vit dans le cœur; ici, uniquement le pont.
//  • Pas d’ECC ici. Quantification Y/Cb/Cr simple et déterministe.
// ============================================================================

#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "ternary_image_codec_v6_min.hpp" // Word27, PixelYCbCrQuant, SubwordMode, StdRes, std_res_for

// == [1] Types & déclarations stb ============================================
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
// NB: Les headers stb sont C++-safe; l’enrobage extern "C" est acceptable ici
// car on a aussi déclaré les 4 symboles extern "C" ci-dessus.
extern "C" {
#include "stb_image.h"
#include "stb_image_write.h"
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif

// == [2] RGB↔YCbCr + (dé)quantification =====================================
inline void rgb_to_ycbcr(uint8_t R,uint8_t G,uint8_t B,uint8_t& Y,uint8_t& Cb,uint8_t& Cr)
{
    float r=R,g=G,b=B;
    float y = 0.299f*r + 0.587f*g + 0.114f*b;
    float cb= -0.168736f*r - 0.331264f*g + 0.5f*b + 128.0f;
    float cr= 0.5f*r - 0.418688f*g - 0.081312f*b + 128.0f;
    Y =(uint8_t)std::clamp<int>((int)std::lround(y),  0,255);
    Cb=(uint8_t)std::clamp<int>((int)std::lround(cb), 0,255);
    Cr=(uint8_t)std::clamp<int>((int)std::lround(cr), 0,255);
}
inline void ycbcr_to_rgb(uint8_t Y,uint8_t Cb,uint8_t Cr,uint8_t& R,uint8_t& G,uint8_t& B)
{
    float y=Y, cb=Cb-128.0f, cr=Cr-128.0f;
    float r=y+1.402f*cr, g=y-0.344136f*cb-0.714136f*cr, b=y+1.772f*cb;
    R=(uint8_t)std::clamp<int>((int)std::lround(r),0,255);
    G=(uint8_t)std::clamp<int>((int)std::lround(g),0,255);
    B=(uint8_t)std::clamp<int>((int)std::lround(b),0,255);
}
inline PixelYCbCrQuant quantize_ycbcr(uint8_t Y,uint8_t Cb,uint8_t Cr)
{
    PixelYCbCrQuant q{};
    q.Yq=(uint16_t)std::clamp<int>((int)std::lround(Y*(242.0/255.0)),0,242);
    int cb_off=(int)std::lround((Cb-128)*(40.0/128.0));
    int cr_off=(int)std::lround((Cr-128)*(40.0/128.0));
    q.Cbq=(int16_t)std::clamp(cb_off,-40,40);
    q.Crq=(int16_t)std::clamp(cr_off,-40,40);
    return q;
}
inline void dequantize_ycbcr(const PixelYCbCrQuant& q,uint8_t& Y,uint8_t& Cb,uint8_t& Cr)
{
    Y =(uint8_t)std::clamp<int>((int)std::lround(q.Yq*(255.0/242.0)),0,255);
    Cb=(uint8_t)std::clamp<int>((int)std::lround(128+q.Cbq*(128.0/40.0)),0,255);
    Cr=(uint8_t)std::clamp<int>((int)std::lround(128+q.Crq*(128.0/40.0)),0,255);
}

// == [3] Outils image (resize NN, centrage) ==================================
inline void resize_rgb_nn(const ImageU8& src,int dstW,int dstH,ImageU8& dst)
{
    dst.w=dstW;
    dst.h=dstH;
    dst.c=3;
    dst.data.assign((size_t)dstW*dstH*3,0);
    if(src.w<=0||src.h<=0) return;
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
    const int x0=std::max(0,(canvasW-src.w)/2);
    const int y0=std::max(0,(canvasH-src.h)/2);
    for(int y=0; y<src.h; ++y)
    {
        if(y+y0<0 || y+y0>=canvasH) continue;
        const uint8_t* sp=&src.data[(size_t)y*src.w*3];
        uint8_t* dp=&dst.data[(size_t)(y+y0)*canvasW*3 + (size_t)x0*3];
        std::copy(sp, sp+(size_t)src.w*3, dp);
    }
}
inline int pad_even(int w)
{
    return (w%2==0)? w : (w+1);
}

// == [4] I/O disque basiques =================================================
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

// == [5] Ponts image ↔ RAW ===================================================
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
            if(idx>=q.size()) return; // garde-fou
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

// Déclarations RAW (définies dans le cœur)
inline bool encode_raw_pixels_to_words(const std::vector<PixelYCbCrQuant>& px,std::vector<Word27>& out);
inline bool decode_raw_words_to_pixels(const std::vector<Word27>& in,std::vector<PixelYCbCrQuant>& out);
inline bool encode_raw_pixels_to_words_subword(const std::vector<PixelYCbCrQuant>& px, SubwordMode sub, std::vector<Word27>& out);
inline bool decode_raw_words_to_pixels_subword(const std::vector<Word27>& in, SubwordMode sub, std::vector<PixelYCbCrQuant>& out);

// Extraction fenêtre centrale (quant) — utilisé au décodage robuste
inline void extract_center_q(const std::vector<PixelYCbCrQuant>& q_full,
                             int fullW,int fullH,
                             int subW,int subH,
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

// == [5.A] Encodage fichier → words (centrage S27 + fallback direct) =========
inline bool image_to_words_subword(const std::string& path,
                                   SubwordMode sub,
                                   bool centered,
                                   std::vector<Word27>& out_words)
{
    ImageU8 src;
    if(!load_image_rgb8(path, src)) return false;

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
        // Catégorie 1: canevas S27 + tentative “embed”
        const StdRes big = std_res_for(SubwordMode::S27);
        ImageU8 canvas;
        blit_center_rgb(work, big.w, big.h, canvas);

        // Parité éventuelle (prudence si le cœur l’exige)
        const int evenW = pad_even(canvas.w);
        if(evenW!=canvas.w)
        {
            ImageU8 pad = canvas;
            pad.w = evenW;
            pad.data.resize((size_t)evenW*pad.h*3);
            for(int y=0; y<canvas.h; ++y)
            {
                const uint8_t* srcp=&canvas.data[(size_t)y*canvas.w*3];
                uint8_t* dstp=&pad.data[(size_t)y*evenW*3];
                std::copy(srcp, srcp+(size_t)canvas.w*3, dstp);
                const uint8_t* last=&srcp[(canvas.w-1)*3];
                dstp[(evenW-1)*3+0]=last[0];
                dstp[(evenW-1)*3+1]=last[1];
                dstp[(evenW-1)*3+2]=last[2];
            }
            canvas.swap(pad);
        }

        std::vector<PixelYCbCrQuant> q_full;
        rgb_to_quant_stream(canvas, q_full);
        if( encode_raw_pixels_to_words_subword(q_full, sub, out_words) )
        {
            return true;
        }

        // Fallback Catégorie 2: encodage direct du format cible
        std::vector<PixelYCbCrQuant> q_sub;
        rgb_to_quant_stream(work, q_sub);
        return encode_raw_pixels_to_words_subword(q_sub, sub, out_words);
    }

    // Catégorie 2: encodage direct (S27 natif ou centered=false)
    std::vector<PixelYCbCrQuant> q;
    rgb_to_quant_stream(work, q);
    return encode_raw_pixels_to_words_subword(q, sub, out_words);
}

// == [5.B] Décodage words → image (robuste S27/sub) ==========================
inline bool words_to_image_subword(const std::vector<Word27>& words,
                                   SubwordMode sub,
                                   int w,int h,
                                   const std::string& out_path_png)
{
    std::vector<PixelYCbCrQuant> q;
    if(!decode_raw_words_to_pixels_subword(words, sub, q)) return false;

    const StdRes big = std_res_for(SubwordMode::S27);
    const StdRes tgt = std_res_for(sub);
    const size_t need_sub = (size_t)w*h;
    const size_t full_S27 = (size_t)big.w*big.h;

    ImageU8 img;
    if(q.size() == need_sub)
    {
        // Décodage direct au format cible
        quant_stream_to_rgb(q, w, h, img);
        return save_image_png(out_path_png, img);
    }
    if(q.size() == full_S27 && sub!=SubwordMode::S27)
    {
        // Le cœur a renvoyé une S27 : extraire la fenêtre centrale sub
        std::vector<PixelYCbCrQuant> q_sub;
        q_sub.reserve(need_sub);
        extract_center_q(q, big.w, big.h, tgt.w, tgt.h, q_sub);
        quant_stream_to_rgb(q_sub, w, h, img);
        return save_image_png(out_path_png, img);
    }

    // Best-effort (dimension inattendue) : tenter (w,h)
    quant_stream_to_rgb(q, w, h, img);
    return save_image_png(out_path_png, img);
}

// == [5.C] Raccourcis hérités (S27) ==========================================
inline bool image_to_words27(const std::string& path,
                             std::vector<Word27>& out_words,
                             SubwordMode sub=SubwordMode::S27,
                             bool centered=true)
{
    return image_to_words_subword(path, sub, centered, out_words);
}
inline bool words27_to_image(const std::vector<Word27>& words,
                             int w,int h,
                             const std::string& out_path_png)
{
    return words_to_image_subword(words, SubwordMode::S27, w,h, out_path_png);
}
