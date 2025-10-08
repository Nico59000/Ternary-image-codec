// ============================================================================
//  File: src/minitest_codec.cpp — Mini-tests codec ternaire v6
//  Build (exemples) :
//    g++ -std=c++17 -O2 -Iinclude -Ithird_party \
//        src/compile_stb.cpp src/minitest_codec.cpp -o minitest
//
//  Si tu as un self-test RS dans le cœur : ajoute -DTEST_WITH_RS_SELFTEST
// ============================================================================

#include <iostream>
#include <vector>
#include <random>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "ternary_image_codec_v6_min.hpp" // PixelYCbCrQuant, Word27, SubwordMode, StdRes, std_res_for
#include "io_image.hpp"                   // helpers RGB/YCbCr, resize, blit, quant, etc.

// ------------------ ASSERT minimaliste --------------------------------------
#define T_ASSERT(expr) do{ if(!(expr)){ \
    std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " -> " #expr "\n"; return false; } }while(0)

// Tolérance RGB (due aux conversions/arrondis)
static inline bool close_u8(uint8_t a, uint8_t b, int tol=2){ return (int)std::abs((int)a-(int)b) <= tol; }
static inline bool close_rgb(const uint8_t* A, const uint8_t* B, int tol=2){
    return close_u8(A[0],B[0],tol) && close_u8(A[1],B[1],tol) && close_u8(A[2],B[2],tol);
}

// Génère un motif RGB8 simple (damier)
static void make_rgb_pattern(int w, int h, uint8_t r1,uint8_t g1,uint8_t b1,
                                            uint8_t r2,uint8_t g2,uint8_t b2,
                                            ImageU8& out){
    out.w=w; out.h=h; out.c=3; out.data.assign((size_t)w*h*3,0);
    for(int y=0;y<h;++y){
        for(int x=0;x<w;++x){
            bool a = ((x/8 + y/8) % 2)==0;
            uint8_t* p=&out.data[(size_t)(y*w+x)*3];
            p[0]= a? r1:r2; p[1]= a? g1:g2; p[2]= a? b1:b2;
        }
    }
}

// Convertit RGB8 -> quant -> words (RAW-N direct)
static bool rgb_to_words_rawN(const ImageU8& rgb, SubwordMode sub, std::vector<Word27>& words){
    ImageU8 work=rgb;
    StdRes tgt = std_res_for(sub);
    if(rgb.w!=tgt.w || rgb.h!=tgt.h) resize_rgb_nn(rgb, tgt.w, tgt.h, work);
    std::vector<PixelYCbCrQuant> q; rgb_to_quant_stream(work, q);
    return encode_raw_pixels_to_words_subword(q, sub, words);
}

// Convertit RGB8 -> inner resize -> center in outer canvas -> words (canevas centré)
static bool rgb_to_words_centered_in_canvas(const ImageU8& rgb, SubwordMode outer, SubwordMode inner,
                                            std::vector<Word27>& words){
    StdRes Rout = std_res_for(outer);
    StdRes Rin  = std_res_for(inner);
    if(outer==SubwordMode::S15){ return false; } // pas d'inner autorisé
    ImageU8 innerR; resize_rgb_nn(rgb, Rin.w, Rin.h, innerR);
    ImageU8 canvas; blit_center_rgb(innerR, Rout.w, Rout.h, canvas);
    std::vector<PixelYCbCrQuant> q; rgb_to_quant_stream(canvas, q);
    return encode_raw_pixels_to_words_subword(q, outer, words);
}

// ------------------ TEST A : round-trip balanced RAW-N ----------------------
static bool test_roundtrip_balanced(SubwordMode sub){
    StdRes R = std_res_for(sub);
    // motif synthétique pour couvrir la quantif
    ImageU8 rgb; make_rgb_pattern(R.w, R.h, 32,200,64,  200,32,220, rgb);

    // encode RAW-N
    std::vector<Word27> words;
    T_ASSERT( rgb_to_words_rawN(rgb, sub, words) );

    // decode -> quant -> rgb
    std::vector<PixelYCbCrQuant> q;
    T_ASSERT( decode_raw_words_to_pixels_subword(words, sub, q) );
    T_ASSERT( (int)q.size() >= R.w*R.h );
    ImageU8 rgb2; quant_stream_to_rgb(q, R.w, R.h, rgb2);

    // compare approx
    for(int y=0;y<R.h;++y){
        for(int x=0;x<R.w;++x){
            const uint8_t* a=&rgb.data[(size_t)(y*R.w+x)*3];
            const uint8_t* b=&rgb2.data[(size_t)(y*R.w+x)*3];
            if(!close_rgb(a,b,4)){
                std::cerr<<"Mismatch @("<<x<<","<<y<<") rawN="<<(int)sub<<"\n";
                return false;
            }
        }
    }
    return true;
}

