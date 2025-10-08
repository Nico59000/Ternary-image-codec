// ============================================================================
//  File: include/ternary_image_codec_v6_min.hpp  (part 1/2)
//  Minimal core for the v6 codec (compact docs). Provides:
//   - GF(27) arithmetic (p(x)=x^3+2x+1)
//   - RS(26,k) encoder/decoder (k∈{24,22,20,18})
//   - Superframe header (27 symbols) with ternary CRC-12
//   - RAW <-> Word27 packing (2 pixels/word example)
//   - Subword modes (S27/S24/S21/S18/S15), centering helpers
//   - 2D interleave (boustrophedon), UEP bands, beacon, scrambler
//   - EncoderContext/DecoderContext + profile encode/decode
//  Keep comments concise to avoid canvas limits.
// ============================================================================
#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <algorithm>
#include <random>

// ---- Base trits/symbols ----
using UTrit = uint8_t;   // 0..2
using GF27  = uint8_t;   // 0..26 (3^3)
static constexpr int TRITS_PER_WORD=27, SYM_PER_WORD=9;
inline GF27 pack3(UTrit a,UTrit b,UTrit c)
{
    return (GF27)(a + 3*b + 9*c);
}
inline std::array<UTrit,3> unpack3(GF27 s)
{
    return { (UTrit)(s%3), (UTrit)((s/3)%3), (UTrit)((s/9)%3) };
}

// ---- Profiles / RS params ----
enum class ProfileID: uint8_t { RAW_MODE=0xFF, P1_RS26_24=0, P2_RS26_22=1, P3_RS26_20=2, P4_RS26_18=3, P5_RS26_22_2D=4 };
struct RSParams
{
    uint8_t n=26,k=22;
};
inline RSParams rs_params_for(ProfileID p)
{
    switch(p)
    {
    case ProfileID::P1_RS26_24:
        return{26,24};
    case ProfileID::P2_RS26_22:
        return{26,22};
    case ProfileID::P3_RS26_20:
        return{26,20};
    case ProfileID::P4_RS26_18:
        return{26,18};
    case ProfileID::P5_RS26_22_2D:
        return{26,22};
    default:
        return{26,22};
    }
}

// ---- UEP bands / tiles / scrambler / beacon / coset ----
static constexpr int NUM_BANDS=9;
struct UEPLayout
{
    std::array<uint8_t,NUM_BANDS> band_profile{};
};
inline void uep_uniform(UEPLayout& u, uint8_t idx=1)
{
    for(int i=0; i<NUM_BANDS; ++i) u.band_profile[i]=idx%4;
}
inline void uep_luma_priority(UEPLayout& u)
{
    for(int i=0; i<9; ++i) u.band_profile[i]=1;
    u.band_profile[0]=u.band_profile[3]=u.band_profile[6]=2;
}
struct Tile2D
{
    uint16_t w=0,h=0;
};
struct ScramblerSeed
{
    uint32_t a=1,b=1,s0=1;
};
inline GF27 scramble_symbol(GF27 s,const ScramblerSeed& seed,uint32_t& st)
{
    st=((seed.a*st)+seed.b)%3;
    auto d=unpack3(s);
    for(auto& x:d)x=(UTrit)((x+st)%3);
    return pack3(d[0],d[1],d[2]);
}
inline GF27 descramble_symbol(GF27 s,const ScramblerSeed& seed,uint32_t& st)
{
    st=((seed.a*st)+seed.b)%3;
    auto d=unpack3(s);
    for(auto& x:d)x=(UTrit)((3+x-(st%3))%3);
    return pack3(d[0],d[1],d[2]);
}
struct SparseBeaconCfg
{
    uint32_t words_period=0;
    uint8_t band_slot=0;
    bool enabled=false;
};
struct BeaconPayload
{
    ProfileID profile;
    uint16_t frame_seq_mod;
    uint8_t health_flags;
};
inline GF27 encode_beacon_symbol(const BeaconPayload& b)
{
    uint8_t p=(uint8_t)b.profile;
    uint8_t s=(uint8_t)(b.frame_seq_mod%5);
    uint8_t h=(uint8_t)(b.health_flags%3);
    return (GF27)((p + 5*s + 15*h)%27);
}
enum class CosetID: uint8_t { C0=0,C1=1,C2=2 };

// ---- Subword & centering ----
enum class SubwordMode: uint8_t { S27=27,S24=24,S21=21,S18=18,S15=15 };
inline int payload_len_for(SubwordMode m)
{
    return (int)m;
}
struct StdRes
{
    uint16_t w,h;
};
inline StdRes std_res_for(SubwordMode m)
{
    switch(m)
    {
    case SubwordMode::S27:
        return{7680,4320};
    case SubwordMode::S24:
        return{3840,2160};
    case SubwordMode::S21:
        return{1920,1080};
    case SubwordMode::S18:
        return{1280,720};
    case SubwordMode::S15:
        return{854,480};
    }
    return{7680,4320};
}
struct ActiveWindow
{
    uint32_t x0,y0,w,h;
};
inline ActiveWindow centered_window(SubwordMode m)
{
    auto B=std_res_for(SubwordMode::S27);
    auto T=std_res_for(m);
    return { (uint32_t)((B.w-T.w)/2), (uint32_t)((B.h-T.h)/2), T.w, T.h };
}

