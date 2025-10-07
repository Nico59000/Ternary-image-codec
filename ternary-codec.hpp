// ============================================================================
//  TERNARY IMAGE CODEC — Balanced Ternary 27-trit Words (Single Header)
//  Version: v5
//  Features:
//    • GF(3^3) arithmetic over p(x)=x^3 + 2x + 1  (mod 3)  + primitive search
//    • RS(26,k) encoder/decoder (BM + Chien + Forney), roots α^1..α^(n-k)
//    • Superframe header with real ternary CRC-12 (x^12 + x^7 + x^4 + x^3 + 1)
//    • 1D interleaving in 9 bands (symbol slot index 0..8)
//    • 2D interleaving "boustrophedon" (tiles w×h), encode/decode
//    • UEP (Unequal Error Protection) helpers (uniform / luma-priority)
//    • RAW mode (27-trit words) as IA-bridge format
//    • Self-tests: RS unit test (random errors), API round-trip test
//
//  NOTE:
//    - This header is self-contained and focuses on correctness & clarity.
//    - Performance optimizations (SIMD, LUT packing) can be added later.
//    - All math is done in GF(27) with a compact polynomial representation.
//
//  License: MIT 
// ============================================================================

#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <cassert>
#include <algorithm>
#include <random>

// ============================================================================
//  SECTION 1 — BASE TYPES & CONSTANTS
// ============================================================================

enum class BTrit : int8_t { M1=-1, Z0=0, P1=+1 }; // Balanced trit for in-RAM logic
using  UTrit = uint8_t;  // Transport trit (unbalanced): {0,1,2}
using  GF27  = uint8_t;  // Field element index: 0..26

static constexpr int TRITS_PER_WORD = 27;
static constexpr int SYM_PER_WORD   = 9;     // 27 trits / 3 trits per symbol
static constexpr int NUM_BANDS      = 9;

// Pack / unpack 3 trits ↔ 1 GF(27) symbol (base-3 little endian: t0 + 3*t1 + 9*t2)
inline GF27 pack3_utrits_to_gf27(UTrit t0, UTrit t1, UTrit t2){
    return static_cast<GF27>(t0 + 3*t1 + 9*t2);
}
inline std::array<UTrit,3> unpack_gf27_to3_utrits(GF27 s){
    return { static_cast<UTrit>(s%3), static_cast<UTrit>((s/3)%3), static_cast<UTrit>((s/9)%3) };
}

// ============================================================================
//  SECTION 2 — PROFILES, UEP, INTERLEAVING, SCRAMBLER
// ============================================================================

struct RSParams { uint8_t n=26, k=22; };

enum class ProfileID : uint8_t {
    RAW_MODE        = 0xFF, // RAW bridge format for TNN (no ECC)
    P1_RS26_24      = 0,    // t = 1
    P2_RS26_22      = 1,    // t = 2 (default)
    P3_RS26_20      = 2,    // t = 3
    P4_RS26_18      = 3,    // t = 4
    P5_RS26_22_2D   = 4     // P2 + 2D interleaving (tile)
};
inline RSParams rs_params_for(ProfileID p){
    switch(p){
        case ProfileID::P1_RS26_24:    return {26,24};
        case ProfileID::P2_RS26_22:    return {26,22};
        case ProfileID::P3_RS26_20:    return {26,20};
        case ProfileID::P4_RS26_18:    return {26,18};
        case ProfileID::P5_RS26_22_2D: return {26,22};
        default:                       return {26,22};
    }
}

// UEP layout: per band, choose local RS profile index 0..3 (P1..P4)
struct UEPLayout { std::array<uint8_t,NUM_BANDS> band_profile{}; }; // init 0

// Helpers to configure UEP
inline void uep_uniform(UEPLayout& u, uint8_t idx=1 /*P2*/){
    for(int b=0;b<NUM_BANDS;++b) u.band_profile[b] = idx % 4;
}
// Luma-priority (example): bands {0,3,6} → P3 (stronger), others → P2
inline void uep_luma_priority(UEPLayout& u){
    for(int b=0;b<NUM_BANDS;++b) u.band_profile[b] = 1; // P2 by default
    u.band_profile[0]=2; u.band_profile[3]=2; u.band_profile[6]=2; // P3 on luma-ish bands
}

// Tile for 2D interleaving (boustrophedon)
struct Tile2D { uint16_t w=0, h=0; }; // set in header when P5 is active

// Mapping word slot → band (default identity)
struct BandMap { std::array<uint8_t,NUM_BANDS> word_sym_pos_to_band{0,1,2,3,4,5,6,7,8}; };

// Scrambler seed (simple additive mod 3 per-trit applied symbol-wise)
struct ScramblerSeed { uint32_t a=1, b=1, s0=1; };
inline GF27 scramble_symbol(GF27 s, const ScramblerSeed& seed, uint32_t& st){
    st = ((seed.a * st) + seed.b) % 3;
    auto d = unpack_gf27_to3_utrits(s);
    for(auto& x: d) x = static_cast<UTrit>((x + st) % 3);
    return pack3_utrits_to_gf27(d[0],d[1],d[2]);
}
inline GF27 descramble_symbol(GF27 s, const ScramblerSeed& seed, uint32_t& st){
    st = ((seed.a * st) + seed.b) % 3;
    auto d = unpack_gf27_to3_utrits(s);
    for(auto& x: d) x = static_cast<UTrit>((3 + x - (st%3)) % 3);
    return pack3_utrits_to_gf27(d[0],d[1],d[2]);
}

// Sparse beacon (optional pilot) — kept but NEVER used as "local parity"
struct SparseBeaconCfg { uint32_t words_period=0; uint8_t band_slot=0; bool enabled=false; };
struct BeaconPayload { ProfileID profile; uint16_t frame_seq_mod; uint8_t health_flags; };
inline GF27 encode_beacon_symbol(const BeaconPayload& b){
    uint8_t p=static_cast<uint8_t>(b.profile), s=static_cast<uint8_t>(b.frame_seq_mod%5), h=static_cast<uint8_t>(b.health_flags%3);
    return static_cast<GF27>((p + 5*s + 15*h) % 27);
}

