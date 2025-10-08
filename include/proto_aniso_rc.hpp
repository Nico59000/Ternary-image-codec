// ============================================================================
//  File: include/proto_aniso_rc.hpp — Ridgelet/Curvelet-lite (anisotrope, sans entropie)
//  Project: Ternary Image/Video Codec v6
//
//  OBJECTIF
//  --------
//  • Approche "ridgelets locaux" par blocs NxN (N=32 par défaut) :
//      - Projections Radon discrètes sur un petit jeu d'angles (8 directions par défaut).
//      - Pour chaque projection (1D), transformée Haar entière (lifting) + ternarisation robuste (balanced).
//      - LL (moyenne Y du bloc) stockée en u8 (option), le reste en trits {-1,0,+1}.
//  • Reconstruction approximate par rétro-projection pondérée (facultative, pour QA).
//  • Sérialisation sans entropie via pack base-243 (5 trits → 1 octet), compatible avec ECC/RS.
//
//  ÉQUILIBRE COÛT/TEMPS/RESSOURCES
//  --------------------------------
//   Paramètres choisis pour être efficaces sans dépendances lourdes (FFT/CZT non requises).
//   Complexité ~ O(W*H * (#angles)) avec des blocs indépendants (parallélisable).
//
//  DÉPENDANCES
//  -----------
//   - "io_image.hpp"  (ImageU8, conversions RGB<->YCbCr, resize)
//   - "ternary_image_codec_v6_min.hpp" (types de base)
//
//  INTERFACE HAUTE-NIVEAU
//  ----------------------
//    AnisoRCParams P; AnisoRCArtifacts A;
//    proto_aniso_rc_encode(rgb, P, A);
//    // A.trits (balanced) + A.block_LL (u8) + meta par bloc  → pack_base243(A.trits, A.bytes)
//    // Reconstruction (option QA):
//    ImageU8 recon; proto_aniso_rc_reconstruct(A, P, recon);
//
// ============================================================================

#pragma once
#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <string>

#include "ternary_image_codec_v6_min.hpp"
#include "io_image.hpp"

// =============================== [0] Paramètres ==============================

struct AnisoRCParams {
    int   block = 32;          // Taille bloc N×N (multiple de 2 recommandé)
    int   angles = 8;          // # directions (parmi table fixe ci-dessous)
    float tern_thresh_z = 1.2f;// seuil robust z-score pour ternarisation
    bool  keep_LL_u8 = true;   // stocker un DC par bloc en u8
    bool  normalize_proj = true;// normalise projections par longueur moyenne
    // Table d’angles (degrés). On en prend 'angles' premiers.
    // 0, 22.5, 45, 67.5, 90, 112.5, 135, 157.5
    std::vector<float> angle_table_deg = {0.f,22.5f,45.f,67.5f,90.f,112.5f,135.f,157.5f};
};

// =============================== [1] Artéfacts ===============================

struct AnisoRCArtifacts {
    // Dimensions et discrétisation
    int W=0, H=0, N=0;       // image padded, taille bloc
    int blocksX=0, blocksY=0;

    // Stockage par bloc :
    //  - LL (u8) si keep_LL_u8
    //  - Pour chaque bloc & chaque angle : projections Haar→détails ternarisés
    //    On sérialise tout en un seul vecteur de trits balanced.
    std::vector<uint8_t> block_LL;     // size = blocksX*blocksY (option)
    std::vector<int8_t>  trits;        // balanced {-1,0,+1}
    std::vector<uint8_t> bytes;        // pack base-243 (option)

    // Métadonnées pour décoder la structure :
    //  - proj_len : longueur des projections par angle (identique pour tous blocs ici)
    //  - off_trits_per_block : nombre de trits consommés par bloc
    int proj_len = 0;
    int angles_used = 0;
    size_t trits_per_block = 0;

    // Pour reconstruction approx (QA)
    //  On mémorise l’ordre de sortie des coefficients 1D (après Haar) : [LL | détails]
    //  mais comme on ne stocke que ternarisation des détails, on reconstituera les
    //  détails à ±T. LL du signal 1D n’est pas stocké (absorbé par block_LL 2D).
};

// =============================== [2] Utils base-243 ==========================

inline uint8_t rc_bal_to_unb(int8_t b){ return (uint8_t)(b + 1); }
inline int8_t  rc_unb_to_bal(uint8_t u){ return (int8_t)u - 1; }

