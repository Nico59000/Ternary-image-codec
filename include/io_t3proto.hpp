// ============================================================================
//  File: include/io_t3proto.hpp — Conteneur minimal .t3proto (DOC+, v1.1)
//  Project: Ternary Image/Video Codec v6
//
//  BUT
//  ---
//  Conteneur “laboratoire” binaire pour flux ternaires prototypés (.t3proto).
//  • Transporte: dimensions, profil, métadonnées JSON, flux balanced (trits {-1,0,1})
//    et/ou flux packé base-243 (octets).
//  • Fournit I/O robustes, et calcule/écrit toujours `n_trits` au header.
//
//  FORMAT (LE, ver=1)
//  ------------------
//   magic[4]   = "T3PT"
//   ver(u8)=1, profile(u8)=[0=None,1=HaarTernary,2=AnisoRC]
//   flags(u16) bit0: PACK_PRESENT, bit1: BAL_PRESENT
//   width(u32), height(u32)
//   n_trits(u64)  // #trits balanced (exact, ou inféré si flux packé seul)
//   n_bytes(u64)  // #octets packés
//   meta_len(u32), meta_json[meta_len]  // UTF-8 (peut contenir "counts": {...})
//   if BAL_PRESENT:  n_trits octets  (trits {-1,0,1} mappés {0,1,2})
//   if PACK_PRESENT: n_bytes octets  (base-243 ; 5 trits → 1 octet)
//
//  GARDE-FOUS
//  ----------
//  • ECC/RS GF(27) hors de ce fichier. Conversion balanced↔unbalanced stricte via helpers.
//  • `n_trits` écrit même si seul le pack est présent (via inférence métadonnées).
//
//  API
//  ---
//   bool t3proto_write(path, profile, W,H, balanced*, packed*, meta_json);
//   bool t3proto_read (path, profile, W,H, balanced*, packed*, meta_out );
//   + utilitaires internes (IO LE, inférence n_trits).
//
//  EXEMPLES
//  --------
//   // écrire un fichier avec trits balanced + pack
//   t3proto::t3proto_write("out.t3proto", ProtoProfile::HaarTernary, W,H,
//                          &bal, &bytes, meta);
//
//   // lire uniquement le pack
//   std::vector<uint8_t> bytes;
//   t3proto::t3proto_read("in.t3proto", prof, W,H, nullptr, &bytes, nullptr);
// ============================================================================

#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

#include "codec_profiles.hpp" // ProtoProfile + helpers trits

namespace t3proto
{

// ---- Flags
enum : uint16_t { F_PACK_PRESENT = 1u<<0, F_BAL_PRESENT = 1u<<1 };

// ---- IO LE helpers
inline bool wr_u16(FILE* f, uint16_t v)
{
    uint8_t b[2]= {uint8_t(v&0xFF),uint8_t((v>>8)&0xFF)};
    return std::fwrite(b,1,2,f)==2;
}
inline bool wr_u32(FILE* f, uint32_t v)
{
    uint8_t b[4]= {uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)};
    return std::fwrite(b,1,4,f)==4;
}
inline bool wr_u64(FILE* f, uint64_t v)
{
    uint8_t b[8];
    for(int i=0; i<8; ++i) b[i]=uint8_t(v>>(8*i));
    return std::fwrite(b,1,8,f)==8;
}
inline bool rd_u16(FILE* f, uint16_t& v)
{
    uint8_t b[2];
    if(std::fread(b,1,2,f)!=2) return false;
    v=uint16_t(b[0]|(uint16_t(b[1])<<8));
    return true;
}
inline bool rd_u32(FILE* f, uint32_t& v)
{
    uint8_t b[4];
    if(std::fread(b,1,4,f)!=4) return false;
    v=uint32_t(b[0]|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24));
    return true;
}
inline bool rd_u64(FILE* f, uint64_t& v)
{
    uint8_t b[8];
    if(std::fread(b,1,8,f)!=8) return false;
    v=0;
    for(int i=7; i>=0; --i) v=(v<<8)|b[i];
    return true;
}
inline bool wr_bytes(FILE* f, const void* p, size_t n)
{
    return std::fwrite(p,1,n,f)==n;
}
inline bool rd_bytes(FILE* f, void* p, size_t n)
{
    return std::fread (p,1,n,f)==n;
}

