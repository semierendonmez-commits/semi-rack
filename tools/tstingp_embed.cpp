// Generator: g++ -std=c++17 -Iengines tools/tstingp_embed.cpp engines/tstingp/IngpEngine.cpp (reads /home/claude/tst-ingp/Resources).
#include "tstingp/Sdif.h"
#include "tstingp/IngpEngine.h"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace tstingp;
static std::string slurp(const std::string& p){std::ifstream f(p,std::ios::binary);std::stringstream s;s<<f.rdbuf();return s.str();}
int main(){
  const char* R="/home/claude/tst-ingp/Resources/";
  printf("// Generated from the plugin's Resources (texts/1..9.txt, sdif/*.sdif) for the Rack port.\n");
  printf("// Texts: only the bytes that reach the 2048-bit field are kept. SDIF: only the frames the\n");
  printf("// factory presets use, already turned into slider values (valuesToSliders(frameToValues())).\n");
  printf("// The SDIF files themselves (4 MB) are not shipped; Load SDIF... reads any .sdif file.\n#pragma once\n#include <cstdint>\nnamespace tstingp_data {\n");
  printf("struct Text { const char* name; int len; const uint8_t* bytes; };\n");
  for(int i=1;i<=9;i++){
    std::string t=slurp(std::string(R)+"texts/"+std::to_string(i)+".txt");
    // keep bytes until 2048 bits
    size_t bits=0,n=0; for(;n<t.size()&&bits<2048;n++){unsigned char c=t[n]; if(c=='\r'||c=='\n'||c==0) continue; int b=0; for(unsigned v=c;v;v>>=1) b++; bits+=b;}
    printf("static const uint8_t kText%d[] = {",i);
    for(size_t k=0;k<n;k++) printf("%s%u",k?",":"",(unsigned char)t[k]);
    printf("};\n");
  }
  printf("static const Text kTexts[9] = {");
  for(int i=1;i<=9;i++) printf("{\"%d.txt\", (int) sizeof kText%d, kText%d},",i,i,i);
  printf("};\n");
  struct P{const char* f; double ms, lvl; const char* id;} ps[]={{"gtrr004",400,10,"Wasp"},{"gtr_001_02",1500,7,"Embodied"},{"gtrr001",250,3,"Garage"},{"gtrr006",2000,10,"Freeze"},{"gtrr002",900,10,"Slow"}};
  printf("struct SdifSliders { float freq[32], amp[32]; };\n");
  for(auto&p:ps){
    std::string d=slurp(std::string(R)+"sdif/"+p.f+".sdif"); SdifFile f; if(!f.parse(d.data(),d.size())){fprintf(stderr,"fail %s\n",p.f);return 1;}
    auto* fr=f.frameAt(p.ms*0.001); auto s=valuesToSliders(frameToValues(*fr),1.0,p.lvl);
    printf("static const SdifSliders kSdif%s = { {",p.id);
    for(int v=0;v<32;v++) printf("%s%#.6gf",v?",":"",v<(int)s.freq.size()?s.freq[v]:0.f);
    printf("}, {"); for(int v=0;v<32;v++) printf("%s%#.6gf",v?",":"",v<(int)s.amp.size()?s.amp[v]:0.f);
    printf("} }; // %s @ %.0f ms, level %.0f\n",p.f,p.ms,p.lvl);
  }
  printf("} // namespace tstingp_data\n");
}