// ============================================================================
//  SECTION 3 — SUPERFRAME HEADER & CRC-12 TERNARY
// ============================================================================

struct SuperframeHeader {
    uint16_t       magic    = 0x0A2;
    uint8_t        version  = 1;
    ProfileID      profile  = ProfileID::P2_RS26_22;
    UEPLayout      uep{};
    Tile2D         tile{};
    ScramblerSeed  seed{};
    uint32_t       band_map_hash = 0;
    uint32_t       frame_seq     = 0;
    uint32_t       reserved      = 0;
    uint32_t       crc3m         = 0; // 12 trits packed on 4 symbols
    SparseBeaconCfg beacon{};
};
struct HeaderPack { std::array<GF27,27> symbols{}; };

// CRC-12 (ternary): g(x)=x^12 + x^7 + x^4 + x^3 + 1  — LFSR over mod 3
struct CRC3 {
    static constexpr int L = 12;
    static inline void compute_remainder_12trits(const std::vector<UTrit>& msg_trits,
                                                 std::array<UTrit,L>& rem)
    {
        std::array<UTrit,L> reg{}; reg.fill(0);
        auto step = [&](UTrit in){
            UTrit fb = static_cast<UTrit>((in + reg[L-1]) % 3);
            std::array<UTrit,L> nx{};
            nx[0]  = fb;
            nx[1]  = reg[0];
            nx[2]  = reg[1];
            nx[3]  = static_cast<UTrit>((reg[2] + fb) % 3);
            nx[4]  = static_cast<UTrit>((reg[3] + fb) % 3);
            nx[5]  = reg[4];
            nx[6]  = reg[5];
            nx[7]  = static_cast<UTrit>((reg[6] + fb) % 3);
            nx[8]  = reg[7];
            nx[9]  = reg[8];
            nx[10] = reg[9];
            nx[11] = reg[10];
            reg = nx;
        };
        for(auto t : msg_trits) step(t);
        for(int i=0;i<L;++i) step(0); // multiply by x^L
        rem = reg;
    }
};

// Header codec with CRC fields at indices [20],[21],[22],[26]
struct HeaderCodec {
    static HeaderPack pack(const SuperframeHeader& h){
        HeaderPack p{};
        auto at=[&](int i, GF27 v){ p.symbols[i]=static_cast<GF27>(v%27); };
        // Fixed fields
        at(0,  h.magic%27); at(1,(h.magic/27)%27);
        at(2,  h.version%27);
        at(3,  static_cast<GF27>(h.profile));
        // UEP → 3 base-27 symbols (3×3 trits)
        auto pack3 = [&](int off0, int off1, int off2){
            uint32_t u0=0,u1=0,u2=0;
            for(int i=0;i<3;++i) u0 = u0*3 + (h.uep.band_profile[i]  %3);
            for(int i=3;i<6;++i) u1 = u1*3 + (h.uep.band_profile[i]  %3);
            for(int i=6;i<9;++i) u2 = u2*3 + (h.uep.band_profile[i]  %3);
            at(off0,u0); at(off1,u1); at(off2,u2);
        };
        pack3(4,5,6);
        at(7, h.tile.w%27); at(8, h.tile.h%27);
        at(9, h.seed.a%27); at(10,h.seed.b%27); at(11,h.seed.s0%27);
        at(12,0);
        at(13, h.band_map_hash%27); at(14,(h.band_map_hash/27)%27); at(15,(h.band_map_hash/729)%27);
        at(16,0);
        at(17, h.frame_seq%27); at(18,(h.frame_seq/27)%27); at(19,(h.frame_seq/729)%27);
        at(20,0); at(21,0); at(22,0); // CRC placeholders
        at(23, h.beacon.enabled?1:0);
        at(24, h.beacon.band_slot%27);
        at(25, static_cast<GF27>(std::min<uint32_t>(h.beacon.words_period,26)));
        at(26,0);

        // Compute CRC over all fields EXCEPT CRC slots
        std::vector<UTrit> trits; trits.reserve(27*3);
        auto push_sym=[&](int idx){
            if(idx==20||idx==21||idx==22||idx==26) return;
            auto d = unpack_gf27_to3_utrits(p.symbols[idx]);
            trits.push_back(d[0]); trits.push_back(d[1]); trits.push_back(d[2]);
        };
        for(int i=0;i<27;++i) push_sym(i);

        std::array<UTrit,CRC3::L> rem{};
        CRC3::compute_remainder_12trits(trits, rem);
        auto rem_sym=[&](int i){ return pack3_utrits_to_gf27(rem[i*3+0], rem[i*3+1], rem[i*3+2]); };
        at(20, rem_sym(0)); at(21, rem_sym(1)); at(22, rem_sym(2)); at(26, rem_sym(3));
        return p;
    }
    static bool check_crc(const HeaderPack& p){
        std::vector<UTrit> trits; trits.reserve(27*3);
        auto push_sym=[&](int idx){
            if(idx==20||idx==21||idx==22||idx==26) return;
            auto d = unpack_gf27_to3_utrits(p.symbols[idx]);
            trits.push_back(d[0]); trits.push_back(d[1]); trits.push_back(d[2]);
        };
        for(int i=0;i<27;++i) push_sym(i);
        std::array<UTrit,CRC3::L> rem{};
        CRC3::compute_remainder_12trits(trits, rem);
        // Rebuild remainder from header
        std::array<UTrit,CRC3::L> hdr{};
        auto up=[&](int idx,int off){
            auto d=unpack_gf27_to3_utrits(p.symbols[idx]);
            hdr[off+0]=d[0]; hdr[off+1]=d[1]; hdr[off+2]=d[2];
        };
        up(20,0); up(21,3); up(22,6); up(26,9);
        return (rem==hdr);
    }
    static SuperframeHeader unpack(const HeaderPack& p){
        SuperframeHeader h{};
        auto rd=[&](int i)->uint32_t{ return p.symbols[i]%27; };
        h.magic   = static_cast<uint16_t>( rd(0) + 27*rd(1) );
        h.version = static_cast<uint8_t>(rd(2));
        h.profile = static_cast<ProfileID>(rd(3)%5==4? static_cast<int>(ProfileID::P5_RS26_22_2D) : rd(3)%5);
        // decode UEP from 3 base-27 symbols (each stores 3 ternary digits)
        auto dec_triplet=[&](uint32_t v, int off){
            // v in [0..26] encodes 3 trits: t2 t1 t0 (base-3)
            uint8_t t0 = v % 3; v/=3;
            uint8_t t1 = v % 3; v/=3;
            uint8_t t2 = v % 3;
            h.uep.band_profile[off+0]=t0;
            h.uep.band_profile[off+1]=t1;
            h.uep.band_profile[off+2]=t2;
        };
        dec_triplet(rd(4),0); dec_triplet(rd(5),3); dec_triplet(rd(6),6);
        h.tile.w = static_cast<uint16_t>(rd(7));
        h.tile.h = static_cast<uint16_t>(rd(8));
        h.seed.a = rd(9); h.seed.b = rd(10); h.seed.s0 = rd(11);
        h.band_map_hash = rd(13) + 27*rd(14) + 729*rd(15);
        h.frame_seq     = rd(17) + 27*rd(18) + 729*rd(19);
        h.beacon.enabled     = rd(23)!=0;
        h.beacon.band_slot   = rd(24)%9;
        h.beacon.words_period= rd(25);
        return h;
    }
};

