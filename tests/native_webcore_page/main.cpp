/* Local trusted-document diagnostic. Uses the original internal headless Page
 * workflow; it is not the browser or a network-loading implementation. */
#include "config.h"
#include "BrowserICUData.h"
#include <WebCore/Color.h>
#include <WebCore/Document.h>
#include <WebCore/DocumentLoader.h>
#include <WebCore/DocumentView.h>
#include <WebCore/DocumentWriter.h>
#include <WebCore/EmptyClients.h>
#include <WebCore/FrameLoader.h>
#include <WebCore/GraphicsContextCairo.h>
#include <WebCore/IntRect.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/LocalFrameInlines.h>
#include <WebCore/LocalFrameView.h>
#include <WebCore/Page.h>
#include <WebCore/PageConfiguration.h>
#include <WebCore/ScriptController.h>
#include <WebCore/Settings.h>
#include <JavaScriptCore/JSValue.h>
#include <JavaScriptCore/SourceTaintedOrigin.h>
#include <pal/SessionID.h>
#include <wtf/MainThread.h>
#include <wtf/RunLoop.h>
#include <wtf/text/WTFString.h>
#include <cairo.h>
#include <fontconfig/fontconfig.h>
#include <stdint.h>
#include <stdio.h>
#include <variant>

using namespace WebCore;
using namespace WTF::StringLiterals;

#define CHECK(x) do { if (!(x)) { printf("[FAIL] local page line %u: %s\n",__LINE__,#x); return 1; } } while(0)
static constexpr unsigned viewportWidth=512,viewportHeight=256;
uint32_t webcore_page_pixels[viewportWidth*viewportHeight];
uint64_t webcore_page_hash;
volatile unsigned webcore_page_frames;

static constexpr char html[]=R"HTML(<!doctype html><html><head><meta charset="utf-8">
<title>CHOS local page</title><style>
html,body{margin:0;padding:0;width:512px;height:256px;overflow:hidden;background:#ffffff}
#message{position:absolute;left:12px;top:12px;height:24px;line-height:24px;font:18px/24px "Noto Sans",sans-serif;color:#000000}
#a{position:absolute;left:17px;top:64px;width:71px;height:39px;background:#123456}
#b{position:absolute;left:240px;top:90px;width:96px;height:61px;background:#228844}
#c{position:absolute;left:370px;top:130px;width:64px;height:37px;box-sizing:border-box;border:2px solid #334455;background:#ddeeff}
</style></head><body><div id="message">CHOS native WebCore Ω</div>
<div id="a"></div><div id="b"></div><div id="c"></div></body></html>)HTML";

static bool inside(unsigned x,unsigned y,unsigned left,unsigned top,unsigned w,unsigned h)
{
    return x>=left && y>=top && x<left+w && y<top+h;
}

static int check_pixels(unsigned epoch)
{
    unsigned text_pixels=0;
    uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned y=0;y<viewportHeight;++y)for(unsigned x=0;x<viewportWidth;++x) {
        uint32_t pixel=webcore_page_pixels[y*viewportWidth+x];
        for(unsigned shift=0;shift<32;shift+=8) {
            hash^=(pixel>>shift)&255u;hash*=UINT64_C(1099511628211);
        }
        // Font pixels are checked for genuine black/gray glyph coverage. The
        // flat CSS geometry outside this region has a complete analytic oracle.
        if(inside(x,y,8,8,496,40)) {
            CHECK((pixel>>24)==255u);
            CHECK(((pixel>>16)&255u)==((pixel>>8)&255u) && ((pixel>>8)&255u)==(pixel&255u));
            text_pixels+=pixel!=0xffffffffu;
            continue;
        }
        uint32_t expected=0xffffffffu;
        if(inside(x,y,epoch?121:17,64,71,39))expected=epoch?0xffcc3366u:0xff123456u;
        if(inside(x,y,epoch?247:240,epoch?85:90,96,61))expected=0xff228844u;
        if(inside(x,y,370,130,64,37))
            expected=inside(x,y,372,132,60,33)?0xffddeeffu:0xff334455u;
        if(pixel!=expected) {
            printf("[PIXEL] epoch=%u x=%u y=%u expected=%08x actual=%08x\n",epoch,x,y,expected,pixel);
            return 1;
        }
    }
    CHECK(text_pixels>64);
    webcore_page_hash=hash;++webcore_page_frames;
    printf("[PAGE FRAME] epoch=%u glyph_pixels=%u hash=%016llx\n",epoch,text_pixels,(unsigned long long)hash);
    return 0;
}