// ---- Header + ternary CRC12 ----
struct SuperframeHeader
{
    uint16_t magic=0x0A2;
    uint8_t version=1;
    ProfileID profile=ProfileID::P2_RS26_22;
    UEPLayout uep{};
    Tile2D tile{};
    ScramblerSeed seed{};
    uint32_t band_map_hash=0;
    uint32_t frame_seq=0;
    uint32_t reserved=0;
    uint32_t crc3m=0;
    SparseBeaconCfg beacon{};
    SubwordMode subword=SubwordMode::S27;
    bool centered=true;
    CosetID coset=CosetID::C0;
};
struct HeaderPack
{
    std::array<GF27,27> symbols{};
};
struct CRC3
{
    static constexpr int L=12;
    static inline void rem12(const std::vector<UTrit>& msg,std::array<UTrit,L>& out)
    {
        std::array<UTrit,L> r{};
        r.fill(0);
        auto step=[&](UTrit in)
        {
            UTrit fb=(UTrit)((in + r[L-1])%3);
            std::array<UTrit,L> nx{};
            nx[0]=fb;
            nx[1]=r[0];
            nx[2]=r[1];
            nx[3]=(UTrit)((r[2]+fb)%3);
            nx[4]=(UTrit)((r[3]+fb)%3);
            nx[5]=r[4];
            nx[6]=r[5];
            nx[7]=(UTrit)((r[6]+fb)%3);
            nx[8]=r[7];
            nx[9]=r[8];
            nx[10]=r[9];
            nx[11]=r[10];
            r=nx;
        };
        for(auto t:msg) step(t);
        for(int i=0; i<L; ++i) step(0);
        out=r;
    }
};
struct HeaderCodec
{
    static HeaderPack pack(const SuperframeHeader& h)
    {
        HeaderPack p{};
        auto at=[&](int i,GF27 v)
        {
            p.symbols[i]=(GF27)(v%27);
        };
        at(0,h.magic%27);
        at(1,(h.magic/27)%27);
        at(2,h.version%27);
        at(3,(GF27)h.profile);
        auto pack3bands=[&]()
        {
            uint32_t u0=0,u1=0,u2=0;
            for(int i=0; i<3; ++i) u0=u0*3+(h.uep.band_profile[i]%3);
            for(int i=3; i<6; ++i) u1=u1*3+(h.uep.band_profile[i]%3);
            for(int i=6; i<9; ++i) u2=u2*3+(h.uep.band_profile[i]%3);
            at(4,u0);
            at(5,u1);
            at(6,u2);
        };
        pack3bands();
        at(7,h.tile.w%27);
        at(8,h.tile.h%27);
        at(9,h.seed.a%27);
        at(10,h.seed.b%27);
        at(11,h.seed.s0%27);
        uint8_t sub=0;
        switch(h.subword)
        {
        case SubwordMode::S27:
            sub=0;
            break;
        case SubwordMode::S24:
            sub=1;
            break;
        case SubwordMode::S21:
            sub=2;
            break;
        case SubwordMode::S18:
            sub=3;
            break;
        case SubwordMode::S15:
            sub=4;
            break;
        }
        at(12,(GF27)((sub + 9*(h.centered?1:0))%27));
        at(13,h.band_map_hash%27);
        at(14,(h.band_map_hash/27)%27);
        at(15,(h.band_map_hash/729)%27);
        at(16,(GF27)((uint8_t)h.coset%3));
        at(17,h.frame_seq%27);
        at(18,(h.frame_seq/27)%27);
        at(19,(h.frame_seq/729)%27);
        at(20,0);
        at(21,0);
        at(22,0);
        at(23,h.beacon.enabled?1:0);
        at(24,h.beacon.band_slot%27);
        at(25,(GF27)std::min<uint32_t>(h.beacon.words_period,26));
        at(26,0);
        std::vector<UTrit> tr;
        tr.reserve(27*3);
        auto push=[&](int i)
        {
            if(i==20||i==21||i==22||i==26) return;
            auto d=unpack3(p.symbols[i]);
            tr.insert(tr.end(), d.begin(), d.end());
        };
        for(int i=0; i<27; ++i) push(i);
        std::array<UTrit,CRC3::L> r{};
        CRC3::rem12(tr,r);
        auto RSym=[&](int i)
        {
            return pack3(r[i*3+0],r[i*3+1],r[i*3+2]);
        };
        at(20,RSym(0));
        at(21,RSym(1));
        at(22,RSym(2));
        at(26,RSym(3));
        return p;
    }
    static bool check(const HeaderPack& p)
    {
        std::vector<UTrit> tr;
        tr.reserve(27*3);
        auto push=[&](int i)
        {
            if(i==20||i==21||i==22||i==26) return;
            auto d=unpack3(p.symbols[i]);
            tr.insert(tr.end(), d.begin(), d.end());
        };
        for(int i=0; i<27; ++i) push(i);
        std::array<UTrit,CRC3::L> r{};
        CRC3::rem12(tr,r);
        std::array<UTrit,CRC3::L> h{};
        auto up=[&](int idx,int off)
        {
            auto d=unpack3(p.symbols[idx]);
            h[off+0]=d[0];
            h[off+1]=d[1];
            h[off+2]=d[2];
        };
        up(20,0);
        up(21,3);
        up(22,6);
        up(26,9);
        return (r==h);
    }
    static SuperframeHeader unpack(const HeaderPack& p)
    {
        SuperframeHeader h{};
        auto rd=[&](int i)
        {
            return p.symbols[i]%27;
        };
        h.magic=(uint16_t)(rd(0)+27*rd(1));
        h.version=(uint8_t)rd(2);
        h.profile=(ProfileID)(rd(3)%5);
        auto dec3=[&](uint32_t v,int off)
        {
            uint8_t t0=v%3;
            v/=3;
            uint8_t t1=v%3;
            v/=3;
            uint8_t t2=v%3;
            h.uep.band_profile[off+0]=t0;
            h.uep.band_profile[off+1]=t1;
            h.uep.band_profile[off+2]=t2;
        };
        dec3(rd(4),0);
        dec3(rd(5),3);
        dec3(rd(6),6);
        h.tile.w=(uint16_t)rd(7);
        h.tile.h=(uint16_t)rd(8);
        h.seed.a=rd(9);
        h.seed.b=rd(10);
        h.seed.s0=rd(11);
        {
            uint32_t v=rd(12)%27;
            uint8_t cen=(v/9)%3;
            uint8_t sub=v%9;
            switch(sub)
            {
            case 0:
                h.subword=SubwordMode::S27;
                break;
            case 1:
                h.subword=SubwordMode::S24;
                break;
            case 2:
                h.subword=SubwordMode::S21;
                break;
            case 3:
                h.subword=SubwordMode::S18;
                break;
            case 4:
                h.subword=SubwordMode::S15;
                break;
            default:
                h.subword=SubwordMode::S27;
            }
            h.centered=(cen!=0);
        }
        h.band_map_hash=rd(13)+27*rd(14)+729*rd(15);
        h.coset=(CosetID)(rd(16)%3);
        h.frame_seq=rd(17)+27*rd(18)+729*rd(19);
        h.beacon.enabled=rd(23)!=0;
        h.beacon.band_slot=rd(24)%9;
        h.beacon.words_period=rd(25);
        return h;
    }
};