// ------------------ TEST B : canevas centré (glissant) ----------------------
static bool test_centered_canvas(SubwordMode outer, SubwordMode inner){
    StdRes Rout=std_res_for(outer);
    StdRes Rin =std_res_for(inner);

    // motif "inner" plus petit
    ImageU8 rgbInner; make_rgb_pattern(Rin.w, Rin.h, 255,0,0, 0,0,255, rgbInner);

    // encode en centrant inner dans outer
    std::vector<Word27> words;
    T_ASSERT( rgb_to_words_centered_in_canvas(rgbInner, outer, inner, words) );

    // decode en taille outer
    std::vector<PixelYCbCrQuant> q;
    T_ASSERT( decode_raw_words_to_pixels_subword(words, outer, q) );
    ImageU8 rgbOut; quant_stream_to_rgb(q, Rout.w, Rout.h, rgbOut);

    // Vérifie qu'une large bordure (hors fenêtre inner centrée) est présente :
    // on attend des pixels noirs (issus de blit canvas clair à 0) autour.
    // On mesure 4 bandes (gauche/droite/haut/bas) de 16 px d'épaisseur.
    const int m = 16;
    // coins/bords
    auto chkBand = [&](int x0,int y0,int x1,int y1){
        for(int y=y0;y<y1;++y){
            for(int x=x0;x<x1;++x){
                const uint8_t* p=&rgbOut.data[(size_t)(y*Rout.w+x)*3];
                if(!(p[0]==0 && p[1]==0 && p[2]==0)) return false;
            }
        }
        return true;
    };
    bool ok =
        chkBand(0,0,Rout.w,m) &&                      // bande haute
        chkBand(0,Rout.h-m,Rout.w,Rout.h) &&          // bande basse
        chkBand(0,0,m,Rout.h) &&                      // bande gauche
        chkBand(Rout.w-m,0,Rout.w,Rout.h);            // bande droite
    if(!ok){
        std::cerr<<"Bords noirs attendus non détectés (outer="<<(int)outer<<", inner="<<(int)inner<<")\n";
    }
    return ok;
}

// ------------------ TEST C : upscale vs direct RAW-N ------------------------
static bool test_upscale_vs_direct(SubwordMode subSmall, SubwordMode subBig){
    // On fabrique une image à la petite résolution, puis :
    //  (1) on l'encode en RAW-N "petit" -> decode -> upscale CPU
    //  (2) on l'encode centrée dans canevas "grand" (outer) -> decode
    // Les deux sorties doivent être visuellement proches au centre.
    StdRes Rs = std_res_for(subSmall);
    StdRes Rb = std_res_for(subBig);

    ImageU8 rgbSmall; make_rgb_pattern(Rs.w, Rs.h, 20,200,40, 210,30,230, rgbSmall);

    // (1) direct petit
    std::vector<Word27> wSmall;
    T_ASSERT( rgb_to_words_rawN(rgbSmall, subSmall, wSmall) );
    std::vector<PixelYCbCrQuant> qSmall; T_ASSERT( decode_raw_words_to_pixels_subword(wSmall, subSmall, qSmall) );
    ImageU8 imgSmall; quant_stream_to_rgb(qSmall, Rs.w, Rs.h, imgSmall);
    ImageU8 imgSmallUp; resize_rgb_nn(imgSmall, Rb.w, Rb.h, imgSmallUp);

    // (2) centré dans grand
    std::vector<Word27> wCentered;
    T_ASSERT( rgb_to_words_centered_in_canvas(rgbSmall, subBig, subSmall, wCentered) );
    std::vector<PixelYCbCrQuant> qBig; T_ASSERT( decode_raw_words_to_pixels_subword(wCentered, subBig, qBig) );
    ImageU8 imgBig; quant_stream_to_rgb(qBig, Rb.w, Rb.h, imgBig);

    // Compare la zone centrale (fenêtre Rs au centre de Rb)
    int x0=(Rb.w-Rs.w)/2, y0=(Rb.h-Rs.h)/2;
    for(int y=0;y<Rs.h;++y){
        for(int x=0;x<Rs.w;++x){
            const uint8_t* a = &imgSmallUp.data[(size_t)((y0+y)*Rb.w + (x0+x))*3];
            const uint8_t* b = &imgBig.data    [(size_t)((y0+y)*Rb.w + (x0+x))*3];
            if(!close_rgb(a,b,5)){
                std::cerr<<"centre mismatch @("<<x<<","<<y<<") small="<<(int)subSmall<<" big="<<(int)subBig<<"\n";
                return false;
            }
        }
    }
    return true;
}

// ------------------ TEST D : RS/GF (optionnel si dispo) ---------------------
#ifdef TEST_WITH_RS_SELFTEST
extern bool selftest_rs_unit(); // fournie/implémentée dans le cœur
static bool test_rs_self(){ return selftest_rs_unit(); }
#else
static bool test_rs_self(){ std::cout<<"[SKIP] RS self-test (TEST_WITH_RS_SELFTEST non défini)\n"; return true; }
#endif

// ------------------ DRIVER ---------------------------------------------------
int main(){
    bool ok = true;

    // A) Round-trip balanced RAW-N
    ok &= test_roundtrip_balanced(SubwordMode::S27);
    ok &= test_roundtrip_balanced(SubwordMode::S24);
    ok &= test_roundtrip_balanced(SubwordMode::S21);
    ok &= test_roundtrip_balanced(SubwordMode::S18);
    ok &= test_roundtrip_balanced(SubwordMode::S15);
    std::cout << "[A] roundtrip RAW-N : " << (ok? "OK":"FAIL") << "\n";

    // B) Canevas centré glissant (quelques cas)
    ok &= test_centered_canvas(SubwordMode::S27, SubwordMode::S24);
    ok &= test_centered_canvas(SubwordMode::S24, SubwordMode::S21);
    ok &= test_centered_canvas(SubwordMode::S21, SubwordMode::S18);
    ok &= test_centered_canvas(SubwordMode::S18, SubwordMode::S15);
    std::cout << "[B] centered canvas : " << (ok? "OK":"FAIL") << "\n";

    // C) Upscale vs centré
    ok &= test_upscale_vs_direct(SubwordMode::S21, SubwordMode::S27);
    ok &= test_upscale_vs_direct(SubwordMode::S18, SubwordMode::S24);
    std::cout << "[C] upscale vs direct : " << (ok? "OK":"FAIL") << "\n";

    // D) RS/GF (si dispo)
    ok &= test_rs_self();
    std::cout << "[D] RS/GF self-test : " << (ok? "OK":"FAIL") << "\n";

    std::cout << (ok? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n");
    return ok? 0 : 1;
}
