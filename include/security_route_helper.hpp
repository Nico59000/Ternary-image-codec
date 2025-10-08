// ============================================================================
//  File: include/security_route_helper.hpp — Overlay de route (TTL/Hops/Phase)
//  Project: Ternary Image/Video Codec v6
//
//  OBJET
//  -----
//  • Préparer/avancer la redirection “AODV-light” via META **sans** lire le payload :
//      - décrémenter TTL, incrémenter HOPS,
//      - poser "route_next", "route_via",
//      - gérer "route_phase" : 0→1 (tour 1: PREP), 1→2 (tour 2: ACCEPT).
//  • Fonctions utilitaires get/set pour phase et sandbox.
// ============================================================================

#pragma once
#include <string>
#include <cstdint>
#include <algorithm>

#include "security_policy.hpp" // réuse JSON-lite helpers

namespace T3Route {

// ------------------ Getters best-effort
inline uint64_t get_uint_best_effort(const std::string& js, const char* flat_key, const char* nested_key){
    uint64_t v=0; if(T3Security::meta_find_uint(js,flat_key,v)) return v;
    size_t pos; if(T3Security::meta_find_key(js,"route",pos)) if(T3Security::meta_find_uint(js.substr(pos),nested_key,v)) return v;
    return 0;
}
inline std::string get_str_best_effort(const std::string& js, const char* flat_key, const char* nested_key){
    std::string s; if(T3Security::meta_find_str(js,flat_key,s)) return s;
    size_t pos; if(T3Security::meta_find_key(js,"route",pos)) if(T3Security::meta_find_str(js.substr(pos),nested_key,s)) return s;
    return {};
}
inline uint8_t get_phase_best_effort(const std::string& js){
    uint64_t p = get_uint_best_effort(js, "route_phase", "phase");
    if(p>2) p=2; return (uint8_t)p;
}

// ------------------ Set/Insert naïfs
inline void set_or_insert_uint(std::string& js, const std::string& key, uint64_t val){
    size_t p;
    if(T3Security::meta_find_key(js,key,p)){
        p = js.find(':', p); if(p!=std::string::npos){ ++p; while(p<js.size() && (js[p]==' '||js[p]=='\t')) ++p;
            size_t a=p; while(p<js.size() && std::isdigit((unsigned char)js[p])) ++p;
            js.replace(a, p-a, std::to_string(val)); return;
        }
    }
    std::string ins="\""+key+"\": "+std::to_string(val);
    size_t last=js.find_last_of('}');
    if(last==std::string::npos){ if(!js.empty() && js.back()!='\n') js.push_back('\n'); js += "{ "+ins+" }"; }
    else {
        size_t k=last; while(k>0 && (js[k-1]==' '||js[k-1]=='\t'||js[k-1]=='\r'||js[k-1]=='\n')) --k;
        bool needComma = (k>0 && js[k-1] != '{'); js.insert(last, std::string(needComma? ", ":"")+ins+" ");
    }
}
inline void set_or_insert_str(std::string& js, const std::string& key, const std::string& val){
    size_t p;
    if(T3Security::meta_find_key(js,key,p)){
        p = js.find(':', p); if(p!=std::string::npos){
            size_t q1=js.find('"',p); if(q1!=std::string::npos){ size_t q2=js.find('"',q1+1);
                if(q2!=std::string::npos){ js.replace(q1+1, q2-q1-1, val); return; } }
        }
    }
    std::string ins="\""+key+"\": \""+val+"\"";
    size_t last=js.find_last_of('}');
    if(last==std::string::npos){ if(!js.empty() && js.back()!='\n') js.push_back('\n'); js += "{ "+ins+" }"; }
    else {
        size_t k=last; while(k>0 && (js[k-1]==' '||js[k-1]=='\t'||js[k-1]=='\r'||js[k-1]=='\n')) --k;
        bool needComma = (k>0 && js[k-1] != '{'); js.insert(last, std::string(needComma? ", ":"")+ins+" ");
    }
}
inline void set_or_insert_bool(std::string& js, const std::string& key, bool val){
    size_t p;
    if(T3Security::meta_find_key(js,key,p)){
        p = js.find(':', p); if(p!=std::string::npos){
            ++p; while(p<js.size() && (js[p]==' '||js[p]=='\t')) ++p;
            size_t a=p; while(p<js.size() && (std::isalpha((unsigned char)js[p])||js[p]=='_')) ++p;
            js.replace(a, p-a, val?"true":"false"); return;
        }
    }
    std::string ins="\""+key+"\": "+(val?"true":"false");
    size_t last=js.find_last_of('}');
    if(last==std::string::npos){ if(!js.empty() && js.back()!='\n') js.push_back('\n'); js += "{ "+ins+" }"; }
    else {
        size_t k=last; while(k>0 && (js[k-1]==' '||js[k-1]=='\t'||js[k-1]=='\r'||js[k-1]=='\n')) --k;
        bool needComma = (k>0 && js[k-1] != '{'); js.insert(last, std::string(needComma? ", ":"")+ins+" ");
    }
}

// ------------------ API : overlay route

/**
 * @brief Préparer une redirection (overlay META) — phase explicite.
 * @param meta_in   JSON source (inchangé)
 * @param via       Domaine qui forwarde (audit) — peut être vide
 * @param next      Domaine cible (si connu) — vide possible au tour 1 (prep)
 * @param ttl_after TTL décrémenté à poser (0 => pas de forward)
 * @param hops_inc  Incrément de hops (souvent 1)
 * @param phase     1 pour PREP (tour 1), 2 pour ACCEPT (tour 2)
 * @param meta_out  JSON résultant
 * @return true si overlay appliqué (ttl_after>0), false sinon.
 */
inline bool prepare_redirect_meta_phase(const std::string& meta_in,
                                        const std::string& via,
                                        const std::string& next,
                                        uint8_t ttl_after,
                                        uint8_t hops_inc,
                                        uint8_t phase,
                                        std::string& meta_out)
{
    if(ttl_after==0){ meta_out=meta_in; return false; }
    if(phase>2) phase=2;
    meta_out = meta_in;

    // HOPS
    uint64_t hops0 = get_uint_best_effort(meta_in, "route_hops", "hops");
    set_or_insert_uint(meta_out, "route_hops", hops0 + hops_inc);

    // TTL
    set_or_insert_uint(meta_out, "route_ttl",  (uint64_t)ttl_after);

    // VIA / NEXT
    if(!via.empty())  set_or_insert_str(meta_out, "route_via",  via);
    if(!next.empty()) set_or_insert_str(meta_out, "route_next", next);

    // PHASE
    set_or_insert_uint(meta_out, "route_phase", (uint64_t)phase);

    return true;
}

/**
 * @brief Variante “tour 1” : PREP (pose phase=1, pas forcément de route_next).
 */
inline bool prepare_redirect_meta_prep(const std::string& meta_in,
                                       const std::string& via,
                                       uint8_t ttl_after,
                                       std::string& meta_out)
{
    return prepare_redirect_meta_phase(meta_in, via, /*next*/"", ttl_after, /*hops_inc*/1, /*phase*/1, meta_out);
}

/**
 * @brief Variante “tour 2” : ACCEPT (pose phase=2 + next).
 */
inline bool prepare_redirect_meta_accept(const std::string& meta_in,
                                         const std::string& via,
                                         const std::string& next,
                                         uint8_t ttl_after,
                                         std::string& meta_out)
{
    return prepare_redirect_meta_phase(meta_in, via, next, ttl_after, /*hops_inc*/1, /*phase*/2, meta_out);
}

/**
 * @brief Marqueurs de fin (optionnels) pour audit/traçage.
 */
inline void mark_accepted(std::string& js){ set_or_insert_bool(js, "route_accepted", true); }
inline void mark_sandbox(std::string& js, const std::string& reason="overlap_no_accept"){
    set_or_insert_bool(js, "route_sandbox", true);
    set_or_insert_str(js,  "route_reason", reason);
}

} // namespace T3Route