// ---- GF(27) arithmetic (poly x^3+2x+1) ----
inline GF27 gf27_add(GF27 a,GF27 b)
{
    UTrit a0=a%3,a1=(a/3)%3,a2=(a/9)%3;
    UTrit b0=b%3,b1=(b/3)%3,b2=(b/9)%3;
    return (GF27)(((a0+b0)%3)+3*((a1+b1)%3)+9*((a2+b2)%3));
}
inline GF27 gf27_sub(GF27 a,GF27 b)
{
    auto s=[&](int x,int y)
    {
        int z=x-y;
        z%=3;
        if(z<0) z+=3;
        return z;
    };
    UTrit a0=a%3,a1=(a/3)%3,a2=(a/9)%3;
    UTrit b0=b%3,b1=(b/3)%3,b2=(b/9)%3;
    return (GF27)( s(a0,b0) + 3*s(a1,b1) + 9*s(a2,b2) );
}
inline GF27 gf27_mul_poly(GF27 a,GF27 b)
{
    if(a==0||b==0) return 0;
    int a0=a%3,a1=(a/3)%3,a2=(a/9)%3;
    int b0=b%3,b1=(b/3)%3,b2=(b/9)%3;
    int r0=(a0*b0)%3, r1=(a0*b1+a1*b0)%3, r2=(a0*b2+a1*b1+a2*b0)%3, r3=(a1*b2+a2*b1)%3, r4=(a2*b2)%3;
    r1=(r1+r3)%3;
    r0=(r0+2*r3)%3;
    r2=(r2+r4)%3;
    r1=(r1+2*r4)%3;
    return (GF27)(r0+3*r1+9*r2);
}
struct GF27Tables
{
    std::array<GF27,26*3> exp{};
    std::array<int16_t,27> log{};
    std::array<GF27,27*27> mul{};
    std::array<GF27,27> inv{};
    GF27 primitive=0;
};
struct GF27Context
{
    GF27Tables tab{};
    int order_of(GF27 g) const
    {
        if(g==0||g==1) return -1;
        GF27 x=1;
        for(int i=1; i<=26; ++i)
        {
            x=gf27_mul_poly(x,g);
            if(x==1) return i;
        }
        return -1;
    }
    void init()
    {
        GF27 prim=0;
        for(GF27 c=2; c<27; ++c)
        {
            if(order_of(c)==26)
            {
                prim=c;
                break;
            }
        }
        if(prim==0) prim=3;
        tab.primitive=prim;
        tab.log.fill(-1);
        tab.exp[0]=1;
        tab.log[1]=0;
        for(int i=1; i<26; ++i)
        {
            tab.exp[i]=gf27_mul_poly(tab.exp[i-1],prim);
            tab.log[ tab.exp[i] ]=i;
        }
        for(int i=26; i<26*3; ++i) tab.exp[i]=tab.exp[i-26];
        for(int a=0; a<27; ++a) for(int b=0; b<27; ++b) tab.mul[a*27+b]=gf27_mul_poly((GF27)a,(GF27)b);
        tab.inv[0]=0;
        for(int a=1; a<27; ++a)
        {
            int la=tab.log[a];
            int ii=(26-la)%26;
            tab.inv[a]=tab.exp[ii];
        }
    }
    inline GF27 add(GF27 a,GF27 b) const
    {
        return gf27_add(a,b);
    } inline GF27 sub(GF27 a,GF27 b) const
    {
        return gf27_sub(a,b);
    } inline GF27 mul(GF27 a,GF27 b) const
    {
        return tab.mul[a*27+b];
    } inline GF27 inv(GF27 a) const
    {
        return tab.inv[a];
    } inline GF27 pow_alpha(int e) const
    {
        int m=(e%26+26)%26;
        return tab.exp[m];
    } inline int log(GF27 a) const
    {
        return tab.log[a];
    }
};

