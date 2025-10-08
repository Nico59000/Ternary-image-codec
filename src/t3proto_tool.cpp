// ============================================================================
//  File: src/t3proto_tool.cpp — CLI pour .t3proto (prototypes)
//  Project: Ternary Image/Video Codec v6
//
//  COMMANDES
//  ---------
//  t3proto_tool encode --in img.png --out stream.t3proto --profile {haar|rc}
//                      [--no-pack] [--no-balanced]
//                      [--haar-tile 8 --haar-thresh 6]
//                      [--rc-block 32 --rc-angles 8 --rc-z 1.2]
//
//  t3proto_tool info   stream.t3proto [--json]
//
//  t3proto_tool export-unb  stream.t3proto --out tri_unb.bin
//  t3proto_tool export-bal  stream.t3proto --out tri_bal.bin
//
//  t3proto_tool repack      in.t3proto --to {packed|balanced} --out out.t3proto
//                           [--keep-balanced] [--keep-packed]
//                           [--n-trits N] [--guess] [--strict]
//                           [--force-exact N]   # = --to balanced --n-trits N --strict
//
//  t3proto_tool cat         --out merged.t3proto a.t3proto b.t3proto ...
//                           [--require-balanced] [--require-packed]
//
//  BUILD
//  -----
//   g++ -std=c++17 -O2 -Iinclude -Ithird_party \
//       src/compile_stb.cpp src/t3proto_tool.cpp -o t3proto_tool
//
//  NOTE
//  ----
//   • "encode" nécessite les prototypes (flags -DPROTO_*).
//   • "info/export/repack/cat" n'ont pas besoin des prototypes pour lire/écrire.
// ============================================================================

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cctype>

#include "io_image.hpp"         // ImageU8, load_image_rgb8(...)
#include "codec_profiles.hpp"   // encode_prototype_ternary(...), ProtoConfig, pack/unpack helpers
#include "io_t3proto.hpp"       // t3proto::{t3proto_write,t3proto_read}

// ---------------------------------------------------------------------- Usage
static void usage()
{
    std::cerr <<
              "t3proto_tool encode --in <img> --out <file.t3proto> --profile {haar|rc}\n"
              "                   [--no-pack] [--no-balanced]\n"
              "                   [--haar-tile N] [--haar-thresh T]\n"
              "                   [--rc-block N] [--rc-angles A] [--rc-z Z]\n"
              "t3proto_tool info <file.t3proto> [--json]\n"
              "t3proto_tool export-unb  <file.t3proto> --out tri_unb.bin\n"
              "t3proto_tool export-bal  <file.t3proto> --out tri_bal.bin\n"
              "t3proto_tool repack <in.t3proto> --to {packed|balanced} --out <out.t3proto>\n"
              "                   [--keep-balanced] [--keep-packed] [--n-trits N] [--guess] [--strict]\n"
              "                   [--force-exact N]\n"
              "t3proto_tool cat --out merged.t3proto <a.t3proto> <b.t3proto> ...\n"
              "                   [--require-balanced] [--require-packed]\n";
}
static bool eqi(const std::string& a, const char* b)
{
    if(a.size()!=std::strlen(b)) return false;
    for(size_t i=0; i<a.size(); ++i) if(std::tolower(a[i])!=std::tolower(b[i])) return false;
    return true;
}

// -------- helpers très simples pour extraire des infos de meta JSON (sans lib)
static bool meta_find_int(const std::string& meta, const std::string& key, uint64_t& out)
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
        val = val*10 + (meta[pos]-'0');
        ++pos;
    }
    if(!any) return false;
    out=val;
    return true;
}
static bool meta_find_str(const std::string& meta, const std::string& key, std::string& out)
{
    auto pos = meta.find("\""+key+"\"");
    if(pos==std::string::npos) return false;
    pos = meta.find(':', pos);
    if(pos==std::string::npos) return false;
    pos = meta.find('"', pos);
    if(pos==std::string::npos) return false;
    ++pos;
    auto end = meta.find('"', pos);
    if(end==std::string::npos) return false;
    out = meta.substr(pos, end-pos);
    return true;
}

