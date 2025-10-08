// ============================================================================
//  File: include/proto_noentropy.hpp — Prototype "sans entropie" ECC de compression/decompression du payload balanced-ternary
//  Project: Ternary Image/Video Codec v6
//
//  OBJECTIF
//  --------
//  1) Tuiles 2D Haar (lifting entier) sur Y, quantif ternaire (−1/0/+1) par seuil.
//  2) "Spectral sketch" compact (DCT grossière) → bacs radiaux×orientations ternarisés.
//  3) Pack/Unpack base-243 (5 trits -> 1 octet) pour sérialiser sans entropie.
//  4) Tout en balanced ternary à l’interface; {0,1,2} seulement pour le pack.
//
//  DÉPENDANCES
//  -----------
//   - "io_image.hpp" (RGB<->YCbCr, resize)  ; "ternary_image_codec_v6_min.hpp" (types)
//  UTILISATION TYPIQUE
//  -------------------
//    ProtoParams P;
//    ProtoArtifacts A;
//    ImageU8 rgb = ...; // charge ton image
//    proto_tile_haar_ternary(rgb, P, A);     // remplit A.tile_LL, A.tile_trits
//    proto_spectral_sketch(rgb, P, A);       // remplit A.sketch_trits
//    pack_base243(A.tile_trits, A.tile_bytes);
//    pack_base243(A.sketch_trits, A.sketch_bytes);
//    // ensuite: stocke les bytes, ou encapsule en .t3p/.t3v, puis ECC.
//
// ============================================================================

#pragma once
#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

#include "ternary_image_codec_v6_min.hpp"
#include "io_image.hpp" // rgb_to_ycbcr, resize_rgb_nn etc.

// =============================== [0] Params/Artifacts =======================

struct ProtoParams {
    int tile = 8;           // taille tuile NxN (8 recommandé)
    int sketchSize = 32;    // grille DCT NxN (32 recommandé sur image downscalée)
    int sketchDown = 256;   // taille carrée de downscale pour sketch
    int radialBins = 8;     // # bacs radiaux
    int angleBins = 8;      // # bacs angulaires
    int thresh = 6;         // seuil ternaire pour coefficients Haar (unités LSB Y)
    bool keep_LL_u8 = true; // si true: LL conservé en 8-bit (sinon on le ternarise aussi)
};

struct ProtoArtifacts {
    // ---- Tuilage Haar ----
    int tilesX=0, tilesY=0, N=0;         // N=tile
    std::vector<uint8_t> tile_LL;        // LL pour chaque tuile (si keep_LL_u8)
    std::vector<int8_t>  tile_trits;     // trits balanced pour LH/HL/HH de toutes tuiles
    std::vector<uint8_t> tile_bytes;     // version packée base-243 (option)

    // ---- Spectral sketch ----
    int rb=0, ab=0;                      // dims sketch (radialBins, angleBins)
    std::vector<int8_t>  sketch_trits;   // trits balanced du sketch (rb*ab)
    std::vector<uint8_t> sketch_bytes;   // version packée base-243 (option)
};

// =============================== [1] Pack base-243 ==========================
// 5 trits unbalanced ({0,1,2}) -> 1 octet [0..242]; on packe un nombre arbitraire de trits.

inline uint8_t trit_bal_to_unb(int8_t b){ return (uint8_t)(b + 1); }
inline int8_t  trit_unb_to_bal(uint8_t u){ return (int8_t)u - 1; }

inline void pack_base243(const std::vector<int8_t>& trits_bal, std::vector<uint8_t>& out_bytes){
    out_bytes.clear();
    // map balanced -> unbalanced
    std::vector<uint8_t> u; u.reserve(trits_bal.size());
    for(int8_t b : trits_bal) u.push_back(trit_bal_to_unb(b)); // {-1,0,1}->{0,1,2}
    // pack 5 par 5
    for(size_t i=0;i<u.size(); i+=5){
        int k = (int)std::min<size_t>(5, u.size()-i);
        uint32_t v=0, p=1;
        for(int j=0;j<k;++j){ v += (uint32_t)u[i+j]*p; p*=3; }
        out_bytes.push_back((uint8_t)v);
    }
}
inline void unpack_base243(const std::vector<uint8_t>& bytes, size_t n_trits, std::vector<int8_t>& out_trits_bal){
    std::vector<uint8_t> u; u.reserve(n_trits);
    size_t blocks = (n_trits + 4)/5;
    for(size_t b=0;b<blocks;++b){
        uint32_t v = (b<bytes.size()? bytes[b]:0);
        int k = (int)std::min<size_t>(5, n_trits - b*5);
        for(int j=0;j<k;++j){ u.push_back((uint8_t)(v%3)); v/=3; }
    }
    out_trits_bal.resize(n_trits);
    for(size_t i=0;i<n_trits;++i) out_trits_bal[i] = trit_unb_to_bal(u[i]);
}