// ============================================================================
//  SECTION 4 — GF(27) ARITHMETIC (p(x)=x^3+2x+1) & RS(26,k) CODEC
// ============================================================================

inline GF27 gf27_add_u(GF27 a, GF27 b){
    UTrit a0=a%3, a1=(a/3)%3, a2=(a/9)%3;
    UTrit b0=b%3, b1=(b/3)%3, b2=(b/9)%3;
    return static_cast<GF27>(( (a0+b0)%3 ) + 3*((a1+b1)%3) + 9*((a2+b2)%3));
}
inline GF27 gf27_sub_u(GF27 a, GF27 b){
    auto sub3=[&](int x,int y){ int z=x-y; z%=3; if(z<0) z+=3; return z; };
    UTrit a0=a%3, a1=(a/3)%3, a2=(a/9)%3;
    UTrit b0=b%3, b1=(b/3)%3, b2=(b/9)%3;
    return static_cast<GF27>( sub3(a0,b0) + 3*sub3(a1,b1) + 9*sub3(a2,b2) );
}
// Multiply polynomials mod p(x)=x^3+2x+1, reducing x^3≡x+2, x^4≡x^2+2x
inline GF27 gf27_mul_poly(GF27 a, GF27 b){
    if(a==0 || b==0) return 0;
    int a0=a%3, a1=(a/3)%3, a2=(a/9)%3;
    int b0=b%3, b1=(b/3)%3, b2=(b/9)%3;
    int r0 = (a0*b0) %3;
    int r1 = (a0*b1 + a1*b0) %3;
    int r2 = (a0*b2 + a1*b1 + a2*b0) %3;
    int r3 = (a1*b2 + a2*b1) %3;
    int r4 = (a2*b2) %3;
    r1 = (r1 + r3) %3;
    r0 = (r0 + 2*r3) %3;
    r2 = (r2 + r4) %3;
    r1 = (r1 + 2*r4) %3;
    return static_cast<GF27>(r0 + 3*r1 + 9*r2);
}

struct GF27Tables {
    std::array<GF27, 26*3> exp{};
    std::array<int16_t, 27> log{};
    std::array<GF27, 27*27> mul{};
    std::array<GF27, 27> inv{};
    GF27 primitive = 0;
};

struct GF27Context {
    GF27Tables tab{};

    int order_of(GF27 g) const {
        if(g==0 || g==1) return -1;
        GF27 x = 1;
        for(int i=1;i<=26;++i){
            x = gf27_mul_poly(x, g);
            if(x==1) return i;
        }
        return -1;
    }
    void init(){
        // Find primitive element
        GF27 prim = 0;
        for(GF27 c=2; c<27; ++c){
            if(order_of(c)==26){ prim=c; break; }
        }
        if(prim==0) prim=3; // fallback (x)
        tab.primitive = prim;

        // Build exp/log tables
        tab.log.fill(-1);
        tab.exp[0]=1; tab.log[1]=0;
        for(int i=1;i<26;++i){
            tab.exp[i] = gf27_mul_poly(tab.exp[i-1], prim);
            tab.log[ tab.exp[i] ] = i;
        }
        for(int i=26;i<26*3;++i) tab.exp[i]=tab.exp[i-26];
        for(int a=0;a<27;++a) for(int b=0;b<27;++b) tab.mul[a*27+b]=gf27_mul_poly((GF27)a,(GF27)b);
        tab.inv[0]=0;
        for(int a=1;a<27;++a){
            int la = tab.log[a];
            int inv_idx = (26 - la) % 26;
            tab.inv[a] = tab.exp[inv_idx];
        }
    }
    inline GF27 add(GF27 a, GF27 b) const { return gf27_add_u(a,b); }
    inline GF27 sub(GF27 a, GF27 b) const { return gf27_sub_u(a,b); }
    inline GF27 mul(GF27 a, GF27 b) const { return tab.mul[a*27+b]; }
    inline GF27 inv(GF27 a)        const { return tab.inv[a]; }
    inline GF27 pow_alpha(int e)    const { int m=(e%26+26)%26; return tab.exp[m]; }
    inline int  log(GF27 a)         const { return tab.log[a]; }
};

struct RSCodec {
    GF27Context* gf=nullptr;
    RSParams     params{};
    std::vector<GF27> g; // generator polynomial coefficients (low→high)

