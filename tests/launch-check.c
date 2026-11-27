#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "launch-check.h"

struct fake {
    bool partition, fat32, fail, cycle, cancel, bad_chain;
};
static void put(uint8_t *p, uint32_t v, unsigned n) {
    for(unsigned i=0;i<n;i++) p[i]=v>>(i*8);
}
static bool read(void *context, uint32_t sector, uint8_t b[512]) {
    struct fake *f=context;
    memset(b, 0, 512);
    b[510]=0x55; b[511]=0xaa;
    unsigned start=f->partition ? 63 : 0;
    if (f->partition && !sector) {
        b[446+4]=f->fat32 ? 0xc : 6;
        put(b+446+8,start,4); put(b+446+12,0x300000,4);
        return true;
    }
    if (sector==start) {
        b[0]=0xeb; put(b+11,512,2); b[13]=32;
        put(b+14,1,2); b[16]=2;
        put(b+17,f->fat32 ? 0 : 512,2);
        put(b+22,f->fat32 ? 0 : 64,2);
        put(b+32,f->fat32 ? 0x280000 : 0x100000,4);
        put(b+36,f->fat32 ? 1024 : 0,4);
        if (!f->fat32) put(b+22,256,2);
        if (f->fat32) b[40]=0x81; /* FAT 2 active. */
        return true;
    }
    assert(sector==start+1+(f->fat32 ? 1024 : 0));
    if (f->fail) return false;
    unsigned width=f->fat32 ? 4 : 2;
    put(b+width*2, f->bad_chain ? 0 : 7, width);
    put(b+width*7, f->cycle ? 2 : 0x0ffffff8, width);
    return true;
}
static bool proceed(void *context) { return !((struct fake *)context)->cancel; }
int main(void) {
    struct fake f={0};
    struct nrio_launch_chain chain;
    for (int i=0; i<3; i++) {
        f.partition=i!=0; f.fat32=i==2;
        assert(nrio_check_launch_chain(read,proceed,&f,2,0x4001,&chain)==NRIO_LAUNCH_CHAIN_OK);
        assert(chain.runs==2 && chain.last_sector-chain.first_sector==5*32);
    }
    f.cycle=true;
    assert(nrio_check_launch_chain(read,proceed,&f,2,0x4001,&chain)==NRIO_LAUNCH_CHAIN_INVALID);
    f.cycle=false; f.bad_chain=true;
    assert(nrio_check_launch_chain(read,proceed,&f,2,0x4001,&chain)==NRIO_LAUNCH_CHAIN_INVALID);
    f.bad_chain=false; f.fail=true;
    assert(nrio_check_launch_chain(read,proceed,&f,2,0x4001,&chain)==NRIO_LAUNCH_CHAIN_IO);
    f.fail=false; f.cancel=true;
    assert(nrio_check_launch_chain(read,proceed,&f,2,0x4001,&chain)==NRIO_LAUNCH_CHAIN_CANCELLED);
    puts("FAT ROM-chain checks passed");
}