// ---- RS(26,k) codec ----
struct RSCodec
{
    GF27Context* gf=nullptr;
    RSParams params{};
    std::vector<GF27> g;
    void init(GF27Context* c, RSParams p)
    {
        gf=c;
        params=p;
        build_gen();
    }
    void build_gen()
    {
        int r=params.n-params.k;
        g.assign(1,(GF27)1);
        for(int i=1; i<=r; ++i)
        {
            std::vector<GF27> ng(g.size()+1,0);
            GF27 root=gf->pow_alpha(i);
            for(size_t j=0; j<g.size(); ++j)
            {
                ng[j]   = gf->sub(ng[j], gf->mul(g[j], root));
                ng[j+1] = gf->add(ng[j+1], g[j]);
            }
            g.swap(ng);
        }
    }
    bool encode_block(const GF27* data_k, GF27* out_n) const
    {
        int r=params.n-params.k;
        std::vector<GF27> T(params.n,0);
        for(int i=0; i<params.k; ++i) T[i]=data_k[i];
        for(int i=0; i<params.k; ++i)
        {
            GF27 coef=T[i];
            if(coef==0) continue;
            for(int j=0; j<=r; ++j)
            {
                GF27 prod=gf->mul(g[j],coef);
                T[i+j]=gf->sub(T[i+j],prod);
            }
        }
        for(int i=0; i<params.k; ++i) out_n[i]=data_k[i];
        for(int i=0; i<r; ++i) out_n[params.k+i]=T[params.k+i];
        return true;
    }
    GF27 poly_eval(const std::vector<GF27>& p, GF27 x) const
    {
        GF27 acc=0;
        for(int i=(int)p.size()-1; i>=0; --i)
        {
            acc=gf->mul(acc,x);
            acc=gf->add(acc,p[i]);
        }
        return acc;
    }
    bool decode_block(GF27* inout_n, GF27* out_k) const
    {
        const int n=params.n,k=params.k,r=n-k,t=r/2;
        std::vector<GF27> S(r,0);
        bool all0=true;
        for(int j=0; j<r; ++j)
        {
            GF27 acc=0;
            for(int i=0; i<n; ++i)
            {
                GF27 xpow=gf->pow_alpha(((j+1)*i)%26);
                acc=gf->add(acc, gf->mul(inout_n[i],xpow));
            }
            S[j]=acc;
            if(acc!=0) all0=false;
        }
        if(all0)
        {
            for(int i=0; i<k; ++i) out_k[i]=inout_n[i];
            return true;
        }
        std::vector<GF27> sigma(1,1), B(1,1);
        int L=0,m=1;
        for(int nS=0; nS<r; ++nS)
        {
            GF27 delta=S[nS];
            for(int i=1; i<=L; ++i) if(i<(int)sigma.size()) delta=gf->add(delta, gf->mul(sigma[i], S[nS-i]));
            if(delta!=0)
            {
                auto T=sigma;
                std::vector<GF27> dB(B.size());
                for(size_t i=0; i<B.size(); ++i) dB[i]=gf->mul(delta,B[i]);
                std::vector<GF27> xmdB(m+dB.size(),0);
                for(size_t i=0; i<dB.size(); ++i) xmdB[m+i]=dB[i];
                size_t nd=std::max(sigma.size(), xmdB.size());
                std::vector<GF27> ns(nd,0);
                for(size_t i=0; i<nd; ++i)
                {
                    GF27 a=(i<sigma.size()?sigma[i]:0), b=(i<xmdB.size()?xmdB[i]:0);
                    ns[i]=gf->sub(a,b);
                }
                sigma.swap(ns);
                if(2*L<=nS)
                {
                    GF27 invd=gf->inv(delta);
                    B.resize(T.size());
                    for(size_t i=0; i<T.size(); ++i) B[i]=gf->mul(T[i],invd);
                    L=nS+1-L;
                    m=1;
                }
                else
                {
                    m+=1;
                }
            }
            else
            {
                m+=1;
            }
        }
        std::vector<GF27> Sx(r+1,0);
        for(int j=0; j<r; ++j) Sx[j]=S[j];
        std::vector<GF27> Omega(Sx.size()+sigma.size()-1,0);
        for(size_t i=0; i<Sx.size(); ++i) for(size_t j=0; j<sigma.size(); ++j) Omega[i+j]=gf->add(Omega[i+j], gf->mul(Sx[i],sigma[j]));
        if((int)Omega.size()>r) Omega.resize(r);
        std::vector<int> err_pos;
        err_pos.reserve(t);
        for(int i=0; i<n; ++i)
        {
            GF27 x=gf->pow_alpha((-i)%26);
            GF27 acc=0;
            for(int d=(int)sigma.size()-1; d>=0; --d)
            {
                acc=gf->mul(acc,x);
                acc=gf->add(acc, sigma[d]);
            }
            if(acc==0) err_pos.push_back(i);
        }
        if((int)err_pos.size()>t) return false;
        std::vector<GF27> sigmap((sigma.size()>1)?sigma.size()-1:1,0);
        if(sigma.size()>=2)
        {
            for(size_t i=1; i<sigma.size(); ++i)
            {
                int im=(int)(i%3);
                if(im==0) sigmap[i-1]=0;
                else if(im==1) sigmap[i-1]=sigma[i];
                else
                {
                    GF27 a=sigma[i];
                    UTrit a0=a%3,a1=(a/3)%3,a2=(a/9)%3;
                    auto m2=[&](UTrit t)->UTrit{ return (UTrit)((2*t)%3); };
                    sigmap[i-1]=(GF27)(m2(a0)+3*m2(a1)+9*m2(a2));
                }
            }
        }
        for(int pos: err_pos)
        {
            GF27 Xin=gf->pow_alpha((-pos)%26);
            GF27 num=0, den=0;
            for(int d=(int)Omega.size()-1; d>=0; --d)
            {
                num=gf->mul(num,Xin);
                num=gf->add(num,Omega[d]);
            }
            for(int d=(int)sigmap.size()-1; d>=0; --d)
            {
                den=gf->mul(den,Xin);
                den=gf->add(den,sigmap[d]);
            }
            if(den==0) return false;
            GF27 mag=gf->mul( gf->sub((GF27)0,num), gf->inv(den) );
            inout_n[pos]=gf->add(inout_n[pos],mag);
        }
        for(int i=0; i<k; ++i) out_k[i]=inout_n[i];
        return true;
    }
};

