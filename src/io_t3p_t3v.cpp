// ============================================================================
//  File: src/io_t3p_t3v.cpp — Simple T3P/T3V containers (DOC+)
// ============================================================================

#include "io_t3p_t3v.hpp"
#include <cstdio>
#include <cstring>
#include <cerrno>

namespace {

struct File {
    FILE* f=nullptr;
    ~File(){ if(f) std::fclose(f); }
    bool open(const std::string& p, const char* mode){ f=std::fopen(p.c_str(), mode); return f!=nullptr; }
};

static uint32_t crc32_acc(const void* data, size_t n){
    // CRC32 min (polynôme 0xEDB88320) — implémentation simplifiée
    static uint32_t table[256]; static bool init=false;
    if(!init){
        for(uint32_t i=0;i<256;++i){
            uint32_t c=i;
            for(int k=0;k<8;++k) c = (c&1)? (0xEDB88320u ^ (c>>1)) : (c>>1);
            table[i]=c;
        }
        init=true;
    }
    uint32_t c=0xFFFFFFFFu;
    const uint8_t* p=(const uint8_t*)data;
    for(size_t i=0;i<n;++i) c = table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

template<typename T>
static bool write_le(FILE* f, const T& v){
    return std::fwrite(&v, sizeof(T), 1, f)==1;
}
template<typename T>
static bool read_le(FILE* f, T& v){
    return std::fread(&v, sizeof(T), 1, f)==1;
}
static bool write_bytes(FILE* f, const void* p, size_t n){
    return std::fwrite(p, 1, n, f)==n;
}
static bool read_bytes(FILE* f, void* p, size_t n){
    return std::fread(p, 1, n, f)==n;
}

} // namespace

using namespace T3Container;

// =============================== .t3p =======================================

bool t3p_write(const std::string& path,
               SubwordMode sub, int w, int h,
               const std::vector<Word27>& words,
               const std::string& meta_json,
               std::string* err)
{
    File fp; if(!fp.open(path, "wb")){ if(err)*err=strerror(errno); return false; }

    const char magic[4] = {'T','3','P','6'};
    uint8_t ver = 6;
    uint8_t subu = (uint8_t)sub;
    uint16_t W = (uint16_t)w, H = (uint16_t)h;
    uint32_t meta_len = (uint32_t)meta_json.size();
    uint64_t words_count = (uint64_t)words.size();

    // Header sans CRC
    if(!write_bytes(fp.f, magic, 4)) goto io_err;
    if(!write_le(fp.f, ver)) goto io_err;
    if(!write_le(fp.f, subu)) goto io_err;
    if(!write_le(fp.f, W)) goto io_err;
    if(!write_le(fp.f, H)) goto io_err;
    if(!write_le(fp.f, meta_len)) goto io_err;
    if(!write_le(fp.f, words_count)) goto io_err;

    // CRC du header logique (hors magic/ver)
    {
        struct HdrCrcBuf {
            uint8_t ver, subu;
            uint16_t W, H;
            uint32_t meta_len;
            uint64_t words_count;
        } b{ver, subu, W, H, meta_len, words_count};
        uint32_t hdr_crc = crc32_acc(&b, sizeof(b));
        if(!write_le(fp.f, hdr_crc)) goto io_err;
    }

    // META (plaintext JSON)
    if(meta_len){
        if(!write_bytes(fp.f, meta_json.data(), meta_len)) goto io_err;
    }

    // Payload mots (LE)
    if(words_count){
        if(!write_bytes(fp.f, words.data(), sizeof(Word27)*words.size())) goto io_err;
        uint32_t pl_crc = crc32_acc(words.data(), sizeof(Word27)*words.size());
        if(!write_le(fp.f, pl_crc)) goto io_err;
    } else {
        uint32_t pl_crc = 0; if(!write_le(fp.f, pl_crc)) goto io_err;
    }

    return true;
io_err:
    if(err)*err="t3p_write: I/O error";
    return false;
}

bool t3p_read_header(const std::string& path,
                     SubwordMode& out_sub, int& out_w, int& out_h,
                     std::string& out_meta_json,
                     uint64_t& out_words_count,
                     std::string* err)
{
    out_meta_json.clear(); out_words_count=0; out_w=out_h=0; out_sub=SubwordMode::S27;

    File fp; if(!fp.open(path, "rb")){ if(err)*err=strerror(errno); return false; }
    char magic[4]; if(!read_bytes(fp.f, magic, 4)) goto io_err;
    if(std::memcmp(magic, "T3P6", 4)!=0){ if(err)*err="t3p: bad magic"; return false; }

    uint8_t ver=0, subu=0; uint16_t W=0,H=0; uint32_t meta_len=0; uint64_t words_count=0;
    if(!read_le(fp.f, ver)) goto io_err;
    if(!read_le(fp.f, subu)) goto io_err;
    if(!read_le(fp.f, W)) goto io_err;
    if(!read_le(fp.f, H)) goto io_err;
    if(!read_le(fp.f, meta_len)) goto io_err;
    if(!read_le(fp.f, words_count)) goto io_err;

    uint32_t hdr_crc=0; if(!read_le(fp.f, hdr_crc)) goto io_err;
    {
        struct HdrCrcBuf {
            uint8_t ver, subu;
            uint16_t W, H;
            uint32_t meta_len;
            uint64_t words_count;
        } b{ver, subu, W, H, meta_len, words_count};
        if(crc32_acc(&b, sizeof(b)) != hdr_crc){ if(err)*err="t3p: header crc mismatch"; return false; }
    }

    out_sub = (SubwordMode)subu; out_w=W; out_h=H; out_words_count=words_count;

    if(meta_len){
        out_meta_json.resize(meta_len);
        if(!read_bytes(fp.f, out_meta_json.data(), meta_len)) goto io_err;
    } else { out_meta_json.clear(); }

    return true;
io_err:
    if(err)*err="t3p_read_header: I/O error";
    return false;
}

bool t3p_read_payload(const std::string& path,
                      const ApproveMetaFn& approve_meta,
                      std::vector<Word27>& out_words,
                      std::string* err)
{
    out_words.clear();

    File fp; if(!fp.open(path, "rb")){ if(err)*err=strerror(errno); return false; }
    char magic[4]; if(!read_bytes(fp.f, magic, 4)) goto io_err;
    if(std::memcmp(magic, "T3P6", 4)!=0){ if(err)*err="t3p: bad magic"; return false; }

    uint8_t ver=0, subu=0; uint16_t W=0,H=0; uint32_t meta_len=0; uint64_t words_count=0;
    if(!read_le(fp.f, ver)) goto io_err;
    if(!read_le(fp.f, subu)) goto io_err;
    if(!read_le(fp.f, W)) goto io_err;
    if(!read_le(fp.f, H)) goto io_err;
    if(!read_le(fp.f, meta_len)) goto io_err;
    if(!read_le(fp.f, words_count)) goto io_err;

    uint32_t hdr_crc=0; if(!read_le(fp.f, hdr_crc)) goto io_err;
    {
        struct HdrCrcBuf {
            uint8_t ver, subu;
            uint16_t W, H;
            uint32_t meta_len;
            uint64_t words_count;
        } b{ver, subu, W, H, meta_len, words_count};
        if(crc32_acc(&b, sizeof(b)) != hdr_crc){ if(err)*err="t3p: header crc mismatch"; return false; }
    }

    std::string meta;
    if(meta_len){
        meta.resize(meta_len);
        if(!read_bytes(fp.f, meta.data(), meta_len)) goto io_err;
    }

    // === APPROVE META-ONLY ===
    if(approve_meta && !approve_meta(meta)){
        if(err)*err="t3p: meta not approved — payload not read";
        return false;
    }

    // === Read payload ===
    out_words.resize(words_count);
    if(words_count){
        if(!read_bytes(fp.f, out_words.data(), sizeof(Word27)*words_count)) goto io_err;
        uint32_t pl_crc=0; if(!read_le(fp.f, pl_crc)) goto io_err;
        if(crc32_acc(out_words.data(), sizeof(Word27)*words_count) != pl_crc){
            if(err)*err="t3p: payload crc mismatch"; return false;
        }
    } else {
        uint32_t pl_crc=0; if(!read_le(fp.f, pl_crc)) goto io_err;
        if(pl_crc!=0){ if(err)*err="t3p: payload crc mismatch (empty)"; return false; }
    }

    return true;
io_err:
    if(err)*err="t3p_read_payload: I/O error";
    return false;
}

// =============================== .t3v =======================================
// v6-min : header global + index simple (offset/words/meta_len par frame)

bool t3v_write(const std::string& path,
               SubwordMode sub, int w, int h,
               const std::vector<std::vector<Word27>>& frames,
               const std::string& meta_json_global,
               const std::vector<std::string>& metas_per_frame,
               std::string* err)
{
    File fp; if(!fp.open(path, "wb")){ if(err)*err=strerror(errno); return false; }

    const char magic[4] = {'T','3','V','6'};
    uint8_t ver=6, subu=(uint8_t)sub; uint16_t W=(uint16_t)w, H=(uint16_t)h;
    uint64_t frame_count = (uint64_t)frames.size();
    uint32_t meta_g_len  = (uint32_t)meta_json_global.size();

    // Header
    if(!write_bytes(fp.f, magic, 4)) goto io_err;
    if(!write_le(fp.f, ver)) goto io_err;
    if(!write_le(fp.f, subu)) goto io_err;
    if(!write_le(fp.f, W)) goto io_err;
    if(!write_le(fp.f, H)) goto io_err;
    if(!write_le(fp.f, frame_count)) goto io_err;
    if(!write_le(fp.f, meta_g_len)) goto io_err;

    // CRC header
    struct HdrBuf { uint8_t ver,subu; uint16_t W,H; uint64_t frame_count; uint32_t meta_g_len; }
    hb{ver,subu,W,H,frame_count,meta_g_len};
    uint32_t hdr_crc = crc32_acc(&hb, sizeof(hb));
    if(!write_le(fp.f, hdr_crc)) goto io_err;

    // Meta globale
    if(meta_g_len && !write_bytes(fp.f, meta_json_global.data(), meta_g_len)) goto io_err;

    // Placeholder index (sera réécrit ensuite)
    const long idx_pos = std::ftell(fp.f);
    std::vector<T3Container::T3VFrameIndex> index(frames.size());
    for(size_t i=0;i<frames.size();++i){
        uint64_t off=0, words=frames[i].size(); uint32_t ml= (metas_per_frame.size()==frames.size()) ? (uint32_t)metas_per_frame[i].size() : 0;
        if(!write_le(fp.f, off)) goto io_err; // offset placeholder
        if(!write_le(fp.f, words)) goto io_err;
        if(!write_le(fp.f, ml)) goto io_err;
    }

    // Frames data
    for(size_t i=0;i<frames.size();++i){
        index[i].offset = (uint64_t)std::ftell(fp.f);
        const std::string metaF = (metas_per_frame.size()==frames.size()) ? metas_per_frame[i] : std::string();
        index[i].meta_len = (uint32_t)metaF.size();
        index[i].words    = (uint64_t)frames[i].size();

        // Ecrire meta frame
        if(index[i].meta_len){
            if(!write_bytes(fp.f, metaF.data(), index[i].meta_len)) goto io_err;
        }
        // Ecrire mots
        if(index[i].words){
            if(!write_bytes(fp.f, frames[i].data(), sizeof(Word27)*frames[i].size())) goto io_err;
            uint32_t pl_crc = crc32_acc(frames[i].data(), sizeof(Word27)*frames[i].size());
            if(!write_le(fp.f, pl_crc)) goto io_err;
        } else {
            uint32_t pl_crc=0; if(!write_le(fp.f, pl_crc)) goto io_err;
        }
    }

    // Réécrire l’index avec offsets complets
    std::fseek(fp.f, idx_pos, SEEK_SET);
    for(size_t i=0;i<index.size();++i){
        if(!write_le(fp.f, index[i].offset)) goto io_err;
        if(!write_le(fp.f, index[i].words))  goto io_err;
        if(!write_le(fp.f, index[i].meta_len)) goto io_err;
    }
    return true;

io_err:
    if(err)*err="t3v_write: I/O error";
    return false;
}

bool t3v_read_header(const std::string& path,
                     SubwordMode& out_sub, int& out_w, int& out_h,
                     std::string& out_meta_json_global,
                     uint64_t& out_frame_count,
                     std::vector<T3VFrameIndex>& out_index,
                     std::string* err)
{
    out_meta_json_global.clear(); out_index.clear();
    out_sub=SubwordMode::S27; out_w=out_h=0; out_frame_count=0;

    File fp; if(!fp.open(path, "rb")){ if(err)*err=strerror(errno); return false; }
    char magic[4]; if(!read_bytes(fp.f, magic, 4)) goto io_err;
    if(std::memcmp(magic, "T3V6", 4)!=0){ if(err)*err="t3v: bad magic"; return false; }

    uint8_t ver=0, subu=0; uint16_t W=0,H=0; uint64_t frame_count=0; uint32_t meta_g_len=0;
    if(!read_le(fp.f, ver)) goto io_err;
    if(!read_le(fp.f, subu)) goto io_err;
    if(!read_le(fp.f, W)) goto io_err;
    if(!read_le(fp.f, H)) goto io_err;
    if(!read_le(fp.f, frame_count)) goto io_err;
    if(!read_le(fp.f, meta_g_len)) goto io_err;

    uint32_t hdr_crc=0; if(!read_le(fp.f, hdr_crc)) goto io_err;
    struct HdrBuf { uint8_t ver,subu; uint16_t W,H; uint64_t frame_count; uint32_t meta_g_len; }
    hb{ver,subu,W,H,frame_count,meta_g_len};
    if(crc32_acc(&hb, sizeof(hb)) != hdr_crc){ if(err)*err="t3v: header crc mismatch"; return false; }

    out_sub=(SubwordMode)subu; out_w=W; out_h=H; out_frame_count=frame_count;

    if(meta_g_len){
        out_meta_json_global.resize(meta_g_len);
        if(!read_bytes(fp.f, out_meta_json_global.data(), meta_g_len)) goto io_err;
    }

    out_index.resize((size_t)frame_count);
    for(size_t i=0;i<out_index.size();++i){
        if(!read_le(fp.f, out_index[i].offset)) goto io_err;
        if(!read_le(fp.f, out_index[i].words))  goto io_err;
        if(!read_le(fp.f, out_index[i].meta_len)) goto io_err;
    }
    return true;

io_err:
    if(err)*err="t3v_read_header: I/O error";
    return false;
}

bool t3v_read_frame(const std::string& path,
                    uint64_t frame_idx,
                    const ApproveMetaFn& approve_meta,
                    std::vector<Word27>& out_words,
                    std::string* err)
{
    out_words.clear();

    // Lire header + index
    SubwordMode sub; int W=0,H=0; std::string meta_g; uint64_t fc=0; std::vector<T3VFrameIndex> idx;
    if(!t3v_read_header(path, sub, W, H, meta_g, fc, idx, err)) return false;
    if(frame_idx >= fc){ if(err)*err="t3v: frame idx OOB"; return false; }

    File fp; if(!fp.open(path, "rb")){ if(err)*err=strerror(errno); return false; }
    // Aller au bloc frame
    const T3VFrameIndex& fi = idx[(size_t)frame_idx];
    if(std::fseek(fp.f, (long)fi.offset, SEEK_SET)!=0){ if(err)*err="t3v: seek frame failed"; return false; }

    // Lire meta frame
    std::string meta;
    if(fi.meta_len){
        meta.resize(fi.meta_len);
        if(!read_bytes(fp.f, meta.data(), fi.meta_len)){ if(err)*err="t3v: read frame meta failed"; return false; }
    }

    // === APPROVE META-ONLY ===
    if(approve_meta && !approve_meta(meta)){
        if(err)*err="t3v: meta not approved — frame payload not read";
        return false;
    }

    // Lire payload
    out_words.resize((size_t)fi.words);
    if(fi.words){
        if(!read_bytes(fp.f, out_words.data(), sizeof(Word27)*out_words.size())){ if(err)*err="t3v: read frame payload failed"; return false; }
        uint32_t pl_crc=0; if(!read_le(fp.f, pl_crc)){ if(err)*err="t3v: read frame crc failed"; return false; }
        if(crc32_acc(out_words.data(), sizeof(Word27)*out_words.size()) != pl_crc){
            if(err)*err="t3v: frame payload crc mismatch"; return false;
        }
    } else {
        uint32_t pl_crc=0; if(!read_le(fp.f, pl_crc)){ if(err)*err="t3v: read frame crc failed"; return false; }
        if(pl_crc!=0){ if(err)*err="t3v: empty frame crc mismatch"; return false; }
    }

    return true;
}