// -------- lecture rapide du header .t3proto pour peek n_trits/n_bytes/flags
namespace peek
{
struct Counts
{
    uint64_t n_trits=0, n_bytes=0;
    uint16_t flags=0;
};
inline bool read_counts(const std::string& path, Counts& C)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if(!f) return false;
    char magic[4];
    if(std::fread(magic,1,4,f)!=4)
    {
        std::fclose(f);
        return false;
    }
    if(std::memcmp(magic,"T3PT",4)!=0)
    {
        std::fclose(f);
        return false;
    }
    uint8_t ver=0, prof=0;
    uint16_t flags=0;
    uint32_t W=0,H=0;
    uint64_t ntr=0,nby=0;
    uint32_t mlen=0;
    auto rd_u16=[&](uint16_t& v)
    {
        uint8_t b[2];
        if(std::fread(b,1,2,f)!=2) return false;
        v=uint16_t(b[0]|(uint16_t(b[1])<<8));
        return true;
    };
    auto rd_u32=[&](uint32_t& v)
    {
        uint8_t b[4];
        if(std::fread(b,1,4,f)!=4) return false;
        v=uint32_t(b[0]|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24));
        return true;
    };
    auto rd_u64=[&](uint64_t& v)
    {
        uint8_t b[8];
        if(std::fread(b,1,8,f)!=8) return false;
        v=0;
        for(int i=7; i>=0; --i) v=(v<<8)|b[i];
        return true;
    };
    if(std::fread(&ver,1,1,f)!=1 || ver!=1)
    {
        std::fclose(f);
        return false;
    }
    if(std::fread(&prof,1,1,f)!=1)
    {
        std::fclose(f);
        return false;
    }
    if(!rd_u16(flags))
    {
        std::fclose(f);
        return false;
    }
    if(!rd_u32(W) || !rd_u32(H))
    {
        std::fclose(f);
        return false;
    }
    if(!rd_u64(ntr) || !rd_u64(nby))
    {
        std::fclose(f);
        return false;
    }
    if(!rd_u32(mlen))
    {
        std::fclose(f);
        return false;
    }
    if(mlen>0) std::fseek(f, (long)mlen, SEEK_CUR);
    std::fclose(f);
    C.n_trits=ntr;
    C.n_bytes=nby;
    C.flags=flags;
    return true;
}
}

// --------- injecter/mettre-à-jour les clés dans le bloc "counts" du meta JSON
static void meta_upsert_counts(std::string& meta,
                               uint64_t ntr, uint64_t pbytes,
                               int tail, bool exact)
{
    auto ensure_counts_block = [&](std::string& js)
    {
        auto pos = js.find("\"counts\"");
        if(pos!=std::string::npos) return; // déjà présent
        // insérer , "counts":{...} avant la dernière '}'
        std::string counts = std::string("\"counts\":{\"n_trits\":") + std::to_string(ntr) +
                             ",\"tail_trits\":" + std::to_string(std::max(0,tail)) +
                             ",\"packed_bytes\":" + std::to_string(pbytes) +
                             ",\"exact_n_trits\":" + (exact ? "true":"false") + "}";
        if(!js.empty() && js.front()=='{' && js.back()=='}')
        {
            size_t last = js.find_last_of('}');
            if(last!=std::string::npos && last>0)
            {
                js.insert(last, std::string(js.size()>2? ", ":" ")+counts);
                return;
            }
        }
        js = std::string("{") + counts + "}";
    };

    ensure_counts_block(meta);

    auto replace_key = [&](const std::string& key, const std::string& val, bool quote)
    {
        auto pos = meta.find("\"counts\"");
        if(pos==std::string::npos) return;
        pos = meta.find('{', pos);
        if(pos==std::string::npos) return;
        size_t end = meta.find('}', pos);
        if(end==std::string::npos) return;
        auto kpos = meta.find("\""+key+"\"", pos);
        if(kpos==std::string::npos || kpos>end)
        {
            std::string ins = std::string(",\"")+key+"\": "+(quote?"\"":"")+val+(quote?"\"":"");
            meta.insert(end, ins);
            return;
        }
        auto cpos = meta.find(':', kpos);
        if(cpos==std::string::npos || cpos>end) return;
        ++cpos;
        while(cpos<meta.size() && (meta[cpos]==' '||meta[cpos]=='\t')) ++cpos;
        size_t vstart=cpos, vend=vstart;
        bool inq = (meta[vstart]=='"');
        if(inq)
        {
            ++vstart;
            vend = meta.find('"', vstart);
        }
        else
        {
            while(vend<meta.size() && vend<=end && meta[vend]!=',' && meta[vend]!='}') ++vend;
        }
        if(vend==std::string::npos || vend>meta.size()) vend = end;
        std::string newv = (quote? "\"":"") + val + (quote? "\"": "");
        meta.replace(inq? vstart-1 : vstart, (inq? (vend-vstart+2):(vend-vstart)), newv);
    };

    replace_key("n_trits",       std::to_string(ntr), false);
    replace_key("tail_trits",    std::to_string(std::max(0,tail)), false);
    replace_key("packed_bytes",  std::to_string(pbytes), false);
    replace_key("exact_n_trits", (exact? "true":"false"), false);
}