// ---- RAW packing example (2 pixels/word) ----
struct Word27
{
    std::array<GF27,SYM_PER_WORD> sym{};
};
struct PixelYCbCrQuant
{
    uint16_t Yq=0;
    int16_t Cbq=0, Crq=0;
};
inline void i2tr(uint32_t v,int w,std::array<UTrit,27>&d,int s)
{
    for(int i=0; i<w; ++i)
    {
        d[s+i]=(UTrit)(v%3);
        v/=3;
    }
}
inline uint32_t tr2i(const std::array<UTrit,27>&d,int w,int s)
{
    uint32_t val=0,p=1;
    for(int i=0; i<w; ++i)
    {
        val+=p*d[s+i];
        p*=3;
    }
    return val;
}
inline void pack_two_pixels(const PixelYCbCrQuant& a,const PixelYCbCrQuant& b,Word27& w)
{
    std::array<UTrit,27> T{};
    T.fill(0);
    i2tr(a.Yq,5,T,0);
    i2tr((uint32_t)(a.Cbq+40),4,T,5);
    i2tr((uint32_t)(a.Crq+40),4,T,9);
    i2tr(b.Yq,5,T,13);
    i2tr((uint32_t)(b.Cbq+40),4,T,18);
    i2tr((uint32_t)(b.Crq+40),4,T,22);
    T[26]=0;
    for(int s=0; s<9; ++s) w.sym[s]=pack3(T[s*3+0],T[s*3+1],T[s*3+2]);
}
inline void unpack_two_pixels(const Word27& w,PixelYCbCrQuant& a,PixelYCbCrQuant& b)
{
    std::array<UTrit,27> T{};
    for(int s=0; s<9; ++s)
    {
        auto d=unpack3(w.sym[s]);
        T[s*3]=d[0];
        T[s*3+1]=d[1];
        T[s*3+2]=d[2];
    }
    a.Yq=tr2i(T,5,0);
    a.Cbq=(int16_t)tr2i(T,4,5)-40;
    a.Crq=(int16_t)tr2i(T,4,9)-40;
    b.Yq=tr2i(T,5,13);
    b.Cbq=(int16_t)tr2i(T,4,18)-40;
    b.Crq=(int16_t)tr2i(T,4,22)-40;
}
inline bool encode_raw_pixels_to_words(const std::vector<PixelYCbCrQuant>& px,std::vector<Word27>& out)
{
    out.clear();
    out.reserve((px.size()+1)/2);
    for(size_t i=0; i<px.size(); i+=2)
    {
        Word27 w{};
        pack_two_pixels(px[i], (i+1<px.size()?px[i+1]:PixelYCbCrQuant{}), w);
        out.push_back(w);
    }
    return true;
}
inline bool decode_raw_words_to_pixels(const std::vector<Word27>& in,std::vector<PixelYCbCrQuant>& out)
{
    out.clear();
    out.reserve(in.size()*2);
    for(const auto& w: in)
    {
        PixelYCbCrQuant a{},b{};
        unpack_two_pixels(w,a,b);
        out.push_back(a);
        out.push_back(b);
    }
    return true;
}

// ---- Interleave 2D (boustrophedon) ----
inline void interleave2D_boustrophedon(std::vector<GF27>& syms, Tile2D tile)
{
    if(!tile.w||!tile.h) return;
    size_t A=(size_t)tile.w*tile.h;
    if(A==0) return;
    std::vector<GF27> out;
    out.reserve(syms.size());
    size_t i=0;
    while(i<syms.size())
    {
        size_t take=std::min(A, syms.size()-i);
        std::vector<GF27> tmp(syms.begin()+i, syms.begin()+i+take);
        for(uint16_t r=0; r<tile.h; ++r)
        {
            if(r%2==0)
            {
                for(uint16_t c=0; c<tile.w && (size_t)r*tile.w+c<take; ++c) out.push_back(tmp[r*tile.w+c]);
            }
            else
            {
                for(int c=tile.w-1; c>=0; --c)
                {
                    size_t idx=(size_t)r*tile.w+c;
                    if(idx<take) out.push_back(tmp[idx]);
                }
            }
        }
        i+=take;
    }
    syms.swap(out);
}
inline void deinterleave2D_boustrophedon(std::vector<GF27>& syms, Tile2D tile)
{
    if(!tile.w||!tile.h) return;
    size_t A=(size_t)tile.w*tile.h;
    if(A==0) return;
    std::vector<GF27> out;
    out.reserve(syms.size());
    size_t i=0;
    while(i<syms.size())
    {
        size_t take=std::min(A, syms.size()-i);
        std::vector<GF27> tmp(take);
        size_t k=0;
        for(uint16_t r=0; r<tile.h; ++r)
        {
            if(r%2==0)
            {
                for(uint16_t c=0; c<tile.w && (size_t)r*tile.w+c<take; ++c) tmp[r*tile.w+c]=syms[i+(k++)];
            }
            else
            {
                for(int c=tile.w-1; c>=0; --c)
                {
                    size_t idx=(size_t)r*tile.w+c;
                    if(idx<take) tmp[idx]=syms[i+(k++)];
                }
            }
        }
        out.insert(out.end(), tmp.begin(), tmp.end());
        i+=take;
    }
    syms.swap(out);
}

