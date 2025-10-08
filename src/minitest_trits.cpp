// ============================================================================
//  File: src/minitest_trits.cpp — Tests ciblés trits (balanced/unbalanced)
//  Project: Ternary Image/Video Codec v6
//
//  Objectifs testés :
//   [T1] Conversion trit à trit : balanced {-1,0,1} <-> unbalanced {0,1,2}
//   [T2] Encodage d'entiers signés en base ternaire équilibrée (L trits fixés),
//        puis décodage -> intégrité (plage dans [-((3^L-1)/2), +((3^L-1)/2)]).
//   [T3] Pack/Unpack “base-243” (5 trits unbalanced -> 1 octet 0..242) pour
//        vecteurs de longueur arbitraire (avec n_trits conservé côté dépack).
//   [T4] Rapport: par SubwordMode ∈ {S27,S24,S21,S18,S15} avec N∈{27,24,21,18,15}
//        - stats erreurs
//        - CRC-12 (poly 0x80F) sur le flux packé unbalanced
//        - parité ternaire (somme mod 3) sur le flux unbalanced
//
//  Notes :
//   • Ce test est auto-contenu pour T1/T2/T3. Il n'exige pas d'APIs cœur
//     spécifiques (Word27, GF, etc.). Il n’altère pas le cœur.
//   • Si vous exposez selftest_rs_unit() dans le cœur, décommentez la macro
//     TEST_WITH_RS_SELFTEST pour intégrer un statut RS/GF au rapport.
//
//  Build (exemples) :
//    g++ -std=c++17 -O2 -Iinclude -Ithird_party src/minitest_trits.cpp -o minitest_trits
//    # (optionnel) avec STB TU si besoin de compat include: + src/compile_stb.cpp
//
//  Exécution :
//    ./minitest_trits  -> imprime un rapport JSON lisible sur stdout
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <random>
#include <limits>
#include <iostream>
#include <iomanip>
#include <algorithm>

#include "ternary_image_codec_v6_min.hpp" // SubwordMode, StdRes, std_res_for

// ============================ [TRITS CONVERSIONS] ============================
// Balanced {-1,0,1}  <-> Unbalanced {0,1,2}
static inline uint8_t  trit_bal_to_unb(int8_t b)   { /* pre: b∈{-1,0,1} */ return (uint8_t)(b + 1); }
static inline int8_t   trit_unb_to_bal(uint8_t u)  { /* pre: u∈{0,1,2} */ return (int8_t)(u) - 1;  }

// ============================ [INT <-> BALANCED] ============================
// Encode un entier signé 'v' en L trits balanced (little-endian digit order).
// Hyp: v ∈ [ -((3^L-1)/2) , +((3^L-1)/2) ]  => représentation canonique.
static void int_to_balanced_trits_fixed(int64_t v, int L, std::vector<int8_t>& out_bal){
    out_bal.assign((size_t)L, 0);
    // Algorithme classique: divisions par 3 avec report pour remainder==2
    for(int i=0; i<L; ++i){
        int64_t r = v % 3; v /= 3;
        if(r == 2){ out_bal[(size_t)i] = -1; v += 1; }
        else      { out_bal[(size_t)i] = (int8_t)r; } // 0 ou 1
    }
    // Les digits sont en { -1,0,1 } MAIS les 1 ci-dessus sont encore "0/1"
    // Corrige: 0 -> 0, 1 -> 1 (ok), et -1 reste -1. Rien à faire de plus.
    // (Pour r==2, on a mis -1, sinon 0/1)
    // NB : pour des valeurs extrêmes, si v != 0 après L digits, l'appelant a
    // donné un 'v' hors plage; ici on tronque silencieusement.
}

// Décodage trits balanced (little-endian) -> entier signé
static int64_t balanced_trits_to_int(const std::vector<int8_t>& bal){
    // somme b[i]*3^i
    int64_t acc = 0;
    int64_t pow3 = 1;
    for(size_t i=0;i<bal.size();++i){
        acc += (int64_t)bal[i] * pow3;
        pow3 *= 3;
    }
    return acc;
}

