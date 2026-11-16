/* Host-only fixture encoder. Link this file with the same verified original
 * libwebp source as the target, never with a substitute target library. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webp/encode.h>
#include <webp/decode.h>
static int write_file(const char *directory, const char *name, const void *data, size_t length) {
    char path[4096];
    if (snprintf(path,sizeof(path),"%s/%s",directory,name) >= (int)sizeof(path)) return 1;
    FILE *file=fopen(path,"wb"); if(!file) return 1;
    int failed=fwrite(data,1,length,file)!=length; return fclose(file)||failed;
}
static int image(const char *directory,const char *name,unsigned seed,int lossless) {
    enum { WIDTH=19,HEIGHT=13,STRIDE=WIDTH*4 };
    uint8_t rgba[HEIGHT*STRIDE];
    for(unsigned y=0;y<HEIGHT;++y)for(unsigned x=0;x<WIDTH;++x){
        uint8_t *p=&rgba[y*STRIDE+4*x];
        p[0]=(uint8_t)(x*17+y*3+seed*53);p[1]=(uint8_t)(x*5+y*29+seed*31);
        p[2]=(uint8_t)(x*11+y*7+seed*97);p[3]=(uint8_t)((x+y+seed)%5 ? 255 : (x*13+y*19+seed)%255);
    }
    WebPConfig config; WebPPicture picture; WebPMemoryWriter writer;
    if(!WebPConfigInit(&config)||!WebPPictureInit(&picture)) return 1;
    config.lossless=lossless;config.quality=lossless?100.f:73.f;config.method=6;config.exact=1;
    picture.width=WIDTH;picture.height=HEIGHT;picture.use_argb=1;
    WebPMemoryWriterInit(&writer);picture.writer=WebPMemoryWrite;picture.custom_ptr=&writer;
    if(!WebPPictureImportRGBA(&picture,rgba,STRIDE)||!WebPEncode(&config,&picture)) return 1;
    char filename[64];snprintf(filename,sizeof(filename),"%s.webp",name);
    int failed=write_file(directory,filename,writer.mem,writer.size);
    /* Lossless reference pixels come directly from the independent integer
     * source formula. Lossy vectors compare target output with original host
     * decoding, with this distinction recorded in cases.json. */
    int width=0,height=0;uint8_t *reference=WebPDecodeRGBA(writer.mem,writer.size,&width,&height);
    if(!reference||width!=WIDTH||height!=HEIGHT||(lossless&&memcmp(reference,rgba,sizeof(rgba)))) return 1;
    snprintf(filename,sizeof(filename),"%s.rgba",name);
    failed|=write_file(directory,filename,lossless?rgba:reference,sizeof(rgba));
    WebPFree(reference);WebPPictureFree(&picture);WebPMemoryWriterClear(&writer);return failed;
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    if(image(argv[1],"lossless",0,1)||image(argv[1],"lossy",1,0)||image(argv[1],"frame2",2,1))return 1;
    printf("original libwebp encoder=%06x decoder=%06x\n",WebPGetEncoderVersion(),WebPGetDecoderVersion());
    return 0;
}