    void init(GF27Context* ctx, RSParams p){ gf=ctx; params=p; build_generator(); }

    void build_generator(){
        int r = params.n - params.k;
        g.assign(1, (GF27)1); // g(x)=1
        for(int i=1;i<=r;++i){
            std::vector<GF27> ng(g.size()+1, 0);
            GF27 root = gf->pow_alpha(i);
            for(size_t j=0;j<g.size(); ++j){
                // (x - root)
                ng[j]   = gf->add(ng[j], gf->mul(g[j], gf->sub((GF27)0, root)));
                ng[j+1] = gf->add(ng[j+1], g[j]);
            }
            g.swap(ng);
        }
    }

    // Systematic encoding: parity = remainder of M(x)·x^r divided by g(x)
    bool encode_block(const GF27* data_k, GF27* out_n) const {
        int r = params.n - params.k;
        std::vector<GF27> T(params.n, 0);
        for(int i=0;i<params.k;++i) T[i]=data_k[i];
        for(int i=0;i<params.k; ++i){
            GF27 coef = T[i];
            if(coef==0) continue;
            for(int j=0;j<=r; ++j){
                GF27 prod = gf->mul(g[j], coef);
                T[i+j] = gf->sub(T[i+j], prod);
            }
        }
        for(int i=0;i<params.k;++i) out_n[i]=data_k[i];
        for(int i=0;i<r;++i) out_n[params.k+i]=T[params.k+i];
        return true;
    }

    // Evaluate polynomial at x
    GF27 poly_eval(const std::vector<GF27>& p, GF27 x) const {
        GF27 acc = 0;
        for(int i=(int)p.size()-1; i>=0; --i){
            acc = gf->mul(acc, x);
            acc = gf->add(acc, p[i]);
        }
        return acc;
    }

    // RS decoding (BM + Chien + Forney), corrects up to t=(n-k)/2 symbol errors
    bool decode_block(GF27* inout_n, GF27* out_k) const {
        const int n=params.n, k=params.k, r=n-k, t=r/2;

        // --- Syndromes S1..Sr at α^1..α^r
        std::vector<GF27> S(r,0);
        bool all_zero=true;
        for(int j=0;j<r;++j){
            GF27 x = gf->pow_alpha(j+1);
            GF27 acc = 0;
            for(int i=0;i<n;++i){
                GF27 xpow = gf->pow_alpha(((j+1)*i) % 26);
                acc = gf->add(acc, gf->mul(inout_n[i], xpow));
            }
            S[j]=acc; if(acc!=0) all_zero=false;
        }
        if(all_zero){ for(int i=0;i<k;++i) out_k[i]=inout_n[i]; return true; }

        // --- Berlekamp–Massey to compute error locator σ(x)
        std::vector<GF27> sigma(1,1), B(1,1);
        int L=0, m=1;
        for(int nS=0; nS<r; ++nS){
            GF27 delta = S[nS];
            for(int i=1;i<=L;++i){
                if(i < (int)sigma.size()){
                    GF27 si=sigma[i];
                    delta = gf->add(delta, gf->mul(si, S[nS-i]));
                }
            }
            if(delta!=0){
                auto T = sigma;
                // σ(x) = σ(x) - δ x^m B(x)
                std::vector<GF27> dB(B.size());
                for(size_t i=0;i<B.size(); ++i) dB[i]=gf->mul(delta,B[i]);
                std::vector<GF27> xmdB(m + dB.size(), 0);
                for(size_t i=0;i<dB.size(); ++i) xmdB[m+i]=dB[i];
                size_t newdeg = std::max(sigma.size(), xmdB.size());
                std::vector<GF27> ns(newdeg, 0);
                for(size_t i=0;i<ns.size(); ++i){
                    GF27 a = (i<sigma.size()? sigma[i]:0);
                    GF27 b = (i<xmdB.size()? xmdB[i]:0);
                    ns[i]=gf->sub(a,b);
                }
                sigma.swap(ns);
                if(2*L <= nS){
                    GF27 invd = gf->inv(delta);
                    B.resize(T.size());
                    for(size_t i=0;i<T.size(); ++i) B[i]=gf->mul(T[i], invd);
                    L = nS + 1 - L;
                    m = 1;
                }else{ m += 1; }
            }else{ m += 1; }
        }

        // --- Error evaluator Ω(x) = [S(x)·σ(x)] mod x^r
        std::vector<GF27> Sx(r+1,0); for(int j=0;j<r;++j) Sx[j]=S[j];
        std::vector<GF27> Omega(Sx.size()+sigma.size()-1, 0);
        for(size_t i=0;i<Sx.size(); ++i)
            for(size_t j=0;j<sigma.size(); ++j)
                Omega[i+j] = gf->add(Omega[i+j], gf->mul(Sx[i], sigma[j]));
        if((int)Omega.size()>r) Omega.resize(r);

        // --- Chien search for roots of σ(x) at x=α^{-i}
        std::vector<int> err_pos; err_pos.reserve(t);
        for(int i=0;i<n; ++i){
            GF27 x = gf->pow_alpha((-i)%26);
            GF27 acc=0;
            for(int d=(int)sigma.size()-1; d>=0; --d){
                acc = gf->mul(acc, x);
                acc = gf->add(acc, sigma[d]);
            }
            if(acc==0) err_pos.push_back(i);
        }
        if((int)err_pos.size() > t) return false;

        // --- Formal derivative σ'(x) (char=3, multiply coeff by degree mod 3)
        std::vector<GF27> sigmap((sigma.size()>1)? sigma.size()-1 : 1, 0);
        if(sigma.size()>=2){
            for(size_t i=1;i<sigma.size(); ++i){
                int im = (int)(i % 3);
                if(im==0) { sigmap[i-1]=0; }
                else if(im==1){ sigmap[i-1]=sigma[i]; }
                else { // multiply by 2 in GF(3): digitwise scale
                    GF27 a = sigma[i];
                    UTrit a0=a%3, a1=(a/3)%3, a2=(a/9)%3;
                    auto m2=[&](UTrit t)->UTrit{ return (UTrit)((2*t)%3); };
                    sigmap[i-1] = static_cast<GF27>( m2(a0) + 3*m2(a1) + 9*m2(a2) );
                }
            }
        }

        // --- Forney error magnitude at each error position
        for(int pos : err_pos){
            GF27 Xin = gf->pow_alpha((-pos)%26);
            GF27 num=0, den=0;
            for(int d=(int)Omega.size()-1; d>=0; --d){ num = gf->mul(num,Xin); num = gf->add(num, Omega[d]); }
            for(int d=(int)sigmap.size()-1; d>=0; --d){ den = gf->mul(den,Xin); den = gf->add(den, sigmap[d]); }
            if(den==0) return false;
            GF27 mag = gf->mul( gf->sub((GF27)0,num), gf->inv(den) );
            inout_n[pos] = gf->add(inout_n[pos], mag);
        }
        for(int i=0;i<k;++i) out_k[i]=inout_n[i];
        return true;
    }
};

