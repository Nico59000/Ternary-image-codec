// ============================================================================
//  File: src/codec_profiles.cpp — Routage encode_prototype_ternary (stub)
//  Project: Ternary Image/Video Codec v6
// ============================================================================

#include "codec_profiles.hpp"

// Profils compilables (au choix, OFF par défaut)
#ifdef PROTO_HAAR_TERNARY
#include "proto_noentropy.hpp"
#endif
#ifdef PROTO_ANISO_RC
#include "proto_aniso_rc.hpp"
#endif

namespace
{
constexpr const char* kVer_Haar  = "haar_v0.1.0";
constexpr const char* kVer_Aniso = "aniso_rc_v0.1.0";

bool has_profile(ProtoProfile p)
{
    switch(p)
    {
    case ProtoProfile::HaarTernary:
#ifdef PROTO_HAAR_TERNARY
        return true;
#else
        return false;
#endif
    case ProtoProfile::AnisoRC:
#ifdef PROTO_ANISO_RC
        return true;
#else
        return false;
#endif
    default:
        return false;
    }
}
} // anon

bool encode_prototype_available(ProtoProfile p)
{
    return has_profile(p);
}

std::string describe_prototype_build()
{
    std::ostringstream os;
    os << "{\n  \"prototypes\": [";
    bool first=true;
    if(has_profile(ProtoProfile::HaarTernary))
    {
        if(!first) os << ",";
        first=false;
        os << "\n    {\"id\":1,\"name\":\"HaarTernary\",\"version\":\"" << kVer_Haar << "\"}";
    }
    if(has_profile(ProtoProfile::AnisoRC))
    {
        if(!first) os << ",";
        first=false;
        os << "\n    {\"id\":2,\"name\":\"AnisoRC\",\"version\":\"" << kVer_Aniso << "\"}";
    }
    if(first) os << "\n    {\"id\":0,\"name\":\"None\",\"version\":\"-\"}";
    os << "\n  ]\n}";
    return os.str();
}

