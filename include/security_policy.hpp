// ============================================================================
//  File: include/security_policy.hpp — Moteur d’approbation lecture (DOC+)
//  Project: Ternary Image/Video Codec v6
//
//  OBJET
//  -----
//  • Décider l’accès lecture sur méta JSON (meta-only), sans lire le payload.
//  • Multi-domaines, hiérarchie limitée, proximité, TTL/Hops bornés.
//  • Superposition “tiers bas” avec rotation ternaire équilibrée.
//  • Préparation au 1er tour par le voisin ; acceptation au 2ᵉ tour seulement.
//    Si superposition présente mais aucune acceptation après 2 tours → SANDBOX.
//  • API publique : decide_ex(...), decide(...), t3p_approve_with_policy(...),
//    t3v_approve_with_policy(...).  (Pas d’accès payload ici.)
// ============================================================================

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cctype>
#include <algorithm>

namespace T3Security
{

// ------------------ JSON-lite helpers (naïfs)
inline bool meta_find_key(const std::string& js, const std::string& key, size_t& pos_out)
{
    size_t p = js.find("\""+key+"\"");
    if(p==std::string::npos) return false;
    pos_out = p;
    return true;
}
inline bool meta_find_str(const std::string& js, const std::string& key, std::string& out)
{
    size_t p;
    if(!meta_find_key(js,key,p)) return false;
    p = js.find(':', p);
    if(p==std::string::npos) return false;
    p = js.find('"', p);
    if(p==std::string::npos) return false;
    size_t e = js.find('"', ++p);
    if(e==std::string::npos) return false;
    out = js.substr(p, e-p);
    return true;
}
inline bool meta_find_uint(const std::string& js, const std::string& key, uint64_t& out)
{
    size_t p;
    if(!meta_find_key(js,key,p)) return false;
    p = js.find(':', p);
    if(p==std::string::npos) return false;
    ++p;
    while(p<js.size() && (js[p]==' '||js[p]=='\t')) ++p;
    if(p>=js.size()) return false;
    uint64_t v=0;
    bool any=false;
    while(p<js.size() && std::isdigit((unsigned char)js[p]))
    {
        any=true;
        v=v*10+(js[p]-'0');
        ++p;
    }
    if(!any) return false;
    out=v;
    return true;
}
inline uint64_t fnv1a64(const void* data, size_t n)
{
    const uint8_t* p=(const uint8_t*)data;
    uint64_t h=1469598103934665603ull;
    for(size_t i=0; i<n; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}
inline uint64_t fnv1a64(const std::string& s)
{
    return fnv1a64(s.data(), s.size());
}

inline bool starts_with(const std::string& s, const std::string& p)
{
    return s.size()>=p.size() && std::equal(p.begin(), p.end(), s.begin());
}
inline uint8_t domain_depth(const std::string& d)
{
    if(d.empty()) return 0;
    uint8_t depth=1;
    for(char c: d) if(c=='/') ++depth;
    return depth;
}
inline std::string domain_root_of(const std::string& d)
{
    size_t p = d.find('/');
    return (p==std::string::npos)? d : d.substr(0,p+1);
}

// ------------------ Proximité
enum class ProxClass : uint8_t { Local=0, Near=1, Far=2, Unknown=255 };
inline ProxClass prox_from_str(const std::string& s)
{
    if(s=="local") return ProxClass::Local;
    if(s=="near") return ProxClass::Near;
    if(s=="far")   return ProxClass::Far;
    return ProxClass::Unknown;
}

// ------------------ Tag Build
struct BuildTag
{
    std::string domain;
    std::string build_hash;
    uint64_t    version   = 0;
    uint64_t    type_hash = 0;
    ProxClass   pclass    = ProxClass::Unknown;
    uint32_t    radius_m  = 0;

    // Routage contrôlé
    uint8_t     route_ttl   = 0;   // sauts restants
    uint8_t     route_hops  = 0;   // sauts effectués
    uint8_t     route_phase = 0;   // 0=init, 1=PREP, 2=ACCEPT
    std::string route_origin;
};

inline BuildTag extract_build_from_meta(const std::string& meta)
{
    BuildTag b{};
    std::string s;
    uint64_t v=0;
    if(meta_find_str(meta,"domain", s))      b.domain = s;
    if(meta_find_str(meta,"build_hash", s))  b.build_hash = s;
    if(meta_find_str(meta,"type_hash", s))
    {
        if(s.rfind("fnv64:",0)==0)
        {
            std::string hex = s.substr(6);
            uint64_t val=0;
            for(char c: hex)
            {
                val<<=4;
                if(c>='0'&&c<='9') val|=(c-'0');
                else if(c>='a'&&c<='f') val|=(10+(c-'a'));
                else if(c>='A'&&c<='F') val|=(10+(c-'A'));
            }
            b.type_hash = val;
        }
        else
        {
            b.type_hash = fnv1a64(s);
        }
    }
    if(meta_find_uint(meta,"version", v))     b.version = v;
    if(meta_find_str (meta,"class", s))       b.pclass = prox_from_str(s);
    if(meta_find_uint(meta,"radius_m", v))    b.radius_m = (uint32_t)v;
    if(meta_find_uint(meta,"route_ttl", v))   b.route_ttl  = (uint8_t)std::min<uint64_t>(v,255);
    if(meta_find_uint(meta,"route_hops", v))  b.route_hops = (uint8_t)std::min<uint64_t>(v,255);
    if(meta_find_uint(meta,"route_phase", v)) b.route_phase= (uint8_t)std::min<uint64_t>(v,2);
    if(meta_find_str (meta,"origin", s))      b.route_origin = s;
    size_t pos;
    if(meta_find_key(meta,"route", pos))
    {
        if(meta_find_uint(meta.substr(pos),"ttl", v))     b.route_ttl   = (uint8_t)std::min<uint64_t>(v,255);
        if(meta_find_uint(meta.substr(pos),"hops", v))    b.route_hops  = (uint8_t)std::min<uint64_t>(v,255);
        if(meta_find_uint(meta.substr(pos),"phase", v))   b.route_phase = (uint8_t)std::min<uint64_t>(v,2);
        if(meta_find_str (meta.substr(pos),"origin", s))  b.route_origin= s;
    }
    if(b.type_hash==0) b.type_hash = fnv1a64(b.domain) ^ (b.version*0x9E3779B185EBCA87ull);
    return b;
}

// ------------------ Décision
enum class Decision : uint8_t { INTERNAL=0, COEXIST_ACCEPTED=1, UNKNOWN_SANDBOX=2, REJECT=3 };

// ------------------ Politique
struct Policy
{

    // Appartenances multiples (ordre = priorité; index 0 = dominant)
    struct Membership
    {
        std::string domain_prefix;
        std::string hash_prefix_hex;
        uint32_t    local_radius_m = 0; // 0 = non contraint (membre)
    };
    std::vector<Membership> memberships;
    Membership self{}; // compat

    // Interne explicitement autorisé
    struct Allow
    {
        std::string domain_prefix;
        std::string hash_prefix_hex;
    };
    std::vector<Allow> internal_allow;

    // Externes coexistant admis
    struct Coexist
    {
        std::string domain_prefix;
        std::string hash_prefix_hex;
        uint32_t    radius_max_m = 0;
        ProxClass   max_class    = ProxClass::Near;
    };
    std::vector<Coexist> coexist_allow;

    // Racines autorisées + profondeur maxi
    std::vector<std::string> allowed_roots;
    uint8_t max_depth = 3;

    // Apparences externes attendues
    std::vector<std::string> visual_whitelist_domains;

    // Redirections explicites (facultatif)
    struct Redirect
    {
        std::string from_domain_prefix, to_domain_prefix;
        uint8_t ttl_min=1, ttl_max=3;
    };
    std::vector<Redirect> redirects;

    // Limites globales TTL/hops
    uint8_t ttl_global_max  = 3;
    uint8_t hops_global_max = 6;

    // Superposition (tiers bas)
    bool enable_overlap_redirect = true;

    // Rotor ternaire équilibré
    struct Rotor
    {
        uint64_t tick = 0;
    } rotor;

    // === Callbacks pair-à-pair pour superposition (préparation/acceptation) ===
    // Tour 1 : voisin prépare une cible (tiers bas de SA hiérarchie).
    bool (*overlap_prepare_suggest)(const std::string& requester_domain,
                                    const std::string& neighbor_domain,
                                    const BuildTag& tag,
                                    std::string& out_second_target,
                                    void* user) = nullptr;
    void* overlap_prep_user = nullptr;

    // Tour 2 : acceptation optionnelle de la cible préparée.
    bool (*overlap_second_accept)(const std::string& requester_domain,
                                  const std::string& prepared_target_domain,
                                  const BuildTag& tag,
                                  void* user) = nullptr;
    void* overlap_accept_user = nullptr;

    // Cache des préparations (clé = requester_domain) — validité 1 “tour”
    struct Prep
    {
        std::string requester_domain;
        std::string prepared_target;
        uint8_t window=1;
    };
    std::vector<Prep> prepared_cache;

    // Sandbox & voisinage (AODV-light)
    void (*on_unknown_sandbox)(const BuildTag&, const std::string& meta, void* user) = nullptr;
    void* user_data = nullptr;
    bool (*query_neighbor_accept)(const BuildTag&, void* user) = nullptr;
    void* neighbor_user = nullptr;

    static Policy make_default()
    {
        return Policy{};
    }
};

// ------------------ NextHop & DecisionEx
struct NextHop
{
    bool should_redirect=false;
    std::string target_domain;
    uint8_t ttl_after=0;
};
struct DecisionEx
{
    Decision decision=Decision::UNKNOWN_SANDBOX;
    BuildTag tag{};
    NextHop next{};
};

// ------------------ Helpers
inline bool match_prefix_hex(const std::string& hex, const std::string& prefix)
{
    if(prefix.empty()) return true;
    if(hex.size()<prefix.size()) return false;
    return std::equal(prefix.begin(), prefix.end(), hex.begin());
}
inline bool match_membership(const Policy::Membership& m, const BuildTag& tag)
{
    return starts_with(tag.domain, m.domain_prefix) && match_prefix_hex(tag.build_hash, m.hash_prefix_hex);
}
inline bool match_allow(const Policy::Allow& a, const BuildTag& tag)
{
    return starts_with(tag.domain, a.domain_prefix) && match_prefix_hex(tag.build_hash, a.hash_prefix_hex);
}
inline bool match_coexist(const Policy::Coexist& c, const BuildTag& tag)
{
    if(!starts_with(tag.domain, c.domain_prefix)) return false;
    if(!match_prefix_hex(tag.build_hash, c.hash_prefix_hex)) return false;
    if(tag.radius_m > c.radius_max_m) return false;
    if(tag.pclass!=ProxClass::Unknown && tag.pclass > c.max_class) return false;
    return true;
}
inline bool match_redirect(const Policy::Redirect& r, const BuildTag& tag, uint8_t ttl)
{
    if(!starts_with(tag.domain, r.from_domain_prefix)) return false;
    if(ttl < r.ttl_min || ttl > r.ttl_max) return false;
    return true;
}

// Rotation ternaire équilibrée
inline int8_t tri_wave(uint64_t tick)
{
    switch(tick%3ull)
    {
    case 0:
        return -1;
    case 1:
        return 0;
    default:
        return +1;
    }
}
inline int8_t bal_from_prox(ProxClass pc)
{
    switch(pc)
    {
    case ProxClass::Local:
        return -1;
    case ProxClass::Near:
        return 0;
    case ProxClass::Far:
        return +1;
    default:
        return 0;
    }
}
inline uint32_t unb_from_bal_sum(int8_t a, int8_t b)
{
    int s=a+b;
    if(s<-1)s=-1;
    if(s>+1)s=+1;
    return (uint32_t)(s+1);
}
inline uint32_t seed_from(const BuildTag& t)
{
    return (uint32_t)(fnv1a64(t.domain) ^ (t.version*0x9E3779B185EBCA87ull) ^ t.radius_m);
}

// Superposition : candidats “tiers bas”
struct Cand
{
    std::string domain_prefix;
    bool is_member=false;
    uint32_t radius_max=0;
    uint8_t depth=0;
};
inline void collect_known_domains(const Policy& pol, std::vector<Cand>& out)
{
    out.clear();
    for(const auto& m: pol.memberships) out.push_back({m.domain_prefix,true, m.local_radius_m, (uint8_t)domain_depth(m.domain_prefix)});
    if(!pol.self.domain_prefix.empty()) out.push_back({pol.self.domain_prefix,true, pol.self.local_radius_m, (uint8_t)domain_depth(pol.self.domain_prefix)});
    for(const auto& cx: pol.coexist_allow) out.push_back({cx.domain_prefix,false,cx.radius_max_m,(uint8_t)domain_depth(cx.domain_prefix)});
}
inline bool share_root(const std::string& a, const std::string& b)
{
    return domain_root_of(a)==domain_root_of(b);
}
inline std::vector<Cand> overlap_bottom_candidates(const Policy& pol, const BuildTag& tag)
{
    std::vector<Cand> known;
    collect_known_domains(pol, known);
    std::vector<Cand> overlap;
    for(const auto& c: known) if(share_root(c.domain_prefix, tag.domain)) overlap.push_back(c);
    if(overlap.empty()) return {};
    uint8_t maxd=0;
    for(const auto& c: overlap) if(c.depth>maxd) maxd=c.depth;
    std::vector<Cand> bottom;
    for(const auto& c: overlap) if(c.depth==maxd)
        {
            if(c.is_member || tag.radius_m<=c.radius_max) bottom.push_back(c);
        }
    return bottom;
}

// Cache prep
inline Policy::Prep* find_prep(Policy& pol, const std::string& requester)
{
    for(auto& p: pol.prepared_cache) if(p.requester_domain==requester) return &p;
    return nullptr;
}
inline void tick_and_drop_preps(Policy& pol)
{
    for(auto& p: pol.prepared_cache)
    {
        if(p.window>0) --p.window;
    }
    pol.prepared_cache.erase(
        std::remove_if(pol.prepared_cache.begin(), pol.prepared_cache.end(),
                       [](const Policy::Prep& p)
    {
        return p.window==0 && p.prepared_target.empty();
    }),
    pol.prepared_cache.end());
}

// ------------------ Décision principale
inline DecisionEx decide_ex(const Policy& pol_const, const std::string& meta)
{
    Policy& pol = const_cast<Policy&>(pol_const); // MAJ rotor/cache autorisées
    tick_and_drop_preps(pol);

    DecisionEx R{};
    R.tag = extract_build_from_meta(meta);

    // 0) Garde-fous racines/profondeur
    if(!pol.allowed_roots.empty())
    {
        bool ok=false;
        for(const auto& root: pol.allowed_roots)
        {
            if(starts_with(R.tag.domain, root))
            {
                ok=true;
                break;
            }
        }
        if(!ok)
        {
            if(pol.on_unknown_sandbox) pol.on_unknown_sandbox(R.tag, meta, pol.user_data);
            return R;
        }
    }
    if(pol.max_depth>0 && domain_depth(R.tag.domain)>pol.max_depth)
    {
        if(pol.on_unknown_sandbox) pol.on_unknown_sandbox(R.tag, meta, pol.user_data);
        return R;
    }

    // 1) INTERNAL (memberships + compat self)
    bool internal=false;
    for(const auto& m: pol.memberships)
    {
        if(match_membership(m, R.tag))
        {
            internal=true;
            break;
        }
    }
    if(!internal && !pol.self.domain_prefix.empty() &&
            starts_with(R.tag.domain, pol.self.domain_prefix) &&
            match_prefix_hex(R.tag.build_hash, pol.self.hash_prefix_hex)) internal=true;
    if(internal)
    {
        R.decision=Decision::INTERNAL;
        return R;
    }

    // 2) INTERNAL via allow
    for(const auto& a: pol.internal_allow)
    {
        if(match_allow(a, R.tag))
        {
            R.decision=Decision::INTERNAL;
            return R;
        }
    }

    // 3) COEXIST externes (+ visuel)
    for(const auto& c: pol.coexist_allow)
    {
        if(match_coexist(c, R.tag))
        {
            if(!pol.visual_whitelist_domains.empty())
            {
                bool vis=false;
                for(const auto& v: pol.visual_whitelist_domains)
                {
                    if(starts_with(R.tag.domain, v))
                    {
                        vis=true;
                        break;
                    }
                }
                if(!vis) break;
            }
            R.decision=Decision::COEXIST_ACCEPTED;
            return R;
        }
    }

    // 4) AODV-light voisinage (meta-only)
    if(pol.query_neighbor_accept && pol.query_neighbor_accept(R.tag, pol.neighbor_user))
    {
        R.decision=Decision::COEXIST_ACCEPTED;
        return R;
    }

    // 5) Redirection contrôlée (TTL/hops)
    uint8_t ttl_cap = std::min<uint8_t>(R.tag.route_ttl, pol.ttl_global_max);
    if(ttl_cap>0 && R.tag.route_hops<pol.hops_global_max)
    {
        auto cands = pol.enable_overlap_redirect ? overlap_bottom_candidates(pol, R.tag) : std::vector<Cand> {};
        if(!cands.empty())
        {
            // Superposition présente ⇒ 2 tours stricts
            if(R.tag.route_phase < 1)
            {
                // TOUR 1: PREP
                uint32_t seed = seed_from(R.tag);
                int8_t w = tri_wave(pol.rotor.tick), r = bal_from_prox(R.tag.pclass);
                size_t idx = ((size_t)seed + (size_t)unb_from_bal_sum(w,r)) % cands.size();
                const auto& neighbor = cands[idx];

                if(pol.overlap_prepare_suggest)
                {
                    std::string second_target;
                    bool ok = pol.overlap_prepare_suggest(R.tag.domain, neighbor.domain_prefix, R.tag,
                                                          second_target, pol.overlap_prep_user);
                    if(ok && !second_target.empty())
                    {
                        if(Policy::Prep* p=find_prep(pol, R.tag.domain))
                        {
                            p->prepared_target=second_target;
                            p->window=1;
                        }
                        else
                        {
                            pol.prepared_cache.push_back({R.tag.domain, second_target, 1});
                        }
                    }
                }
                pol.rotor.tick++; // avance la rotation
                return R; // phase reste gérée côté route_helper
            }

            // TOUR 2: ACCEPT si une préparation existe
            if(Policy::Prep* p = find_prep(pol, R.tag.domain))
            {
                bool ok = true;
                if(pol.overlap_second_accept)
                {
                    ok = pol.overlap_second_accept(R.tag.domain, p->prepared_target, R.tag, pol.overlap_accept_user);
                }
                if(ok && !p->prepared_target.empty())
                {
                    R.next.should_redirect = true;
                    R.next.target_domain   = p->prepared_target;
                    R.next.ttl_after       = (uint8_t)(ttl_cap - 1);
                    p->prepared_target.clear();
                    p->window=0; // consommé
                    pol.rotor.tick++;
                    return R;
                }
                // refus au tour 2 → SANDBOX
                p->prepared_target.clear();
                p->window=0;
                if(pol.on_unknown_sandbox) pol.on_unknown_sandbox(R.tag, meta, pol.user_data);
                return R;
            }

            // Pas de préparation trouvée au tour 2 → SANDBOX
            if(pol.on_unknown_sandbox) pol.on_unknown_sandbox(R.tag, meta, pol.user_data);
            return R;
        }

        // Pas de superposition → fallbacks optionnels
        for(const auto& r: pol.redirects)
        {
            if(match_redirect(r, R.tag, ttl_cap))
            {
                R.next.should_redirect=true;
                R.next.target_domain=r.to_domain_prefix;
                R.next.ttl_after=(uint8_t)(ttl_cap-1);
                return R;
            }
        }
        for(const auto& m: pol.memberships)
        {
            if(!starts_with(m.domain_prefix, R.tag.domain))
            {
                R.next.should_redirect=true;
                R.next.target_domain=m.domain_prefix;
                R.next.ttl_after=(uint8_t)(ttl_cap-1);
                return R;
            }
        }
        for(const auto& c: pol.coexist_allow)
        {
            R.next.should_redirect=true;
            R.next.target_domain=c.domain_prefix;
            R.next.ttl_after=(uint8_t)(ttl_cap-1);
            return R;
        }
    }

    // 6) Sandbox (meta-only)
    if(pol.on_unknown_sandbox) pol.on_unknown_sandbox(R.tag, meta, pol.user_data);
    return R; // UNKNOWN_SANDBOX
}

// ------------------ Décision simple → approve()
inline Decision decide(const Policy& pol, const std::string& meta)
{
    return decide_ex(pol, meta).decision;
}

// ------------------ Adaptateurs approve() pour t3p/t3v
inline bool t3p_approve_with_policy(const char* meta_json, void* user)
{
    if(!user || !meta_json) return false;
    const Policy* pol = reinterpret_cast<const Policy*>(user);
    Decision d = decide(*pol, std::string(meta_json));
    return (d==Decision::INTERNAL || d==Decision::COEXIST_ACCEPTED);
}
inline bool t3v_approve_with_policy(uint64_t /*idx*/, const char* meta_frame_json, void* user)
{
    if(!user || !meta_frame_json) return false;
    const Policy* pol = reinterpret_cast<const Policy*>(user);
    Decision d = decide(*pol, std::string(meta_frame_json));
    return (d==Decision::INTERNAL || d==Decision::COEXIST_ACCEPTED);
}

} // namespace T3Security
