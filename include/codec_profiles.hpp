// ============================================================================
//  File: include/codec_profiles.hpp — Profils proto & helpers trits (DOC+)
//  Project: Ternary Image/Video Codec v6
//
//  BUT
//  ---
//  • Déclare les profils prototypes (HaarTernary, AnisoRC).
//  • Paramètres `ProtoConfig` (seulement ceux utilisés par les outils).
//  • Helpers de trits balanced↔unbalanced (inline).
//  • Prototypes de packing base-243 (5 trits → 1 octet) et de l’encodeur proto.
//
//  GARDE-FOUS
//  ----------
//  • Les conversions balanced {-1,0,1} ↔ unbalanced {0,1,2} sont **pures**.
//  • L’ECC/RS GF(27) vit ailleurs (cœur codec), non exposé ici.
//
//  API
//  ---
//   enum class ProtoProfile { None=0, HaarTernary=1, AnisoRC=2 };
//   struct ProtoConfig { ... };
//   bool encode_prototype_available(ProtoProfile p);
//   bool encode_prototype_ternary(const struct ImageU8& rgb, const ProtoConfig& cfg,
//                                 std::vector<int8_t>& out_balanced,
//                                 std::vector<uint8_t>* out_packed_base243,
//                                 std::string& meta_json);
//
//   void pack_base243_from_balanced(const std::vector<int8_t>& bal, std::vector<uint8_t>& out);
//   void unpack_base243_to_balanced(const std::vector<uint8_t>& bytes, size_t n_trits, std::vector<int8_t>& out);
//
//   inline uint8_t trit_bal_to_unb(int8_t b);
//   inline int8_t  trit_unb_to_bal(uint8_t u);
//
//  EXEMPLES
//  --------
//   ProtoConfig cfg; cfg.profile=ProtoProfile::HaarTernary; cfg.pack_base243=true;
//   std::vector<int8_t> bal; std::vector<uint8_t> bytes; std::string meta;
//   encode_prototype_ternary(rgb, cfg, bal, &bytes, meta);
// ============================================================================

#pragma once
#include <cstdint>
#include <vector>
#include <string>

// Profil des prototypes
enum class ProtoProfile : uint8_t { None=0, HaarTernary=1, AnisoRC=2 };

// Configuration minimale (seulement les champs utilisés par le CLI/outils)
struct ProtoConfig
{
    ProtoProfile profile = ProtoProfile::None;

    // Haar
    int   haar_tile   = 8;
    int   haar_thresh = 6;

    // Aniso Ridgelet/Curvelet prototype
    int   rc_block    = 32;
    int   rc_angles   = 8;
    float rc_tern_z   = 1.2f;

    // Sortie packée base-243 (octets) en plus des trits balanced
    bool  pack_base243 = true;

    // (Extensions possibles: keep_LL_u8, normalize_proj, etc.)
};

// --- Helpers trits (inline, sûrs)
inline uint8_t trit_bal_to_unb(int8_t b)
{
    // map {-1,0,+1} → {0,1,2} avec clamp de sûreté
    if(b < -1) b = -1;
    if(b > 1) b = 1;
    return (uint8_t)(b + 1);
}
inline int8_t trit_unb_to_bal(uint8_t u)
{
    // map {0,1,2} → {-1,0,+1} avec clamp de sûreté
    if(u > 2) u = 2;
    return (int8_t)((int)u - 1);
}

// --- Disponibilité des profils (implémentations en .cpp)
bool encode_prototype_available(ProtoProfile p);

// --- Encodeur prototype → balanced (+ optional pack base-243) + meta JSON
struct ImageU8; // fwd decl (défini dans io_image.hpp)
bool encode_prototype_ternary(const ImageU8& rgb,
                              const ProtoConfig& cfg,
                              std::vector<int8_t>& out_balanced,
                              std::vector<uint8_t>* out_packed_base243, // peut être nullptr
                              std::string& meta_json);

// --- Packing base-243 (5 trits balanced → 1 octet) & inverse
void pack_base243_from_balanced(const std::vector<int8_t>& balanced,
                                std::vector<uint8_t>& out_bytes);
void unpack_base243_to_balanced(const std::vector<uint8_t>& bytes,
                                size_t n_trits,
                                std::vector<int8_t>& out_balanced);
