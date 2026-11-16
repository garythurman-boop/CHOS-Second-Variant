#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#ifdef __CHOS__
#include <chos/libc.h>
#endif
#define CHECK(x) do {if(!(x)){printf("[FAIL] fontconfig line %d: %s (errno=%d)\n",__LINE__,#x,errno);return 1;}}while(0)
static const char *names[]={"NotoSans-Regular.ttf","NotoNaskhArabic-Regular.ttf","NotoSansDevanagari-Regular.ttf","3e46c3b84c1370a06594736c7f8acebf810bbb3b.ttf","872d2955d326bd6676a06f66b8238ebbaabc212f.ttf"};
static char paths[5][512];
static FcConfig *config;
static int match(const char *family,const char *lang,FcChar32 character,const char *expected){
 FcPattern *p=FcPatternCreate();CHECK(p);CHECK(FcPatternAddString(p,FC_FAMILY,(const FcChar8*)family));CHECK(FcPatternAddString(p,FC_LANG,(const FcChar8*)lang));CHECK(FcPatternAddInteger(p,FC_WEIGHT,FC_WEIGHT_REGULAR));CHECK(FcPatternAddInteger(p,FC_SLANT,FC_SLANT_ROMAN));CHECK(FcPatternAddDouble(p,FC_PIXEL_SIZE,18));
 FcCharSet *cs=FcCharSetCreate();CHECK(cs&&FcCharSetAddChar(cs,character)&&FcPatternAddCharSet(p,FC_CHARSET,cs));FcCharSetDestroy(cs);
 CHECK(FcConfigSubstitute(config,p,FcMatchPattern));FcDefaultSubstitute(p);FcResult result;FcPattern *font=FcFontMatch(config,p,&result);CHECK(font&&result==FcResultMatch);
 FcChar8 *selected=NULL,*file=NULL,*style=NULL;CHECK(FcPatternGetString(font,FC_FAMILY,0,&selected)==FcResultMatch);CHECK(!strcmp((char*)selected,expected));CHECK(FcPatternGetString(font,FC_FILE,0,&file)==FcResultMatch);CHECK(FcPatternGetString(font,FC_STYLE,0,&style)==FcResultMatch);
 FcCharSet *coverage;CHECK(FcPatternGetCharSet(font,FC_CHARSET,0,&coverage)==FcResultMatch&&FcCharSetHasChar(coverage,character));
 int weight,slant,index;CHECK(FcPatternGetInteger(font,FC_WEIGHT,0,&weight)==FcResultMatch);CHECK(FcPatternGetInteger(font,FC_SLANT,0,&slant)==FcResultMatch&&slant==FC_SLANT_ROMAN);CHECK(FcPatternGetInteger(font,FC_INDEX,0,&index)==FcResultMatch);
 FT_Library lib;FT_Face face;CHECK(!FT_Init_FreeType(&lib));CHECK(!FT_New_Face(lib,(char*)file,index,&face));CHECK(FcFreeTypeCharIndex(face,character));CHECK(!strcmp(face->family_name,expected));FT_Done_Face(face);FT_Done_FreeType(lib);
 FcChar8 *formatted=FcPatternFormat(font,(const FcChar8*)"%{family}|%{style}|%{weight}");CHECK(formatted&&strchr((char*)formatted,'|'));FcStrFree(formatted);
 formatted=FcPatternFormat(font,(const FcChar8*)"%{family|delete( )}");CHECK(formatted&&!strchr((char*)formatted,' '));FcStrFree(formatted);
 FcPattern *render=FcFontRenderPrepare(config,p,font);CHECK(render);double pixels;FcBool antialias;CHECK(FcPatternGetDouble(render,FC_PIXEL_SIZE,0,&pixels)==FcResultMatch&&pixels==18);CHECK(FcPatternGetBool(render,FC_ANTIALIAS,0,&antialias)==FcResultMatch&&antialias);FcPatternDestroy(render);
 FcFontSet *sorted=FcFontSort(config,p,FcTrue,NULL,&result);CHECK(sorted&&sorted->nfont>=3);FcFontSetDestroy(sorted);
 FcPatternDestroy(font);FcPatternDestroy(p);return 0;
}
static int style_fallback(void){
 FcPattern *p=FcPatternCreate();CHECK(p&&FcPatternAddString(p,FC_FAMILY,(const FcChar8*)"Noto Sans"));
 CHECK(FcPatternAddInteger(p,FC_WEIGHT,FC_WEIGHT_BOLD)&&FcPatternAddInteger(p,FC_SLANT,FC_SLANT_ITALIC));
 CHECK(FcConfigSubstitute(config,p,FcMatchPattern));FcDefaultSubstitute(p);FcResult result;FcPattern *font=FcFontMatch(config,p,&result);CHECK(font&&result==FcResultMatch);
 int weight,slant;CHECK(FcPatternGetInteger(font,FC_WEIGHT,0,&weight)==FcResultMatch&&weight==FC_WEIGHT_REGULAR);CHECK(FcPatternGetInteger(font,FC_SLANT,0,&slant)==FcResultMatch&&slant==FC_SLANT_ROMAN);FcPatternDestroy(font);FcPatternDestroy(p);return 0;
}
static int all_matches(void){CHECK(!match("Noto Sans","en",'A',"Noto Sans"));CHECK(!match("sans-serif","en",'A',"Noto Sans"));CHECK(!match("Noto Sans","ar",0x0627,"Noto Naskh Arabic"));CHECK(!match("Noto Sans","hi",0x0915,"Noto Sans Devanagari"));return 0;}
static void *worker(void *ignored){(void)ignored;for(unsigned i=0;i<3;++i)if(all_matches())return(void*)1;return NULL;}
static int unconfigured(void){
 errno=0;CHECK(!FcConfigReference(NULL)&&errno==ENOTSUP);
 CHECK(!FcConfigGetCurrent()&&!FcInitBringUptoDate()&&!FcInit());
 FcPattern *pattern=FcPatternCreate();CHECK(pattern);FcResult result;CHECK(!FcFontMatch(NULL,pattern,&result));FcPatternDestroy(pattern);FcFini();return 0;
}
static int config_errors(void){
 const char *invalid[]={"<fontconfig>","<fontconfig><alias>","<fontconfig><alias><family>sans","<fontconfig><broken></fontconfig>"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){FcConfig *bad=FcConfigCreate();CHECK(bad&&!FcConfigParseAndLoadFromMemory(bad,(const FcChar8*)invalid[i],FcTrue));FcConfigDestroy(bad);}
 FcConfig *empty=FcConfigCreate();CHECK(empty&&FcConfigParseAndLoadFromMemory(empty,(const FcChar8*)"<fontconfig/>",FcFalse));FcConfigDestroy(empty);return 0;
}
int main(int argc,char **argv){
#ifdef __CHOS__
 (void)argc;(void)argv;
 for(unsigned i=0;i<5;++i)snprintf(paths[i],sizeof(paths[i]),"/FONTS/F%u.TTF",i);
#else
 CHECK(argc==2);for(unsigned i=0;i<5;++i)snprintf(paths[i],sizeof(paths[i]),"%s/%s",argv[1],names[i]);
#endif
 CHECK(FcGetVersion()==21701);CHECK(!unconfigured()&&!config_errors());config=FcConfigCreate();CHECK(config);
 const FcChar8 rules[]="<fontconfig><alias><family>sans-serif</family><prefer><family>Noto Sans</family></prefer></alias><match target='pattern'><edit name='antialias' mode='assign'><bool>true</bool></edit></match></fontconfig>";
 CHECK(FcConfigParseAndLoadFromMemory(config,rules,FcTrue));
 CHECK(!FcConfigParseAndLoad(config,(const FcChar8*)"/UNSUPPORTED.CONF",FcFalse));CHECK(!FcConfigAppFontAddDir(config,(const FcChar8*)"/FONTS"));CHECK(!FcConfigAppFontAddFile(config,(const FcChar8*)"/MISSING-FONT.TTF"));
 for(unsigned i=0;i<5;++i)CHECK(FcConfigAppFontAddFile(config,(FcChar8*)paths[i]));
 FcFontSet *catalog=FcConfigGetFonts(config,FcSetApplication);CHECK(catalog&&catalog->nfont>=5);
 for(int i=0;i<catalog->nfont;++i){FcChar8 *family,*style;int weight,slant;FcPattern *font=catalog->fonts[i];CHECK(FcPatternGetString(font,FC_FAMILY,0,&family)==FcResultMatch);CHECK(FcPatternGetString(font,FC_STYLE,0,&style)==FcResultMatch);CHECK(FcPatternGetInteger(font,FC_WEIGHT,0,&weight)==FcResultMatch);CHECK(FcPatternGetInteger(font,FC_SLANT,0,&slant)==FcResultMatch);printf("[FC FONT] %s | %s | weight=%d slant=%d\n",family,style,weight,slant);}
 CHECK(FcConfigSetCurrent(config)&&FcInit()&&FcInitBringUptoDate());FcFini();CHECK(!all_matches()&&!style_fallback());
 FcPattern *p=FcPatternCreate();FcObjectSet *os=FcObjectSetBuild(FC_FAMILY,NULL);CHECK(p&&os);FcFontSet *list=FcFontList(config,p,os);CHECK(list&&list->nfont>=3);FcFontSetDestroy(list);FcObjectSetDestroy(os);FcPatternDestroy(p);
 puts("[ OK ] actual Fontconfig file catalogue,family,language,charset,fallback and face reopen passed");
 pthread_t threads[3];pthread_attr_t attr;CHECK(!pthread_attr_init(&attr)&&!pthread_attr_setstacksize(&attr,128*1024));for(unsigned i=0;i<3;++i)CHECK(!pthread_create(&threads[i],&attr,worker,NULL));for(unsigned i=0;i<3;++i){void *r=NULL;CHECK(!pthread_join(threads[i],&r)&&!r);}CHECK(!pthread_attr_destroy(&attr));
 FcConfigAppFontClear(config);CHECK(!FcConfigGetFonts(config,FcSetApplication));FcConfigDestroy(config);FcFini();CHECK(!unconfigured());
#ifdef __CHOS__
 chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats));printf("[FC HEAP] allocations=%lu mapped=%lu\n",(unsigned long)stats.allocations,(unsigned long)stats.mapped_bytes);
#endif
 puts("[ OK ] actual Fontconfig native threads and catalogue cleanup passed");return 0;
}
