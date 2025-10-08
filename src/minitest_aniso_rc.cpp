// ============================================================================
//  File: src/minitest_aniso_rc.cpp — Test ridgelet/curvelet-lite
//  Build:
//    g++ -std=c++17 -O2 -Iinclude -Ithird_party \
//        src/compile_stb.cpp src/minitest_aniso_rc.cpp -o minitest_aniso_rc
//  Run:
//    ./minitest_aniso_rc --in input.jpg --png-out recon_rc.png --json > report_rc.json
// ============================================================================

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <iomanip>

#include "io_image.hpp"
#include "proto_aniso_rc.hpp"

static double psnr_y(const ImageU8& a, const ImageU8& b){
    if(a.w!=b.w||a.h!=b.h||a.c!=1||b.c!=1) return 0.0;
    double mse=0.0; size_t N=(size_t)a.w*a.h;
    for(size_t i=0;i<N;++i){ double d=(double)a.data[i]- (double)b.data[i]; mse += d*d; }
    mse/= (double)N; if(mse<=1e-12) return 99.0;
    return 10.0*std::log10(255.0*255.0/mse);
}

struct Args{
    std::string in;
    std::string png="recon_rc.png";
    bool json=false;
};
static bool parse(int argc,char**argv, Args& A){
    for(int i=1;i<argc;++i){
        std::string s=argv[i];
        if(s=="--in" && i+1<argc) A.in=argv[++i];
        else if(s=="--png-out" && i+1<argc) A.png=argv[++i];
        else if(s=="--json") A.json=true;
    }
    return !A.in.empty();
}

int main(int argc,char**argv){
    Args A{}; if(!parse(argc,argv,A)){
        std::cerr<<"Usage: "<<argv[0]<<" --in <image> [--png-out recon_rc.png] [--json]\n";
        return 2;
    }

    ImageU8 rgb;
    if(!load_image_rgb8(A.in, rgb)){ std::cerr<<"cannot load: "<<A.in<<"\n"; return 1; }

    // Référence Y
    ImageU8 Yref; Yref.w=rgb.w; Yref.h=rgb.h; Yref.c=1; Yref.data.assign((size_t)rgb.w*rgb.h,0);
    for(int y=0;y<rgb.h;++y){
        for(int x=0;x<rgb.w;++x){
            const uint8_t* p=&rgb.data[(size_t)(y*rgb.w+x)*3];
            uint8_t Y,Cb,Cr; rgb_to_ycbcr(p[0],p[1],p[2],Y,Cb,Cr);
            Yref.data[(size_t)y*rgb.w+x]=Y;
        }
    }

    // Encode
    AnisoRCParams P;
    // équilibrage coût/ressources : blocs 32, 8 angles, z-th=1.2
    // (tu peux jouer : P.block=16 pour plus fin / plus coûteux)
    AnisoRCArtifacts ARC;
    proto_aniso_rc_encode(rgb, P, ARC);

    // Pack (sans entropie)
    std::vector<uint8_t> packed; proto_aniso_rc_pack(ARC, packed);

    // Reconstruction approx
    ImageU8 Yrec; proto_aniso_rc_reconstruct(ARC, P, Yrec);

    // Remap Y→RGB pour PNG
    ImageU8 reconRGB; reconRGB.w=Yrec.w; reconRGB.h=Yrec.h; reconRGB.c=3; reconRGB.data.assign((size_t)Yrec.w*Yrec.h*3,0);
    for(int y=0;y<Yrec.h;++y){
        for(int x=0;x<Yrec.w;++x){
            uint8_t Y=Yrec.data[(size_t)y*Yrec.w+x];
            uint8_t R,G,B; ycbcr_to_rgb(Y,128,128,R,G,B);
            uint8_t* p=&reconRGB.data[(size_t)(y*Yrec.w+x)*3];
            p[0]=R; p[1]=G; p[2]=B;
        }
    }
    save_image_png(A.png, reconRGB);

    // Métriques simples
    // On redimensionne Yref aux dims pad si besoin
    ImageU8 YrefPad = Yref;
    if(Yref.w!=Yrec.w || Yref.h!=Yrec.h){
        ImageU8 tmp; resize_rgb_nn(rgb, Yrec.w, Yrec.h, tmp);
        // re-extrait Y
        YrefPad.w=Yrec.w; YrefPad.h=Yrec.h; YrefPad.c=1; YrefPad.data.assign((size_t)Yrec.w*Yrec.h,0);
        for(int y=0;y<Yrec.h;++y){
            for(int x=0;x<Yrec.w;++x){
                const uint8_t* p=&tmp.data[(size_t)(y*tmp.w+x)*3];
                uint8_t Y,Cb,Cr; rgb_to_ycbcr(p[0],p[1],p[2],Y,Cb,Cr);
                YrefPad.data[(size_t)y*Yrec.w+x]=Y;
            }
        }
    }

    double psnr = psnr_y(YrefPad, Yrec);
    size_t trits = ARC.trits.size();
    size_t bytes = packed.size();
    double bpp   = (double)bytes*8.0 / (double)(ARC.W*ARC.H);

    if(A.json){
        std::cout<<"{\n"
                 <<"  \"aniso_rc\": {\n"
                 <<"    \"input\": \""<<A.in<<"\",\n"
                 <<"    \"W\": "<<ARC.W<<", \"H\": "<<ARC.H<<", \"block\": "<<P.block<<",\n"
                 <<"    \"angles\": "<<ARC.angles_used<<", \"proj_len\": "<<ARC.proj_len<<",\n"
                 <<"    \"trits\": "<<trits<<", \"packed_bytes\": "<<bytes<<", \"bpp\": "<<std::fixed<<std::setprecision(3)<<bpp<<",\n"
                 <<"    \"psnrY\": "<<std::setprecision(2)<<psnr<<",\n"
                 <<"    \"png\": \""<<A.png<<"\"\n"
                 <<"  }\n}\n";
    } else {
        std::cout<<"== aniso_rc ==\n"
                 <<"image: "<<A.in<<"\n"
                 <<"dims: "<<ARC.W<<" x "<<ARC.H<<"  block="<<P.block<<"\n"
                 <<"angles: "<<ARC.angles_used<<"  proj_len="<<ARC.proj_len<<"\n"
                 <<"trits: "<<trits<<"  packed_bytes="<<bytes<<"  bpp="<<std::fixed<<std::setprecision(3)<<bpp<<"\n"
                 <<"PSNR(Y): "<<std::setprecision(2)<<psnr<<" dB\n"
                 <<"out PNG: "<<A.png<<"\n";
    }

    return 0;
}