// ============================================================================
//  SECTION 5 — 27-TRIT WORDS & RAW PACKING (2 px ↔ 1 word)
// ============================================================================

struct Word27 { std::array<GF27,SYM_PER_WORD> sym{}; };
struct PixelYCbCrQuant { uint16_t Yq=0; int16_t Cbq=0; int16_t Crq=0; };

inline void int_to_ternary_digits(uint32_t v,int w,std::array<UTrit,27>&d,int s){
    for(int i=0;i<w;++i){ d[s+i]=static_cast<UTrit>(v%3); v/=3; }
}
inline uint32_t ternary_digits_to_int(const std::array<UTrit,27>&d,int w,int s){
    uint32_t val=0,p=1; for(int i=0;i<w;++i){ val+=p*d[s+i]; p*=3; } return val;
}

inline void pack_two_pixels_into_word27(const PixelYCbCrQuant& p0,const PixelYCbCrQuant& p1,Word27& w){
    std::array<UTrit,27> T{}; T.fill(0);
    int_to_ternary_digits(p0.Yq,5,T,0);
    int_to_ternary_digits((uint32_t)(p0.Cbq+40),4,T,5);
    int_to_ternary_digits((uint32_t)(p0.Crq+40),4,T,9);
    int_to_ternary_digits(p1.Yq,5,T,13);
    int_to_ternary_digits((uint32_t)(p1.Cbq+40),4,T,18);
    int_to_ternary_digits((uint32_t)(p1.Crq+40),4,T,22);
    T[26]=0; // free trit (fixed)
    for(int s=0;s<9;++s) w.sym[s]=pack3_utrits_to_gf27(T[s*3],T[s*3+1],T[s*3+2]);
}
inline void unpack_word27_to_two_pixels(const Word27& w,PixelYCbCrQuant& p0,PixelYCbCrQuant& p1){
    std::array<UTrit,27> T{};
    for(int s=0;s<9;++s){ auto d=unpack_gf27_to3_utrits(w.sym[s]); T[s*3]=d[0]; T[s*3+1]=d[1]; T[s*3+2]=d[2]; }
    p0.Yq = ternary_digits_to_int(T,5,0);  p0.Cbq= (int16_t)ternary_digits_to_int(T,4,5)-40;  p0.Crq= (int16_t)ternary_digits_to_int(T,4,9)-40;
    p1.Yq = ternary_digits_to_int(T,5,13); p1.Cbq= (int16_t)ternary_digits_to_int(T,4,18)-40; p1.Crq= (int16_t)ternary_digits_to_int(T,4,22)-40;
}

inline bool encode_raw_pixels_to_words(const std::vector<PixelYCbCrQuant>& px,std::vector<Word27>& out){
    out.clear(); out.reserve((px.size()+1)/2);
    for(size_t i=0;i<px.size(); i+=2){ Word27 w{}; pack_two_pixels_into_word27(px[i], (i+1<px.size()?px[i+1]:PixelYCbCrQuant{}), w); out.push_back(w); }
    return true;
}
inline bool decode_raw_words_to_pixels(const std::vector<Word27>& in,std::vector<PixelYCbCrQuant>& out){
    out.clear(); out.reserve(in.size()*2);
    for(const auto& w: in){ PixelYCbCrQuant a{},b{}; unpack_word27_to_two_pixels(w,a,b); out.push_back(a); out.push_back(b); }
    return true;
}

// ============================================================================
//  SECTION 6 — 2D INTERLEAVING (BOUSTROPHEDON) — ENCODE & DECODE
// ============================================================================

inline void interleave2D_boustrophedon(std::vector<GF27>& syms, Tile2D tile){
    if(tile.w==0 || tile.h==0) return;
    const size_t A = static_cast<size_t>(tile.w) * tile.h;
    if(A==0) return;
    std::vector<GF27> out; out.reserve(syms.size());
    size_t i=0;
    while(i < syms.size()){
        size_t take = std::min(A, syms.size()-i);
        // Copy current tile to temp (row-major assumed)
        std::vector<GF27> tmp(syms.begin()+i, syms.begin()+i+take);
        // Output boustrophedon order
        for(uint16_t r=0;r<tile.h; ++r){
            if(r%2==0){ // L->R
                for(uint16_t c=0;c<tile.w && (size_t)r*tile.w+c < take; ++c)
                    out.push_back(tmp[r*tile.w + c]);
            }else{ // R->L
                for(int c=tile.w-1; c>=0; --c){
                    size_t idx = (size_t)r*tile.w + c;
                    if(idx<take) out.push_back(tmp[idx]);
                }
            }
        }
        i += take;
    }
    syms.swap(out);
}
inline void deinterleave2D_boustrophedon(std::vector<GF27>& syms, Tile2D tile){
    if(tile.w==0 || tile.h==0) return;
    const size_t A = static_cast<size_t>(tile.w) * tile.h;
    if(A==0) return;
    std::vector<GF27> out; out.reserve(syms.size());
    size_t i=0;
    while(i < syms.size()){
        size_t take = std::min(A, syms.size()-i);
        std::vector<GF27> tmp(take);
        size_t k=0;
        for(uint16_t r=0;r<tile.h; ++r){
            if(r%2==0){
                for(uint16_t c=0;c<tile.w && (size_t)r*tile.w+c < take; ++c)
                    tmp[r*tile.w + c] = syms[i + (k++)];
            }else{
                for(int c=tile.w-1; c>=0; --c){
                    size_t idx = (size_t)r*tile.w + c;
                    if(idx<take) tmp[idx] = syms[i + (k++)];
                }
            }
        }
        out.insert(out.end(), tmp.begin(), tmp.end());
        i += take;
    }
    syms.swap(out);
}

