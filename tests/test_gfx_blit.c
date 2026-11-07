#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/gfx.h"
int main(void) {
    uint32_t in[3*7+2],out[8*12+2],expected[8*12+2];
    for(unsigned i=0;i<sizeof(in)/sizeof(*in);++i)in[i]=0x00a00000u+i;
    chos_surface_t src={in+1,5,3,7},dst={out+1,9,8,12};
    const int positions[]={INT32_MIN,-10,-2,-1,0,1,3,7,9,INT32_MAX};
    const int clips[][4]={{0,0,9,8},{2,1,4,3},{-2,-1,5,4},{0,0,0,8},{INT32_MAX,0,10,8}};
    unsigned cases=0;
    for(unsigned scale=1;scale<=4;++scale)for(unsigned xi=0;xi<10;++xi)for(unsigned yi=0;yi<10;++yi)
    for(unsigned c=0;c<5;++c){
        for(unsigned i=0;i<sizeof(out)/sizeof(*out);++i)out[i]=expected[i]=0x12345678;
        int x=positions[xi],y=positions[yi];const int *clip=clips[c];
        chos_gfx_set_clip(&dst,clip[0],clip[1],clip[2],clip[3]);
        /* Independent forward oracle: expand each source pixel into its cells. */
        for(unsigned sy=0;sy<3;++sy)for(unsigned sx=0;sx<5;++sx)
        for(unsigned py=0;py<scale;++py)for(unsigned px=0;px<scale;++px){
            int64_t dx=(int64_t)x+sx*scale+px,dy=(int64_t)y+sy*scale+py;
            if(dx>=0&&dx<9&&dy>=0&&dy<8&&dx>=clip[0]&&dy>=clip[1]&&
               dx<(int64_t)clip[0]+clip[2]&&dy<(int64_t)clip[1]+clip[3])
                expected[1+dy*12+dx]=in[1+sy*7+sx]|0xff000000u;
        }
        assert(chos_gfx_blit_xrgb_scaled(&dst,x,y,&src,scale));
        assert(!memcmp(out,expected,sizeof(out)));++cases;
        chos_gfx_reset_clip(&dst);
    }
    memcpy(expected,out,sizeof(out));
    assert(!chos_gfx_blit_xrgb_scaled(NULL,0,0,&src,1));
    assert(!chos_gfx_blit_xrgb_scaled(&dst,0,0,NULL,1));
    assert(!chos_gfx_blit_xrgb_scaled(&dst,0,0,&src,0));
    assert(!chos_gfx_blit_xrgb_scaled(&dst,0,0,&src,5));
    src.stride_pixels=4;assert(!chos_gfx_blit_xrgb_scaled(&dst,0,0,&src,1));
    assert(!memcmp(out,expected,sizeof(out)));
    printf("XRGB blit: %u forward-oracle cases,scale1..4,clipping,extreme coordinates,stride/canaries and invalid-input preservation passed\n",cases);
}
