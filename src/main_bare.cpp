// ============================================================================
//  File: src/main_bare.cpp — Démo minimale & auto-tests
//  Build rapide (ex. g++ -std=c++17 -O2 main_bare.cpp)
// ============================================================================
#include <iostream>
#include <vector>
#include "ternary_image_codec_v6_min.hpp"
#include "header_inline_impl.hpp"
#include "t3p_io.hpp"
#include "t3v_io.hpp"

int main(){
    // Quick self-tests
    bool ok_rs  = selftest_rs_unit();
    bool ok_api = selftest_api_roundtrip();
    std::cout << "RS unit: " << (ok_rs?"OK":"FAIL") << "\n";
    std::cout << "API roundtrip: " << (ok_api?"OK":"FAIL") << "\n";
    if(!(ok_rs&&ok_api)) return 1;

    // Petit exemple: fabriquer 10 pixels synthétiques et les écrire en RAW-N S21
    std::vector<PixelYCbCrQuant> px(10);
    for(size_t i=0;i<px.size();++i){ px[i].Yq=(uint16_t)((i*11)%243); px[i].Cbq=(int16_t)((int)i%81-40); px[i].Crq=(int16_t)((int)(i*2)%81-40); }

    std::vector<Word27> words;
    encode_raw_pixels_to_words_subword(px, SubwordMode::S21, words);

    // Sauvegarde .t3v mono-frame
    t3v::write_single("demo_s21.t3v", words);

    // Export .t3p en UTrit (extraction N=21)
    std::vector<UTrit> ut; extract_subword_stream_from_words(words, (int)SubwordMode::S21, ut);
    t3p::write("demo_s21.t3p", ut);

    std::cout << "Wrote demo_s21.t3v and demo_s21.t3p\n";
    return 0;
}