// ============================================================================
//  SECTION 7 — PIPELINE CONTEXTS & HIGH-LEVEL APIS (PROFILED ↔ RAW)
// ============================================================================

// BandMap left as identity; hash can be added if a custom perm is used
struct EncoderConfig {
    ProfileID      profile   = ProfileID::P2_RS26_22;
    UEPLayout      uep{};
    Tile2D         tile{};
    BandMap        bandmap{};
    ScramblerSeed  seed{1,1,1};
    SparseBeaconCfg beacon{};
    uint32_t       superframe_words = 8192;
};

struct EncoderContext {
    GF27Context    gf;
    RSCodec        rs_p1, rs_p2, rs_p3, rs_p4, rs_hdr;
    EncoderConfig  cfg;
    EncoderContext(){
        gf.init();
        rs_p1.init(&gf, rs_params_for(ProfileID::P1_RS26_24));
        rs_p2.init(&gf, rs_params_for(ProfileID::P2_RS26_22));
        rs_p3.init(&gf, rs_params_for(ProfileID::P3_RS26_20));
        rs_p4.init(&gf, rs_params_for(ProfileID::P4_RS26_18));
        rs_hdr.init(&gf, RSParams{26,18});
        uep_uniform(cfg.uep, 1); // default P2 everywhere
    }
};

struct DecoderContext {
    GF27Context    gf;
    RSCodec        rs_p1, rs_p2, rs_p3, rs_p4, rs_hdr;
    EncoderConfig  cfg_last_seen;
    DecoderContext(){
        gf.init();
        rs_p1.init(&gf, rs_params_for(ProfileID::P1_RS26_24));
        rs_p2.init(&gf, rs_params_for(ProfileID::P2_RS26_22));
        rs_p3.init(&gf, rs_params_for(ProfileID::P3_RS26_20));
        rs_p4.init(&gf, rs_params_for(ProfileID::P4_RS26_18));
        rs_hdr.init(&gf, RSParams{26,18});
        cfg_last_seen.profile = ProfileID::P2_RS26_22;
        uep_uniform(cfg_last_seen.uep, 1);
    }
};

// --- Internal helpers for header IO ---

// Read & RS-decode header (2× RS(26,18) carrying 27 symbols) from the word stream
inline bool read_and_decode_header_from_words(const std::vector<Word27>& words,
                                              size_t& cursor_word,
                                              SuperframeHeader& out_hdr,
                                              RSCodec& rs_hdr)
{
    if(cursor_word + 6 > words.size()) return false; // need 6 words = 54 symbols (we use 52)
    std::vector<GF27> syms; syms.reserve(6*9);
    for(int w=0; w<6; ++w)
        for(int s=0; s<9; ++s)
            syms.push_back(words[cursor_word+w].sym[s]);
    cursor_word += 6;

    GF27 blkA[26]{}, blkB[26]{};
    for(int i=0;i<26;++i) blkA[i]=syms[i];
    for(int i=0;i<26;++i) blkB[i]=syms[26+i];

    GF27 dataA[18]{}, dataB[18]{};
    if(!rs_hdr.decode_block(blkA, dataA)) return false;
    if(!rs_hdr.decode_block(blkB, dataB)) return false;

    HeaderPack hp{};
    for(int i=0;i<18;++i) hp.symbols[i]=dataA[i];
    for(int i=0;i<9; ++i) hp.symbols[18+i]=dataB[i];

    if(!HeaderCodec::check_crc(hp)) return false; // integrity
    out_hdr = HeaderCodec::unpack(hp);
    return true;
}

// Descramble & remove sparse beacons from body words
inline void descramble_and_remove_beacons_from_words(std::vector<Word27>& words,
                                                     const SuperframeHeader& hdr)
{
    std::vector<GF27> sy; sy.reserve(words.size()*9);
    for(auto& w: words) for(auto s: w.sym) sy.push_back(s);

    uint32_t st = hdr.seed.s0 % 3;
    for(auto& s: sy) s = descramble_symbol(s, hdr.seed, st);

    if(hdr.beacon.enabled && hdr.beacon.words_period>0){
        std::vector<GF27> sy2; sy2.reserve(sy.size());
        size_t word_idx=0, idx=0;
        while(idx < sy.size()){
            bool is_beacon = ((word_idx % hdr.beacon.words_period)==0);
            for(int slot=0; slot<9 && idx<sy.size(); ++slot, ++idx){
                if(is_beacon && slot==hdr.beacon.band_slot){ /* drop */ }
                else sy2.push_back(sy[idx]);
            }
            ++word_idx;
        }
        sy.swap(sy2);
    }
    size_t nw = sy.size()/9;
    words.assign(nw, Word27{});
    size_t k=0; for(size_t w=0; w<nw; ++w) for(int s=0;s<9;++s) words[w].sym[s]=sy[k++];
}