// ---- Subword helpers ----
inline void extract_subword_trits_from_word(const Word27& w,int N,std::array<UTrit,27>& out)
{
    for(int s=0; s<9; ++s)
    {
        auto d=unpack3(w.sym[s]);
        out[s*3]=d[0];
        out[s*3+1]=d[1];
        out[s*3+2]=d[2];
    }
    (void)N;
}
inline void inject_subword_trits_into_word(const UTrit* inN,int N,Word27& w,UTrit fill=0)
{
    std::array<UTrit,27> T{};
    for(int i=0; i<N; ++i) T[i]=inN[i];
    for(int i=N; i<27; ++i) T[i]=fill;
    for(int s=0; s<9; ++s) w.sym[s]=pack3(T[s*3+0],T[s*3+1],T[s*3+2]);
}
inline void extract_subword_stream_from_words(const std::vector<Word27>& words,int N,std::vector<UTrit>& out)
{
    out.clear();
    out.reserve(words.size()*N);
    std::array<UTrit,27> T{};
    for(const auto& w: words)
    {
        extract_subword_trits_from_word(w,N,T);
        for(int i=0; i<N; ++i) out.push_back(T[i]);
    }
}
inline void build_words_from_subword_stream(const std::vector<UTrit>& in,int N,std::vector<Word27>& out,UTrit fill=0)
{
    out.clear();
    size_t idx=0;
    while(idx<in.size())
    {
        Word27 w{};
        UTrit buf[27] {};
        int take=std::min(N,(int)(in.size()-idx));
        for(int i=0; i<take; ++i) buf[i]=in[idx+i];
        inject_subword_trits_into_word(buf,N,w,fill);
        out.push_back(w);
        idx+=take;
    }
}

// ---- Contexts / API ----
struct EncoderConfig
{
    ProfileID profile=ProfileID::P2_RS26_22;
    UEPLayout uep{};
    Tile2D tile{};
    ScramblerSeed seed{1,1,1};
    SparseBeaconCfg beacon{};
    uint32_t superframe_words=8192;
    SubwordMode subword=SubwordMode::S27;
    bool centered=true;
    CosetID coset=CosetID::C0;
};
struct DecoderConfigSeen
{
    ProfileID profile=ProfileID::P2_RS26_22;
    UEPLayout uep{};
    Tile2D tile{};
    ScramblerSeed seed{1,1,1};
    SparseBeaconCfg beacon{};
    SubwordMode subword=SubwordMode::S27;
    bool centered=true;
    CosetID coset=CosetID::C0;
};
struct EncoderContext
{
    GF27Context gf;
    RSCodec rs_p1,rs_p2,rs_p3,rs_p4,rs_hdr;
    EncoderConfig cfg;
    EncoderContext()
    {
        gf.init();
        rs_p1.init(&gf,rs_params_for(ProfileID::P1_RS26_24));
        rs_p2.init(&gf,rs_params_for(ProfileID::P2_RS26_22));
        rs_p3.init(&gf,rs_params_for(ProfileID::P3_RS26_20));
        rs_p4.init(&gf,rs_params_for(ProfileID::P4_RS26_18));
        rs_hdr.init(&gf,RSParams{26,18});
        uep_uniform(cfg.uep,1);
    }
};
struct DecoderContext
{
    GF27Context gf;
    RSCodec rs_p1,rs_p2,rs_p3,rs_p4,rs_hdr;
    DecoderConfigSeen cfg_last_seen;
    DecoderContext()
    {
        gf.init();
        rs_p1.init(&gf,rs_params_for(ProfileID::P1_RS26_24));
        rs_p2.init(&gf,rs_params_for(ProfileID::P2_RS26_22));
        rs_p3.init(&gf,rs_params_for(ProfileID::P3_RS26_20));
        rs_p4.init(&gf,rs_params_for(ProfileID::P4_RS26_18));
        rs_hdr.init(&gf,RSParams{26,18});
        uep_uniform(cfg_last_seen.uep,1);
    }
};