bool encode_prototype_ternary(const ImageU8& rgb,
                              const ProtoConfig& cfg,
                              std::vector<int8_t>& balanced_trits,
                              std::vector<uint8_t>* packed_base243,
                              std::string& meta_json)
{
    balanced_trits.clear();
    if(packed_base243) packed_base243->clear();
    meta_json.clear();

    if(cfg.profile==ProtoProfile::None) return false;
    if(!has_profile(cfg.profile))       return false;

    // ----- HAAR TERNAIRE ----------------------------------------------------
    if(cfg.profile==ProtoProfile::HaarTernary)
    {
#ifdef PROTO_HAAR_TERNARY
        ProtoParams P; // défauts du proto_noentropy
        if(cfg.haar_tile        > 0) P.tile        = cfg.haar_tile;
        if(cfg.haar_thresh      > 0) P.thresh      = cfg.haar_thresh;
        if(cfg.haar_sketchSize  > 0) P.sketchSize  = cfg.haar_sketchSize;
        if(cfg.haar_sketchDown  > 0) P.sketchDown  = cfg.haar_sketchDown;
        if(cfg.haar_radialBins  > 0) P.radialBins  = cfg.haar_radialBins;
        if(cfg.haar_angleBins   > 0) P.angleBins   = cfg.haar_angleBins;
        P.keep_LL_u8 = cfg.haar_keep_LL_u8;

        ProtoArtifacts A;
        proto_tile_haar_ternary(rgb, P, A);
        proto_spectral_sketch(rgb, P, A);

        // Concat ordre: [tiles_details | sketch]
        const size_t ofs_tiles  = 0;
        const size_t len_tiles  = A.tile_trits.size();
        const size_t ofs_sketch = len_tiles;
        const size_t len_sketch = A.sketch_trits.size();

        balanced_trits.reserve(len_tiles + len_sketch);
        balanced_trits.insert(balanced_trits.end(), A.tile_trits.begin(),   A.tile_trits.end());
        balanced_trits.insert(balanced_trits.end(), A.sketch_trits.begin(), A.sketch_trits.end());

        if(packed_base243 && cfg.pack_base243)
        {
            pack_base243_from_balanced(balanced_trits, *packed_base243);
        }

        // ... après avoir rempli `balanced_trits` et éventuellement `*packed_base243`
        const size_t ntr   = balanced_trits.size();
        const size_t tail  = ntr % 5;
        const size_t pbytes= (packed_base243 && cfg.pack_base243)
                             ? packed_base243->size()
                             : (ntr + 4) / 5;

        std::ostringstream m;
        m << "{"
          << "\"proto\":\"HaarTernary\","
          << "\"version\":\"" << kVer_Haar << "\","
          << "\"params\":{"
          << "\"tile\":"<<P.tile<<",\"thresh\":"<<P.thresh<<","
          << "\"sketchSize\":"<<P.sketchSize<<",\"sketchDown\":"<<P.sketchDown<<","
          << "\"radialBins\":"<<P.radialBins<<",\"angleBins\":"<<P.angleBins<<","
          << "\"keep_LL_u8\":"<<(P.keep_LL_u8? "true":"false")
          << "},"
          << "\"layout\":{"
          << "\"order\":\"tiles_then_sketch\","
          << "\"ofs_tiles\":"<<ofs_tiles<<",\"len_tiles\":"<<len_tiles<<","
          << "\"ofs_sketch\":"<<ofs_sketch<<",\"len_sketch\":"<<len_sketch<<","
          << "\"balanced\":true"
          << "},"
          << "\"counts\":{"
          << "\"n_trits\":"<<ntr<<",\"tail_trits\":"<<tail<<",\"packed_bytes\":"<<pbytes
          << ",\"exact_n_trits\":true"
          << "}"
          meta_json = m.str();

        return true;
#else
        return false;
#endif
    }

    // ----- ANISO RC ---------------------------------------------------------
    if(cfg.profile==ProtoProfile::AnisoRC)
    {
#ifdef PROTO_ANISO_RC
        AnisoRCParams P;
        if(cfg.rc_block   > 0)   P.block  = cfg.rc_block;
        if(cfg.rc_angles  > 0)   P.angles = cfg.rc_angles;
        if(cfg.rc_tern_z  > 0.f) P.tern_thresh_z = cfg.rc_tern_z;
        P.keep_LL_u8  = cfg.rc_keep_LL_u8;
        P.normalize_proj = cfg.rc_normalize;

        AnisoRCArtifacts A;
        proto_aniso_rc_encode(rgb, P, A);

        balanced_trits = A.trits; // déjà balanced {-1,0,1}

        if(packed_base243 && cfg.pack_base243)
        {
            pack_base243_from_balanced(balanced_trits, *packed_base243);
        }

        const size_t ntr   = balanced_trits.size();
        const size_t tail  = ntr % 5;
        const size_t pbytes= (packed_base243 && cfg.pack_base243)
                             ? packed_base243->size()
                             : (ntr + 4) / 5;

        std::ostringstream m;
        m << "{"
          << "\"proto\":\"AnisoRC\","
          << "\"version\":\"" << kVer_Aniso << "\","
          << "\"params\":{"
          << "\"block\":"<<P.block<<",\"angles\":"<<P.angles<<","
          << "\"z_thresh\":"<<P.tern_thresh_z<<","
          << "\"keep_LL_u8\":"<<(P.keep_LL_u8? "true":"false")<<","
          << "\"normalize_proj\":"<<(P.normalize_proj? "true":"false")
          << "},"
          << "\"layout\":{"
          << "\"order\":\"trits_only\","
          << "\"trits_per_block\":"<<A.trits_per_block<<","
          << "\"balanced\":true"
          << "},"
          << "\"counts\":{"
          << "\"n_trits\":"<<ntr<<",\"tail_trits\":"<<tail<<",\"packed_bytes\":"<<pbytes
          << ",\"exact_n_trits\":true"
          << "}"
          meta_json = m.str();

        return true;
#else
        return false;
#endif
    }

    return false;
}