inline void rc_pack_base243(const std::vector<int8_t>& bal, std::vector<uint8_t>& out){
    out.clear(); out.reserve((bal.size()+4)/5);
    uint32_t v=0, p=1; int k=0;
    for(size_t i=0;i<bal.size();++i){
        uint8_t u = rc_bal_to_unb(bal[i]); // {-1,0,1}->{0,1,2}
        v += (uint32_t)u * p; p *= 3; ++k;
        if(k==5){ out.push_back((uint8_t)v); v=0; p=1; k=0; }
    }
    if(k) out.push_back((uint8_t)v);
}
inline void rc_unpack_base243(const std::vector<uint8_t>& bytes, size_t n_trits, std::vector<int8_t>& bal){
    bal.clear(); bal.reserve(n_trits);
    size_t need = (n_trits+4)/5;
    size_t bi=0;
    while(bi<need){
        uint32_t v = (bi < bytes.size()? bytes[bi] : 0);
        ++bi;
        for(int j=0;j<5 && bal.size()<n_trits; ++j){
            uint8_t u = (uint8_t)(v % 3); v/=3;
            bal.push_back(rc_unb_to_bal(u));
        }
    }
}

// =============================== [3] Haar 1D entier ==========================
inline void rc_haar1d(std::vector<int>& s){ // in-place: [A | D]
    const int L=(int)s.size();
    std::vector<int> A(L/2), D(L/2);
    for(int i=0;i<L/2;++i){
        int x0=s[2*i], x1=s[2*i+1];
        int sum = x0 + x1;
        int dif = x0 - x1;
        A[i] = sum>>1;
        D[i] = dif;
    }
    for(int i=0;i<L/2;++i) s[i]=A[i];
    for(int i=0;i<L/2;++i) s[L/2+i]=D[i];
}
inline void rc_haar1d_inv(std::vector<int>& s){ // inverse approximative
    const int L=(int)s.size();
    std::vector<int> x(L);
    for(int i=0;i<L/2;++i){
        int a=s[i], d=s[L/2+i];
        x[2*i]   = a + (d>>1);
        x[2*i+1] = a - ((d - (d>>1)));
    }
    s.swap(x);
}

// =============================== [4] Projections radon discrètes (bloc) =====
//
// Paramétrisation simple : pour un angle θ, on approxime ρ = x cosθ + y sinθ,
// discrétisé sur [-R, +R], R ≈ ceil(N*sqrt(2)/2). On cumule par "bin ρ".
//
// Optimisations :
//  - On pré-calcule cos/sin pour chaque angle.
//  - On normalise par #échantillons par bin si normalize_proj==true.

struct RC_Angle {
    float deg=0.f, rad=0.f, c=1.f, s=0.f;
};
inline void rc_prepare_angles(const AnisoRCParams& P, std::vector<RC_Angle>& out){
    out.clear();
    int A = std::min<int>((int)P.angle_table_deg.size(), P.angles);
    out.reserve(A);
    for(int i=0;i<A;++i){
        float d = P.angle_table_deg[i];
        float r = d * (float)M_PI / 180.f;
        out.push_back({d,r,std::cos(r),std::sin(r)});
    }
}

inline int rc_proj_len_for_block(int N){
    // Longueur de ρ sur bloc NxN ~ 2 * ceil(N*sqrt(2)/2) + 1
    int R = (int)std::ceil( (float)N * 0.70710678f ); // ~N/√2
    return 2*R + 1;
}

inline void rc_block_projections_Y(const uint8_t* Yplane, int W, int H,
                                   int x0, int y0, int N,
                                   const std::vector<RC_Angle>& angs,
                                   bool normalize_proj,
                                   std::vector<std::vector<int>>& proj){
    const int PL = rc_proj_len_for_block(N);
    proj.assign(angs.size(), std::vector<int>(PL, 0));
    std::vector<std::vector<int>> cnt(angs.size(), std::vector<int>(PL, 0));

    float cx = (N-1)*0.5f, cy = (N-1)*0.5f;
    int R = (PL-1)/2;

    for(int y=0;y<N;++y){
        for(int x=0;x<N;++x){
            int X = x0 + x; int Y = y0 + y;
            if(X<0||X>=W||Y<0||Y>=H) continue;
            uint8_t Yv = Yplane[(size_t)Y*W + X];
            float xf = x - cx, yf = y - cy;
            for(size_t a=0;a<angs.size();++a){
                int rho = (int)std::lround( xf*angs[a].c + yf*angs[a].s );
                int bin = rho + R; if(bin<0||bin>=PL) continue;
                proj[a][bin] += (int)Yv;
                cnt[a][bin]  += 1;
            }
        }
    }
    if(normalize_proj){
        for(size_t a=0;a<angs.size();++a){
            for(int i=0;i<PL;++i){
                if(cnt[a][i]>0) proj[a][i] = (proj[a][i] + cnt[a][i]/2) / cnt[a][i];
            }
        }
    }
}

