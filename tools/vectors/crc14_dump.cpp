// Dumps CRC14 oracle vectors using WSJT-X's own lib/crc14.cpp (Boost augmented_crc<14,0x2757>).
// Format per line: <hex 12 bytes> <crc14 decimal>
// Group A: encode174_91 packing — 77 message bits + 3 zero bits in bytes 0..9, bytes 10..11 zero.
// Group B: raw byte arrays of various lengths (1..16), to pin the generic function.
#include <cstdio>
#include <cstdint>
#include <cstring>
extern "C" { short crc14(unsigned char const*, int); bool crc14_check(unsigned char const*, int); }
static uint64_t s = 0x9E3779B97F4A7C15ULL; // fixed seed, xorshift64*
static uint64_t rnd(){ s ^= s>>12; s ^= s<<25; s ^= s>>27; return s*0x2545F4914F6CDD1DULL; }
int main(){
  unsigned char b[16];
  // Group A
  printf("# A: 77-bit message packed as encode174_91 does (bits MSB-first into 10 bytes, last 3 bits of byte 9 zero, bytes 10,11 zero); crc14 over 12 bytes\n");
  for(int t=0;t<200;t++){
    unsigned char bits[77];
    if(t==0) memset(bits,0,77); else if(t==1) memset(bits,1,77);
    else for(int i=0;i<77;i++) bits[i]=(rnd()>>40)&1;
    memset(b,0,12);
    for(int i=0;i<77;i++) if(bits[i]) b[i/8]|=0x80>>(i%8);
    int c=crc14(b,12);
    printf("A ");
    for(int i=0;i<12;i++) printf("%02x",b[i]);
    printf(" %d\n",c);
  }
  // Group B
  printf("# B: raw bytes, length L; crc14 over L bytes\n");
  for(int L=1;L<=16;L++) for(int t=0;t<4;t++){
    for(int i=0;i<L;i++) b[i]=(t==0)?0:(t==1?0xff:(rnd()>>32)&0xff);
    int c=crc14(b,L);
    printf("B %d ",L);
    for(int i=0;i<L;i++) printf("%02x",b[i]);
    printf(" %d\n",c);
  }
  // Group C: Boost augmented semantics — CRC placed in the LAST 14 bits of the 96 (bits 82..95);
  // crc14_check over the same 12 bytes must then be true. (WSJT-X itself never calls crc14_check for FT8;
  // this pins the generic function so a C port of crc14_check is testable.)
  printf("# C: 12 bytes = 82 payload bits + crc14 in the last 14 bits; crc14_check over 12 bytes (1=ok)\n");
  int ok=0;
  for(int t=0;t<50;t++){
    memset(b,0,12);
    for(int i=0;i<82;i++) if((rnd()>>40)&1) b[i/8]|=0x80>>(i%8);
    int c=crc14(b,12);
    b[10]|=(c>>8)&0x3f; b[11]=c&0xff;
    bool chk=crc14_check(b,12); ok+=chk;
    printf("C ");
    for(int i=0;i<12;i++) printf("%02x",b[i]);
    printf(" %d\n",chk?1:0);
  }
  fprintf(stderr,"C round-trips ok: %d/50\n",ok);
  return 0;
}