// =============================== [2] Haar 1D (lifting entier) ===============
// In-place sur un vecteur de longueur N (N pair). Sortie: [approx... | detail...]

inline void haar1d_int(std::vector<int>& v){
    const int N=(int)v.size();
    std::vector<int> a(N/2), d(N/2);
    for(int i=0;i<N/2;++i){
        int s = v[2*i] + v[2*i+1];     // somme
        int t = v[2*i] - v[2*i+1];     // diff
        a[i] = (s>>1);                 // approx (entier)
        d[i] = t;                      // detail (non-scalé)
    }
    for(int i=0;i<N/2;++i) v[i]=a[i];
    for(int i=0;i<N/2;++i) v[N/2+i]=d[i];
}

inline void haar1d_int_inv(std::vector<int>& v){
    const int N=(int)v.size();
    std::vector<int> x(N);
    for(int i=0;i<N/2;++i){
        int a = v[i];
        int d = v[N/2+i];
        x[2*i]   = a + (d>>1);
        x[2*i+1] = a - ((d - (d>>1))); // évite biais (recompose s=2a, t=d)
    }
    v.swap(x);
}

// =============================== [3] Haar 2D sur tuile NxN ==================

inline void haar2d_int(std::vector<int>& tile, int N){
    // lignes
    for(int y=0;y<N;++y){
        std::vector<int> row(N);
        for(int x=0;x<N;++x) row[x]=tile[(size_t)y*N+x];
        haar1d_int(row);
        for(int x=0;x<N;++x) tile[(size_t)y*N+x]=row[x];
    }
    // colonnes
    for(int x=0;x<N;++x){
        std::vector<int> col(N);
        for(int y=0;y<N;++y) col[y]=tile[(size_t)y*N+x];
        haar1d_int(col);
        for(int y=0;y<N;++y) tile[(size_t)y*N+x]=col[y];
    }
}
inline void haar2d_int_inv(std::vector<int>& tile, int N){
    // colonnes
    for(int x=0;x<N;++x){
        std::vector<int> col(N);
        for(int y=0;y<N;++y) col[y]=tile[(size_t)y*N+x];
        haar1d_int_inv(col);
        for(int y=0;y<N;++y) tile[(size_t)y*N+x]=col[y];
    }
    // lignes
    for(int y=0;y<N;++y){
        std::vector<int> row(N);
        for(int x=0;x<N;++x) row[x]=tile[(size_t)y*N+x];
        haar1d_int_inv(row);
        for(int x=0;x<N;++x) tile[(size_t)y*N+x]=row[x];
    }
}

// =============================== [4] Tuilage + ternarisation ================
// On prend Y (depuis RGB), on découpe en tuiles NxN, Haar2D, puis:
//  - LL stocké u8 (option) ; LH/HL/HH -> trits balanced par seuil ±T.

