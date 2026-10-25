#include <stddef.h>
#include <stdint.h>
#include "chos/user.h"

/* Bounded append/backspace editor for persistent text. ROMFS, binary and
 * truncated files stay read-only; a short read must never destroy an original. */
#define VIEW_BYTES 8192u
#define LINE_BYTES 61u
#define VIEW_LINES 17u
static const char default_path[]="/DOCS/README.TXT";
static char document[VIEW_BYTES];
static size_t document_length,scroll_line;
static uint32_t window_id;
static int editable,modified,truncated,save_error,follow_tail=1;
static chos_file_list_entry_t catalog[CHOS_FILE_LIST_MAX];
static const char *document_path;

static void begin(chos_gui_message_t *m,uint16_t type,size_t size){
    chos_memset(m,0,sizeof(*m));m->header.version=CHOS_GUI_ABI_VERSION;m->header.type=type;
    m->header.size_bytes=(uint32_t)(offsetof(chos_gui_message_t,payload)+size);
}
static long submit(chos_gui_message_t *m){return chos_gui_submit(m,m->header.size_bytes);}
static void draw_text(int x,int y,const char *s,uint32_t color){
    chos_gui_message_t m;size_t n=chos_strlen(s);if(n>=CHOS_GUI_MAX_TEXT)n=CHOS_GUI_MAX_TEXT-1u;
    begin(&m,CHOS_GUI_MSG_DRAW_TEXT,sizeof(m.payload.draw_text));
    m.payload.draw_text.window_id=window_id;m.payload.draw_text.x=x;m.payload.draw_text.y=y;
    m.payload.draw_text.argb=color;m.payload.draw_text.text_length=(uint16_t)n;
    chos_memcpy(m.payload.draw_text.text,s,n);(void)submit(&m);
}
static void close_window(void){
    chos_gui_message_t m;begin(&m,CHOS_GUI_MSG_DESTROY_WINDOW,sizeof(m.payload.window));
    m.payload.window.window_id=window_id;(void)submit(&m);
}
static void next_line(size_t *pos,char line[LINE_BYTES]){
    size_t n=0;
    while(*pos<document_length&&n+1u<LINE_BYTES){
        unsigned char c=(unsigned char)document[(*pos)++];
        if(c=='\n')break;
        if(c=='\r')continue;
        line[n++]=c=='\t'?' ':(c>=32u&&c<127u?(char)c:'.');
    }
    line[n]=0;
}
static size_t count_lines(void){
    char line[LINE_BYTES];size_t pos=0,count=0;
    do{next_line(&pos,line);count++;}while(pos<document_length);
    if(document_length&&document[document_length-1u]=='\n')count++;
    return count;
}
static void redraw(void){
    chos_gui_message_t m;char line[LINE_BYTES],last[LINE_BYTES];
    size_t pos=0,row=0,total=count_lines(),max_scroll=total>VIEW_LINES?total-VIEW_LINES:0;
    if(follow_tail)scroll_line=max_scroll;
    if(scroll_line>max_scroll)scroll_line=max_scroll;
    begin(&m,CHOS_GUI_MSG_CLEAR,sizeof(m.payload.clear));
    m.payload.clear.window_id=window_id;m.payload.clear.background_argb=0xfff4f4f4u;(void)submit(&m);
    draw_text(12,10,document_path,0xff000080u);
    draw_text(12,30,editable?"Type at end | Ctrl+S: save | Escape: save and close":"Read-only | Wheel / PgUp / PgDn: scroll | Escape: close",0xff404040u);
    last[0]=0;
    do{
        next_line(&pos,line);
        if(row>=scroll_line&&row-scroll_line<VIEW_LINES)draw_text(12,58+(int)(row-scroll_line)*16,line,0xff202020u);
        (void)chos_strlcpy(last,line,sizeof(last));row++;
    }while(pos<document_length);
    if(document_length&&document[document_length-1u]=='\n'){last[0]=0;row++;}
    const char *status=save_error?"Save failed. Your edits remain open; retry Ctrl+S.":
        truncated?"Read-only preview: file exceeds the 8191-byte editing limit.":
        editable?(modified?"Modified | append/backspace editing":"Saved | append/backspace editing"):"Read-only source; no changes will be written.";
    draw_text(12,352,status,save_error?0xff800000u:0xff404040u);
    begin(&m,CHOS_GUI_MSG_SET_CARET,sizeof(m.payload.caret));m.payload.caret.window_id=window_id;
    size_t end_row=row-1u;
    m.payload.caret.enabled=editable&&end_row>=scroll_line&&end_row-scroll_line<VIEW_LINES;
    m.payload.caret.x=13;m.payload.caret.y=m.payload.caret.enabled?58+(int32_t)(end_row-scroll_line)*16:0;
    m.payload.caret.width=1;m.payload.caret.height=14;m.payload.caret.argb=0xff202020u;
    m.payload.caret.text_length=(uint32_t)chos_strlen(last);chos_memcpy(m.payload.caret.text,last,m.payload.caret.text_length);
    (void)submit(&m);
}
static int save_document(void){
    if(!modified)return 1;
    if(!editable||chos_write_file(document_path,document,document_length)!=(long)document_length||chos_fsync()<0){save_error=1;redraw();return 0;}
    modified=save_error=0;redraw();return 1;
}
static char key_character(uint32_t key,uint32_t modifiers){
    int shift=(modifiers&1u)!=0,caps=(modifiers&4u)!=0;char c=0;
    if(key>=2u&&key<=11u){const char *normal="1234567890",*upper="!@#$%^&*()";c=shift?upper[key-2u]:normal[key-2u];}
    else if(key>=16u&&key<=25u)c="qwertyuiop"[key-16u];
    else if(key>=30u&&key<=38u)c="asdfghjkl"[key-30u];
    else if(key>=44u&&key<=50u)c="zxcvbnm"[key-44u];
    else if(key==57u)c=' ';
    else if(key==12u)c=shift?'_':'-';else if(key==13u)c=shift?'+':'=';
    else if(key==26u)c=shift?'{':'[';else if(key==27u)c=shift?'}':']';
    else if(key==39u)c=shift?':':';';else if(key==40u)c=shift?'"':'\'';
    else if(key==41u)c=shift?'~':'`';else if(key==43u||key==86u)c=shift?'|':'\\';
    else if(key==51u)c=shift?'<':',';else if(key==52u)c=shift?'>':'.';else if(key==53u)c=shift?'?':'/';
    else if(key==28u)c='\n';else if(key==15u)c='\t';
    if(shift!=caps&&c>='a'&&c<='z')c=(char)(c-'a'+'A');return c;
}
int main(int argc,char **argv){
    chos_gui_message_t m,event;long fd,result;
    document_path=argc>1&&argv&&argv[1]&&argv[1][0]=='/'?argv[1]:default_path;
    fd=chos_open(document_path);if(fd<0)chos_exit(1);
    while(document_length<sizeof(document)-1u){
        size_t room=sizeof(document)-1u-document_length;if(room>CHOS_SYS_MAX_READ)room=CHOS_SYS_MAX_READ;
        long got=chos_read((int)fd,document+document_length,room);
        if(got<0){(void)chos_close((int)fd);chos_exit(2);}if(!got)break;document_length+=(size_t)got;
    }
    if(document_length==sizeof(document)-1u){char extra;long got=chos_read((int)fd,&extra,1);if(got<0){(void)chos_close((int)fd);chos_exit(2);}truncated=got!=0;}
    (void)chos_close((int)fd);document[document_length]=0;
    long count=chos_list_files(catalog,CHOS_FILE_LIST_MAX);
    for(long i=0;i<count&&i<(long)CHOS_FILE_LIST_MAX;i++)if(!chos_strcmp(catalog[i].path,document_path)){
        uint32_t f=catalog[i].flags;editable=!truncated&&(f&CHOS_FILE_FLAG_PERSISTENT)&&(f&CHOS_FILE_FLAG_TEXT)&&!(f&CHOS_FILE_FLAG_READ_ONLY);break;
    }
    for(size_t i=0;i<document_length;i++){
        unsigned char c=(unsigned char)document[i];if(c==0||(c<32u&&c!='\n'&&c!='\r'&&c!='\t')||c>126u)editable=0;
    }
    begin(&m,CHOS_GUI_MSG_CREATE_WINDOW,sizeof(m.payload.create_window));
    m.payload.create_window.x=100;m.payload.create_window.y=70;m.payload.create_window.width=640;m.payload.create_window.height=420;
    m.payload.create_window.background_argb=0xfff4f4f4u;result=submit(&m);if(result<100||result>103)chos_exit(3);window_id=(uint32_t)result;
    const char *title=editable?"CHOS Text Editor":"CHOS Text Viewer";
    begin(&m,CHOS_GUI_MSG_SET_TITLE,sizeof(m.payload.set_title));m.payload.set_title.window_id=window_id;
    m.payload.set_title.title_length=(uint16_t)chos_strlen(title);chos_memcpy(m.payload.set_title.title,title,chos_strlen(title));
    if(submit(&m)){close_window();chos_exit(4);}
    begin(&m,CHOS_GUI_MSG_SHOW_WINDOW,sizeof(m.payload.window));m.payload.window.window_id=window_id;
    if(submit(&m)){close_window();chos_exit(5);}redraw();
    for(;;){
        result=chos_gui_poll_event(&event,sizeof(event));if(result==-(long)CHOS_ERRNO_EAGAIN){(void)chos_sleep_ticks(1);continue;}if(result<0)break;
        if(event.header.type==CHOS_GUI_MSG_CLOSE_REQUEST){if(save_document())break;continue;}
        if(event.header.type==CHOS_GUI_MSG_POINTER_EVENT&&(event.payload.pointer_event.event_flags&CHOS_GUI_EVENT_POINTER_SCROLL)){
            int32_t delta=event.payload.pointer_event.delta_y;if(delta>20)delta=20;if(delta< -20)delta= -20;
            follow_tail=0;size_t n=(size_t)(delta<0?-delta:delta)*3u;
            if(delta<0)scroll_line=scroll_line>n?scroll_line-n:0;else scroll_line+=n;redraw();continue;
        }
        if(event.header.type!=CHOS_GUI_MSG_KEY_EVENT||!(event.payload.key_event.event_flags&CHOS_GUI_EVENT_KEY_DOWN))continue;
        uint32_t key=event.payload.key_event.keycode,mods=event.payload.key_event.modifiers;
        if(key==1u){if(save_document())break;continue;}
        if(mods&2u){if(key==31u)(void)save_document();continue;}
        if(key==102u||key==107u){follow_tail=key==107u;scroll_line=0;redraw();continue;}
        if(key==103u||key==104u||key==108u||key==109u){
            size_t n=key==104u||key==109u?VIEW_LINES-1u:1u;follow_tail=0;
            if(key==103u||key==104u)scroll_line=scroll_line>n?scroll_line-n:0;else scroll_line+=n;redraw();continue;
        }
        if(!editable)continue;
        if(key==14u){if(document_length){document[--document_length]=0;modified=1;save_error=0;follow_tail=1;redraw();}continue;}
        char c=key_character(key,mods);
        if(c&&document_length+1u<sizeof(document)){document[document_length++]=c;document[document_length]=0;modified=1;save_error=0;follow_tail=1;redraw();}
    }
    close_window();chos_exit(0);
}
