// ============================================================================
//  File: include/header_inline_impl.hpp — HeaderCodec {pack,check,unpack}
//  Rappel: balanced exposé; ici on manipule le header en UTrit/GF(27) interne.
// ============================================================================
#pragma once
#include "ternary_image_codec_v6_min.hpp"

inline HeaderPack HeaderCodec::pack(const SuperframeHeader& h){
    HeaderPack p{}; auto at=[&](int i,GF27 v){ p.symbols[i]=(GF27)(v%27); };
    at(0,h.magic%27); at(1,(h.magic/27)%27); at(2,h.version%27); at(3,(GF27)h.profile);
    auto pack3bands=[&](){
        uint32_t u0=0,u1=0,u2=0;
        for(int i=0;i<3;++i) u0=u0*3+(h.uep.band_profile[i]%3);
        for(int i=3;i<6;++i) u1=u1*3+(h.uep.band_profile[i]%3);
        for(int i=6;i<9;++i) u2=u2*3+(h.uep.band_profile[i]%3);
        at(4,u0); at(5,u1); at(6,u2);
    }; pack3bands();
    at(7,h.tile.w%27); at(8,h.tile.h%27);
    at(9,h.seed.a%27); at(10,h.seed.b%27); at(11,h.seed.s0%27);
    uint8_t sub=0; switch(h.subword){
        case SubwordMode::S27:sub=0;break; case SubwordMode::S24:sub=1;break;
        case SubwordMode::S21:sub=2;break; case SubwordMode::S18:sub=3;break;
        case SubwordMode::S15:sub=4;break;
    }
    at(12,(GF27)((sub + 9*(h.centered?1:0))%27));
    at(13,h.band_map_hash%27); at(14,(h.band_map_hash/27)%27); at(15,(h.band_map_hash/729)%27);
    at(16,(GF27)((uint8_t)h.coset%3));
    at(17,h.frame_seq%27); at(18,(h.frame_seq/27)%27); at(19,(h.frame_seq/729)%27);
    at(20,0); at(21,0); at(22,0); at(26,0);
    at(23,h.beacon.enabled?1:0); at(24,h.beacon.band_slot%27);
    at(25,(GF27)std::min<uint32_t>(h.beacon.words_period,26));

    std::vector<UTrit> tr; tr.reserve(27*3);
    auto push=[&](int i){
        if(i==20||i==21||i==22||i==26) return;
        auto d=unpack3(p.symbols[i]); tr.insert(tr.end(), d.begin(), d.end());
    };
    for(int i=0;i<27;++i) push(i);

    std::array<UTrit,CRC3::L> r{}; CRC3::rem12(tr,r);
    auto RSym=[&](int i){ return pack3(r[i*3+0],r[i*3+1],r[i*3+2]); };
    at(20,RSym(0)); at(21,RSym(1)); at(22,RSym(2)); at(26,RSym(3));
    return p;
}

inline bool HeaderCodec::check(const HeaderPack& p){
    std::vector<UTrit> tr; tr.reserve(27*3);
    auto push=[&](int i){
        if(i==20||i==21||i==22||i==26) return;
        auto d=unpack3(p.symbols[i]); tr.insert(tr.end(), d.begin(), d.end());
    };
    for(int i=0;i<27;++i) push(i);
    std::array<UTrit,CRC3::L> r{}; CRC3::rem12(tr,r);
    std::array<UTrit,CRC3::L> h{};
    auto up=[&](int idx,int off){ auto d=unpack3(p.symbols[idx]); h[off+0]=d[0]; h[off+1]=d[1]; h[off+2]=d[2]; };
    up(20,0); up(21,3); up(22,6); up(26,9);
    return (r==h);
}

inline SuperframeHeader HeaderCodec::unpack(const HeaderPack& p){
    SuperframeHeader h{}; auto rd=[&](int i){ return p.symbols[i]%27; };
    h.magic=(uint16_t)(rd(0)+27*rd(1)); h.version=(uint8_t)rd(2);
    h.profile=(ProfileID)(rd(3)%5);
    auto dec3=[&](uint32_t v,int off){
        uint8_t t0=v%3; v/=3; uint8_t t1=v%3; v/=3; uint8_t t2=v%3;
        h.uep.band_profile[off+0]=t0; h.uep.band_profile[off+1]=t1; h.uep.band_profile[off+2]=t2;
    };
    dec3(rd(4),0); dec3(rd(5),3); dec3(rd(6),6);
    h.tile.w=(uint16_t)rd(7); h.tile.h=(uint16_t)rd(8);
    h.seed.a=rd(9); h.seed.b=rd(10); h.seed.s0=rd(11);
    {   uint32_t v=rd(12)%27; uint8_t cen=(v/9)%3; uint8_t sub=v%9;
        switch(sub){ case 0:h.subword=SubwordMode::S27;break; case 1:h.subword=SubwordMode::S24;break;
                      case 2:h.subword=SubwordMode::S21;break; case 3:h.subword=SubwordMode::S18;break;
                      case 4:h.subword=SubwordMode::S15;break; default:h.subword=SubwordMode::S27; }
        h.centered=(cen!=0);
    }
    h.band_map_hash=rd(13)+27*rd(14)+729*rd(15);
    h.coset=(CosetID)(rd(16)%3);
    h.frame_seq=rd(17)+27*rd(18)+729*rd(19);
    h.beacon.enabled=rd(23)!=0; h.beacon.band_slot=rd(24)%9; h.beacon.words_period=rd(25);
    return h;
}