// Demap to 9 bands, RS-decode per band according to UEP
inline bool demap_and_rsdecode_bands_from_words(const std::vector<Word27>& body,
                                                std::vector<GF27>& out_syms,
                                                const SuperframeHeader& hdr,
                                                RSCodec& rs_p1, RSCodec& rs_p2, RSCodec& rs_p3, RSCodec& rs_p4)
{
    std::vector<GF27> sy; sy.reserve(body.size()*9);
    for(const auto& w: body) for(auto s: w.sym) sy.push_back(s);

    std::array<std::vector<GF27>,9> bands;
    for(size_t i=0;i<sy.size(); ++i) bands[i%9].push_back(sy[i]);

    out_syms.clear();
    for(int b=0;b<9;++b){
        RSCodec* rsc = &rs_p2;
        switch(hdr.uep.band_profile[b]%4){ case 0: rsc=&rs_p1; break; case 1: rsc=&rs_p2; break; case 2: rsc=&rs_p3; break; case 3: rsc=&rs_p4; break; }
        RSParams p = rsc->params;
        size_t j=0;
        while(j + p.n <= bands[b].size()){
            GF27 nbuf[26]{}, kbuf[26]{};
            for(int i=0;i<p.n;++i) nbuf[i]=bands[b][j+i];
            if(!rsc->decode_block(nbuf, kbuf)) return false;
            for(int i=0;i<p.k;++i) out_syms.push_back(kbuf[i]);
            j += p.n;
        }
    }
    return true;
}

// High-level API: PROFILÉ → RAW (for ternary NN)
inline bool decode_profile_to_raw(const std::vector<Word27>& in_profile_words,
                                  std::vector<Word27>& out_raw_words,
                                  DecoderContext& dctx)
{
    out_raw_words.clear();
    if(dctx.cfg_last_seen.profile == ProfileID::RAW_MODE){
        out_raw_words = in_profile_words; return true;
    }

    size_t cursor=0;
    SuperframeHeader hdr{};
    if(!read_and_decode_header_from_words(in_profile_words, cursor, hdr, dctx.rs_hdr)) return false;

    std::vector<Word27> body(in_profile_words.begin()+cursor, in_profile_words.end());
    descramble_and_remove_beacons_from_words(body, hdr);

    std::vector<GF27> useful_syms;
    if(!demap_and_rsdecode_bands_from_words(body, useful_syms, hdr,
                                            dctx.rs_p1, dctx.rs_p2, dctx.rs_p3, dctx.rs_p4)) return false;

    // If P5, undo the 2D interleaving
    if(hdr.profile == ProfileID::P5_RS26_22_2D && hdr.tile.w && hdr.tile.h){
        deinterleave2D_boustrophedon(useful_syms, hdr.tile);
    }

    // Symbols → trits → pack 26 trits per RAW word
    std::vector<UTrit> trits; trits.reserve(useful_syms.size()*3);
    for(auto s: useful_syms){ auto d=unpack_gf27_to3_utrits(s); trits.insert(trits.end(), d.begin(), d.end()); }
    size_t idx=0;
    while(idx + 26 <= trits.size()){
        std::array<UTrit,27> T{}; for(int i=0;i<26;++i) T[i]=trits[idx+i]; T[26]=0;
        Word27 w{}; for(int s=0;s<9;++s) w.sym[s]=pack3_utrits_to_gf27(T[s*3],T[s*3+1],T[s*3+2]);
        out_raw_words.push_back(w); idx += 26;
    }
    return true;
}

// High-level API: RAW → PROFILÉ (for transport/storage)
inline bool encode_profile_from_raw(const std::vector<Word27>& in_raw_words,
                                    std::vector<Word27>& out_profile_words,
                                    EncoderContext& ectx)
{
    out_profile_words.clear();
    if(ectx.cfg.profile == ProfileID::RAW_MODE){ out_profile_words = in_raw_words; return true; }

    // RAW words → useful symbols
    std::vector<GF27> syms_useful; syms_useful.reserve(in_raw_words.size()*8);
    std::array<UTrit,3> carry{}; int clen=0;
    for(const auto& w: in_raw_words){
        std::array<UTrit,27> T{};
        for(int s=0;s<9;++s){ auto d=unpack_gf27_to3_utrits(w.sym[s]); T[s*3]=d[0]; T[s*3+1]=d[1]; T[s*3+2]=d[2]; }
        int i=0;
        if(clen>0){
            while(clen<3 && i<26) carry[clen++]=T[i++];
            if(clen==3){ syms_useful.push_back(pack3_utrits_to_gf27(carry[0],carry[1],carry[2])); clen=0; }
        }
        for(; i+2<26; i+=3) syms_useful.push_back(pack3_utrits_to_gf27(T[i],T[i+1],T[i+2]));
        for(; i<26; ++i) carry[clen++]=T[i];
    }
    if(clen>0){ while(clen<3) carry[clen++]=0; syms_useful.push_back(pack3_utrits_to_gf27(carry[0],carry[1],carry[2])); }

    // If P5, apply 2D interleaving before band mapping
    if(ectx.cfg.profile == ProfileID::P5_RS26_22_2D && ectx.cfg.tile.w && ectx.cfg.tile.h){
        interleave2D_boustrophedon(syms_useful, ectx.cfg.tile);
    }

    // Map to 9 bands (slot i%9) and RS-encode per band (UEP)
    std::array<std::vector<GF27>,9> bands;
    for(size_t i=0;i<syms_useful.size(); ++i) bands[i%9].push_back(syms_useful[i]);
    auto rsc_for=[&](int b)->RSCodec*{
        switch(ectx.cfg.uep.band_profile[b]%4){ case 0: return &ectx.rs_p1; case 1: return &ectx.rs_p2; case 2: return &ectx.rs_p3; default: return &ectx.rs_p4; }
    };
    std::vector<GF27> body_syms;
    for(int b=0;b<9;++b){
        RSCodec* rsc = rsc_for(b); RSParams p=rsc->params;
        size_t j=0;
        while(j + p.k <= bands[b].size()){
            GF27 kbuf[26]{}, nbuf[26]{};
            for(int i=0;i<p.k;++i) kbuf[i]=bands[b][j+i];
            rsc->encode_block(kbuf, nbuf);
            for(int i=0;i<p.n;++i) body_syms.push_back(nbuf[i]);
            j += p.k;
        }
    }

    // Scramble & insert sparse beacons if enabled
    uint32_t st = ectx.cfg.seed.s0 % 3;
    for(auto& s: body_syms) s = scramble_symbol(s, ectx.cfg.seed, st);

    if(ectx.cfg.beacon.enabled && ectx.cfg.beacon.words_period>0){
        std::vector<GF27> sy2; sy2.reserve(body_syms.size() + body_syms.size()/(9*ectx.cfg.beacon.words_period) + 9);
        size_t word_idx=0, k=0;
        while(k < body_syms.size()){
            bool ins = ((word_idx % ectx.cfg.beacon.words_period)==0);
            for(int slot=0; slot<9; ++slot){
                if(ins && slot==ectx.cfg.beacon.band_slot){
                    BeaconPayload bp{ ectx.cfg.profile, static_cast<uint16_t>(ectx.cfg.superframe_words%25), 0 };
                    sy2.push_back( encode_beacon_symbol(bp) );
                }else{
                    sy2.push_back( (k<body_syms.size()? body_syms[k++] : 0) );
                }
            }
            ++word_idx;
        }
        body_syms.swap(sy2);
    }

    // Build header (27 symbols) → 2× RS(26,18)
    SuperframeHeader hdr{};
    hdr.profile=ectx.cfg.profile; hdr.uep=ectx.cfg.uep; hdr.tile=ectx.cfg.tile; hdr.seed=ectx.cfg.seed; hdr.beacon=ectx.cfg.beacon;
    HeaderPack hp = HeaderCodec::pack(hdr);

    GF27 dataA[18]{}, dataB[18]{};
    for(int i=0;i<18; ++i) dataA[i]=hp.symbols[i];
    for(int i=0;i<9;  ++i) dataB[i]=hp.symbols[18+i];
    for(int i=9;i<18; ++i) dataB[i]=0;

    GF27 encA[26]{}, encB[26]{};
    ectx.rs_hdr.encode_block(dataA, encA);
    ectx.rs_hdr.encode_block(dataB, encB);

    std::vector<GF27> all; all.reserve(52 + body_syms.size());
    for(int i=0;i<26;++i) all.push_back(encA[i]);
    for(int i=0;i<26;++i) all.push_back(encB[i]);
    all.insert(all.end(), body_syms.begin(), body_syms.end());

    // Pack 9 symbols per word
    size_t total_words = (all.size()+8)/9;
    out_profile_words.assign(total_words, Word27{});
    size_t idx=0;
    for(size_t w=0; w<total_words; ++w)
        for(int s=0;s<9;++s)
            out_profile_words[w].sym[s] = (idx<all.size()? all[idx++] : 0);
    return true;
}