inline bool read_and_decode_header_from_words(const std::vector<Word27>& words,size_t& cursor,SuperframeHeader& out,RSCodec& rs_hdr)
{
    if(cursor+6>words.size()) return false;
    std::vector<GF27> sy;
    sy.reserve(54);
    for(int w=0; w<6; ++w) for(int s=0; s<9; ++s) sy.push_back(words[cursor+w].sym[s]);
    cursor+=6;
    GF27 A[26] {},B[26] {};
    for(int i=0; i<26; ++i) A[i]=sy[i];
    for(int i=0; i<26; ++i) B[i]=sy[26+i];
    GF27 a18[18] {}, b18[18] {};
    if(!rs_hdr.decode_block(A,a18)) return false;
    if(!rs_hdr.decode_block(B,b18)) return false;
    HeaderPack hp{};
    for(int i=0; i<18; ++i) hp.symbols[i]=a18[i];
    for(int i=0; i<9; ++i) hp.symbols[18+i]=b18[i];
    if(!HeaderCodec::check(hp)) return false;
    out=HeaderCodec::unpack(hp);
    return true;
}
inline void descramble_words_inplace(std::vector<Word27>& words,const SuperframeHeader& hdr)
{
    std::vector<GF27> sy;
    sy.reserve(words.size()*9);
    for(const auto& w:words) for(auto s:w.sym) sy.push_back(s);
    uint32_t st=hdr.seed.s0%3;
    for(auto& s:sy) s=descramble_symbol(s,hdr.seed,st);
    size_t k=0;
    for(auto& w:words) for(int s=0; s<9; ++s) w.sym[s]=sy[k++];
}
inline bool demap_and_rsdecode_bands_from_words(const std::vector<Word27>& body,std::vector<GF27>& out_syms,const SuperframeHeader& hdr,RSCodec& r1,RSCodec& r2,RSCodec& r3,RSCodec& r4)
{
    std::array<std::vector<GF27>,9> bands;
    size_t wi=0;
    bool skip=(hdr.beacon.enabled && hdr.beacon.words_period>0);
    for(const auto& w:body)
    {
        for(int slot=0; slot<9; ++slot)
        {
            if(skip && (wi%hdr.beacon.words_period)==0 && slot==hdr.beacon.band_slot) continue;
            bands[slot].push_back(w.sym[slot]);
        }
        ++wi;
    }
    out_syms.clear();
    for(int b=0; b<9; ++b)
    {
        RSCodec* r=&r2;
        switch(hdr.uep.band_profile[b]%4)
        {
        case 0:
            r=&r1;
            break;
        case 1:
            r=&r2;
            break;
        case 2:
            r=&r3;
            break;
        case 3:
            r=&r4;
            break;
        }
        RSParams p=r->params;
        size_t j=0;
        while(j+p.n<=bands[b].size())
        {
            GF27 nbuf[26] {},kbuf[26] {};
            for(int i=0; i<p.n; ++i) nbuf[i]=bands[b][j+i];
            if(!r->decode_block(nbuf,kbuf)) return false;
            for(int i=0; i<p.k; ++i) out_syms.push_back(kbuf[i]);
            j+=p.n;
        }
    }
    return true;
}

inline bool decode_profile_to_raw(const std::vector<Word27>& in,std::vector<Word27>& out,DecoderContext& dctx)
{
    out.clear();
    if(dctx.cfg_last_seen.profile==ProfileID::RAW_MODE)
    {
        out=in;
        return true;
    }
    size_t cur=0;
    SuperframeHeader hdr{};
    if(!read_and_decode_header_from_words(in,cur,hdr,dctx.rs_hdr)) return false;
    dctx.cfg_last_seen.profile=hdr.profile;
    dctx.cfg_last_seen.uep=hdr.uep;
    dctx.cfg_last_seen.tile=hdr.tile;
    dctx.cfg_last_seen.seed=hdr.seed;
    dctx.cfg_last_seen.beacon=hdr.beacon;
    dctx.cfg_last_seen.subword=hdr.subword;
    dctx.cfg_last_seen.centered=hdr.centered;
    dctx.cfg_last_seen.coset=hdr.coset;
    std::vector<Word27> body(in.begin()+cur,in.end());
    descramble_words_inplace(body,hdr);
    std::vector<GF27> use;
    if(!demap_and_rsdecode_bands_from_words(body,use,hdr,dctx.rs_p1,dctx.rs_p2,dctx.rs_p3,dctx.rs_p4)) return false;
    if(hdr.profile==ProfileID::P5_RS26_22_2D && hdr.tile.w && hdr.tile.h)
    {
        deinterleave2D_boustrophedon(use,hdr.tile);
    }
    std::vector<UTrit> tr;
    tr.reserve(use.size()*3);
    for(auto s:use)
    {
        auto d=unpack3(s);
        tr.insert(tr.end(), d.begin(), d.end());
    }
    size_t idx=0;
    while(idx+26<=tr.size())
    {
        std::array<UTrit,27> T{};
        for(int i=0; i<26; ++i) T[i]=tr[idx+i];
        T[26]=0;
        Word27 w{};
        for(int s=0; s<9; ++s) w.sym[s]=pack3(T[s*3],T[s*3+1],T[s*3+2]);
        out.push_back(w);
        idx+=26;
    }
    return true;
}