// ============================ [PACK / UNPACK 5-TRITS -> BYTE] ===============
// Pack 5 trits unbalanced (u∈{0,1,2}) en un octet 0..242 (base-243).
// On regroupe par blocs de 5; le dernier bloc peut être partiel (1..4 trits).
// NB: 'pack' ne stocke pas la longueur; 'unpack' a besoin de n_trits attendu.
static void pack_unbalanced_base243(const std::vector<uint8_t>& trits_unb, std::vector<uint8_t>& out_bytes){
    out_bytes.clear();
    size_t n = trits_unb.size();
    for(size_t i=0; i<n; i+=5){
        int k = (int)std::min<size_t>(5, n - i);
        uint32_t val = 0;
        uint32_t p = 1;
        for(int j=0;j<k;++j){ val += (uint32_t)trits_unb[i+j] * p; p *= 3; }
        out_bytes.push_back((uint8_t)val); // 0..242 (ou moins si k<5)
    }
}
static void unpack_unbalanced_base243(const std::vector<uint8_t>& bytes, size_t n_trits, std::vector<uint8_t>& out_trits_unb){
    out_trits_unb.clear(); out_trits_unb.reserve(n_trits);
    size_t needed_blocks = (n_trits + 4) / 5;
    for(size_t b=0; b<needed_blocks; ++b){
        uint32_t val = (b < bytes.size()? bytes[b] : 0);
        int k = (int)std::min<size_t>(5, n_trits - b*5);
        for(int j=0;j<k;++j){
            uint8_t u = (uint8_t)(val % 3);
            out_trits_unb.push_back(u);
            val /= 3;
        }
    }
    if(out_trits_unb.size() > n_trits) out_trits_unb.resize(n_trits);
}

// ============================ [CRC-12 / PARITÉ TERN] ========================
// CRC-12 (poly 0x80F, largeur 12, init 0x000, no reflect, no xorout).
static uint16_t crc12_0x80F(const uint8_t* data, size_t len){
    uint16_t poly = 0x80F;
    uint16_t crc  = 0x000;
    for(size_t i=0;i<len;++i){
        uint8_t byte = data[i];
        for(int bit=7; bit>=0; --bit){
            uint8_t inb = (uint8_t)((byte >> bit) & 1u);
            uint8_t msb = (uint8_t)((crc >> 11) & 1u);
            crc <<= 1;
            if(msb ^ inb) crc ^= poly;
            crc &= 0x0FFF;
        }
    }
    return crc & 0x0FFF;
}
// Parité ternaire = somme(u) mod 3, sur u∈{0,1,2}
static uint8_t ternary_parity_mod3(const std::vector<uint8_t>& trits_unb){
    uint32_t s=0; for(uint8_t u: trits_unb) s += u;
    return (uint8_t)(s % 3);
}

// ============================ [GÉNÉRATEUR] ==================================
static std::mt19937_64& rng(){
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    return gen;
}
static int64_t pow3i(int L){
    int64_t r=1; while(L-->0){ r*=3; } return r;
}
static int64_t range_min_for_L(int L){ return -( (pow3i(L)-1) / 2 ); }
static int64_t range_max_for_L(int L){ return  ( (pow3i(L)-1) / 2 ); }

// Fabrique un vecteur de N trits balanced “aléatoires” mais biaisés vers le centre
static void make_balanced_vector(int N, std::vector<int8_t>& out){
    std::uniform_int_distribution<int> d(0, 99);
    out.resize((size_t)N);
    for(int i=0;i<N;++i){
        int r=d(rng());
        out[(size_t)i] = (r<10? -1 : (r<90? 0 : 1)); // 10% -1 / 80% 0 / 10% +1
    }
}

// ============================ [TESTS UNITAIRES TRITS] =======================
// T1: balanced <-> unbalanced round-trip
static bool test_t1_balanced_unbalanced_roundtrip(int N, int cases, int& mismatches){
    mismatches = 0;
    std::vector<int8_t>  bal;
    std::vector<uint8_t> unb;
    for(int c=0;c<cases;++c){
        make_balanced_vector(N, bal);
        unb.resize((size_t)N);
        for(int i=0;i<N;++i) unb[(size_t)i] = trit_bal_to_unb(bal[(size_t)i]);
        // back
        for(int i=0;i<N;++i){
            int8_t b = trit_unb_to_bal(unb[(size_t)i]);
            if(b != bal[(size_t)i]){ ++mismatches; break; }
        }
    }
    return mismatches==0;
}

// T2: int <-> balanced (L trits)
static bool test_t2_int_balanced_fixedL(int L, int cases, int& mismatches){
    mismatches = 0;
    int64_t vmin = range_min_for_L(L);
    int64_t vmax = range_max_for_L(L);
    std::uniform_int_distribution<int64_t> D(vmin, vmax);

    for(int c=0;c<cases;++c){
        int64_t v = D(rng());
        std::vector<int8_t> bal;
        int_to_balanced_trits_fixed(v, L, bal);
        int64_t back = balanced_trits_to_int(bal);
        if(back != v) ++mismatches;
    }
    return mismatches==0;
}