static int paint(LocalFrameView& view,unsigned epoch)
{
    view.updateLayoutAndStyleIfNeededRecursive();
    cairo_surface_t *surface=cairo_image_surface_create_for_data(
        reinterpret_cast<unsigned char*>(webcore_page_pixels),CAIRO_FORMAT_ARGB32,viewportWidth,viewportHeight,viewportWidth*4);
    CHECK(cairo_surface_status(surface)==CAIRO_STATUS_SUCCESS);
    {
        GraphicsContextCairo context(surface);
        view.paint(context,IntRect{0,0,viewportWidth,viewportHeight});
    }
    cairo_surface_flush(surface);
    CHECK(cairo_surface_status(surface)==CAIRO_STATUS_SUCCESS);
    cairo_surface_destroy(surface);
    return check_pixels(epoch);
}

static int fonts()
{
    FcConfig *configuration=FcConfigCreate();CHECK(configuration);
    for(unsigned i=0;i<5;++i) {
        char path[32];snprintf(path,sizeof(path),"/FONTS/F%u.TTF",i);
        if(!FcConfigAppFontAddFile(configuration,reinterpret_cast<const FcChar8*>(path))) {
            FcConfigDestroy(configuration);CHECK(false);
        }
    }
    if(!FcConfigSetCurrent(configuration)){FcConfigDestroy(configuration);CHECK(false);}
    // The global catalogue retains its own reference for the engine lifetime.
    FcConfigDestroy(configuration);
    return 0;
}

static int page_case()
{
    puts("[PAGE] constructing page configuration");
    auto configuration=pageConfigurationWithEmptyClients(std::nullopt,PAL::SessionID::defaultSessionID());
    // This fixture supplies only its own trusted local document. The original
    // image-internal helper defaults to all sandbox restrictions, including JS.
    std::get<PageConfiguration::LocalMainFrameCreationParameters>(configuration.mainFrameCreationParameters).effectiveSandboxFlags={};
    puts("[PAGE] creating Page");
    auto page=Page::create(WTF::move(configuration));
    page->settings().setScriptEnabled(true);
    page->settings().setAcceleratedCompositingEnabled(false);
    RefPtr frame=page->localMainFrame();CHECK(frame);
    frame->setView(LocalFrameView::create(*frame));
    puts("[PAGE] initializing main frame");
    frame->init();
    RefPtr view=frame->view();CHECK(view);
    view->setCanHaveScrollbars(false);
    view->setTransparent(false);
    view->setBaseBackgroundColor(Color{Color::white});
    view->setFrameRect(IntRect{0,0,viewportWidth,viewportHeight});
    RefPtr loader=frame->loader().activeDocumentLoader();CHECK(loader);
    auto& writer=loader->writer();
    writer.setMIMEType("text/html"_s);
    puts("[PAGE] beginning document");
    CHECK(writer.begin(URL{"about:blank"_s}));
    puts("[PAGE] parsing HTML");
    writer.insertDataSynchronously(String::fromUTF8(html));
    puts("[PAGE] finishing parser");
    writer.end();
    RefPtr document=frame->document();CHECK(document);
    puts("[PAGE] updating layout");
    document->updateLayoutIgnorePendingStylesheets();
    puts("[PAGE] executing geometry script");
    auto before=frame->script().executeScriptIgnoringException(
        "(()=>{const r=document.getElementById('a').getBoundingClientRect();return document.title==='CHOS local page'&&r.x===17&&r.y===64&&r.width===71&&r.height===39})()"_s,
        JSC::SourceTaintedOrigin::Untainted);
    CHECK(before.isBoolean() && before.asBoolean());
    CHECK(!paint(*view,0));
    auto changed=frame->script().executeScriptIgnoringException(
        "(()=>{const a=document.getElementById('a');a.style.left='121px';a.style.backgroundColor='#cc3366';document.getElementById('b').style.transform='translate(7px,-5px)';document.getElementById('message').textContent='JavaScript changed the DOM';const r=a.getBoundingClientRect();const b=document.getElementById('b').getBoundingClientRect();return r.x===121&&r.y===64&&b.x===247&&b.y===85&&document.getElementById('message').textContent==='JavaScript changed the DOM'})()"_s,
        JSC::SourceTaintedOrigin::Untainted);
    CHECK(changed.isBoolean() && changed.asBoolean());
    document->updateLayoutIgnorePendingStylesheets();
    CHECK(!paint(*view,1));
    puts("[ OK ] actual local HTML CSS layout JavaScript DOM mutation and paint passed");
    return 0;
}

namespace WebCore { void installLocalPageStrategies(); }
bool exerciseLocalPlatform();

int main()
{
    puts("[PAGE] loading ICU");
    CHECK(!chos_browser_icu_init());
    puts("[PAGE] loading fonts");
    CHECK(!fonts());
    puts("[PAGE] initializing main thread");
    WTF::initializeMainThread();
    CHECK(exerciseLocalPlatform());
    puts("[PAGE] starting JavaScriptCore");
    ScriptController::initializeMainThread();
    installLocalPageStrategies();
    CHECK(!page_case());
    puts("[ OK ] local WebCore page diagnostic completed");
    return 0;
}