inline bool encode_profile_from_raw(const std::vector<Word27>& in,std::vector<Word27>& out,EncoderContext& ectx)
{
    out.clear();
    if(ectx.cfg.profile==ProfileID::RAW_MODE)
    {
        out=in;
        return true;
    }
    std::vector<GF27> sy;
    sy.reserve(in.size()*8);
    std::array<UTrit,3> carry{};
    int clen=0;
    for(const auto& w:in)
    {
        std::array<UTrit,27> T{};
        for(int s=0; s<9; ++s)
        {
            auto d=unpack3(w.sym[s]);
            T[s*3]=d[0];
            T[s*3+1]=d[1];
            T[s*3+2]=d[2];
        }
        int i=0;
        if(clen>0)
        {
            while(clen<3 && i<26) carry[clen++]=T[i++];
            if(clen==3)
            {
                sy.push_back(pack3(carry[0],carry[1],carry[2]));
                clen=0;
            }
        }
        for(; i+2<26; i+=3) sy.push_back(pack3(T[i],T[i+1],T[i+2]));
        for(; i<26; ++i) carry[clen++]=T[i];
    }
    if(clen>0)
    {
        while(clen<3) carry[clen++]=0;
        sy.push_back(pack3(carry[0],carry[1],carry[2]));
    }
    if(ectx.cfg.profile==ProfileID::P5_RS26_22_2D && ectx.cfg.tile.w && ectx.cfg.tile.h)
    {
        interleave2D_boustrophedon(sy,ectx.cfg.tile);
    }
    std::array<std::vector<GF27>,9> bands;
    for(size_t i=0; i<sy.size(); ++i) bands[i%9].push_back(sy[i]);
    auto rsc_for=[&](int b)->RSCodec* { switch(ectx.cfg.uep.band_profile[b]%4)
{
case 0:
    return &ectx.rs_p1;
case 1:
    return &ectx.rs_p2;
case 2:
    return &ectx.rs_p3;
default:
    return &ectx.rs_p4;
}
                                  };
std::vector<GF27> body;
for(int b=0; b<9; ++b)
    {
        RSCodec* r=rsc_for(b);
        RSParams p=r->params;
        size_t j=0;
        while(j+p.k<=bands[b].size())
        {
            GF27 kbuf[26] {}, nbuf[26] {};
            for(int i=0; i<p.k; ++i) kbuf[i]=bands[b][j+i];
            r->encode_block(kbuf,nbuf);
            for(int i=0; i<p.n; ++i) body.push_back(nbuf[i]);
            j+=p.k;
        }
    }
    uint32_t st=ectx.cfg.seed.s0%3;
    for(auto& s:body) s=scramble_symbol(s,ectx.cfg.seed,st);
    if(ectx.cfg.beacon.enabled && ectx.cfg.beacon.words_period>0)
    {
        std::vector<GF27> sy2;
        sy2.reserve(body.size()+body.size()/(9*ectx.cfg.beacon.words_period)+9);
        size_t word_idx=0,k=0;
        while(k<body.size())
        {
            bool ins=((word_idx%ectx.cfg.beacon.words_period)==0);
            for(int slot=0; slot<9; ++slot)
            {
                if(ins && slot==ectx.cfg.beacon.band_slot)
                {
                    BeaconPayload bp{ ectx.cfg.profile, (uint16_t)(ectx.cfg.superframe_words%5), 0 };
                    sy2.push_back( encode_beacon_symbol(bp) );
                }
                else
                {
                    sy2.push_back( (k<body.size()? body[k++] : 0) );
                }
            }
            ++word_idx;
        }
        body.swap(sy2);
    }
    SuperframeHeader hdr{};
    hdr.profile=ectx.cfg.profile;
    hdr.uep=ectx.cfg.uep;
    hdr.tile=ectx.cfg.tile;
    hdr.seed=ectx.cfg.seed;
    hdr.beacon=ectx.cfg.beacon;
    hdr.subword=ectx.cfg.subword;
    hdr.centered=ectx.cfg.centered;
    hdr.coset=ectx.cfg.coset;
    HeaderPack hp=HeaderCodec::pack(hdr);
    GF27 A[18] {},B[18] {};
    for(int i=0; i<18; ++i) A[i]=hp.symbols[i];
    for(int i=0; i<9; ++i) B[i]=hp.symbols[18+i];
    for(int i=9; i<18; ++i) B[i]=0;
    GF27 encA[26] {},encB[26] {};
    ectx.rs_hdr.encode_block(A,encA);
    ectx.rs_hdr.encode_block(B,encB);
    std::vector<GF27> all;
    all.reserve(52+body.size());
    for(int i=0; i<26; ++i) all.push_back(encA[i]);
    for(int i=0; i<26; ++i) all.push_back(encB[i]);
    all.insert(all.end(), body.begin(), body.end());
    size_t total_words=(all.size()+8)/9;
    out.assign(total_words, Word27{});
    size_t idx=0;
    for(size_t w=0; w<total_words; ++w) for(int s=0; s<9; ++s) out[w].sym[s]=(idx<all.size()? all[idx++] : 0);
    return true;
}

// ---- Self-tests (optional small) ----
inline bool selftest_rs_unit()
{
    GF27Context gf;
    gf.init();
    std::mt19937 rng(1);
    for(ProfileID pid:
            {
                ProfileID::P1_RS26_24,ProfileID::P2_RS26_22,ProfileID::P3_RS26_20,ProfileID::P4_RS26_18
            })
    {
        RSCodec rs;
        rs.init(&gf, rs_params_for(pid));
        int n=rs.params.n,k=rs.params.k,t=(n-k)/2;
        std::vector<GF27> data(k);
        for(int i=0; i<k; ++i) data[i]=(GF27)((i*5+7)%27);
        std::vector<GF27> code(n);
        rs.encode_block(data.data(),code.data());
        std::uniform_int_distribution<int> pos(0,n-1), val(1,26);
        std::vector<int> used;
        for(int e=0; e<t; ++e)
        {
            int p;
            do
            {
                p=pos(rng);
            }
            while(std::find(used.begin(),used.end(),p)!=used.end());
            used.push_back(p);
            code[p]=gf.add(code[p], (GF27)val(rng));
        }
        std::vector<GF27> outk(k);
        if(!rs.decode_block(code.data(),outk.data())) return false;
        if(outk!=data) return false;
    }
    return true;
}
inline bool selftest_api_roundtrip()
{
    std::vector<PixelYCbCrQuant> px(64);
    for(size_t i=0; i<px.size(); ++i)
    {
        px[i].Yq=(i*7)%243;
        px[i].Cbq=(int16_t)((int(i*3)%81)-40);
        px[i].Crq=(int16_t)((int(i*5)%81)-40);
    }
    std::vector<Word27> raw_in;
    encode_raw_pixels_to_words(px,raw_in);
    EncoderContext e;
    e.cfg.profile=ProfileID::P2_RS26_22;
    uep_luma_priority(e.cfg.uep);
    std::vector<Word27> prof;
    if(!encode_profile_from_raw(raw_in,prof,e)) return false;
    DecoderContext d;
    std::vector<Word27> raw_out;
    if(!decode_profile_to_raw(prof,raw_out,d)) return false;
    size_t L=std::min(raw_in.size(),raw_out.size());
    for(size_t i=0; i<L; ++i) for(int s=0; s<9; ++s) if(raw_in[i].sym[s]!=raw_out[i].sym[s]) return false;
    return true;
}