inline void proto_tile_haar_ternary(const ImageU8& rgb, const ProtoParams& P, ProtoArtifacts& A){
    // Extraire Y et pad à multiples de N
    const int N=P.tile;
    int W = (rgb.w + (N-1)) / N * N;
    int H = (rgb.h + (N-1)) / N * N;

    ImageU8 work=rgb;
    if(W!=rgb.w || H!=rgb.h) resize_rgb_nn(rgb, W, H, work);

    A.N = N;
    A.tilesX = W / N;
    A.tilesY = H / N;

    if(P.keep_LL_u8){
        A.tile_LL.assign((size_t)A.tilesX*A.tilesY, 0);
    } else {
        A.tile_LL.clear();
    }
    A.tile_trits.clear();
    A.tile_trits.reserve((size_t)W*H); // upper bound

    // Parcours tuiles
    for(int ty=0; ty<A.tilesY; ++ty){
        for(int tx=0; tx<A.tilesX; ++tx){
            // construire tuile Y
            std::vector<int> T(N*N, 0);
            for(int y=0;y<N;++y){
                for(int x=0;x<N;++x){
                    const uint8_t* p = &work.data[(size_t)((ty*N+y)*W + (tx*N+x))*3];
                    uint8_t Y,Cb,Cr; rgb_to_ycbcr(p[0],p[1],p[2],Y,Cb,Cr);
                    T[(size_t)y*N+x] = (int)Y;
                }
            }
            // Haar 2D
            haar2d_int(T, N);

            // LL = coin [0..N/2-1]×[0..N/2-1]
            if(P.keep_LL_u8){
                int LL = T[0]; LL = std::clamp(LL, 0, 255);
                A.tile_LL[(size_t)ty*A.tilesX + tx] = (uint8_t)LL;
            } else {
                // Option: ternariser LL aussi (ex: moyenne - Ymid)
                // Ici on s'en tient aux détails pour rester minimal.
            }

            // Détails -> trits
            for(int y=0;y<N;++y){
                for(int x=0;x<N;++x){
                    bool inLL = (x < N/2) && (y < N/2);
                    if(inLL) continue;
                    int c = T[(size_t)y*N+x];
                    int8_t b = (std::abs(c) >= P.thresh) ? ( (c>0)? +1 : -1 ) : 0;
                    A.tile_trits.push_back(b);
                }
            }
        }
    }
}

// =============================== [5] Sketch spectral DCT léger ==============
// Downscale -> DCT-II 2D (brute, taille sketchSize) -> bacs radiaux×angles -> ternarisation.

inline void dct1d(const std::vector<float>& in, std::vector<float>& out){
    const int N=(int)in.size();
    out.assign(N, 0.f);
    const float alpha0 = std::sqrt(1.0f/N);
    const float alpha  = std::sqrt(2.0f/N);
    for(int k=0;k<N;++k){
        float s=0.f;
        for(int n=0;n<N;++n){
            s += in[n] * std::cos( (float)M_PI * ( (2*n+1) * k ) / (2.0f*N) );
        }
        out[k] = (k==0? alpha0 : alpha) * s;
    }
}
inline void dct2d(const std::vector<float>& img, int N, std::vector<float>& out){
    // lignes
    std::vector<float> tmp(N*N,0.f);
    for(int y=0;y<N;++y){
        std::vector<float> row(N), D(N);
        for(int x=0;x<N;++x) row[x]=img[(size_t)y*N+x];
        dct1d(row, D);
        for(int x=0;x<N;++x) tmp[(size_t)y*N+x]=D[x];
    }
    // colonnes
    out.assign(N*N,0.f);
    for(int x=0;x<N;++x){
        std::vector<float> col(N), D(N);
        for(int y=0;y<N;++y) col[y]=tmp[(size_t)y*N+x];
        dct1d(col, D);
        for(int y=0;y<N;++y) out[(size_t)y*N+x]=D[y];
    }
}