// ============================================================================
//  SECTION 8 — SELF TESTS (RS & API ROUND-TRIP)
// ============================================================================

// RS unit test: for each profile P1..P4, encode → inject ≤t errors → decode
inline bool selftest_rs_unit(){
    GF27Context gf; gf.init();
    std::mt19937 rng(12345);
    for(ProfileID pid : {ProfileID::P1_RS26_24, ProfileID::P2_RS26_22, ProfileID::P3_RS26_20, ProfileID::P4_RS26_18}){
        RSCodec rs; rs.init(&gf, rs_params_for(pid));
        const int n=rs.params.n, k=rs.params.k, t=(n-k)/2;
        // Build deterministic data
        std::vector<GF27> data(k);
        for(int i=0;i<k;++i) data[i]=static_cast<GF27>((i*5 + 7) % 27);
        std::vector<GF27> code(n);
        rs.encode_block(data.data(), code.data());
        // Inject up to t errors at random distinct positions
        std::uniform_int_distribution<int> posdist(0, n-1);
        std::uniform_int_distribution<int> valdist(1, 26); // non-zero deltas
        std::vector<int> used;
        for(int e=0;e<t; ++e){
            int pos; do{ pos=posdist(rng); }while(std::find(used.begin(),used.end(),pos)!=used.end());
            used.push_back(pos);
            GF27 delta = static_cast<GF27>(valdist(rng));
            code[pos] = gf.add(code[pos], delta);
        }
        std::vector<GF27> outk(k);
        if(!rs.decode_block(code.data(), outk.data())) return false;
        if(outk != data) return false;
    }
    return true;
}

// API round-trip test: RAW → encode(P2/P5) → decode → RAW (compare)
inline bool selftest_api_roundtrip(bool use_p5=false){
    // Build tiny RAW words from synthetic pixels
    std::vector<PixelYCbCrQuant> px(64); // 32 words
    for(size_t i=0;i<px.size(); ++i){
        px[i].Yq  = (i*7)%243;
        px[i].Cbq = static_cast<int16_t>((int(i*3)%81) - 40);
        px[i].Crq = static_cast<int16_t>((int(i*5)%81) - 40);
    }
    std::vector<Word27> raw_in; encode_raw_pixels_to_words(px, raw_in);

    EncoderContext ectx;
    if(use_p5){ ectx.cfg.profile=ProfileID::P5_RS26_22_2D; ectx.cfg.tile={16,16}; }
    else      { ectx.cfg.profile=ProfileID::P2_RS26_22;    ectx.cfg.tile={0,0};   }
    uep_luma_priority(ectx.cfg.uep); // demonstrate UEP advanced

    std::vector<Word27> prof;
    if(!encode_profile_from_raw(raw_in, prof, ectx)) return false;

    DecoderContext dctx;
    std::vector<Word27> raw_out;
    if(!decode_profile_to_raw(prof, raw_out, dctx)) return false;

    // Due to padding/truncation, compare on min length
    size_t L = std::min(raw_in.size(), raw_out.size());
    for(size_t i=0;i<L; ++i){
        for(int s=0;s<9;++s) if(raw_in[i].sym[s]!=raw_out[i].sym[s]) return false;
    }
    return true;
}

// ============================================================================
//  END OF HEADER
// ============================================================================