// =============================== [5] Ternarisation robuste ===================
//
// Sur un vecteur de détails 1D (après Haar), on calcule médiane & MAD, puis
// z = (v - median)/(1.4826*MAD), seuil ±P.tern_thresh_z -> {-1,0,+1}.

inline void rc_ternarize_details(const std::vector<int>& sig_haar,
                                 float zth, std::vector<int8_t>& out_bal){
    const int L=(int)sig_haar.size();
    const int H = L/2; // détails = seconde moitié
    out_bal.resize(H);
    if(H==0){ out_bal.clear(); return; }

    // médiane & MAD
    std::vector<double> D; D.reserve(H);
    for(int i=0;i<H;++i) D.push_back((double)std::abs(sig_haar[H+i]));
    std::vector<double> tmp = D;
    std::nth_element(tmp.begin(), tmp.begin()+tmp.size()/2, tmp.end());
    double med = tmp[tmp.size()/2];
    for(auto& v: tmp) v = std::abs(v - med);
    std::nth_element(tmp.begin(), tmp.begin()+tmp.size()/2, tmp.end());
    double mad = tmp[tmp.size()/2] + 1e-6;

    for(int i=0;i<H;++i){
        double z = (std::abs((double)sig_haar[H+i]) - med) / (1.4826*mad);
        int8_t b = (z > zth) ? ( (sig_haar[H+i]>0)? +1 : -1 ) : 0;
        out_bal[i] = b;
    }
}

// =============================== [6] Encodage global =========================

inline void proto_aniso_rc_encode(const ImageU8& rgb, const AnisoRCParams& P, AnisoRCArtifacts& A){
    // 0) Préparer image Y et padding
    ImageU8 work = rgb;
    if(work.c!=3){ // on s'assure d'être en RGB
        ImageU8 tmp=work; tmp.c=3; tmp.data.assign((size_t)work.w*work.h*3,0);
        for(int i=0;i<work.w*work.h;++i){ tmp.data[(size_t)i*3+0]=work.data[i]; tmp.data[(size_t)i*3+1]=work.data[i]; tmp.data[(size_t)i*3+2]=work.data[i]; }
        work.swap(tmp);
    }
    // Plan Y séparé
    std::vector<uint8_t> Yplane((size_t)work.w*work.h,0);
    for(int y=0;y<work.h;++y){
        for(int x=0;x<work.w;++x){
            const uint8_t* p=&work.data[(size_t)(y*work.w+x)*3];
            uint8_t Y,Cb,Cr; rgb_to_ycbcr(p[0],p[1],p[2],Y,Cb,Cr);
            Yplane[(size_t)y*work.w+x]=Y;
        }
    }

    const int N = P.block;
    int W = (work.w + N-1)/N * N;
    int H = (work.h + N-1)/N * N;
    if(W!=work.w || H!=work.h){
        ImageU8 padded; resize_rgb_nn(work, W, H, padded);
        // ré-extraire Y
        Yplane.assign((size_t)W*H,0);
        for(int y=0;y<H;++y){
            for(int x=0;x<W;++x){
                const uint8_t* p=&padded.data[(size_t)(y*W+x)*3];
                uint8_t Y,Cb,Cr; rgb_to_ycbcr(p[0],p[1],p[2],Y,Cb,Cr);
                Yplane[(size_t)y*W+x]=Y;
            }
        }
    }
    A.W=W; A.H=H; A.N=N;
    A.blocksX = W/N; A.blocksY = H/N;

    // 1) Préparer angles
    std::vector<RC_Angle> angs; rc_prepare_angles(P, angs);
    A.angles_used = (int)angs.size();
    A.proj_len    = rc_proj_len_for_block(N);

    // 2) Tailles & buffers
    if(P.keep_LL_u8) A.block_LL.assign((size_t)A.blocksX*A.blocksY, 0);
    A.trits.clear();
    // Pour estimer nombre de trits : par bloc, par angle, on retient H=proj_len/2 détails
    A.trits_per_block = (size_t)A.angles_used * (A.proj_len/2);
    A.trits.reserve((size_t)A.blocksX*A.blocksY * A.trits_per_block);

    // 3) Parcours des blocs
    for(int by=0; by<A.blocksY; ++by){
        for(int bx=0; bx<A.blocksX; ++bx){
            int x0=bx*N, y0=by*N;

            // LL bloc = moyenne Y (rapide)
            if(P.keep_LL_u8){
                uint64_t sum=0;
                for(int y=0;y<N;++y){
                    const uint8_t* row=&Yplane[(size_t)(y0+y)*W + x0];
                    for(int x=0;x<N;++x) sum += row[x];
                }
                A.block_LL[(size_t)by*A.blocksX + bx] = (uint8_t)((sum + (N*N/2)) / (N*N));
            }

            // Projections
            std::vector<std::vector<int>> proj;
            rc_block_projections_Y(Yplane.data(), W,H, x0,y0,N, angs, P.normalize_proj, proj);

            // Pour chaque angle : Haar 1D + ternarisation des détails
            for(int a=0;a<A.angles_used;++a){
                std::vector<int> sig = proj[(size_t)a];
                // Longueur de projection doit être paire pour Haar
                if((sig.size() & 1) != 0) sig.push_back(sig.back());
                rc_haar1d(sig);
                std::vector<int8_t> bal;
                rc_ternarize_details(sig, P.tern_thresh_z, bal);
                // Append
                A.trits.insert(A.trits.end(), bal.begin(), bal.end());
            }
        }
    }
}