// ---- Header (in-memory)
struct Header
{
    char     magic[4];
    uint8_t  ver;
    uint8_t  profile;
    uint16_t flags;
    uint32_t width, height;
    uint64_t n_trits;
    uint64_t n_bytes;
    uint32_t meta_len;
};

// ---- Parsing minimal de meta JSON (sans dépendance)
inline bool meta_find_int(const std::string& meta, const std::string& key, uint64_t& out)
{
    auto pos = meta.find("\""+key+"\"");
    if(pos==std::string::npos) return false;
    pos = meta.find(':', pos);
    if(pos==std::string::npos) return false;
    ++pos;
    while(pos<meta.size() && (meta[pos]==' '||meta[pos]=='\t')) ++pos;
    if(pos>=meta.size()) return false;
    uint64_t val=0;
    bool any=false;
    while(pos<meta.size() && std::isdigit((unsigned char)meta[pos]))
    {
        any=true;
        val=val*10+(meta[pos]-'0');
        ++pos;
    }
    if(!any) return false;
    out=val;
    return true;
}

// ---- Inférence n_trits depuis meta + pack
inline uint64_t infer_ntrits_from_meta(ProtoProfile prof,
                                       uint32_t W, uint32_t H,
                                       const std::string& meta_json,
                                       uint64_t packed_bytes)
{
    uint64_t ntr=0;
    if(meta_find_int(meta_json, "n_trits", ntr) && ntr>0) return ntr;

    uint64_t lt=0, ls=0;
    bool has_lt = meta_find_int(meta_json, "len_tiles",  lt);
    bool has_ls = meta_find_int(meta_json, "len_sketch", ls);
    if(has_lt || has_ls)
    {
        uint64_t s=lt+ls;
        if(s>0) return s;
    }

    uint64_t tpb=0, blockN=0;
    if(meta_find_int(meta_json, "trits_per_block", tpb) &&
            meta_find_int(meta_json, "block", blockN) && blockN>0)
    {
        uint64_t bX = (W + blockN - 1) / blockN;
        uint64_t bY = (H + blockN - 1) / blockN;
        uint64_t s = tpb * bX * bY;
        if(s>0) return s;
    }

    uint64_t tail=0;
    if(meta_find_int(meta_json, "tail_trits", tail))
    {
        if(packed_bytes==0) return 0;
        if(tail==0) return packed_bytes * 5ULL;
        if(tail<=5ULL) return (packed_bytes-1ULL)*5ULL + tail;
    }
    return packed_bytes * 5ULL; // borne sup si incertain
}