// ============================================================================
// main
// ============================================================================
int main(int argc, char** argv)
{
    if(argc<2)
    {
        usage();
        return 2;
    }
    std::string cmd = argv[1];

    // ------------------------------------------------------------------ ENCODE
    if(cmd=="encode")
    {
        std::string in, out, profile;
        bool want_pack=true, want_bal=true;
        ProtoConfig cfg{};
        cfg.profile = ProtoProfile::None;

        for(int i=2; i<argc; ++i)
        {
            std::string s=argv[i];
            if(s=="--in" && i+1<argc) in=argv[++i];
            else if(s=="--out" && i+1<argc) out=argv[++i];
            else if(s=="--profile" && i+1<argc)
            {
                profile=argv[++i];
            }
            else if(s=="--no-pack")     want_pack=false;
            else if(s=="--no-balanced") want_bal=false;
            // Haar overrides
            else if(s=="--haar-tile" && i+1<argc)   cfg.haar_tile=std::atoi(argv[++i]);
            else if(s=="--haar-thresh" && i+1<argc) cfg.haar_thresh=std::atoi(argv[++i]);
            // Aniso RC overrides
            else if(s=="--rc-block" && i+1<argc)   cfg.rc_block=std::atoi(argv[++i]);
            else if(s=="--rc-angles" && i+1<argc)  cfg.rc_angles=std::atoi(argv[++i]);
            else if(s=="--rc-z" && i+1<argc)       cfg.rc_tern_z=(float)std::atof(argv[++i]);
        }
        if(in.empty()||out.empty()||profile.empty())
        {
            usage();
            return 2;
        }
        if(eqi(profile,"haar")) cfg.profile = ProtoProfile::HaarTernary;
        else if(eqi(profile,"rc")) cfg.profile = ProtoProfile::AnisoRC;
        else
        {
            std::cerr<<"unknown profile: "<<profile<<"\n";
            return 2;
        }

        ImageU8 rgb;
        if(!load_image_rgb8(in, rgb))
        {
            std::cerr<<"cannot load: "<<in<<"\n";
            return 1;
        }

        std::vector<int8_t>  bal;
        std::vector<uint8_t> bytes;
        std::string meta;

        cfg.pack_base243 = want_pack;

        if(!encode_prototype_available(cfg.profile))
        {
            std::cerr<<"profile not compiled in this build. Rebuild with -DPROTO_*.\n";
            return 1;
        }
        if(!encode_prototype_ternary(rgb, cfg, bal, (want_pack? &bytes: nullptr), meta))
        {
            std::cerr<<"encode_prototype_ternary failed.\n";
            return 1;
        }

        const std::vector<int8_t>*  pBal   = want_bal  ? &bal   : nullptr;
        const std::vector<uint8_t>* pBytes = want_pack ? &bytes : nullptr;

        // Assurer la présence des compteurs + exact_n_trits dans meta
        int tail = (int)(bal.size()%5);
        meta_upsert_counts(meta, (uint64_t)bal.size(), (uint64_t)bytes.size(), tail, /*exact=*/true);

        if(!t3proto::t3proto_write(out, cfg.profile, (uint32_t)rgb.w, (uint32_t)rgb.h,
                                   pBal, pBytes, meta))
        {
            std::cerr<<"t3proto_write failed: "<<out<<"\n";
            return 1;
        }
        std::cout<<"OK: wrote "<<out<<"  (trits="<<bal.size()<<", bytes="<<bytes.size()<<")\n";
        return 0;
    }

    // -------------------------------------------------------------------- INFO
    if(cmd=="info")
    {
        if(argc<3)
        {
            usage();
            return 2;
        }
        std::string path=argv[2];
        bool json=false;
        for(int i=3; i<argc; ++i)
        {
            if(std::string(argv[i])=="--json") json=true;
        }

        ProtoProfile prof=ProtoProfile::None;
        uint32_t W=0,H=0;
        std::string meta;
        if(!t3proto::t3proto_read(path, prof, W,H, nullptr, nullptr, &meta))
        {
            std::cerr<<"read failed: "<<path<<"\n";
            return 1;
        }
        std::vector<int8_t>  bal;
        std::vector<uint8_t> bytes;
        t3proto::t3proto_read(path, prof, W,H, &bal, &bytes, nullptr);

        auto pname=[&]()
        {
            switch(prof)
            {
            case ProtoProfile::HaarTernary:
                return "HaarTernary";
            case ProtoProfile::AnisoRC:
                return "AnisoRC";
            default:
                return "None";
            }
        };

        if(json)
        {
            std::cout << "{\n"
                      << "  \"t3proto\": {\n"
                      << "    \"file\": \""<<path<<"\",\n"
                      << "    \"profile\": \""<<pname()<<"\",\n"
                      << "    \"W\": "<<W<<", \"H\": "<<H<<",\n"
                      << "    \"trits\": "<<bal.size()<<", \"bytes\": "<<bytes.size()<<",\n"
                      << "    \"meta_len\": "<<meta.size()<<"\n"
                      << "  }\n}\n";
        }
        else
        {
            std::cout<<"== .t3proto ==\n"
                     <<"file: "<<path<<"\n"
                     <<"profile: "<<pname()<<"\n"
                     <<"dims: "<<W<<" x "<<H<<"\n"
                     <<"trits: "<<bal.size()<<"  bytes(pack): "<<bytes.size()<<"\n"
                     <<"meta_len: "<<meta.size()<<"\n";
        }
        return 0;
    }

    // --------------------------------------------------------------- EXPORT-UNB
    if(cmd=="export-unb")
    {
        if(argc<5)
        {
            usage();
            return 2;
        }
        std::string path=argv[2], out;
        for(int i=3; i<argc; ++i)
        {
            if(std::string(argv[i])=="--out" && i+1<argc) out=argv[++i];
        }
        if(out.empty())
        {
            usage();
            return 2;
        }

        ProtoProfile prof=ProtoProfile::None;
        uint32_t W=0,H=0;
        std::vector<int8_t> bal;
        if(!t3proto::t3proto_read(path, prof, W,H, &bal, nullptr, nullptr))
        {
            std::cerr<<"read failed: "<<path<<"\n";
            return 1;
        }
        if(bal.empty())
        {
            std::cerr<<"no balanced trits in file.\n";
            return 1;
        }

        std::FILE* f = std::fopen(out.c_str(), "wb");
        if(!f)
        {
            std::cerr<<"cannot open: "<<out<<"\n";
            return 1;
        }
        for(int8_t b : bal)
        {
            uint8_t u = trit_bal_to_unb(b);
            if(std::fwrite(&u,1,1,f)!=1)
            {
                std::fclose(f);
                std::cerr<<"write failed\n";
                return 1;
            }
        }
        std::fclose(f);
        std::cout<<"OK: wrote unbalanced trits to "<<out<<" ("<<bal.size()<<" bytes)\n";
        return 0;
    }

    // --------------------------------------------------------------- EXPORT-BAL
    if(cmd=="export-bal")
    {
        if(argc<5)
        {
            usage();
            return 2;
        }
        std::string path=argv[2], out;
        for(int i=3; i<argc; ++i)
        {
            if(std::string(argv[i])=="--out" && i+1<argc) out=argv[++i];
        }
        if(out.empty())
        {
            usage();
            return 2;
        }

        ProtoProfile prof=ProtoProfile::None;
        uint32_t W=0,H=0;
        std::vector<int8_t> bal;
        if(!t3proto::t3proto_read(path, prof, W,H, &bal, nullptr, nullptr))
        {
            std::cerr<<"read failed: "<<path<<"\n";
            return 1;
        }
        if(bal.empty())
        {
            std::cerr<<"no balanced trits in file.\n";
            return 1;
        }

        std::FILE* f = std::fopen(out.c_str(), "wb");
        if(!f)
        {
            std::cerr<<"cannot open: "<<out<<"\n";
            return 1;
        }
        for(int8_t b : bal)
        {
            if(std::fwrite(&b,1,1,f)!=1)
            {
                std::fclose(f);
                std::cerr<<"write failed\n";
                return 1;
            }
        }
        std::fclose(f);
        std::cout<<"OK: wrote balanced trits to "<<out<<" ("<<bal.size()<<" bytes)\n";
        return 0;
    }

    // ------------------------------------------------------------------- REPACK
    if(cmd=="repack")
    {
        if(argc<6)
        {
            usage();
            return 2;
        }
        std::string in=argv[2], to, out;
        bool keep_bal=false, keep_pack=false, guess=false, strict=false;
        bool force_exact=false;
        uint64_t n_trits_opt=0;
        for(int i=3; i<argc; ++i)
        {
            std::string s=argv[i];
            if(s=="--to" && i+1<argc) to=argv[++i];
            else if(s=="--out" && i+1<argc) out=argv[++i];
            else if(s=="--keep-balanced") keep_bal=true;
            else if(s=="--keep-packed")   keep_pack=true;
            else if(s=="--n-trits" && i+1<argc) n_trits_opt = (uint64_t)std::strtoull(argv[++i], nullptr, 10);
            else if(s=="--guess") guess=true;
            else if(s=="--strict") strict=true;
            else if(s=="--force-exact" && i+1<argc)
            {
                n_trits_opt = (uint64_t)std::strtoull(argv[++i], nullptr, 10);
                strict = true;
                force_exact = true;
                to = "balanced"; // override la cible
            }
        }
        if(in.empty()||out.empty()||to.empty())
        {
            usage();
            return 2;
        }

        ProtoProfile prof=ProtoProfile::None;
        uint32_t W=0,H=0;
        std::string meta;
        std::vector<int8_t>  bal;
        std::vector<uint8_t> bytes;

        if(!t3proto::t3proto_read(in, prof, W,H, &bal, &bytes, &meta))
        {
            std::cerr<<"read failed: "<<in<<"\n";
            return 1;
        }

        if(eqi(to,"packed"))
        {
            if(bytes.empty())
            {
                if(bal.empty())
                {
                    std::cerr<<"nothing to pack: no balanced trits in input.\n";
                    return 1;
                }
                pack_base243_from_balanced(bal, bytes);
            }
            int tail = (int)(bal.size()%5);
            meta_upsert_counts(meta, (uint64_t)bal.size(), (uint64_t)bytes.size(), tail, /*exact=*/true);

            const std::vector<int8_t>*  pBal   = keep_bal? &bal : nullptr;
            const std::vector<uint8_t>* pBytes = &bytes;
            if(!t3proto::t3proto_write(out, prof, W,H, pBal, pBytes, meta))
            {
                std::cerr<<"write failed: "<<out<<"\n";
                return 1;
            }
            std::cout<<"OK: repacked -> packed (bytes="<<bytes.size()<<")\n";
            return 0;
        }
        else if(eqi(to,"balanced"))
        {
            bool exact=false;
            uint64_t ntr = 0;

            if(force_exact)
            {
                // L’utilisateur force explicitement le nombre de trits exact
                ntr = n_trits_opt;
                exact = true;
            }
            else if(!bal.empty())
            {
                ntr = (uint64_t)bal.size();
                exact = true;
            }
            else
            {
                // déterminer n_trits
                peek::Counts C{};
                if(peek::read_counts(in, C) && C.n_trits>0)
                {
                    ntr = C.n_trits;
                    uint64_t tail=0, pbytes=0;
                    bool has_tail = meta_find_int(meta, "tail_trits", tail);
                    bool has_p    = meta_find_int(meta, "packed_bytes", pbytes);
                    if(has_tail) exact = true;
                    else if(has_p && (ntr % 5 == 0) && (ntr == pbytes*5)) exact = true;
                    else exact = false;
                }
                if(ntr==0)
                {
                    uint64_t lt=0, ls=0;
                    bool got = meta_find_int(meta, "len_tiles",  lt) |
                               meta_find_int(meta, "len_sketch", ls);
                    if(got && (lt+ls)>0)
                    {
                        ntr = lt+ls;
                        exact=true;
                    }
                }
                if(ntr==0)
                {
                    uint64_t tpb=0, blockN=0;
                    if(meta_find_int(meta, "trits_per_block", tpb) && meta_find_int(meta, "block", blockN) && blockN>0)
                    {
                        uint64_t bX = (W + blockN - 1) / blockN;
                        uint64_t bY = (H + blockN - 1) / blockN;
                        ntr = tpb * bX * bY;
                        if(ntr>0) exact=true;
                    }
                }
                if(ntr==0 && guess)
                {
                    peek::Counts C2{};
                    if(peek::read_counts(in, C2)) ntr = C2.n_bytes * 5ULL;
                    exact = false;
                }
                if(n_trits_opt>0)
                {
                    ntr = n_trits_opt;
                    exact = true;
                }

                if(ntr==0)
                {
                    std::cerr<<"cannot infer number of trits. Use --n-trits N or --guess.\n";
                    return 1;
                }
            }

            if(strict && !exact)
            {
                std::cerr<<"--strict: exact_n_trits is not guaranteed for this file.\n";
                return 1;
            }

            if(bal.empty())
            {
                unpack_base243_to_balanced(bytes, (size_t)ntr, bal);
            }

            uint64_t pbytes = (uint64_t)bytes.size();
            int tail = (int)(bal.size()%5);
            meta_upsert_counts(meta, (uint64_t)bal.size(), pbytes, tail, /*exact=*/exact);

            const std::vector<int8_t>*  pBal   = &bal;
            const std::vector<uint8_t>* pBytes = keep_pack? &bytes : nullptr;
            if(!t3proto::t3proto_write(out, prof, W,H, pBal, pBytes, meta))
            {
                std::cerr<<"write failed: "<<out<<"\n";
                return 1;
            }
            std::cout<<"OK: repacked -> balanced (trits="<<bal.size()<<", exact="<<(exact?"true":"false")<<")\n";
            return 0;
        }
        else
        {
            std::cerr<<"--to must be 'packed' or 'balanced'\n";
            return 2;
        }
    }

    // ---------------------------------------------------------------------- CAT
    if(cmd=="cat")
    {
        if(argc<5)
        {
            usage();
            return 2;
        }
        std::string out;
        bool req_bal=false, req_pack=false;
        std::vector<std::string> inputs;
        for(int i=2; i<argc; ++i)
        {
            std::string s=argv[i];
            if(s=="--out" && i+1<argc) out=argv[++i];
            else if(s=="--require-balanced") req_bal=true;
            else if(s=="--require-packed")   req_pack=true;
            else if(!s.empty() && s[0]!='-') inputs.push_back(s);
        }
        if(out.empty() || inputs.empty())
        {
            usage();
            return 2;
        }

        ProtoProfile prof_all=ProtoProfile::None;
        uint32_t W_all=0, H_all=0;
        bool have_bal_all=true, have_pack_all=true;
        std::vector<int8_t>  bal_cat;
        std::vector<uint8_t> bytes_cat;
        std::string meta_concat = "{\"concat\":[\n";
        for(size_t idx=0; idx<inputs.size(); ++idx)
        {
            const std::string& path = inputs[idx];
            ProtoProfile p=ProtoProfile::None;
            uint32_t W=0,H=0;
            std::string meta;
            std::vector<int8_t> bal;
            std::vector<uint8_t> bytes;
            if(!t3proto::t3proto_read(path, p, W,H, &bal, &bytes, &meta))
            {
                std::cerr<<"read failed: "<<path<<"\n";
                return 1;
            }
            if(idx==0)
            {
                prof_all=p;
                W_all=W;
                H_all=H;
            }
            else
            {
                if(p!=prof_all || W!=W_all || H!=H_all)
                {
                    std::cerr<<"incompatible file: "<<path<<" (profile/dims differ)\n";
                    return 1;
                }
            }
            have_bal_all  = have_bal_all  && !bal.empty();
            have_pack_all = have_pack_all && !bytes.empty();

            if(!bal.empty())   bal_cat.insert(bal_cat.end(), bal.begin(), bal.end());
            if(!bytes.empty()) bytes_cat.insert(bytes_cat.end(), bytes.begin(), bytes.end());

            meta_concat += std::string("  {\"file\":\"") + path + "\","
                           + "\"trits\":" + std::to_string(bal.size()) + ","
                           + "\"bytes\":" + std::to_string(bytes.size()) + "}";
            if(idx+1<inputs.size()) meta_concat += ",\n";
            else meta_concat += "\n";
        }
        meta_concat += "]}";

        if(req_bal && !have_bal_all)
        {
            std::cerr<<"--require-balanced: at least one input missing balanced trits.\n";
            return 1;
        }
        if(req_pack && !have_pack_all)
        {
            std::cerr<<"--require-packed: at least one input missing packed bytes.\n";
            return 1;
        }

        const std::vector<int8_t>*  pBal   = have_bal_all  ? &bal_cat   : nullptr;
        const std::vector<uint8_t>* pBytes = have_pack_all ? &bytes_cat : nullptr;

        if(!t3proto::t3proto_write(out, prof_all, W_all, H_all, pBal, pBytes, meta_concat))
        {
            std::cerr<<"write failed: "<<out<<"\n";
            return 1;
        }
        std::cout<<"OK: concatenated "<<inputs.size()<<" files -> "<<out
                 <<"  (trits="<<bal_cat.size()<<", bytes="<<bytes_cat.size()<<")\n";
        return 0;
    }

    usage();
    return 2;
}