// T3: pack/unpack base-243 pour N trits
static bool test_t3_pack_unpack_base243(int N, int cases, int& mismatches, uint16_t& crc12, uint8_t& parity3, size_t& total_bytes){
    mismatches = 0; crc12 = 0; parity3 = 0; total_bytes = 0;
    std::vector<int8_t>  bal;
    std::vector<uint8_t> unb, bytes, unb2;

    // Accumulateur global sur tous les cas (concat) pour CRC/parité
    std::vector<uint8_t> concat_bytes;
    std::vector<uint8_t> concat_unb;

    for(int c=0;c<cases;++c){
        make_balanced_vector(N, bal);
        unb.resize((size_t)N);
        for(int i=0;i<N;++i) unb[(size_t)i] = trit_bal_to_unb(bal[(size_t)i]);

        pack_unbalanced_base243(unb, bytes);
        unpack_unbalanced_base243(bytes, (size_t)N, unb2);

        if(unb2.size()!=unb.size() || !std::equal(unb.begin(), unb.end(), unb2.begin())) ++mismatches;

        concat_bytes.insert(concat_bytes.end(), bytes.begin(), bytes.end());
        concat_unb.insert(concat_unb.end(), unb.begin(), unb.end());
        total_bytes += bytes.size();
    }
    crc12  = crc12_0x80F(concat_bytes.data(), concat_bytes.size());
    parity3= ternary_parity_mod3(concat_unb);
    return mismatches==0;
}

// ============================ [RS/GF optionnel] =============================
// Décommentez si vous exposez ce symbole dans le cœur.
// #define TEST_WITH_RS_SELFTEST
#ifdef TEST_WITH_RS_SELFTEST
extern bool selftest_rs_unit();
static bool run_rs_selftest(){ return selftest_rs_unit(); }
#else
static bool run_rs_selftest(){ return true; } // marqué "skipped" dans le rapport
#endif

// ============================ [RAPPORT / DRIVER] ============================
static const char* mode_name(SubwordMode m){
    switch(m){
        case SubwordMode::S27: return "S27";
        case SubwordMode::S24: return "S24";
        case SubwordMode::S21: return "S21";
        case SubwordMode::S18: return "S18";
        case SubwordMode::S15: return "S15";
        default: return "UNK";
    }
}

int main(){
    struct Entry { SubwordMode m; int N; } modes[] = {
        {SubwordMode::S27,27}, {SubwordMode::S24,24}, {SubwordMode::S21,21},
        {SubwordMode::S18,18}, {SubwordMode::S15,15},
    };

    const int CASES_T1 = 200; // # cas par mode pour T1
    const int CASES_T2 = 200; // # cas par mode pour T2
    const int CASES_T3 = 100; // # cas par mode pour T3 (pack/unpack)

    // Rapport JSON “simple”
    std::cout << "{\n";
    std::cout << "  \"report\": {\n";
    std::cout << "    \"tests\": [\n";

    bool first = true;
    bool all_ok = true;

    for(const auto& e : modes){
        int mm1=0, mm2=0, mm3=0;
        uint16_t crc12=0; uint8_t parity3=0; size_t bytes_total=0;

        bool ok1 = test_t1_balanced_unbalanced_roundtrip(e.N, CASES_T1, mm1);
        bool ok2 = test_t2_int_balanced_fixedL(e.N, CASES_T2, mm2);
        bool ok3 = test_t3_pack_unpack_base243(e.N, CASES_T3, mm3, crc12, parity3, bytes_total);

        all_ok = all_ok && ok1 && ok2 && ok3;

        if(!first) std::cout << ",\n";
        first = false;

        std::cout << "      {\n";
        std::cout << "        \"mode\": \"" << mode_name(e.m) << "\",\n";
        std::cout << "        \"N_trits\": " << e.N << ",\n";
        std::cout << "        \"T1_balanced_unbalanced\": {\"cases\": " << CASES_T1 << ", \"mismatches\": " << mm1 << ", \"ok\": " << (ok1? "true":"false") << "},\n";
        std::cout << "        \"T2_int_fixedL\": {\"cases\": " << CASES_T2 << ", \"mismatches\": " << mm2 << ", \"ok\": " << (ok2? "true":"false") << "},\n";
        std::cout << "        \"T3_pack_unpack\": {\"cases\": " << CASES_T3 << ", \"mismatches\": " << mm3 << ", \"bytes_total\": " << bytes_total
                  << ", \"crc12_0x80F\": \"" << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << (crc12 & 0x0FFF)
                  << std::dec << "\", \"parity_mod3\": " << (int)parity3 << ", \"ok\": " << (ok3? "true":"false") << "}\n";
        std::cout << "      }";
    }

    // RS/GF (optionnel)
    bool rs_ok = run_rs_selftest();
    std::cout << "\n    ],\n";
    std::cout << "    \"rs_gf_selftest\": " << (rs_ok? "\"OK or SKIP\"":"\"FAIL\"") << ",\n";
    std::cout << "    \"final_status\": " << (all_ok && rs_ok ? "\"PASS\"" : "\"CHECK\"") << "\n";
    std::cout << "  }\n";
    std::cout << "}\n";

    return (all_ok && rs_ok) ? 0 : 1;
}