// =============================== [7] Reconstruction approx (QA) ==============
//
// On reconstruit par rétro-projection simple :
//   1) Pour chaque projection : on reconstruit la 1D en mettant les détails à ±T (T = médiane MAD inverse approximée, ici on prend T=20 par défaut).
//   2) Backprojection : pour chaque x,y du bloc, on somme les valeurs correspondant au bin ρ(x,y) pour chaque angle, puis on normalise.
//   3) On ajoute le DC du bloc (LL) si keep_LL_u8.
//
// NB: Reconstruction "qualitative", pas une vraie inverse stable (prototype).

inline void proto_aniso_rc_reconstruct(const AnisoRCArtifacts& A, const AnisoRCParams& P, ImageU8& outY){
    const int N=A.N, W=A.W, H=A.H;
    outY.w=W; outY.h=H; outY.c=1; outY.data.assign((size_t)W*H, 0);

    // re-préparer angles (doit matcher l’encode)
    std::vector<RC_Angle> angs; rc_prepare_angles(P, angs);
    const int PL = A.proj_len;
    const int Hlen = PL/2;

    // Un seuil fixe pour reposer les détails (prototype)
    const int T = 20;

    size_t t_ofs = 0;
    for(int by=0; by<A.blocksY; ++by){
        for(int bx=0; bx<A.blocksX; ++bx){
            int x0=bx*N, y0=by*N;
            // accumulateurs
            std::vector<int> acc((size_t)N*N, 0);
            std::vector<int> hits((size_t)N*N, 0);

            // pour chaque angle : reconstruire une projection 1D approx
            for(int a=0;a<A.angles_used;++a){
                // On reconstruit un vecteur sig de longueur PL :
                std::vector<int> sig(PL, 0);
                // détails stockés : Hlen trits
                for(int i=0;i<Hlen;++i){
                    int8_t b = A.trits[t_ofs + (size_t)i];
                    sig[PL/2 + i] = (b==0? 0 : (b>0? +T : -T));
                }
                t_ofs += (size_t)Hlen;
                // Inverse Haar approx
                rc_haar1d_inv(sig);

                // Backprojection
                float cx=(N-1)*0.5f, cy=(N-1)*0.5f;
                int R=(PL-1)/2;
                for(int y=0;y<N;++y){
                    for(int x=0;x<N;++x){
                        int X=x0+x, Y=y0+y;
                        float xf=x-cx, yf=y-cy;
                        int rho = (int)std::lround( xf*angs[a].c + yf*angs[a].s );
                        int bin = rho + R; if(bin<0||bin>=PL) continue;
                        int v = sig[bin];
                        size_t k=(size_t)y*N + x;
                        acc[k] += v;
                        hits[k]+= 1;
                    }
                }
            }

            // Normalisation + ajout DC bloc
            uint8_t DC = (P.keep_LL_u8? A.block_LL[(size_t)by*A.blocksX + bx] : 128);
            for(int y=0;y<N;++y){
                for(int x=0;x<N;++x){
                    size_t k=(size_t)y*N+x;
                    int v = (hits[k]>0? (acc[k]/hits[k]) : 0);
                    int Y = std::clamp( (int)DC + v, 0, 255 );
                    outY.data[(size_t)(y0+y)*W + (x0+x)] = (uint8_t)Y;
                }
            }
        }
    }
}

// =============================== [8] Estimation taille & pack ================

inline size_t proto_aniso_rc_estimated_trits(const AnisoRCArtifacts& A){
    return (size_t)A.blocksX*A.blocksY * A.trits_per_block;
}
inline void proto_aniso_rc_pack(const AnisoRCArtifacts& A, std::vector<uint8_t>& out_bytes){
    rc_pack_base243(A.trits, out_bytes);
}