inline void proto_spectral_sketch(const ImageU8& rgb, const ProtoParams& P, ProtoArtifacts& A){
    // 1) Downscale -> grey (Y)
    ImageU8 small; resize_rgb_nn(rgb, P.sketchDown, P.sketchDown, small);
    std::vector<float> Yf((size_t)P.sketchDown*P.sketchDown, 0.f);
    for(int y=0;y<small.h;++y){
        for(int x=0;x<small.w;++x){
            const uint8_t* p=&small.data[(size_t)(y*small.w+x)*3];
            uint8_t Y,Cb,Cr; rgb_to_ycbcr(p[0],p[1],p[2],Y,Cb,Cr);
            Yf[(size_t)y*small.w+x] = (float)Y - 128.f; // centre
        }
    }
    // 2) Re-échantillonner en N×N (sketchSize) par moyenne bloc simple
    const int N = P.sketchSize;
    std::vector<float> grid(N*N, 0.f);
    int bs = P.sketchDown / N; if(bs<1) bs=1;
    for(int by=0;by<N;++by){
        for(int bx=0;bx<N;++bx){
            double sum=0.0; int cnt=0;
            for(int y=by*bs; y<std::min((by+1)*bs, small.h); ++y){
                for(int x=bx*bs; x<std::min((bx+1)*bs, small.w); ++x){
                    sum += Yf[(size_t)y*small.w+x]; ++cnt;
                }
            }
            grid[(size_t)by*N+bx] = (float)(cnt? sum/cnt : 0.0);
        }
    }
    // 3) DCT 2D
    std::vector<float> F; dct2d(grid, N, F);

    // 4) Agréger en bacs radiaux × angles
    A.rb = P.radialBins; A.ab = P.angleBins;
    A.sketch_trits.assign((size_t)A.rb*A.ab, 0);

    // centre (0,0) ignoré pour l'énergie basse
    auto idx = [&](int r,int a){ return (size_t)r*A.ab + a; };

    // coordonnées polaires discrètes
    float cx = (N-1)/2.0f, cy = (N-1)/2.0f;
    float Rmax = std::hypot(cx, cy);

    std::vector<double> bins((size_t)A.rb*A.ab, 0.0);
    std::vector<int>    counts((size_t)A.rb*A.ab, 0);

    for(int y=0;y<N;++y){
        for(int x=0;x<N;++x){
            if(x==0 && y==0) continue; // skip DC
            float X = (float)x - cx, Y = (float)y - cy;
            float R = std::hypot(X,Y);
            float th = std::atan2(Y,X); if(th<0) th += 2.0f*(float)M_PI;
            int rb = std::min(A.rb-1, (int)std::floor(R / (Rmax+1e-6f) * A.rb));
            int ab = std::min(A.ab-1, (int)std::floor(th / (2.0f*(float)M_PI) * A.ab));
            size_t k = idx(rb,ab);
            bins[k] += std::fabs(F[(size_t)y*N+x]);
            counts[k] += 1;
        }
    }
    // normaliser & ternariser par médiane/MAD
    std::vector<double> vals; vals.reserve(bins.size());
    for(size_t k=0;k<bins.size();++k){
        double v = (counts[k]? bins[k]/counts[k] : 0.0);
        vals.push_back(v);
    }
    // médiane
    std::vector<double> tmp=vals; std::nth_element(tmp.begin(), tmp.begin()+tmp.size()/2, tmp.end());
    double med = tmp[tmp.size()/2];
    // MAD
    for(auto& v: tmp) v = std::fabs(v - med);
    std::nth_element(tmp.begin(), tmp.begin()+tmp.size()/2, tmp.end());
    double mad = tmp[tmp.size()/2] + 1e-6;

    for(size_t k=0;k<vals.size();++k){
        double z = (vals[k] - med) / (1.4826*mad); // robust z-score
        A.sketch_trits[k] = (z > +1.0? +1 : (z < -1.0? -1 : 0));
    }
}

// =============================== [6] Helpers réassemblage ===================
// (optionnel) Déternariser/approx inverse si nécessaire pour tests visuels.

inline void proto_reconstruct_Y_from_tiles(const ProtoArtifacts& A, const ProtoParams& P,
                                           ImageU8& outY){
    // Reconstruction approximative: on ignore LL si keep_LL_u8=false,
    // et on met les détails = ±T (ou 0) selon trits, puis Haar inverse.
    const int N=A.N;
    int W = A.tilesX * N, H = A.tilesY * N;
    outY.w=W; outY.h=H; outY.c=1; outY.data.assign((size_t)W*H, 0);

    size_t idxT=0, idxLL=0;
    for(int ty=0; ty<A.tilesY; ++ty){
        for(int tx=0; tx<A.tilesX; ++tx){
            std::vector<int> T(N*N, 0);
            if(P.keep_LL_u8) T[0] = (int)A.tile_LL[idxLL++];

            for(int y=0;y<N;++y){
                for(int x=0;x<N;++x){
                    bool inLL = (x<N/2)&&(y<N/2);
                    if(inLL) continue;
                    int8_t b = A.tile_trits[idxT++];
                    int v = (b==0? 0 : (b>0? +P.thresh : -P.thresh));
                    T[(size_t)y*N+x] = v;
                }
            }
            haar2d_int_inv(T, N);
            for(int y=0;y<N;++y){
                for(int x=0;x<N;++x){
                    int Y = std::clamp(T[(size_t)y*N+x], 0, 255);
                    outY.data[(size_t)((ty*N+y)*W + (tx*N+x))] = (uint8_t)Y;
                }
            }
        }
    }
}