// ---- WRITE
inline bool t3proto_write(const std::string& path,
                          ProtoProfile profile,
                          uint32_t W, uint32_t H,
                          const std::vector<int8_t>*  balanced_trits,
                          const std::vector<uint8_t>* packed_bytes,
                          const std::string& meta_json)
{
    const bool hasBal  = (balanced_trits && !balanced_trits->empty());
    const bool hasPack = (packed_bytes   && !packed_bytes->empty());

    Header Hd{};
    std::memcpy(Hd.magic, "T3PT", 4);
    Hd.ver     = 1;
    Hd.profile = (uint8_t)profile;
    Hd.flags   = (hasPack? F_PACK_PRESENT:0) | (hasBal? F_BAL_PRESENT:0);
    Hd.width   = W;
    Hd.height  = H;
    Hd.n_bytes = hasPack ? (uint64_t)packed_bytes->size() : 0;
    Hd.meta_len= (uint32_t)meta_json.size();
    Hd.n_trits = hasBal ? (uint64_t)balanced_trits->size()
                 : (hasPack ? infer_ntrits_from_meta(profile,W,H,meta_json,Hd.n_bytes) : 0);

    FILE* f = std::fopen(path.c_str(), "wb");
    if(!f) return false;

    bool ok=true;
    ok &= (std::fwrite(Hd.magic,1,4,f)==4);
    ok &= (std::fwrite(&Hd.ver,1,1,f)==1);
    ok &= (std::fwrite(&Hd.profile,1,1,f)==1);
    ok &= wr_u16(f,Hd.flags) && wr_u32(f,Hd.width) && wr_u32(f,Hd.height);
    ok &= wr_u64(f,Hd.n_trits) && wr_u64(f,Hd.n_bytes);
    ok &= wr_u32(f,Hd.meta_len);
    if(!ok)
    {
        std::fclose(f);
        return false;
    }

    if(Hd.meta_len>0) ok &= wr_bytes(f, meta_json.data(), Hd.meta_len);
    if(!ok)
    {
        std::fclose(f);
        return false;
    }

    if(hasBal)
    {
        std::vector<uint8_t> bal_u;
        bal_u.reserve(balanced_trits->size());
        for(int8_t b : *balanced_trits) bal_u.push_back(trit_bal_to_unb(b));
        ok &= wr_bytes(f, bal_u.data(), bal_u.size());
        if(!ok)
        {
            std::fclose(f);
            return false;
        }
    }
    if(hasPack)
    {
        ok &= wr_bytes(f, packed_bytes->data(), packed_bytes->size());
        if(!ok)
        {
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    return ok;
}

// ---- READ
inline bool t3proto_read(const std::string& path,
                         ProtoProfile& profile,
                         uint32_t& W, uint32_t& H,
                         std::vector<int8_t>*  balanced_trits,
                         std::vector<uint8_t>* packed_bytes,
                         std::string* meta_json_out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if(!f) return false;

    Header Hd{};
    if(std::fread(Hd.magic,1,4,f)!=4 || std::memcmp(Hd.magic,"T3PT",4)!=0)
    {
        std::fclose(f);
        return false;
    }
    if(std::fread(&Hd.ver,1,1,f)!=1 || Hd.ver!=1)
    {
        std::fclose(f);
        return false;
    }
    if(std::fread(&Hd.profile,1,1,f)!=1)
    {
        std::fclose(f);
        return false;
    }
    if(!rd_u16(f,Hd.flags) || !rd_u32(f,Hd.width) || !rd_u32(f,Hd.height))
    {
        std::fclose(f);
        return false;
    }
    if(!rd_u64(f,Hd.n_trits) || !rd_u64(f,Hd.n_bytes) || !rd_u32(f,Hd.meta_len))
    {
        std::fclose(f);
        return false;
    }

    profile = (ProtoProfile)Hd.profile;
    W=Hd.width;
    H=Hd.height;

    if(meta_json_out)
    {
        meta_json_out->assign(Hd.meta_len, '\0');
        if(Hd.meta_len>0 && !rd_bytes(f, meta_json_out->data(), Hd.meta_len))
        {
            std::fclose(f);
            return false;
        }
    }
    else
    {
        if(Hd.meta_len>0) std::fseek(f, (long)Hd.meta_len, SEEK_CUR);
    }

    if(balanced_trits)
    {
        balanced_trits->clear();
        if(Hd.flags & F_BAL_PRESENT)
        {
            std::vector<uint8_t> bal_u((size_t)Hd.n_trits);
            if(Hd.n_trits>0 && !rd_bytes(f, bal_u.data(), bal_u.size()))
            {
                std::fclose(f);
                return false;
            }
            balanced_trits->reserve((size_t)Hd.n_trits);
            for(uint8_t u : bal_u) balanced_trits->push_back(trit_unb_to_bal(u));
        }
    }
    else
    {
        if(Hd.flags & F_BAL_PRESENT) std::fseek(f, (long)Hd.n_trits, SEEK_CUR);
    }

    if(packed_bytes)
    {
        packed_bytes->clear();
        if(Hd.flags & F_PACK_PRESENT)
        {
            packed_bytes->resize((size_t)Hd.n_bytes);
            if(Hd.n_bytes>0 && !rd_bytes(f, packed_bytes->data(), packed_bytes->size()))
            {
                std::fclose(f);
                return false;
            }
        }
    }
    else
    {
        if(Hd.flags & F_PACK_PRESENT) std::fseek(f, (long)Hd.n_bytes, SEEK_CUR);
    }

    std::fclose(f);
    return true;
}

} // namespace t3proto
