#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <locale.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stddef.h>
#include <stdint.h>
#include <linux/netlink.h>
#include <linux/sock_diag.h>
#include <linux/inet_diag.h>
#include <linux/rtnetlink.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>

typedef struct _XftDraw XftDraw;
typedef struct _XftFont XftFont;
typedef struct { unsigned short red, green, blue, alpha; } TaskXRenderColor;
typedef struct { unsigned long pixel; TaskXRenderColor color; } TaskXftColor;
extern XftDraw *XftDrawCreate(Display *, Drawable, Visual *, Colormap);
extern void XftDrawDestroy(XftDraw *);
extern XftFont *XftFontOpenName(Display *, int, const char *);
extern void XftFontClose(Display *, XftFont *);
extern void XftDrawStringUtf8(XftDraw *, const TaskXftColor *, XftFont *, int, int, const unsigned char *, int);
typedef struct { unsigned short width,height; short x,y; short xOff,yOff; } TaskXGlyphInfo;
extern void XftTextExtentsUtf8(Display *, XftFont *, const unsigned char *, int, TaskXGlyphInfo *);

typedef XID TaskPicture;
typedef struct _XRenderPictFormat TaskXRenderPictFormat;
extern TaskXRenderPictFormat *XRenderFindVisualFormat(Display *, Visual *);
extern TaskPicture XRenderCreatePicture(Display *, Drawable, const TaskXRenderPictFormat *, unsigned long, const void *);
extern TaskPicture XRenderCreateSolidFill(Display *, const TaskXRenderColor *);
extern void XRenderComposite(Display *, int, TaskPicture, TaskPicture, TaskPicture, int, int, int, int, int, int, unsigned int, unsigned int);
extern void XRenderFreePicture(Display *, TaskPicture);

#define SW 1180
#define SH 760
#define MAXP 8192
#define NAVW 218
#define BG 0xF5F7FA
#define WHITE 0xFFFFFF
#define INK 0x202938
#define MUTED 0x687386
#define BLUE 0x0078D4
#define PALE 0xE7EEFF
#define LINE 0xE1E6EF
#define GREEN 0x16845B
#define RED 0xC0392B
#define AMBER 0xB7791F
#define PANEL 0xFFFFFF

typedef struct { pid_t pid,ppid; char name[256],user[64],source[16]; char state; unsigned long long rss,vsize,ticks,start_ticks,io_read_bytes,io_write_bytes,net_rx_total,net_tx_total; unsigned long threads; double cpu,disk_read_rate,disk_write_rate,net_rx_rate,net_tx_rate; unsigned have_io:1,have_network:1; } Proc;
typedef struct { pid_t pid; unsigned long long ticks; double cpu; } OldProc;
typedef struct { char name[256]; double cpu_sum; unsigned long samples; unsigned long long peak_rss; } AppStat;
typedef struct { char unit[160],load[32],active[32],sub[32],desc[256]; } Service;
typedef struct { char name[256],path[PATH_MAX]; int disabled,user_owned,hidden_override,system_entry; } Startup;
static Display *D; static int S; static Window W; static GC G; static XFontStruct *F; static XFontSet FS; static XftDraw *XD; static XftFont *XF,*XFUser; static Atom WM_DELETE;
static int ww=SW,hh=SH,page=0,selected=-1,scroll=0,menu=0,menu_x=0,menu_y=0,sort_col=0,sort_desc=0,search_focus=0,run_dialog=0,run_focus=0;
static int dark_mode=1, history_scroll=0, cpu_scroll=0, disk_scroll=0, network_scroll=0, selected_appstat=-1, memory_scroll=0;
static int disk_panel_bottom=0;
static char selected_appstat_name[256]="";
#define RUN_MAX_SUGGESTIONS 2048
#define RUN_VISIBLE 10
#define RUN_ROW_H 24
static char run_command[512]="";
static char run_suggestions[RUN_MAX_SUGGESTIONS][128];
static int run_suggestion_count=0, run_suggestion=-1;
static int visible_run_suggestions[RUN_VISIBLE], visible_run_count=0;
static int run_filtered[RUN_MAX_SUGGESTIONS], run_filtered_count=0, run_scroll=0;
static pid_t pending_action_pid=0;
static char pending_action_text[192]="";
static int pending_action_service=0, pending_action_elevated=0;
static char query[128]="",statusmsg[256]="Ready",selected_path[PATH_MAX]="";
static long long status_changed_ms=0,processing_hold_until_ms=0;
static char queued_status[256]=""; static int queued_status_valid=0;
static void draw(void);
static void setstatus(const char *s);
static long long monotonic_ms(void){struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0)return 0;return (long long)ts.tv_sec*1000LL+ts.tv_nsec/1000000LL;}
static int status_starts(const char *prefix){return strncasecmp(statusmsg,prefix,strlen(prefix))==0;}
static int status_processing(void){return pending_action_pid>0||status_starts("Processing:")||status_starts("Updating")||status_starts("Starting")||status_starts("Working")||status_starts("Enabling")||status_starts("Disabling")||status_starts("Stopping")||status_starts("Restarting")||status_starts("Launching");}
static int status_failed(void){return status_starts("Could not")||status_starts("Failed")||status_starts("Error");}
static int status_action_result(void){return status_starts("Completed")||status_starts("Launched")||status_starts("Enabled")||status_starts("Disabled")||status_starts("Startup entry")||status_starts("Terminate request")||status_starts("Force-stop request")||status_starts("Suspend request")||status_starts("Resume request")||status_starts("Set priority")||status_starts("PID copied")||status_starts("Opened")||status_starts("Opening");}
static void begin_action(const char *label){char b[256];snprintf(b,sizeof b,"Processing: %.220s",label?label:"Working");setstatus(b);draw();XFlush(D);usleep(110000);}
static Proc proc[MAXP]; static OldProc oldproc[MAXP]; static int nproc=0,nold=0;
#define PROC_HASH_SIZE 16384
static int proc_hash[PROC_HASH_SIZE],proc_first_child[MAXP],proc_next_sibling[MAXP],proc_child_tail[MAXP];
#define IOHASH 16384
#define MAX_NET_SOCK 16384
typedef struct { pid_t pid; unsigned long long start_ticks,read_bytes,write_bytes,net_rx,net_tx; long long disk_ms,net_ms; unsigned disk_valid:1,net_valid:1; } IoSlot;
typedef struct { unsigned long long inode; pid_t pid; } TcpOwner;
static IoSlot io_cache[IOHASH];
static TcpOwner tcp_owners[MAX_NET_SOCK];
static int tcp_owner_n=0,proc_net_available=0;
static long long last_proc_net_sample_ms=0;
static int proc_view[MAXP],proc_view_depth[MAXP],proc_view_has_children[MAXP],proc_view_n=0;
static unsigned char proc_view_matched[MAXP],proc_tree_seen[MAXP],proc_filter_cache[MAXP];
static pid_t proc_collapsed[MAXP]; static int proc_collapsed_n=0;
#define MAX_CPU_CORES 256
#define CORE_HISTORY 60
static unsigned char core_hist[MAX_CPU_CORES][CORE_HISTORY];
static unsigned long long core_prev_total[MAX_CPU_CORES],core_prev_idle[MAX_CPU_CORES];
static unsigned char core_prev_valid[MAX_CPU_CORES];
static int core_count=0,core_hist_pos=0,core_hist_count=0;
static AppStat appstats[512]; static int nappstats=0;
static Service services[1024]; static int nservices=0; static time_t services_loaded=0; static int selected_service=-1; static int service_menu=0;
static Startup startups[512]; static int nstartups=0,selected_startup=-1,startup_menu=0; static time_t startups_loaded=0;
static char selected_service_unit[160]="";

enum {
 MA_NONE=0,
 MA_PROC_TERM, MA_PROC_KILL, MA_PROC_TOGGLE, MA_PROC_NICE_LOW, MA_PROC_NICE_NORMAL,
 MA_PROC_OPEN_DIR, MA_PROC_SHOW_PATH, MA_PROC_SEARCH, MA_PROC_COPY_PID, MA_PROC_PROPERTIES,
 MA_SERVICE_START, MA_SERVICE_STOP, MA_SERVICE_RESTART, MA_SERVICE_STATUS, MA_SERVICE_REFRESH,
 MA_STARTUP_OPEN, MA_STARTUP_TOGGLE, MA_STARTUP_PATH
};
typedef struct { char label[96]; int action; } ContextMenuItem;
static ContextMenuItem context_items[12]; static int context_count=0;
static unsigned long long prev_total=0; static double cpu_hist[120],mem_hist[120]; static int histpos=0,histcount=0;
static unsigned long long prev_disk_read=0,prev_disk_write=0,prev_net_rx=0,prev_net_tx=0;
static double disk_read_rate=0,disk_write_rate=0,net_rx_rate=0,net_tx_rate=0; static int io_baseline=0;
#define MAX_GPU 8
typedef struct { char card[16],name[128],vendor[24],driver[64],pci[32]; double util,memutil,temp,power,core_clock,mem_clock,fan; unsigned long long vram_used,vram_total,gtt_used,gtt_total; unsigned have_util:1,have_memutil:1,have_temp:1,have_power:1,have_core:1,have_memclock:1,have_fan:1,have_vram:1,have_gtt:1,fan_rpm:1; } GpuInfo;
static GpuInfo gpus[MAX_GPU]; static int ngpus=0,nvidia_smi_state=0,gpu_scroll=0;
static double gpu_hist[MAX_GPU][120]; static int gpuhistpos=0,gpuhistcount=0;
#define PIDHASH 32768
typedef struct { pid_t pid; unsigned long long ticks; double cpu; } TickSlot;
static TickSlot tickhash[PIDHASH];
static unsigned tickhash_index(pid_t pid){return ((unsigned)pid * 2654435761u) & (PIDHASH-1);}
static void tickhash_clear(void){memset(tickhash,0,sizeof tickhash);}
static void tickhash_put(pid_t pid,unsigned long long ticks,double cpu){unsigned i=tickhash_index(pid);for(unsigned n=0;n<PIDHASH;n++,i=(i+1)&(PIDHASH-1)){if(!tickhash[i].pid||tickhash[i].pid==pid){tickhash[i].pid=pid;tickhash[i].ticks=ticks;tickhash[i].cpu=cpu;return;}}}
static unsigned long long tickhash_get(pid_t pid){unsigned i=tickhash_index(pid);for(unsigned n=0;n<PIDHASH;n++,i=(i+1)&(PIDHASH-1)){if(!tickhash[i].pid)return 0;if(tickhash[i].pid==pid)return tickhash[i].ticks;}return 0;}
static double tickhash_get_cpu(pid_t pid){unsigned i=tickhash_index(pid);for(unsigned n=0;n<PIDHASH;n++,i=(i+1)&(PIDHASH-1)){if(!tickhash[i].pid)return 0.0;if(tickhash[i].pid==pid)return tickhash[i].cpu;}return 0.0;}
typedef struct { unsigned uid; char name[64]; } UserCache; static UserCache user_cache[256]; static int user_cache_n=0;
static const char *username_for_uid(unsigned uid){for(int i=0;i<user_cache_n;i++)if(user_cache[i].uid==uid)return user_cache[i].name;struct passwd *pw=getpwuid((uid_t)uid);const char *name=pw?pw->pw_name:"?";if(user_cache_n<256){user_cache[user_cache_n].uid=uid;snprintf(user_cache[user_cache_n].name,sizeof user_cache[user_cache_n].name,"%s",name);return user_cache[user_cache_n++].name;}return name;}
static IoSlot *io_slot(pid_t pid,unsigned long long start_ticks){unsigned h=((unsigned)pid*2654435761u)&(IOHASH-1);for(unsigned n=0;n<IOHASH;n++,h=(h+1)&(IOHASH-1)){IoSlot*s=&io_cache[h];if(!s->pid){memset(s,0,sizeof(*s));s->pid=pid;s->start_ticks=start_ticks;return s;}if(s->pid==pid){if(s->start_ticks!=start_ticks){memset(s,0,sizeof(*s));s->pid=pid;s->start_ticks=start_ticks;}return s;}}IoSlot*s=&io_cache[((unsigned)pid*2654435761u)&(IOHASH-1)];memset(s,0,sizeof(*s));s->pid=pid;s->start_ticks=start_ticks;return s;}
static int read_process_io(pid_t pid,unsigned long long *r,unsigned long long *w){char path[64],line[256];snprintf(path,sizeof path,"/proc/%ld/io",(long)pid);FILE*f=fopen(path,"r");if(!f)return 0;int hr=0,hw=0;while(fgets(line,sizeof line,f)){if(sscanf(line,"read_bytes: %llu",r)==1)hr=1;else if(sscanf(line,"write_bytes: %llu",w)==1)hw=1;}fclose(f);return hr&&hw;}
static void update_process_disk_io(Proc *p){unsigned long long r=0,w=0;if(!read_process_io(p->pid,&r,&w))return;p->have_io=1;p->io_read_bytes=r;p->io_write_bytes=w;IoSlot*s=io_slot(p->pid,p->start_ticks);long long now=monotonic_ms();if(s->disk_valid&&now>s->disk_ms){double dt=(now-s->disk_ms)/1000.0;if(dt>0){p->disk_read_rate=r>=s->read_bytes?(double)(r-s->read_bytes)/dt:0;p->disk_write_rate=w>=s->write_bytes?(double)(w-s->write_bytes)/dt:0;}}s->read_bytes=r;s->write_bytes=w;s->disk_ms=now;s->disk_valid=1;}
static void rate_part(double bytes_per_sec,char *out,size_t cap,double unit){double v=bytes_per_sec/unit;if(v<0.05)v=0.0;if(v>=100.0)snprintf(out,cap,"%.0f",v);else snprintf(out,cap,"%.1f",v);}
static void format_rate_pair(double a,double b,char *out,size_t cap){double unit=(a>b?a:b)>=1048576.0?1048576.0:1024.0;char aa[24],bb[24];rate_part(a,aa,sizeof aa,unit);rate_part(b,bb,sizeof bb,unit);snprintf(out,cap,"%s/%s %s",aa,bb,unit>1024.0?"M/s":"K/s");}
static void format_process_memory(unsigned long long bytes,char*out,size_t cap){double mib=bytes/1048576.0;if(mib<0.05)snprintf(out,cap,"<0.1 MiB");else if(mib>=1024.0)snprintf(out,cap,"%.1f GiB",mib/1024.0);else snprintf(out,cap,"%.1f MiB",mib);}
static const char *pages[]={"Processes","Performance","App history","Startup apps","Users","Details","Services","Disk & Network","GPU details","System info","CPU details","Memory details"};
#define PAGE_COUNT ((int)(sizeof(pages)/sizeof(pages[0])))
static char list_queries[PAGE_COUNT][128];
static int list_search_focus_page=-1;
static unsigned long CFill(unsigned long c){
 if(!dark_mode)return c;
 if(c==BG||c==0xF4F6FA)return 0x202020;
 if(c==WHITE||c==PANEL||c==0xF8FAFD)return 0x282828;
 if(c==0xEDF1F7)return 0x1C1C1C;
 if(c==0xF0F3F8||c==0xF0F2F5||c==0xE8EDF5||c==0xE8EDF4||c==0xF5F7FA)return 0x303030;
 if(c==0xFAFBFD||c==0xF4F6FA)return 0x252525;
 if(c==BLUE)return 0x005FB7;
 if(c==GREEN)return 0x6CCB8E;
 if(c==RED)return 0xFF7777;
 if(c==AMBER)return 0xF5C451;
 if(c==LINE||c==0xE1E6EF||c==0xE7EBF1||c==0xAEB8C8)return 0x3A3A3A;
 if(c==PALE||c==0xDFE8FF||c==0xE7EEFF||c==0xDCE8FF)return 0x263F55;
 if(c==0xFFF1D6)return 0x463A1F;
 if(c==0xFDE7E7)return 0x432A2A;
 if(c==0xE1F5EA)return 0x293C32;
 if(c==0xD8DDE6)return 0x161616;
 if(c==0xBFC7D4)return 0x181818;
 if(c==0xD7DCE5)return 0x303030;
 if(c==0xE7E8EC)return 0x303030;
 unsigned r=(c>>16)&255,g=(c>>8)&255,b=c&255;
 if(r>236&&g>236&&b>236)return 0x282828;
 if(r>218&&g>218&&b>218)return 0x303030;
 return c;
}
static unsigned long CLine(unsigned long c){if(!dark_mode)return c;if(c==BLUE)return 0x60CDFF;if(c==GREEN)return 0x6CCB8E;if(c==RED)return 0xFF7777;if(c==AMBER)return 0xF5C451;if(c==LINE||c==0xE1E6EF||c==0xE7EBF1||c==0xAEB8C8)return 0x3A3A3A;return c;}
static unsigned long CText(unsigned long c){
 if(!dark_mode)return c;
 if(c==INK)return 0xFFFFFF;
 if(c==MUTED||c==0x687386)return 0xA0A0A0;
 if(c==BLUE)return 0x60CDFF;
 if(c==GREEN)return 0x6CCB8E;
 if(c==RED)return 0xFF7777;
 if(c==AMBER)return 0xF5C451;
 if(c==0xA65C00)return 0xF5C451;
 if(c==WHITE)return 0xFFFFFF;
 return c;
}
static void fill(int x,int y,int w,int h,unsigned long c){XSetForeground(D,G,CFill(c));if(w>0&&h>0)XFillRectangle(D,W,G,x,y,(unsigned)w,(unsigned)h);}
static void stroke(int x,int y,int w,int h,unsigned long c){XSetForeground(D,G,CLine(c));if(w>0&&h>0)XDrawRectangle(D,W,G,x,y,(unsigned)w,(unsigned)h);}
static void ln(int x,int y,int xx,int yy,unsigned long c){XSetForeground(D,G,CLine(c));XDrawLine(D,W,G,x,y,xx,yy);}
static void text(int x,int y,const char *s,unsigned long c){unsigned long out=CText(c);if(XD&&XF){TaskXftColor fc;fc.pixel=out;fc.color.red=(unsigned short)(((out>>16)&255)*257);fc.color.green=(unsigned short)(((out>>8)&255)*257);fc.color.blue=(unsigned short)((out&255)*257);fc.color.alpha=65535;XftDrawStringUtf8(XD,&fc,XF,x,y,(const unsigned char*)s,(int)strlen(s));}else{XSetForeground(D,G,out);if(FS)Xutf8DrawString(D,W,FS,G,x,y,s,(int)strlen(s));else XDrawString(D,W,G,x,y,s,(int)strlen(s));}}
static void load_theme(void){const char *home=getenv("HOME");if(!home)return;char path[PATH_MAX];size_t n=strnlen(home,sizeof path-32);if(n>=sizeof path-32)return;memcpy(path,home,n);strcpy(path+n,"/.config/wintask-linux/theme");FILE*f=fopen(path,"r");if(f){int v=0;if(fscanf(f,"%d",&v)==1)dark_mode=v?1:0;fclose(f);}}
static void save_theme(void){const char *home=getenv("HOME");if(!home)return;char dir[PATH_MAX],path[PATH_MAX];size_t n=strnlen(home,sizeof dir-32);if(n>=sizeof dir-32)return;memcpy(dir,home,n);strcpy(dir+n,"/.config/wintask-linux");mkdir(dir,0700);size_t d=strlen(dir);if(d+7>=sizeof path)return;memcpy(path,dir,d);strcpy(path+d,"/theme");FILE*f=fopen(path,"w");if(f){fprintf(f,"%d\n",dark_mode?1:0);fclose(f);}}
static void format_cpu(double value,char *out,size_t size){if(value<0.001)snprintf(out,size,"<0.001%%");else snprintf(out,size,"%.3f%%",value);}
static int measured_text_width(const char *s){if(XD&&XF){TaskXGlyphInfo e;XftTextExtentsUtf8(D,XF,(const unsigned char*)s,(int)strlen(s),&e);return e.xOff;}return (int)strlen(s)*8;}
static void cliptext(int x,int y,int max,const char *s,unsigned long c){
 char b[1024];snprintf(b,sizeof b,"%s",s?s:"");if(max<1){return;}if(measured_text_width(b)<=max){text(x,y,b,c);return;}
 size_t n=strlen(b),cut=0,best=0;const char *ell="…";char candidate[1024];
 while(cut<n){unsigned char ch=(unsigned char)b[cut];size_t step=(ch<0x80)?1:((ch&0xE0)==0xC0?2:((ch&0xF0)==0xE0?3:4));if(cut+step>n)break;cut+=step;if(cut+strlen(ell)>=sizeof candidate)break;memcpy(candidate,b,cut);strcpy(candidate+cut,ell);if(measured_text_width(candidate)>max)break;best=cut;}
 if(best){memcpy(candidate,b,best);strcpy(candidate+best,ell);text(x,y,candidate,c);}else if(measured_text_width(ell)<=max)text(x,y,ell,c);
}
static void righttext(int x,int y,int w,const char*s,unsigned long c){int tw=measured_text_width(s?s:"");if(tw>w){cliptext(x,y,w,s,c);return;}text(x+w-tw,y,s?s:"",c);}
static int measured_user_width(const char *s){if(XD&&XFUser){TaskXGlyphInfo e;XftTextExtentsUtf8(D,XFUser,(const unsigned char*)s,(int)strlen(s),&e);return e.xOff;}return measured_text_width(s);}
static void text_user(int x,int y,const char *s,unsigned long c){unsigned long out=CText(c);if(XD&&XFUser){TaskXftColor fc;fc.pixel=out;fc.color.red=(unsigned short)(((out>>16)&255)*257);fc.color.green=(unsigned short)(((out>>8)&255)*257);fc.color.blue=(unsigned short)((out&255)*257);fc.color.alpha=65535;XftDrawStringUtf8(XD,&fc,XFUser,x,y,(const unsigned char*)s,(int)strlen(s));}else text(x,y,s,c);}
static void cliptext_user(int x,int y,int max,const char *s,unsigned long c){char b[128];snprintf(b,sizeof b,"%s",s?s:"");if(max<1)return;if(measured_user_width(b)<=max){text_user(x,y,b,c);return;}size_t n=strlen(b),cut=0,best=0;const char *ell="…";char cand[128];while(cut<n){unsigned char ch=(unsigned char)b[cut];size_t step=(ch<0x80)?1:((ch&0xE0)==0xC0?2:((ch&0xF0)==0xE0?3:4));if(cut+step>n)break;cut+=step;if(cut+strlen(ell)>=sizeof cand)break;memcpy(cand,b,cut);strcpy(cand+cut,ell);if(measured_user_width(cand)>max)break;best=cut;}if(best){memcpy(cand,b,best);strcpy(cand+best,ell);text_user(x,y,cand,c);}else if(measured_user_width(ell)<=max)text_user(x,y,ell,c);}
static void center(int x,int y,int w,const char*s,unsigned long c){int tw=measured_text_width(s?s:"");int tx=x+(w-tw)/2;if(tx<x)tx=x;text(tx,y,s?s:"",c);}
static void panel(int x,int y,int w,int h){fill(x,y,w,h,PANEL);stroke(x,y,w,h,LINE);}
static unsigned long long read_cpu_total(void){FILE*f=fopen("/proc/stat","r");if(!f)return 0;char b[512];unsigned long long v[10]={0},sum=0;if(fgets(b,sizeof b,f)){char*p=b+3;int n=sscanf(p,"%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9]);for(int i=0;i<n;i++)sum+=v[i];}fclose(f);return sum;}
static double read_cpu_percent(void){FILE*f=fopen("/proc/stat","r");if(!f)return 0;char b[512];unsigned long long v[10]={0},idle=0,total=0; if(fgets(b,sizeof b,f)){int n=sscanf(b+3,"%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9]);for(int i=0;i<n;i++)total+=v[i];idle=v[3]+v[4];}fclose(f);static unsigned long long pt=0,pi=0;double p=pt&&total>pt?100.0*(double)((total-pt)-(idle-pi))/(double)(total-pt):0;pt=total;pi=idle;if(p<0)p=0;if(p>100)p=100;return p;}
static double read_mem_percent(unsigned long long *used,unsigned long long *total){FILE*f=fopen("/proc/meminfo","r");unsigned long long t=0,a=0;char l[256];if(f){while(fgets(l,sizeof l,f)){if(sscanf(l,"MemTotal: %llu",&t)==1){}else if(sscanf(l,"MemAvailable: %llu",&a)==1){}}fclose(f);}*total=t*1024;*used=(t-a)*1024;return t?100.0*(double)(t-a)/(double)t:0;}
static void process_source(pid_t pid,char *out,size_t cap){
 char path[64],link[PATH_MAX];snprintf(path,sizeof path,"/proc/%d/exe",pid);ssize_t n=readlink(path,link,sizeof link-1);if(n<1){snprintf(out,cap,"Unknown");return;}link[n]=0;
 if(strstr(link,"/.mount_")||strstr(link,".AppImage")){snprintf(out,cap,"AppImage");return;}
 if(!strncmp(link,"/app/",5)||strstr(link,"/flatpak/")){snprintf(out,cap,"Flatpak");return;}
 if(!strncmp(link,"/snap/",6)||strstr(link,"/var/lib/snapd/")){snprintf(out,cap,"Snap");return;}
 if(!strncmp(link,"/nix/store/",11)){snprintf(out,cap,"Nix");return;}
 if(strstr(link,"/steamapps/")||strstr(link,"/compatdata/")||strstr(link,"/Steam/steamapps/")){snprintf(out,cap,"Steam/Proton");return;}
 const char *home=getenv("HOME");if(!strncmp(link,"/usr/local/",11)||!strncmp(link,"/opt/",5)||!strncmp(link,"/mnt/",5)||!strncmp(link,"/media/",7)||!strncmp(link,"/tmp/",5)||(home&&(!strncmp(link,home,strlen(home))&&(strstr(link,"/.local/")||strstr(link,"/bin/")||strstr(link,"/Applications/"))))){snprintf(out,cap,"Manual / local");return;}
 if(!strncmp(link,"/usr/bin/",9)||!strncmp(link,"/usr/sbin/",10)||!strncmp(link,"/usr/lib/",9)||!strncmp(link,"/usr/lib64/",11)||!strncmp(link,"/bin/",5)||!strncmp(link,"/sbin/",6)||!strncmp(link,"/lib/",5)||!strncmp(link,"/lib64/",7)){snprintf(out,cap,"System package");return;}
 snprintf(out,cap,"Unknown");
}
static int cmp_proc(const void*,const void*);
static int cmp_appstat(const void*a,const void*b){return strcasecmp(((const AppStat*)a)->name,((const AppStat*)b)->name);}
static void sample_core_history(void){
 unsigned char current[MAX_CPU_CORES]={0},found[MAX_CPU_CORES]={0};int count=0;
 FILE*f=fopen("/proc/stat","r");if(f){char line[512],label[32];unsigned long long v[10];
  while(fgets(line,sizeof line,f)){
   if(strncmp(line,"cpu",3)||!isdigit((unsigned char)line[3]))continue;
   int n=sscanf(line,"%31s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",label,&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9]);
   if(n<5)continue;
   char*end=NULL;long id=strtol(label+3,&end,10);if(!end||*end||id<0||id>=MAX_CPU_CORES)continue;
   unsigned long long total=0;for(int k=0;k<n-1;k++)total+=v[k];unsigned long long idle=v[3]+(n>5?v[4]:0);int pct=0;
   if(core_prev_valid[id]&&total>core_prev_total[id]){unsigned long long dt=total-core_prev_total[id],di=idle>=core_prev_idle[id]?idle-core_prev_idle[id]:0;if(di>dt)di=dt;pct=(int)(100.0*(double)(dt-di)/(double)dt+0.5);if(pct<0)pct=0;if(pct>100)pct=100;}
   current[id]=(unsigned char)pct;found[id]=1;core_prev_total[id]=total;core_prev_idle[id]=idle;core_prev_valid[id]=1;if((int)id+1>count)count=(int)id+1;
  }fclose(f);
 }
 core_count=count;for(int i=0;i<core_count;i++)core_hist[i][core_hist_pos]=found[i]?current[i]:0;
 core_hist_pos=(core_hist_pos+1)%CORE_HISTORY;if(core_hist_count<CORE_HISTORY)core_hist_count++;
}
static int matches(const char *s);
static int proc_index_for_pid(pid_t pid){if(pid<=0)return -1;unsigned h=((unsigned)pid*2654435761u)&(PROC_HASH_SIZE-1);for(unsigned n=0;n<PROC_HASH_SIZE;n++,h=(h+1)&(PROC_HASH_SIZE-1)){int v=proc_hash[h];if(!v)return -1;if(proc[v-1].pid==pid)return v-1;}return -1;}
static void rebuild_proc_hash(void);
static int proc_matches_current(int i){return matches(proc[i].name)||matches(proc[i].user)||matches(proc[i].source);}
static int proc_pid_collapsed(pid_t pid){for(int i=0;i<proc_collapsed_n;i++)if(proc_collapsed[i]==pid)return 1;return 0;}
static void proc_toggle_collapsed(pid_t pid){for(int i=0;i<proc_collapsed_n;i++)if(proc_collapsed[i]==pid){proc_collapsed[i]=proc_collapsed[--proc_collapsed_n];return;}if(proc_collapsed_n<MAXP)proc_collapsed[proc_collapsed_n++]=pid;}
static void proc_mark_hidden(int i){if(i<0||i>=nproc||proc_tree_seen[i])return;proc_tree_seen[i]=1;for(int c=proc_first_child[i];c>=0;c=proc_next_sibling[c])proc_mark_hidden(c);}
static int proc_subtree_matches(int i){
 if(i<0||i>=nproc)return 0;
 if(proc_filter_cache[i])return proc_filter_cache[i]==2;
 int yes=proc_matches_current(i);
 if(!yes)for(int c=proc_first_child[i];c>=0;c=proc_next_sibling[c])if(proc_subtree_matches(c)){yes=1;break;}
 proc_filter_cache[i]=(unsigned char)(yes?2:1);return yes;
}
static void proc_view_walk(int i,int depth){
 if(i<0||i>=nproc||proc_tree_seen[i])return;
 if(query[0]&&!proc_subtree_matches(i)){proc_mark_hidden(i);return;}
 proc_tree_seen[i]=1;int matched=proc_matches_current(i);
 if(proc_view_n<MAXP){int row=proc_view_n++;proc_view[row]=i;proc_view_depth[row]=depth;proc_view_matched[row]=(unsigned char)matched;int has=0;for(int c=proc_first_child[i];c>=0;c=proc_next_sibling[c])if(!query[0]||proc_subtree_matches(c)){has=1;break;}proc_view_has_children[row]=has;}
 if(proc_pid_collapsed(proc[i].pid)){for(int c=proc_first_child[i];c>=0;c=proc_next_sibling[c])if(!query[0]||proc_subtree_matches(c))proc_mark_hidden(c);return;}
 for(int c=proc_first_child[i];c>=0;c=proc_next_sibling[c])if(!query[0]||proc_subtree_matches(c))proc_view_walk(c,depth+1);
}
static void build_process_view(void){
 proc_view_n=0;rebuild_proc_hash();memset(proc_tree_seen,0,(size_t)nproc);memset(proc_filter_cache,0,(size_t)nproc);
 for(int i=0;i<nproc;i++){proc_first_child[i]=proc_next_sibling[i]=proc_child_tail[i]=-1;}
 for(int i=0;i<nproc;i++){int p=proc_index_for_pid(proc[i].ppid);if(p<0||p==i)continue;if(proc_first_child[p]<0)proc_first_child[p]=i;else proc_next_sibling[proc_child_tail[p]]=i;proc_child_tail[p]=i;}
 for(int i=0;i<nproc;i++)if(proc_index_for_pid(proc[i].ppid)<0||proc_index_for_pid(proc[i].ppid)==i)proc_view_walk(i,0);
 for(int i=0;i<nproc;i++)if(!proc_tree_seen[i])proc_view_walk(i,0);
}
static void process_column_positions(int x,int right,int out[9]){int w=right-x;if(w<1)w=1;out[0]=x+12;out[1]=x+(w*23)/100;out[2]=x+(w*29)/100;out[3]=x+(w*37)/100;out[4]=x+(w*45)/100;out[5]=x+(w*53)/100;out[6]=x+(w*62)/100;out[7]=x+(w*72)/100;out[8]=x+(w*83)/100;}
static int cmp_tcp_owner(const void*a,const void*b){const TcpOwner*x=a,*y=b;if(x->inode<y->inode)return -1;if(x->inode>y->inode)return 1;return (x->pid>y->pid)-(x->pid<y->pid);}
static pid_t tcp_owner_for_inode(unsigned long long inode){int lo=0,hi=tcp_owner_n-1;while(lo<=hi){int m=lo+(hi-lo)/2;unsigned long long v=tcp_owners[m].inode;if(v==inode)return tcp_owners[m].pid;if(v<inode)lo=m+1;else hi=m-1;}return 0;}
static void rebuild_proc_hash(void){memset(proc_hash,0,sizeof(proc_hash));for(int i=0;i<nproc;i++){unsigned h=((unsigned)proc[i].pid*2654435761u)&(PROC_HASH_SIZE-1);for(unsigned k=0;k<PROC_HASH_SIZE;k++,h=(h+1)&(PROC_HASH_SIZE-1))if(!proc_hash[h]){proc_hash[h]=i+1;break;}}}
static int collect_tcp_owners(void){tcp_owner_n=0;for(int i=0;i<nproc&&tcp_owner_n<MAX_NET_SOCK;i++){char dirpath[64];snprintf(dirpath,sizeof dirpath,"/proc/%ld/fd",(long)proc[i].pid);DIR*d=opendir(dirpath);if(!d)continue;struct dirent*e;while((e=readdir(d))&&tcp_owner_n<MAX_NET_SOCK){if(e->d_name[0]=='.')continue;char path[128],link[128];snprintf(path,sizeof path,"%s/%s",dirpath,e->d_name);ssize_t n=readlink(path,link,sizeof link-1);if(n<10)continue;link[n]=0;if(strncmp(link,"socket:[",8))continue;char*end=NULL;unsigned long long inode=strtoull(link+8,&end,10);if(end&&*end==']'&&inode){tcp_owners[tcp_owner_n].inode=inode;tcp_owners[tcp_owner_n].pid=proc[i].pid;tcp_owner_n++;}}closedir(d);}qsort(tcp_owners,tcp_owner_n,sizeof(tcp_owners[0]),cmp_tcp_owner);int out=0;for(int i=0;i<tcp_owner_n;i++)if(!out||tcp_owners[i].inode!=tcp_owners[out-1].inode)tcp_owners[out++]=tcp_owners[i];tcp_owner_n=out;return tcp_owner_n;}
static int query_tcp_diag_family(int family){
 int fd=socket(AF_NETLINK,SOCK_RAW,NETLINK_SOCK_DIAG);if(fd<0)return 0;struct timeval tv={1,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);struct sockaddr_nl local={0},kernel={0};local.nl_family=AF_NETLINK;kernel.nl_family=AF_NETLINK;if(bind(fd,(struct sockaddr*)&local,sizeof local)<0){close(fd);return 0;}
 struct {struct nlmsghdr nlh;struct inet_diag_req_v2 req;} q;memset(&q,0,sizeof q);q.nlh.nlmsg_len=NLMSG_LENGTH(sizeof(q.req));q.nlh.nlmsg_type=SOCK_DIAG_BY_FAMILY;q.nlh.nlmsg_flags=NLM_F_REQUEST|NLM_F_DUMP;q.nlh.nlmsg_seq=(unsigned)family+1;q.req.sdiag_family=(unsigned char)family;q.req.sdiag_protocol=IPPROTO_TCP;q.req.idiag_states=UINT32_MAX;q.req.idiag_ext=(unsigned char)(1U<<(INET_DIAG_INFO-1));if(sendto(fd,&q,q.nlh.nlmsg_len,0,(struct sockaddr*)&kernel,sizeof kernel)<0){close(fd);return 0;}
 char buf[65536];int done=0,ok=1;while(!done){ssize_t got=recv(fd,buf,sizeof buf,0);if(got<0){if(errno==EINTR)continue;ok=0;break;}int rem=(int)got;for(struct nlmsghdr*h=(struct nlmsghdr*)buf;NLMSG_OK(h,rem);h=NLMSG_NEXT(h,rem)){if(h->nlmsg_seq!=(unsigned)family+1)continue;if(h->nlmsg_type==NLMSG_DONE){done=1;break;}if(h->nlmsg_type==NLMSG_ERROR){struct nlmsgerr*er=(struct nlmsgerr*)NLMSG_DATA(h);if(er->error)ok=0;done=1;break;}if(h->nlmsg_type!=SOCK_DIAG_BY_FAMILY||h->nlmsg_len<NLMSG_LENGTH(sizeof(struct inet_diag_msg)))continue;struct inet_diag_msg*m=(struct inet_diag_msg*)NLMSG_DATA(h);int alen=(int)h->nlmsg_len-(int)NLMSG_LENGTH(sizeof(*m));struct rtattr*a=(struct rtattr*)((char*)m+NLMSG_ALIGN(sizeof(*m)));struct tcp_info info;int got_info=0;for(;RTA_OK(a,alen);a=RTA_NEXT(a,alen)){if(a->rta_type==INET_DIAG_INFO){size_t z=(size_t)RTA_PAYLOAD(a);memset(&info,0,sizeof info);if(z>sizeof info)z=sizeof info;if(z>=offsetof(struct tcp_info,tcpi_bytes_sent)+sizeof(info.tcpi_bytes_sent)&&z>=offsetof(struct tcp_info,tcpi_bytes_received)+sizeof(info.tcpi_bytes_received)){memcpy(&info,RTA_DATA(a),z);got_info=1;}break;}}if(!got_info)continue;pid_t pid=tcp_owner_for_inode(m->idiag_inode);if(!pid)continue;int pi=proc_index_for_pid(pid);if(pi<0)continue;Proc*p=&proc[pi];p->net_rx_total+=info.tcpi_bytes_received;p->net_tx_total+=info.tcpi_bytes_sent;p->have_network=1;}}
 close(fd);return done&&ok;
}
static void sample_process_network(void){long long now=monotonic_ms();if(page!=0||now-last_proc_net_sample_ms<2500)return;last_proc_net_sample_ms=now;for(int i=0;i<nproc;i++){proc[i].net_rx_total=proc[i].net_tx_total=0;proc[i].net_rx_rate=proc[i].net_tx_rate=0;proc[i].have_network=0;}collect_tcp_owners();rebuild_proc_hash();int ok4=query_tcp_diag_family(AF_INET),ok6=query_tcp_diag_family(AF_INET6);proc_net_available=ok4||ok6;for(int i=0;i<nproc;i++){Proc*p=&proc[i];IoSlot*slot=io_slot(p->pid,p->start_ticks);if(slot->net_valid&&now>slot->net_ms){double dt=(now-slot->net_ms)/1000.0;if(dt>0){p->net_rx_rate=p->net_rx_total>=slot->net_rx?(double)(p->net_rx_total-slot->net_rx)/dt:0;p->net_tx_rate=p->net_tx_total>=slot->net_tx?(double)(p->net_tx_total-slot->net_tx)/dt:0;}}slot->net_rx=p->net_rx_total;slot->net_tx=p->net_tx_total;slot->net_ms=now;slot->net_valid=1;}}
static unsigned long resource_heat(double level,int alternate){if(level<0)level=0;if(level>1)level=1;double t=level;if(dark_mode){unsigned r0=alternate?0x303030:0x282828,r1=0xD9B52C;int rr=(int)(((r0>>16)&255)*(1-t)+((r1>>16)&255)*t),gg=(int)(((r0>>8)&255)*(1-t)+((r1>>8)&255)*t),bb=(int)((r0&255)*(1-t)+(r1&255)*t);return (unsigned long)((rr<<16)|(gg<<8)|bb);}else{unsigned r0=alternate?0xFAFBFD:0xFFFFFF,r1=0xFFE46B;int rr=(int)(((r0>>16)&255)*(1-t)+((r1>>16)&255)*t),gg=(int)(((r0>>8)&255)*(1-t)+((r1>>8)&255)*t),bb=(int)((r0&255)*(1-t)+(r1&255)*t);return (unsigned long)((rr<<16)|(gg<<8)|bb);}}
static unsigned long heat_text_color(double level){return dark_mode&&level>0.66?0x171717:INK;}

static void draw_triangle(int x,int y,int collapsed){XPoint p[3];if(collapsed){p[0].x=(short)(x-2);p[0].y=(short)(y-4);p[1].x=(short)(x+3);p[1].y=(short)y;p[2].x=(short)(x-2);p[2].y=(short)(y+4);}else{p[0].x=(short)(x-4);p[0].y=(short)(y-2);p[1].x=(short)(x+4);p[1].y=(short)(y-2);p[2].x=(short)x;p[2].y=(short)(y+3);}XSetForeground(D,G,CText(MUTED));XFillPolygon(D,W,G,p,3,Convex,CoordModeOrigin);}
static void refresh(void){pid_t keep_pid=(selected>=0&&selected<nproc)?proc[selected].pid:0;DIR*d=opendir("/proc");if(!d)return;int oldn=nproc;nold=oldn;if(nold>MAXP)nold=MAXP;for(int j=0;j<nold;j++){oldproc[j].pid=proc[j].pid;oldproc[j].ticks=proc[j].ticks;oldproc[j].cpu=proc[j].cpu;}tickhash_clear();for(int j=0;j<nold;j++)tickhash_put(oldproc[j].pid,oldproc[j].ticks,oldproc[j].cpu);nproc=0;unsigned long long total=read_cpu_total();struct dirent*e;long pagesize=sysconf(_SC_PAGESIZE);while((e=readdir(d))&&nproc<MAXP){if(!isdigit((unsigned char)e->d_name[0]))continue;pid_t pid=(pid_t)atoi(e->d_name);char path[128],buf[4096];snprintf(path,sizeof path,"/proc/%d/stat",pid);FILE*f=fopen(path,"r");if(!f)continue;if(!fgets(buf,sizeof buf,f)){fclose(f);continue;}fclose(f);char*l=strchr(buf,'('),*r=strrchr(buf,')');if(!l||!r)continue;Proc*p=&proc[nproc];memset(p,0,sizeof(*p));p->pid=pid;size_t len=(size_t)(r-l-1);if(len>=sizeof p->name)len=sizeof p->name-1;memcpy(p->name,l+1,len);char tail[2048];snprintf(tail,sizeof tail,"%s",r+2);char *save=NULL,*tok=strtok_r(tail," ",&save);int field=0;unsigned long long ut=0,st=0,rss=0;while(tok){field++;if(field==1)p->state=tok[0];if(field==2)p->ppid=(pid_t)strtol(tok,NULL,10);if(field==12)ut=strtoull(tok,NULL,10);if(field==13)st=strtoull(tok,NULL,10);if(field==18)p->threads=strtoul(tok,NULL,10);if(field==20)p->start_ticks=strtoull(tok,NULL,10);if(field==21)p->vsize=strtoull(tok,NULL,10);if(field==22)rss=strtoull(tok,NULL,10);tok=strtok_r(NULL," ",&save);}p->ticks=ut+st;p->rss=rss*(unsigned long long)pagesize;unsigned long long oldticks=tickhash_get(pid);if(oldticks){unsigned long long dt=p->ticks>=oldticks?p->ticks-oldticks:0,dd=total>=prev_total?total-prev_total:0;double instant=dd?100.0*(double)dt/(double)dd:0.0;double recent=tickhash_get_cpu(pid);p->cpu=recent*0.70+instant*0.30;}snprintf(path,sizeof path,"/proc/%d/status",pid);f=fopen(path,"r");if(f){char l2[256];while(fgets(l2,sizeof l2,f))if(!strncmp(l2,"Uid:",4)){unsigned uid=(unsigned)atoi(l2+4);snprintf(p->user,sizeof p->user,"%s",username_for_uid(uid));break;}fclose(f);}if(!p->user[0])strcpy(p->user,"?");if(page==0){update_process_disk_io(p);process_source(pid,p->source,sizeof p->source);}
 int ai=-1;for(int k=0;k<nappstats;k++)if(!strcmp(appstats[k].name,p->name)){ai=k;break;}
 if(ai<0&&nappstats<512){ai=nappstats++;snprintf(appstats[ai].name,sizeof appstats[ai].name,"%s",p->name);}
 if(ai>=0){appstats[ai].cpu_sum+=p->cpu;appstats[ai].samples++;if(p->rss>appstats[ai].peak_rss)appstats[ai].peak_rss=p->rss;}
 nproc++;}closedir(d);prev_total=total;qsort(proc,nproc,sizeof(Proc),cmp_proc);qsort(appstats,nappstats,sizeof(appstats[0]),cmp_appstat);rebuild_proc_hash();selected=-1;if(keep_pid)for(int k=0;k<nproc;k++)if(proc[k].pid==keep_pid){selected=k;break;}sample_process_network();}
static int matches(const char*s){if(!query[0])return 1;char a[512],b[128];snprintf(a,sizeof a,"%s",s);snprintf(b,sizeof b,"%s",query);for(char*p=a;*p;p++)*p=(char)tolower((unsigned char)*p);for(char*p=b;*p;p++)*p=(char)tolower((unsigned char)*p);return strstr(a,b)!=NULL;}
static int page_has_search(int pg){return pg==2||pg==3||pg==4||pg==5||pg==6||pg==7||pg==8||pg==9||pg==10||pg==11;}
static int text_matches_query(const char *q,const char *s){if(!q||!q[0])return 1;if(!s)return 0;char a[1024],b[128];snprintf(a,sizeof a,"%s",s);snprintf(b,sizeof b,"%s",q);for(char*p=a;*p;p++)*p=(char)tolower((unsigned char)*p);for(char*p=b;*p;p++)*p=(char)tolower((unsigned char)*p);return strstr(a,b)!=NULL;}
static int list_matches(int pg,const char *s){return pg==0?matches(s):text_matches_query(pg>=0&&pg<PAGE_COUNT?list_queries[pg]:"",s);}
static int list_search_x(void){return ww-24-300;}
static void draw_list_search(int pg){if(!page_has_search(pg))return;int x=list_search_x(),y=70,w=300;fill(x,y,w,30,WHITE);stroke(x,y,w,30,list_search_focus_page==pg?BLUE:0xAEB8C8);XSetForeground(D,G,CText(MUTED));XDrawArc(D,W,G,x+10,y+8,9,9,0,360*64);ln(x+18,y+17,x+23,y+22,list_search_focus_page==pg?BLUE:MUTED);const char*q=list_queries[pg];cliptext(x+31,y+20,w-62,q[0]?q:"Search this list…",q[0]?INK:MUTED);if(q[0]){fill(x+w-29,y+6,22,18,0xEDF1F7);text(x+w-23,y+20,"×",MUTED);}}
static void reset_search_scroll(int pg){if(pg==2)history_scroll=0;else if(pg==7){disk_scroll=0;network_scroll=0;}else if(pg==8)gpu_scroll=0;else if(pg==10)cpu_scroll=0;else if(pg==11)memory_scroll=0;else scroll=0;}
static void draw_page_subtitle(int x,int y,const char*s){int maxw=list_search_x()-x-12;if(maxw<120)maxw=ww-x-24;cliptext(x,y,maxw,s,MUTED);}

static int cmp_proc(const void*aa,const void*bb){const Proc*a=aa,*b=bb;int r=0;switch(sort_col){case 1:r=(a->pid>b->pid)-(a->pid<b->pid);break;case 2:r=(a->state>b->state)-(a->state<b->state);break;case 3:r=(a->cpu>b->cpu)-(a->cpu<b->cpu);break;case 4:r=(a->rss>b->rss)-(a->rss<b->rss);break;case 5:{double x=a->disk_read_rate+a->disk_write_rate,y=b->disk_read_rate+b->disk_write_rate;r=(x>y)-(x<y);break;}case 6:{double x=a->net_rx_rate+a->net_tx_rate,y=b->net_rx_rate+b->net_tx_rate;r=(x>y)-(x<y);break;}default:r=strcasecmp(a->name,b->name);}return sort_desc?-r:r;}
static Proc *selected_proc(void){return selected>=0&&selected<nproc?&proc[selected]:NULL;}
static int message_is_processing(const char *s){return s&&(!strncasecmp(s,"Processing:",11)||!strncasecmp(s,"Updating",8)||!strncasecmp(s,"Starting",8)||!strncasecmp(s,"Working",7)||!strncasecmp(s,"Enabling",8)||!strncasecmp(s,"Disabling",9)||!strncasecmp(s,"Stopping",8)||!strncasecmp(s,"Restarting",10)||!strncasecmp(s,"Launching",9));}
static void commit_status(const char*s){snprintf(statusmsg,sizeof statusmsg,"%s",s?s:"");status_changed_ms=monotonic_ms();}
static void setstatus(const char*s){long long now=monotonic_ms();if(message_is_processing(s)){queued_status_valid=0;queued_status[0]=0;processing_hold_until_ms=now+850;commit_status(s);return;}if(status_processing()&&now<processing_hold_until_ms){snprintf(queued_status,sizeof queued_status,"%s",s?s:"");queued_status_valid=1;return;}commit_status(s);}
static void finish_queued_status(void){if(queued_status_valid&&monotonic_ms()>=processing_hold_until_ms){char b[256];snprintf(b,sizeof b,"%s",queued_status);queued_status_valid=0;queued_status[0]=0;commit_status(b);draw();}}
static void request_elevated_signal(pid_t pid,int sig,const char *name){
 if(pending_action_pid>0){setstatus("Another system action is still running; wait for it to finish.");return;}
 char pidbuf[32],sigarg[16];snprintf(pidbuf,sizeof pidbuf,"%ld",(long)pid);snprintf(sigarg,sizeof sigarg,"-%s",sig==SIGKILL?"KILL":"TERM");
 pid_t child=fork();
 if(child==0){execlp("pkexec","pkexec","/usr/bin/kill",sigarg,pidbuf,(char*)NULL);execlp("sudo","sudo","-A","/usr/bin/kill",sigarg,pidbuf,(char*)NULL);_exit(127);}
 if(child<0){char b[220];snprintf(b,sizeof b,"Could not request administrator permission: %s",strerror(errno));setstatus(b);return;}
 pending_action_pid=child;pending_action_service=0;pending_action_elevated=1;
 snprintf(pending_action_text,sizeof pending_action_text,"elevated %s for %.80s (PID %ld)",sig==SIGKILL?"force stop":"termination",name,(long)pid);
 setstatus("Processing: requesting administrator approval to stop a protected process…");draw();XFlush(D);
}
static void signal_selected(int sig){Proc*p=selected_proc();if(!p){setstatus("Select a process first.");return;}pid_t pid=p->pid;char name[160];snprintf(name,sizeof name,"%.150s",p->name);char pending[192];snprintf(pending,sizeof pending,"%s %s (PID %d)",sig==SIGTERM?"Ending task":sig==SIGKILL?"Force-stopping task":sig==SIGSTOP?"Suspending process":"Resuming process",name,pid);begin_action(pending);if(kill(pid,sig)==0){char b[256];snprintf(b,sizeof b,"%s sent to %s (PID %d).",sig==SIGTERM?"Terminate request":sig==SIGKILL?"Force-stop request":sig==SIGSTOP?"Suspend request":"Resume request",name,pid);setstatus(b);}else{int err=errno;if(err==EPERM&&(sig==SIGTERM||sig==SIGKILL)){request_elevated_signal(pid,sig,name);return;}char b[256];snprintf(b,sizeof b,"Could not signal PID %d: %s",pid,strerror(err));setstatus(b);}}
static void show_exe(void){Proc*p=selected_proc();if(!p){setstatus("Select a process first.");return;}char src[128];snprintf(src,sizeof src,"/proc/%d/exe",p->pid);ssize_t n=readlink(src,selected_path,sizeof selected_path-1);if(n<0){setstatus("Executable path is unavailable (permission or process exited).");return;}selected_path[n]=0;begin_action("reading executable path");char b[256];snprintf(b,sizeof b,"Executable: %.220s",selected_path);setstatus(b);}
static void open_exe_dir(void){Proc*p=selected_proc();if(!p){setstatus("Select a process first.");return;}char src[128],dst[PATH_MAX];snprintf(src,sizeof src,"/proc/%d/exe",p->pid);ssize_t n=readlink(src,dst,sizeof dst-1);if(n<0){setstatus("Executable path is unavailable.");return;}dst[n]=0;char*slash=strrchr(dst,'/');if(slash)*slash=0;begin_action("opening executable directory");pid_t c=fork();if(c==0){execlp("xdg-open","xdg-open",dst,(char*)NULL);_exit(127);}if(c>0)setstatus("Opening executable directory.");else setstatus("Could not open file manager.");}
static void search_online(void){Proc*p=selected_proc();if(!p)return;begin_action("searching for the selected process online");char url[1024]="https://www.google.com/search?q=";size_t n=strlen(url);for(const char*s=p->name;*s&&n+4<sizeof url;s++){if(isalnum((unsigned char)*s)||*s=='-'||*s=='_')url[n++]=*s;else if(*s==' ')url[n++]='+';else{snprintf(url+n,sizeof url-n,"%%%02X",(unsigned char)*s);n+=3;}}url[n]=0;pid_t c=fork();if(c==0){execlp("xdg-open","xdg-open",url,(char*)NULL);_exit(127);}setstatus(c>0?"Opened web search.":"Could not open browser.");}
static int run_has_candidate(const char *name){for(int i=0;i<run_suggestion_count;i++)if(!strcasecmp(run_suggestions[i],name))return 1;return 0;}
static void run_add_candidate(const char *name){if(!name||!*name||strchr(name,'/')||strlen(name)>=sizeof run_suggestions[0]||run_suggestion_count>=RUN_MAX_SUGGESTIONS||run_has_candidate(name))return;snprintf(run_suggestions[run_suggestion_count++],sizeof run_suggestions[0],"%s",name);}
static void build_run_suggestions(void){
 static const char *common[]={"firefox","google-chrome","chromium","dolphin","konsole","systemsettings","gnome-calculator","kcalc","kate","code","htop","btop","top","nautilus","thunar","pavucontrol","nm-connection-editor","xterm","kitty","steam","lutris","flatpak","systemctl","reboot","shutdown","bash","fish","sh","ls","find","grep","nano","vim","nvim","gedit","gwenview","okular","spectacle","discover","ksysguard","plasma-systemmonitor","kwrite","ark","filelight","partitionmanager","firefox-developer-edition",NULL};
 run_suggestion_count=0;for(int i=0;common[i];i++)run_add_candidate(common[i]);
 const char *path=getenv("PATH");if(!path||!*path)return;
 char *copy=strdup(path);if(!copy)return;char *save=NULL;
 for(char *dir=strtok_r(copy,":",&save);dir&&run_suggestion_count<RUN_MAX_SUGGESTIONS;dir=strtok_r(NULL,":",&save)){
  if(!*dir) dir=".";
  DIR*d=opendir(dir); if(!d) continue; struct dirent*e;
  while((e=readdir(d))&&run_suggestion_count<RUN_MAX_SUGGESTIONS){if(e->d_name[0]=='.'||!isalpha((unsigned char)e->d_name[0]))continue;char full[PATH_MAX];if(snprintf(full,sizeof full,"%s/%s",dir,e->d_name)>=(int)sizeof full)continue;if(access(full,X_OK)==0)run_add_candidate(e->d_name);}
  closedir(d);
 }free(copy);
}
static const char *run_filter_text(void){static char filter[512];snprintf(filter,sizeof filter,"%s",run_command);char *space=strpbrk(filter," \t");if(space)*space=0;return filter;}
static int run_matches(const char *candidate){const char *filter=run_filter_text();if(!*filter)return 1;return strncasecmp(candidate,filter,strlen(filter))==0||strcasestr(candidate,filter)!=NULL;}
static int cmp_run_suggestions(const void*a,const void*b){return strcasecmp((const char*)a,(const char*)b);}
static void run_task_dialog(void){build_run_suggestions();qsort(run_suggestions,run_suggestion_count,sizeof(run_suggestions[0]),cmp_run_suggestions);run_dialog=1;run_focus=1;run_suggestion=-1;run_scroll=0;run_filtered_count=visible_run_count=0;run_command[0]=0;XSetInputFocus(D,W,RevertToParent,CurrentTime);setstatus("Ready: type a command; matching apps and executables appear below.");}
static int run_dialog_w(void){int w=680;return ww-24<w?ww-24:w;}
static int run_dialog_h(void){int h=500;return hh-24<h?hh-24:h;}
static int run_dialog_x(void){return (ww-run_dialog_w())/2;}
static int run_dialog_y(void){return (hh-run_dialog_h())/2;}
static int run_dialog_rows(void){int rows=(run_dialog_h()-238)/RUN_ROW_H;if(rows>RUN_VISIBLE)rows=RUN_VISIBLE;if(rows<1)rows=1;return rows;}
static int run_filtered_pos(int candidate){for(int i=0;i<run_filtered_count;i++)if(run_filtered[i]==candidate)return i;return -1;}
static void run_update_filtered(void){
 run_filtered_count=0;
 const char *filter=run_filter_text();

 for(int pass=0;pass<2;pass++){
  for(int i=0;i<run_suggestion_count&&run_filtered_count<RUN_MAX_SUGGESTIONS;i++){
   const char *candidate=run_suggestions[i];
   int prefix=!*filter||strncasecmp(candidate,filter,strlen(filter))==0;
   if(run_matches(candidate)&&prefix==(pass==0))run_filtered[run_filtered_count++]=i;
  }
 }
 int rows=run_dialog_rows(),maxscroll=run_filtered_count-rows;if(maxscroll<0)maxscroll=0;
 if(run_scroll<0)run_scroll=0;
 if(run_scroll>maxscroll)run_scroll=maxscroll;
 if(run_suggestion>=0&&run_filtered_pos(run_suggestion)<0)run_suggestion=-1;
 if(run_suggestion<0&&run_filtered_count>0)run_suggestion=run_filtered[run_scroll];
 visible_run_count=0;for(int i=run_scroll;i<run_filtered_count&&visible_run_count<rows;i++)visible_run_suggestions[visible_run_count++]=run_filtered[i];
}
static void launch_command(void){if(!run_command[0]&&run_suggestion<0){setstatus("Type a command or choose a suggestion first.");return;}if(!run_command[0]&&run_suggestion>=0)snprintf(run_command,sizeof run_command,"%s",run_suggestions[run_suggestion]);char command[512];snprintf(command,sizeof command,"%s",run_command);begin_action("launching command");pid_t child=fork();if(child==0){setsid();execl("/bin/sh","sh","-c",command,(char*)NULL);_exit(127);}if(child>0){char b[256];snprintf(b,sizeof b,"Launched command: %.220s",command);setstatus(b);}else{char b[256];snprintf(b,sizeof b,"Could not start command: %s",strerror(errno));setstatus(b);}run_dialog=0;run_focus=0;}
static void run_choose_suggestion(int delta){run_update_filtered();if(!run_filtered_count)return;int pos=run_filtered_pos(run_suggestion);if(pos<0)pos=run_scroll;pos=(pos+delta)%run_filtered_count;if(pos<0)pos+=run_filtered_count;run_suggestion=run_filtered[pos];int rows=run_dialog_rows();if(pos<run_scroll)run_scroll=pos;else if(pos>=run_scroll+rows)run_scroll=pos-rows+1;run_update_filtered();}
static int action_banner_visible(void){
 if(!status_processing()&&!status_failed()&&!status_action_result())return 0;
 if(!status_processing()&&status_changed_ms>0&&monotonic_ms()-status_changed_ms>5000)return 0;
 return 1;
}
static int user_badge_width(void);
static int user_badge_x(void);
static void draw_action_banner(void){
 if(!action_banner_visible())return;
 int processing=status_processing(),failed=status_failed();
 unsigned long bg=processing?0xFFF1D6:(failed?0xFDE7E7:0xE1F5EA);
 unsigned long fg=processing?AMBER:(failed?RED:GREEN);
 int bw=198,bh=36,by=11;
 int bx=user_badge_x()-12-bw;if(bx<NAVW+12)bx=NAVW+12;
 fill(bx,by,bw,bh,bg);stroke(bx,by,bw,bh,fg);fill(bx+10,by+13,9,9,fg);
 text(bx+26,by+23,processing?"PROCESSING…":(failed?"ACTION FAILED":"ACTION COMPLETE"),fg);
}
static int user_badge_width(void){
 const char *who=username_for_uid((unsigned)getuid());
 int maxw=ww-NAVW-48;if(maxw<96)maxw=96;
 int w=measured_user_width(who)+30;
 if(w<116)w=116;
 if(w>330)w=330;
 if(w>maxw)w=maxw;
 return w;
}
static int user_badge_x(void){return ww-24-user_badge_width();}
static void draw_user_badge(void){
 const char *who=username_for_uid((unsigned)getuid());
 int bx=user_badge_x(),bw=user_badge_width(),by=13,bh=36;
 fill(bx,by,bw,bh,0xF0F3F8);
 stroke(bx,by,bw,bh,LINE);
 fill(bx+1,by+7,3,bh-14,BLUE);
 cliptext_user(bx+14,by+23,bw-28,who,INK);
}
static void draw_nav(void){
 fill(0,0,ww,hh,BG);fill(0,0,ww,62,PANEL);fill(0,0,5,62,BLUE);text_user(24,39,"LinuxManager",INK);ln(0,61,ww,61,LINE);draw_user_badge();
 fill(0,62,NAVW,hh-86,0xEDF1F7);
 for(int i=0;i<PAGE_COUNT;i++){int y=86+i*38;if(page==i){fill(9,y-23,NAVW-18,33,0xDFE8FF);fill(9,y-23,4,33,BLUE);}text(28,y,pages[i],page==i?BLUE:INK);}
 int ty=hh-60;fill(12,ty,NAVW-24,28,dark_mode?0x34445A:0xFFFFFF);stroke(12,ty,NAVW-24,28,dark_mode?0x50617A:LINE);fill(23,ty+9,10,10,dark_mode?0x60CDFF:BLUE);text(42,ty+19,dark_mode?"Light mode":"Dark mode",dark_mode?0x60CDFF:BLUE);
 ln(NAVW,62,NAVW,hh-24,LINE);fill(0,hh-24,ww,24,PANEL);ln(0,hh-24,ww,hh-24,LINE);
 unsigned long sc=MUTED;if(status_failed())sc=RED;else if(status_processing())sc=AMBER;else if(status_action_result()||!strncasecmp(statusmsg,"Selected",8)||!strncasecmp(statusmsg,"Ready",5))sc=GREEN;
 fill(12,hh-15,7,7,sc);cliptext(28,hh-7,ww-44,statusmsg,sc);
}
static void draw_processes(int details){
 (void)details;int x=NAVW+26,top=84,right=ww-24,tablew=right-x;char b[256];
 text(x,top,pages[page],INK);text(x,top+24,"Yellow shading intensifies with use.",MUTED);
 int barY=top+38;fill(x,barY,340,30,WHITE);stroke(x,barY,340,30,search_focus?BLUE:0xAEB8C8);XSetForeground(D,G,CText(MUTED));XDrawArc(D,W,G,x+10,barY+8,9,9,0,360*64);ln(x+18,barY+17,x+23,barY+22,search_focus?BLUE:MUTED);cliptext(x+31,barY+20,270,query[0]?query:"Click or type to search…",query[0]?INK:MUTED);if(query[0]){fill(x+307,barY+7,24,16,0xEDF1F7);text(x+314,barY+20,"×",MUTED);}fill(right-194,barY,194,30,BLUE);center(right-194,barY+20,194,"Run new task",WHITE);
 int y=top+90;fill(x,y,tablew,32,0xF0F3F8);int pos[9];process_column_positions(x,right,pos);
 const char*heads[] ={"Name","PID","Status","User","CPU","Memory","Disk R/W","Network","Source"};for(int j=0;j<9;j++)text(pos[j],y+21,heads[j],MUTED);ln(x,y+32,right,y+32,LINE);
 build_process_view();int rowtop=y+33,baseline=y+53,visible=(hh-44-rowtop+29)/30;if(visible<1)visible=1;int maxscroll=proc_view_n>visible?proc_view_n-visible:0;if(scroll>maxscroll)scroll=maxscroll;if(scroll<0)scroll=0;unsigned long long memused=0,memtotal=0;read_mem_percent(&memused,&memtotal);
 for(int row=scroll;row<proc_view_n&&row<scroll+visible;row++){
  int i=proc_view[row],yy=baseline+(row-scroll)*30;if(yy>hh-44)break;Proc*p=&proc[i];int depth=proc_view_depth[row];if(depth>8)depth=8;int indent=depth*14;int nm_x=pos[0]+indent+15;int name_w=pos[1]-nm_x-8;if(name_w<24)name_w=24;
  if(i==selected)fill(x,yy-20,tablew,29,status_processing()?0xFFF1D6:PALE);else if((row-scroll)%2)fill(x,yy-20,tablew,29,0xFAFBFD);
  double cpuheat=p->cpu/8.0;if(cpuheat>1)cpuheat=1;double memheat=memtotal?(double)p->rss/(double)memtotal/0.20:0;if(memheat>1)memheat=1;double diskheat=(p->disk_read_rate+p->disk_write_rate)/(5.0*1048576.0);if(diskheat>1)diskheat=1;double netheat=(p->net_rx_rate+p->net_tx_rate)/(5.0*1048576.0);if(netheat>1)netheat=1;
  int cells[4]={4,5,6,7};double heats[4]={cpuheat,memheat,diskheat,netheat};int have[4]={1,1,p->have_io,p->have_network&&proc_net_available};for(int k=0;k<4;k++)if(have[k]){int cw=pos[cells[k]+1]-pos[cells[k]]-3;if(cells[k]==7)cw=pos[8]-pos[7]-3;fill(pos[cells[k]]-3,yy-20,cw,29,resource_heat(heats[k],(row-scroll)%2));}
  if(depth>0){int branch=pos[0]+(depth-1)*14+4;if(branch>pos[0]+112)branch=pos[0]+112;ln(branch,yy-15,branch,yy-5,LINE);ln(branch,yy-5,branch+8,yy-5,LINE);}
  if(proc_view_has_children[row])draw_triangle(pos[0]+indent+4,yy-5,proc_pid_collapsed(p->pid));
  cliptext(nm_x,yy,name_w,p->name,proc_view_matched[row]?INK:MUTED);snprintf(b,sizeof b,"%d",p->pid);text(pos[1],yy,b,INK);const char*state=p->state=='R'?"Running":p->state=='S'?"Sleeping":p->state=='D'?"Uninterruptible":p->state=='T'?"Stopped":p->state=='Z'?"Zombie":p->state=='I'?"Idle":"Background";unsigned long statecolor=p->state=='R'?GREEN:p->state=='Z'?RED:(p->state=='T'||p->state=='D')?AMBER:p->state=='I'?BLUE:MUTED;cliptext(pos[2],yy,pos[3]-pos[2]-7,state,statecolor);if(i==selected)stroke(x,yy-20,tablew,29,BLUE);cliptext(pos[3],yy,pos[4]-pos[3]-7,p->user,MUTED);format_cpu(p->cpu,b,sizeof b);cliptext(pos[4],yy,pos[5]-pos[4]-5,b,heat_text_color(cpuheat));format_process_memory(p->rss,b,sizeof b);cliptext(pos[5],yy,pos[6]-pos[5]-5,b,heat_text_color(memheat));
  if(p->have_io)format_rate_pair(p->disk_read_rate,p->disk_write_rate,b,sizeof b);else snprintf(b,sizeof b,"N/A");cliptext(pos[6],yy,pos[7]-pos[6]-5,b,heat_text_color(diskheat));if(!proc_net_available)snprintf(b,sizeof b,"N/A");else if(p->have_network)format_rate_pair(p->net_rx_rate,p->net_tx_rate,b,sizeof b);else snprintf(b,sizeof b,"—");cliptext(pos[7],yy,pos[8]-pos[7]-5,b,heat_text_color(netheat));cliptext(pos[8],yy,right-pos[8]-8,p->source[0]?p->source:"Unknown",MUTED);
 }
 if(!proc_view_n)text(x+15,rowtop+20,"No matching processes.",MUTED);
}
static void draw_graph(int x,int y,int w,int h,const char*title,const char*value,double*hist,int count,unsigned long color){
 panel(x,y,w,h);text(x+16,y+24,title,INK);cliptext(x+w-270,y+24,254,value,color);
 int gx=x+16,gy=y+40,gw=w-32,gh=h-58;for(int k=1;k<4;k++)ln(gx,gy+gh*k/4,gx+gw,gy+gh*k/4,LINE);
 if(count>1){for(int i=1;i<count;i++){int a=(histpos-count+i-1+120)%120,b=(histpos-count+i+120)%120;int x1=gx+(i-1)*gw/(count-1),x2=gx+i*gw/(count-1);int y1=gy+gh-(int)(hist[a]*gh/100.0),y2=gy+gh-(int)(hist[b]*gh/100.0);ln(x1,y1,x2,y2,color);}}
}
static void draw_performance(void){
 int x=NAVW+26,y=84,w=ww-NAVW-50;unsigned long long used,total;double mp=read_mem_percent(&used,&total);double cp=cpu_hist[(histpos+119)%120];long ncpu=sysconf(_SC_NPROCESSORS_ONLN);if(ncpu<1)ncpu=1;
 char a[128],b[128];snprintf(a,sizeof a,"%.1f%% | %.2f / %ld cores",cp,cp*(double)ncpu/100.0,ncpu);snprintf(b,sizeof b,"%.1f%% | %.2f / %.2f GiB",mp,used/1073741824.0,total/1073741824.0);
 text(x,y,"Performance",INK);text(x,y+24,"Live CPU, memory, disk I/O and network throughput.",MUTED);int gw=(w-24)/2,gw2=w-gw-24,gh=(hh-y-100)/2;if(gh<160)gh=160;
 draw_graph(x,y+42,gw,gh,"CPU usage",a,cpu_hist,histcount,BLUE);draw_graph(x+gw+24,y+42,gw2,gh,"Memory usage",b,mem_hist,histcount,GREEN);
 struct sysinfo si;sysinfo(&si);char z[256];snprintf(z,sizeof z,"Logical processors: %ld",ncpu);text(x,y+gh+65,z,INK);snprintf(z,sizeof z,"Memory in use: %.2f GiB / %.2f GiB",used/1073741824.0,total/1073741824.0);text(x,y+gh+91,z,INK);snprintf(z,sizeof z,"Disk read: %.1f KiB/s | Disk write: %.1f KiB/s",disk_read_rate/1024.0,disk_write_rate/1024.0);text(x,y+gh+117,z,INK);snprintf(z,sizeof z,"Network received: %.1f KiB/s | Network sent: %.1f KiB/s",net_rx_rate/1024.0,net_tx_rate/1024.0);text(x,y+gh+143,z,INK);snprintf(z,sizeof z,"System uptime: %ld days, %ld hours, %ld minutes | Processes: %d",si.uptime/86400,(si.uptime/3600)%24,(si.uptime/60)%60,nproc);text(x,y+gh+169,z,INK);
}
typedef struct {char name[64],home[256],shell[256];unsigned uid;} UserEntry;
static int cmp_user_entry(const void *a,const void *b){return strcasecmp(((const UserEntry*)a)->name,((const UserEntry*)b)->name);}
static void draw_users(void){int x=NAVW+26,y=84,right=ww-24;text(x,y,"Users",INK);draw_page_subtitle(x,y+24,"Accounts known to the system, sorted alphabetically by username.");draw_list_search(4);y+=54;fill(x,y,right-x,32,0xF0F3F8);text(x+12,y+21,"Username",MUTED);text(x+300,y+21,"UID",MUTED);text(x+430,y+21,"Home directory",MUTED);text(x+760,y+21,"Login shell",MUTED);y+=52;static UserEntry items[1024];int count=0;struct passwd*pw;setpwent();while((pw=getpwent())&&count<1024){snprintf(items[count].name,sizeof items[count].name,"%s",pw->pw_name?pw->pw_name:"");snprintf(items[count].home,sizeof items[count].home,"%s",pw->pw_dir?pw->pw_dir:"");snprintf(items[count].shell,sizeof items[count].shell,"%s",pw->pw_shell?pw->pw_shell:"");items[count].uid=(unsigned)pw->pw_uid;count++;}endpwent();qsort(items,count,sizeof(items[0]),cmp_user_entry);int matches_count=0;for(int i=0;i<count;i++){char blob[PATH_MAX*2+256];snprintf(blob,sizeof blob,"%.60s %.255s %.255s %u",items[i].name,items[i].home,items[i].shell,items[i].uid);if(list_matches(4,blob))matches_count++;}int visible=(hh-44-y)/28;if(visible<0)visible=0;if(scroll>matches_count-visible)scroll=matches_count>visible?matches_count-visible:0;int pos=0,drawn=0;for(int i=0;i<count;i++){char blob[PATH_MAX*2+256];snprintf(blob,sizeof blob,"%.60s %.255s %.255s %u",items[i].name,items[i].home,items[i].shell,items[i].uid);if(!list_matches(4,blob))continue;if(pos++<scroll||drawn>=visible)continue;int yy=y+drawn*28;if(drawn%2==0)fill(x,yy-19,right-x,26,WHITE);cliptext(x+12,yy,270,items[i].name,INK);char b[32];snprintf(b,sizeof b,"%u",items[i].uid);text(x+300,yy,b,INK);cliptext(x+430,yy,300,items[i].home,MUTED);cliptext(x+760,yy,right-(x+760)-12,items[i].shell,MUTED);drawn++;}if(!drawn)text(x+12,y,list_queries[4][0]?"No accounts match your search.":"No local accounts found.",MUTED);}
static int cmp_startup(const void *a,const void *b){return strcasecmp(((const Startup*)a)->name,((const Startup*)b)->name);}
static int cmp_service(const void *a,const void *b){return strcasecmp(((const Service*)a)->unit,((const Service*)b)->unit);}
static int startup_hidden_true(const char *path){FILE*f=fopen(path,"r");if(!f)return 0;char line[512];int hidden=0;while(fgets(line,sizeof line,f)){if(!strncasecmp(line,"Hidden=",7)){char*v=line+7;while(isspace((unsigned char)*v))v++;if(!strncasecmp(v,"true",4)||!strncmp(v,"1",1)||!strncasecmp(v,"yes",3))hidden=1;break;}}fclose(f);return hidden;}
static void startup_base_name(const char *filename,char *out,size_t size,int *disabled){snprintf(out,size,"%s",filename);*disabled=0;size_t n=strlen(out);if(n>9&&!strcmp(out+n-9,".disabled")){out[n-9]=0;*disabled=1;}}
static int startup_find_name(const char *name){for(int i=0;i<nstartups;i++)if(!strcmp(startups[i].name,name))return i;return -1;}
static int safe_path_suffix(char *out,size_t cap,const char *path,const char *suffix){size_t a=strlen(path),b=strlen(suffix);if(a+b>=cap)return 0;memcpy(out,path,a);memcpy(out+a,suffix,b+1);return 1;}
static int safe_path_join(char *out,size_t cap,const char *dir,const char *name){size_t a=strlen(dir),b=strlen(name);if(a+b+2>cap)return 0;memcpy(out,dir,a);out[a]='/';memcpy(out+a+1,name,b+1);return 1;}
static void load_startups(void){
 time_t now=time(NULL);if(startups_loaded&&now-startups_loaded<5)return;startups_loaded=now;nstartups=0;
 char dirs[3][PATH_MAX];const char*home=getenv("HOME");snprintf(dirs[0],sizeof dirs[0],"%s/.config/autostart",home?home:"");snprintf(dirs[1],sizeof dirs[1],"/etc/xdg/autostart");snprintf(dirs[2],sizeof dirs[2],"/usr/share/autostart");

 for(int d=2;d>=0;d--){DIR*dir=opendir(dirs[d]);if(!dir)continue;struct dirent*e;while((e=readdir(dir))&&nstartups<512){if(e->d_name[0]=='.')continue;char base[256];int disabled=0;startup_base_name(e->d_name,base,sizeof base,&disabled);if(!strstr(base,".desktop"))continue;int idx=startup_find_name(base);if(idx>=0&&d==1)continue;
  int was_system=idx>=0?startups[idx].system_entry:0;
  if(idx<0)idx=nstartups++;
  Startup*st=&startups[idx];memset(st,0,sizeof(*st));snprintf(st->name,sizeof st->name,"%s",base);snprintf(st->path,sizeof st->path,"%.3000s/%.1000s",dirs[d],e->d_name);st->user_owned=(d==0);st->system_entry=(d!=0)||was_system;st->disabled=disabled;
  if(d==0&&!disabled&&startup_hidden_true(st->path)){st->disabled=1;char backup[PATH_MAX];snprintf(backup,sizeof backup,"%s.taskmgr-backup",st->path);st->hidden_override=access(backup,F_OK)==0?2:1;}
 }closedir(dir);}
 qsort(startups,nstartups,sizeof(startups[0]),cmp_startup);
}
static int startup_filtered_index(int position){int seen=0;for(int i=0;i<nstartups;i++){if(!list_matches(3,startups[i].name)&&!list_matches(3,startups[i].path))continue;if(seen++==position)return i;}return -1;}
static void draw_startup(void){int x=NAVW+26,y=84,right=ww-24;text(x,y,"Startup apps",INK);draw_page_subtitle(x,y+24,"XDG autostart entries. Right-click to enable or disable for your account.");draw_list_search(3);y+=54;fill(x,y,right-x,32,0xF0F3F8);text(x+12,y+21,"Startup entry",MUTED);text(x+390,y+21,"Status",MUTED);text(x+570,y+21,"Location",MUTED);y+=52;load_startups();int count=0;for(int i=0;i<nstartups;i++)if(list_matches(3,startups[i].name)||list_matches(3,startups[i].path))count++;int visible=(hh-44-y)/28;if(visible<0)visible=0;if(scroll>count-visible)scroll=count>visible?count-visible:0;for(int pos=scroll;pos<count&&pos<scroll+visible;pos++){int i=startup_filtered_index(pos);if(i<0)continue;Startup*st=&startups[i];int yy=y+(pos-scroll)*28;if(i==selected_startup)fill(x,yy-19,right-x,26,status_processing()?0xFFF1D6:PALE);else if((pos-scroll)%2==0)fill(x,yy-19,right-x,26,WHITE);cliptext(x+12,yy,350,st->name,INK);text(x+390,yy,st->disabled?"Disabled":"Enabled",st->disabled?AMBER:GREEN);cliptext(x+570,yy,right-x-580,st->path,MUTED); }if(!count)text(x+12,y,list_queries[3][0]?"No startup entries match your search.":"No XDG autostart entries found.",MUTED);}
static void startup_action(int action){
 if(selected_startup<0||selected_startup>=nstartups){setstatus("Select a startup entry first.");return;}
 Startup st=startups[selected_startup];
 if(action==0){char dir[PATH_MAX];snprintf(dir,sizeof dir,"%s",st.path);char*slash=strrchr(dir,'/');if(slash)*slash=0;begin_action("opening startup entry folder");pid_t c=fork();if(c==0){execlp("xdg-open","xdg-open",dir,(char*)NULL);_exit(127);}if(c>0)setstatus("Opened startup entry location.");else{char b[192];snprintf(b,sizeof b,"Could not open startup folder: %s",strerror(errno));setstatus(b);}return;}
 if(action==2){char b[256];begin_action("reading startup entry path");snprintf(b,sizeof b,"Startup file: %.220s",st.path);setstatus(b);return;}
 if(pending_action_pid>0){setstatus("Updating another system action; wait for it to finish.");return;}
 char dst[PATH_MAX],userdir[PATH_MAX];const char*home=getenv("HOME");if(!home||!home[0]){setstatus("Could not find the home directory for this user.");return;}snprintf(userdir,sizeof userdir,"%s/.config/autostart",home);char cfg[PATH_MAX];snprintf(cfg,sizeof cfg,"%s/.config",home);mkdir(cfg,0700);mkdir(userdir,0700);
 char action_label[220];snprintf(action_label,sizeof action_label,"%s startup entry: %.150s",st.disabled?"Enabling":"Disabling",st.name);begin_action(action_label);
 int ok=0;char errbuf[180]="";
 if(st.user_owned&&st.hidden_override&&st.disabled){if(unlink(st.path)==0){if(st.hidden_override==2){char backup[PATH_MAX];if(!safe_path_suffix(backup,sizeof backup,st.path,".taskmgr-backup")){snprintf(errbuf,sizeof errbuf,"backup path is too long");}else if(rename(backup,st.path)!=0)snprintf(errbuf,sizeof errbuf,"override removed but backup restore failed: %s",strerror(errno));else ok=1;}else ok=1;}else snprintf(errbuf,sizeof errbuf,"%s",strerror(errno));}
 else if(st.user_owned&&st.system_entry&&!st.disabled){char backup[PATH_MAX];if(!safe_path_suffix(backup,sizeof backup,st.path,".taskmgr-backup"))snprintf(errbuf,sizeof errbuf,"file path is too long");else if(access(backup,F_OK)==0)snprintf(errbuf,sizeof errbuf,"backup already exists; refusing to overwrite it");else if(rename(st.path,backup)!=0)snprintf(errbuf,sizeof errbuf,"%s",strerror(errno));else{FILE*f=fopen(st.path,"w");if(!f){snprintf(errbuf,sizeof errbuf,"%s",strerror(errno));rename(backup,st.path);}else{int wr=fprintf(f,"[Desktop Entry]\nType=Application\nName=Disabled by Linux Task Manager\nHidden=true\nX-TaskManager-Disabled=true\n");int cr=fclose(f);if(wr>0&&cr==0)ok=1;else{snprintf(errbuf,sizeof errbuf,"could not write Hidden=true override");unlink(st.path);rename(backup,st.path);}}}}
 else if(st.user_owned){snprintf(dst,sizeof dst,"%s",st.path);if(st.disabled){size_t n=strlen(dst);if(n>9&&!strcmp(dst+n-9,".disabled"))dst[n-9]=0;else snprintf(errbuf,sizeof errbuf,"disabled entry name is invalid");}else{if(strlen(dst)+9>=sizeof dst)snprintf(errbuf,sizeof errbuf,"file path is too long");else strncat(dst,".disabled",sizeof dst-strlen(dst)-1);}if(!errbuf[0]){if(rename(st.path,dst)==0)ok=1;else snprintf(errbuf,sizeof errbuf,"%s",strerror(errno));}}
 else if(st.disabled){if(!safe_path_join(dst,sizeof dst,userdir,st.name))snprintf(errbuf,sizeof errbuf,"file path is too long");else if(unlink(dst)==0)ok=1;else snprintf(errbuf,sizeof errbuf,"%s",strerror(errno));}
 else {if(!safe_path_join(dst,sizeof dst,userdir,st.name))snprintf(errbuf,sizeof errbuf,"file path is too long");else{FILE*f=fopen(dst,"w");if(!f)snprintf(errbuf,sizeof errbuf,"%s",strerror(errno));else{int wr=fprintf(f,"[Desktop Entry]\nType=Application\nName=Disabled by Linux Task Manager\nHidden=true\nX-TaskManager-Disabled=true\n");int cr=fclose(f);if(wr>0&&cr==0)ok=1;else snprintf(errbuf,sizeof errbuf,"could not write user override");}}}
 if(ok){char b[220];snprintf(b,sizeof b,"Startup entry %s: %.160s. Changes apply to future logins.",st.disabled?"enabled":"disabled",st.name);startups_loaded=0;load_startups();setstatus(b);}else{char b[256];snprintf(b,sizeof b,"Could not change startup entry: %.180s",errbuf[0]?errbuf:"unknown error");setstatus(b);}
}
static int history_filtered_index(int position){int seen=0;for(int i=0;i<nappstats;i++)if(list_matches(2,appstats[i].name)){if(seen++==position)return i;}return -1;}
static void draw_history(void){
 int x=NAVW+26,y=84,right=ww-24;char b[256];text(x,y,"App history",INK);draw_page_subtitle(x,y+24,"Select rows with a click; scroll to browse session CPU and peak-memory history.");draw_list_search(2);y+=54;
 fill(x,y,right-x,32,0xF0F3F8);text(x+12,y+21,"Application / process",MUTED);text(x+450,y+21,"Average CPU",MUTED);text(x+620,y+21,"Peak memory",MUTED);text(x+790,y+21,"Samples",MUTED);y+=52;
 int count=0;for(int i=0;i<nappstats;i++)if(list_matches(2,appstats[i].name))count++;int visible=(hh-62-y)/29;if(visible<1)visible=1;int maxscroll=count>visible?count-visible:0;if(history_scroll>maxscroll)history_scroll=maxscroll;if(history_scroll<0)history_scroll=0;
 int end=history_scroll+visible;if(end>count)end=count;int shown=0;
 for(int pos=history_scroll;pos<end;pos++){int i=history_filtered_index(pos);if(i<0||!appstats[i].samples)continue;int yy=y+shown*29;int picked=selected_appstat_name[0]&&!strcmp(selected_appstat_name,appstats[i].name);if(picked)fill(x,yy-19,right-x,27,status_processing()?0xFFF1D6:PALE);else if(shown%2==0)fill(x,yy-19,right-x,27,WHITE);
  cliptext(x+12,yy,410,appstats[i].name,INK);format_cpu(appstats[i].cpu_sum/(double)appstats[i].samples,b,sizeof b);text(x+450,yy,b,INK);snprintf(b,sizeof b,"%.1f MB",appstats[i].peak_rss/1048576.0);text(x+620,yy,b,INK);snprintf(b,sizeof b,"%lu",appstats[i].samples);text(x+790,yy,b,MUTED);shown++;}
 if(!shown)text(x+12,y,list_queries[2][0]?"No history entries match your search.":"Collecting application usage…",MUTED);
 text(x,y<hh-24?hh-36:y,"History is session-only. It resets when the app closes.",MUTED);
}
static int service_find_unit(const char *unit){if(!unit||!unit[0])return -1;for(int i=0;i<nservices;i++)if(!strcmp(services[i].unit,unit))return i;return -1;}
static void load_services(void){
 time_t now=time(NULL);if(services_loaded&&now-services_loaded<5)return;services_loaded=now;nservices=0;
 FILE*f=popen("systemctl list-units --type=service --all --no-legend --no-pager 2>/dev/null","r");
 if(!f){services_loaded=0;return;}
 char linebuf[1024];while(fgets(linebuf,sizeof linebuf,f)&&nservices<1024){Service *sv=&services[nservices];memset(sv,0,sizeof(*sv));int got=sscanf(linebuf,"%159s %31s %31s %31s %255[^\n]",sv->unit,sv->load,sv->active,sv->sub,sv->desc);if(got>=4)nservices++;}
 pclose(f);qsort(services,nservices,sizeof(services[0]),cmp_service);selected_service=service_find_unit(selected_service_unit);
}
static int service_filtered_index(int position){int seen=0;for(int i=0;i<nservices;i++){char blob[600];snprintf(blob,sizeof blob,"%.150s %.30s %.30s %.30s %.250s",services[i].unit,services[i].load,services[i].active,services[i].sub,services[i].desc);if(!list_matches(6,blob))continue;if(seen++==position)return i;}return -1;}
static void draw_services(void){int x=NAVW+26,y=84,right=ww-24;text(x,y,"Services",INK);draw_page_subtitle(x,y+24,"Systemd units. Right-click for state-appropriate actions; results are verified.");draw_list_search(6);y+=54;fill(x,y,right-x,32,0xF0F3F8);text(x+12,y+21,"Unit",MUTED);text(x+430,y+21,"Load",MUTED);text(x+570,y+21,"Active",MUTED);text(x+740,y+21,"Description",MUTED);y+=52;load_services();int count=0;for(int i=0;i<nservices;i++){char blob[600];snprintf(blob,sizeof blob,"%.150s %.30s %.30s %.30s %.250s",services[i].unit,services[i].load,services[i].active,services[i].sub,services[i].desc);if(list_matches(6,blob))count++;}int visible=(hh-44-y)/28;if(visible<0)visible=0;if(scroll>count-visible)scroll=count>visible?count-visible:0;for(int pos=scroll;pos<count&&pos<scroll+visible;pos++){int i=service_filtered_index(pos);if(i<0)continue;Service*sv=&services[i];int yy=y+(pos-scroll)*28;if(selected_service_unit[0]&&!strcmp(sv->unit,selected_service_unit))fill(x,yy-19,right-x,26,status_processing()?0xFFF1D6:PALE);else if((pos-scroll)%2==0)fill(x,yy-19,right-x,26,WHITE);cliptext(x+12,yy,400,sv->unit,INK);cliptext(x+430,yy,120,sv->load,MUTED);cliptext(x+570,yy,150,sv->active,!strcmp(sv->active,"active")?GREEN:INK);cliptext(x+740,yy,right-x-750,sv->desc,MUTED);}if(!count)text(x+12,y,list_queries[6][0]?"No services match your search.":"No systemd service units available.",MUTED);}


static int read_service_properties(const char *unit,char *active,size_t asz,char *sub,size_t ssz,char *type,size_t tsz,char *result,size_t rsz){
 int fds[2];if(pipe(fds)<0)return 0;pid_t c=fork();if(c<0){close(fds[0]);close(fds[1]);return 0;}
 if(c==0){dup2(fds[1],STDOUT_FILENO);close(fds[0]);close(fds[1]);execlp("systemctl","systemctl","show","--no-pager","--property=ActiveState","--property=SubState","--property=Type","--property=Result",unit,(char*)NULL);_exit(127);}
 close(fds[1]);FILE*f=fdopen(fds[0],"r");if(!f){close(fds[0]);int st;waitpid(c,&st,0);return 0;}
 char line[256];while(fgets(line,sizeof line,f)){line[strcspn(line,"\r\n")]=0;char*eq=strchr(line,'=');if(!eq)continue;*eq++=0;if(!strcmp(line,"ActiveState"))snprintf(active,asz,"%s",eq);else if(!strcmp(line,"SubState"))snprintf(sub,ssz,"%s",eq);else if(!strcmp(line,"Type"))snprintf(type,tsz,"%s",eq);else if(!strcmp(line,"Result"))snprintf(result,rsz,"%s",eq);}
 fclose(f);int st=0;pid_t wr;do{wr=waitpid(c,&st,0);}while(wr<0&&errno==EINTR);if(wr!=c)return 0;return WIFEXITED(st)&&WEXITSTATUS(st)==0&&active[0];
}
static int wait_for_service_state(const char *action,const char *unit){
 pid_t cmd=fork();if(cmd<0)return 30;if(cmd==0){execlp("systemctl","systemctl",action,unit,(char*)NULL);_exit(127);}int st=0;while(waitpid(cmd,&st,0)<0){if(errno==EINTR)continue;return 30;}if(!WIFEXITED(st)||WEXITSTATUS(st)!=0)return 30;
 int failed_count=0;for(int tries=0;tries<60;tries++){
  char active[64]="",sub[64]="",type[64]="",result[64]="";
  if(!read_service_properties(unit,active,sizeof active,sub,sizeof sub,type,sizeof type,result,sizeof result))return 31;
  if(!strcmp(action,"stop")){if(!strcmp(active,"inactive"))return 0;}
  else if(!strcmp(active,"active")||(!strcmp(type,"oneshot")&&!strcmp(active,"inactive")&&!strcmp(result,"success")))return 0;
  if(!strcmp(active,"failed")){if(++failed_count>=4)return 33;}else failed_count=0;
  usleep(250000);
 }
 return 32;
}
static void service_action(const char *action){
 if(!selected_service_unit[0]){setstatus("Select a service first.");return;}
 if(pending_action_pid>0){setstatus("Updating another system action; please wait for it to finish.");return;}
 char unit[160];snprintf(unit,sizeof unit,"%s",selected_service_unit);
 pid_t child=fork();if(child==0)_exit(wait_for_service_state(action,unit));
 if(child>0){pending_action_pid=child;pending_action_service=1;pending_action_elevated=0;snprintf(pending_action_text,sizeof pending_action_text,"systemctl %s %.130s",action,unit);char b[220];snprintf(b,sizeof b,"Processing: systemctl %s %.130s; verifying actual service state…",action,unit);setstatus(b);services_loaded=0;draw();XFlush(D);}
 else{char b[180];snprintf(b,sizeof b,"Could not start service action: %s",strerror(errno));setstatus(b);}
}
static void poll_pending_action(void){
 if(pending_action_pid<=0)return;
 int st=0;pid_t r=waitpid(pending_action_pid,&st,WNOHANG);
 if(r==0)return;
 if(r<0&&errno==EINTR)return;
 char b[256];int elevated=pending_action_elevated,service=pending_action_service,code=(r>0&&WIFEXITED(st))?WEXITSTATUS(st):-1;
 if(service){
  if(code==0)snprintf(b,sizeof b,"Completed and verified: %.185s reached the requested state.",pending_action_text);
  else if(code==30)snprintf(b,sizeof b,"Failed: %.155s returned an error. Check authorization, whether the unit is masked, and its status.",pending_action_text);
  else if(code==31)snprintf(b,sizeof b,"Could not verify %.160s: systemd did not return a readable live state.",pending_action_text);
  else if(code==32)snprintf(b,sizeof b,"Failed: %.130s returned success, but the service did not reach the requested state within 15 seconds.",pending_action_text);
  else if(code==33)snprintf(b,sizeof b,"Failed: %.150s ended in systemd's failed state.",pending_action_text);
  else snprintf(b,sizeof b,"Failed: %.175s was interrupted before its result could be verified.",pending_action_text);
 }else if(r<0){snprintf(b,sizeof b,"Could not verify action result: %s",strerror(errno));}
 else if(code==0){snprintf(b,sizeof b,"Completed: %.210s",pending_action_text);if(elevated)selected=-1;}
 else if(elevated&&code==126){snprintf(b,sizeof b,"Failed: administrator authentication was cancelled or denied.");}
 else if(elevated&&code==127){snprintf(b,sizeof b,"Failed: could not launch pkexec/sudo or /usr/bin/kill; check PolicyKit or sudo askpass is available.");}
 else if(elevated&&code>=0){snprintf(b,sizeof b,"Failed: elevated kill returned exit %d; the process may have exited or the action was denied.",code);}
 else if(code>=0){snprintf(b,sizeof b,"Failed: %.180s (exit %d; permissions or state may prevent it)",pending_action_text,code);}
 else if(WIFSIGNALED(st)){snprintf(b,sizeof b,"Failed: %.180s (signal %d)",pending_action_text,WTERMSIG(st));}
 else snprintf(b,sizeof b,"Action finished: %.190s",pending_action_text);
 setstatus(b);pending_action_pid=0;pending_action_service=0;pending_action_elevated=0;pending_action_text[0]=0;services_loaded=0;
}

static void format_bytes_short(unsigned long long bytes,char*out,size_t cap){
 static const char*units[]={"B","KiB","MiB","GiB","TiB","PiB"};double n=(double)bytes;int u=0;while(n>=1024.0&&u<5){n/=1024.0;u++;}if(!u)snprintf(out,cap,"%llu B",bytes);else snprintf(out,cap,"%.1f %s",n,units[u]);
}
static int storage_device_name(const char*name){char path[PATH_MAX];snprintf(path,sizeof path,"/sys/block/%.180s",name);return access(path,F_OK)==0;}
static void draw_disk_network(void){
 int x=NAVW+26,y=84,w=ww-NAVW-50;char line[512],name[64];text(x,y,"Disk & Network",INK);draw_page_subtitle(x,y+24,"Aligned storage totals and per-interface network counters, including packet counts.");draw_list_search(7);
 int top=y+54,avail=hh-44-top;if(avail<190)avail=190;int dh=avail*40/100;if(dh<126)dh=126;if(dh>240)dh=240;int ny=top+dh+12,nh=hh-44-ny;if(nh<130)nh=130;disk_panel_bottom=top+dh;
 panel(x,top,w,dh);text(x+16,top+25,"Storage devices",BLUE);int devx=x+16,readx=x+(w*38)/100,writex=x+(w*68)/100;int hy=top+50;fill(x+1,hy,w-2,27,0xF0F3F8);text(devx,hy+19,"Device",MUTED);text(readx,hy+19,"Total read",MUTED);text(writex,hy+19,"Total written",MUTED);
 int visible=(dh-100)/25;if(visible<1)visible=1;int count=0;FILE*f=fopen("/proc/diskstats","r");if(f){while(fgets(line,sizeof line,f)){unsigned maj,min;unsigned long long rd=0,wr=0;if(sscanf(line,"%u %u %63s %*u %*u %llu %*u %*u %*u %*u %llu",&maj,&min,name,&rd,&wr)==5&&storage_device_name(name)&&list_matches(7,name))count++;}fclose(f);}int dmax=count>visible?count-visible:0;if(disk_scroll>dmax)disk_scroll=dmax;if(disk_scroll<0)disk_scroll=0;
 f=fopen("/proc/diskstats","r");int row=0,drawn=0;if(f){while(fgets(line,sizeof line,f)){unsigned maj,min;unsigned long long rd=0,wr=0;if(sscanf(line,"%u %u %63s %*u %*u %llu %*u %*u %*u %*u %llu",&maj,&min,name,&rd,&wr)!=5||!storage_device_name(name)||!list_matches(7,name))continue;if(row++<disk_scroll)continue;if(drawn>=visible)break;int yy=top+98+drawn*25;if(drawn%2==0)fill(x+2,yy-18,w-4,24,WHITE);char rb[48],wb[48];format_bytes_short(rd*512ULL,rb,sizeof rb);format_bytes_short(wr*512ULL,wb,sizeof wb);cliptext(devx,yy,readx-devx-18,name,INK);cliptext(readx,yy,writex-readx-18,rb,INK);cliptext(writex,yy,x+w-writex-18,wb,INK);drawn++;}fclose(f);}if(!drawn)text(x+16,top+98,list_queries[7][0]?"No storage devices match your search.":"No storage device counters available.",MUTED);
 panel(x,ny,w,nh);text(x+16,ny+25,"Network interfaces",GREEN);int ix=x+16,rx=x+(w*24)/100,rpx=x+(w*43)/100,tx=x+(w*56)/100,tpx=x+(w*77)/100;hy=ny+50;fill(x+1,hy,w-2,27,0xF0F3F8);text(ix,hy+19,"Interface",MUTED);text(rx,hy+19,"Received",MUTED);text(rpx,hy+19,"RX packets",MUTED);text(tx,hy+19,"Sent",MUTED);text(tpx,hy+19,"TX packets",MUTED);
 visible=(nh-100)/27;if(visible<1)visible=1;count=0;f=fopen("/proc/net/dev","r");int skip=0;if(f){while(fgets(line,sizeof line,f)){if(skip++<2)continue;if(sscanf(line," %63[^:]:",name)==1&&list_matches(7,name))count++;}fclose(f);}int nmax=count>visible?count-visible:0;if(network_scroll>nmax)network_scroll=nmax;if(network_scroll<0)network_scroll=0;
 f=fopen("/proc/net/dev","r");skip=0;row=0;drawn=0;if(f){while(fgets(line,sizeof line,f)){if(skip++<2)continue;unsigned long long rxbytes=0,rxpackets=0,txbytes=0,txpackets=0;if(sscanf(line," %63[^:]: %llu %llu %*u %*u %*u %*u %*u %*u %llu %llu",name,&rxbytes,&rxpackets,&txbytes,&txpackets)!=5||!list_matches(7,name))continue;if(row++<network_scroll)continue;if(drawn>=visible)break;int yy=ny+98+drawn*27;if(drawn%2==0)fill(x+2,yy-19,w-4,26,WHITE);char rb[48],tb[48],rp[32],tp[32];format_bytes_short(rxbytes,rb,sizeof rb);format_bytes_short(txbytes,tb,sizeof tb);snprintf(rp,sizeof rp,"%llu",rxpackets);snprintf(tp,sizeof tp,"%llu",txpackets);cliptext(ix,yy,rx-ix-12,name,INK);cliptext(rx,yy,rpx-rx-14,rb,INK);cliptext(rpx,yy,tx-rpx-14,rp,INK);cliptext(tx,yy,tpx-tx-14,tb,INK);cliptext(tpx,yy,x+w-tpx-14,tp,INK);drawn++;}fclose(f);}if(!drawn)text(x+16,ny+98,list_queries[7][0]?"No interfaces match your search.":"No network interfaces reported.",MUTED);
}
static int read_sys_text(const char *path,char *out,size_t cap){FILE*f=fopen(path,"r");if(!f)return 0;int ok=fgets(out,(int)cap,f)!=NULL;fclose(f);if(ok)out[strcspn(out,"\r\n")]=0;return ok;}
static int read_sys_number(const char *path,double *v){char b[128],*e;if(!read_sys_text(path,b,sizeof b))return 0;errno=0;double n=strtod(b,&e);if(e==b||errno==ERANGE)return 0;*v=n;return 1;}
static void path_join(char*out,size_t cap,const char*a,const char*b){if(!cap)return;size_t na=strlen(a),nb=strlen(b);if(na>=cap)na=cap-1;memcpy(out,a,na);size_t avail=cap-na-1;if(nb>avail)nb=avail;memcpy(out+na,b,nb);out[na+nb]=0;}
static void read_hwmon(GpuInfo*g,const char*base){char dirpath[512];path_join(dirpath,sizeof dirpath,base,"/hwmon");DIR*d=opendir(dirpath);if(!d)return;struct dirent*e;while((e=readdir(d))){if(strncmp(e->d_name,"hwmon",5))continue;char h[512],f[512];double v;snprintf(h,sizeof h,"%.400s/%.15s",dirpath,e->d_name);
 if(!g->have_temp){snprintf(f,sizeof f,"%.480s/temp1_input",h);if(read_sys_number(f,&v)&&v>0&&v<200000){g->temp=v/1000.0;g->have_temp=1;}}
 if(!g->have_power){int got;snprintf(f,sizeof f,"%.480s/power1_average",h);got=read_sys_number(f,&v);if(!got){snprintf(f,sizeof f,"%.480s/power1_input",h);got=read_sys_number(f,&v);}if(got&&v>0&&v<1000000000.0){g->power=v/1000000.0;g->have_power=1;}}
 if(!g->have_fan){snprintf(f,sizeof f,"%.480s/fan1_input",h);if(read_sys_number(f,&v)&&v>=0){g->fan=v;g->have_fan=1;g->fan_rpm=1;}}
 if(!g->have_core){snprintf(f,sizeof f,"%.480s/freq1_input",h);if(read_sys_number(f,&v)&&v>0){g->core_clock=v/1000000.0;g->have_core=1;}}
 }closedir(d);}
static int find_program(const char*name){const char*path=getenv("PATH");if(!path)return 0;char copy[8192];snprintf(copy,sizeof copy,"%s",path);char*save=NULL;for(char*d=strtok_r(copy,":",&save);d;d=strtok_r(NULL,":",&save)){char p[PATH_MAX];snprintf(p,sizeof p,"%s/%s",*d?d:".",name);if(access(p,X_OK)==0)return 1;}return 0;}
static int csv_num(char *s,double*out){while(isspace((unsigned char)*s))s++;if(!*s||!strncasecmp(s,"N/A",3)||!strncasecmp(s,"[N/A]",5))return 0;char*e;errno=0;double v=strtod(s,&e);if(e==s||errno==ERANGE)return 0;*out=v;return 1;}
static void read_nvidia_stats(void){if(!nvidia_smi_state)nvidia_smi_state=find_program("nvidia-smi")?1:-1;if(nvidia_smi_state<0)return;FILE*f=popen("nvidia-smi --query-gpu=name,utilization.gpu,utilization.memory,memory.total,memory.used,temperature.gpu,power.draw,clocks.gr,clocks.mem,fan.speed --format=csv,noheader,nounits 2>/dev/null","r");if(!f){nvidia_smi_state=-1;return;}char line[768];int idx=0;while(fgets(line,sizeof line,f)&&idx<MAX_GPU){char*field[10],*cur=line;int n=0;field[n++]=cur;while(*cur&&n<10){if(*cur==','){*cur=0;field[n++]=cur+1;}cur++;}if(n<10)continue;int target=-1,seen=0;for(int i=0;i<ngpus;i++)if(!strcmp(gpus[i].vendor,"NVIDIA")){if(seen++==idx){target=i;break;}}idx++;if(target<0)continue;GpuInfo*g=&gpus[target];double v;if(*field[0])snprintf(g->name,sizeof g->name,"%.120s",field[0]);if(csv_num(field[1],&v)){g->util=v;g->have_util=1;}if(csv_num(field[2],&v)){g->memutil=v;g->have_memutil=1;}double mt,mu;if(csv_num(field[3],&mt)&&csv_num(field[4],&mu)){g->vram_total=(unsigned long long)(mt*1048576.0);g->vram_used=(unsigned long long)(mu*1048576.0);g->have_vram=1;}if(csv_num(field[5],&v)){g->temp=v;g->have_temp=1;}if(csv_num(field[6],&v)){g->power=v;g->have_power=1;}if(csv_num(field[7],&v)){g->core_clock=v;g->have_core=1;}if(csv_num(field[8],&v)){g->mem_clock=v;g->have_memclock=1;}if(csv_num(field[9],&v)){g->fan=v;g->have_fan=1;g->fan_rpm=0;}}
 int rc=pclose(f);if(rc==-1&&idx==0)nvidia_smi_state=-1;}
static void update_gpu(void){ngpus=0;DIR*d=opendir("/sys/class/drm");if(d){struct dirent*e;while((e=readdir(d))&&ngpus<MAX_GPU){if(strncmp(e->d_name,"card",4)||!isdigit((unsigned char)e->d_name[4])||strchr(e->d_name,'-'))continue;GpuInfo*g=&gpus[ngpus];memset(g,0,sizeof(*g));snprintf(g->card,sizeof g->card,"%.15s",e->d_name);char base[256],path[512],buf[256];snprintf(base,sizeof base,"/sys/class/drm/%.200s/device",e->d_name);
 snprintf(path,sizeof path,"%s/vendor",base);unsigned long vendor=0;if(read_sys_text(path,buf,sizeof buf))vendor=strtoul(buf,NULL,0);const char*vn=vendor==0x10de?"NVIDIA":vendor==0x1002?"AMD":vendor==0x8086?"Intel":vendor?"Other vendor":"Unknown vendor";snprintf(g->vendor,sizeof g->vendor,"%s",vn);
 snprintf(path,sizeof path,"%s/device",base);char devid[64]="";if(read_sys_text(path,buf,sizeof buf)){snprintf(devid,sizeof devid,"%.60s",buf);snprintf(g->pci,sizeof g->pci,"%04lx:%.25s",vendor&0xffff,devid[0]?devid:"unknown");}else snprintf(g->pci,sizeof g->pci,"vendor 0x%04lx",vendor&0xffff);
 snprintf(path,sizeof path,"%s/driver",base);ssize_t z=readlink(path,buf,sizeof buf-1);if(z>0){buf[z]=0;char*slash=strrchr(buf,'/');snprintf(g->driver,sizeof g->driver,"%.60s",slash?slash+1:buf);}else strcpy(g->driver,"unknown");
 snprintf(path,sizeof path,"%s/product_name",base);if(!read_sys_text(path,g->name,sizeof g->name)||!g->name[0])snprintf(g->name,sizeof g->name,"%.60s graphics (%.16s)",vn,e->d_name);
 double v;snprintf(path,sizeof path,"%s/gpu_busy_percent",base);if(read_sys_number(path,&v)){g->util=v;g->have_util=1;}snprintf(path,sizeof path,"%s/gt_busy_percent",base);if(!g->have_util&&read_sys_number(path,&v)){g->util=v;g->have_util=1;}
 snprintf(path,sizeof path,"%s/mem_info_vram_used",base);double mu=0,mt=0;int hu=read_sys_number(path,&mu);snprintf(path,sizeof path,"%s/mem_info_vram_total",base);int ht=read_sys_number(path,&mt);if(hu&&ht&&mt>0){g->vram_used=(unsigned long long)mu;g->vram_total=(unsigned long long)mt;g->have_vram=1;}
 snprintf(path,sizeof path,"%s/mem_info_gtt_used",base);hu=read_sys_number(path,&mu);snprintf(path,sizeof path,"%s/mem_info_gtt_total",base);ht=read_sys_number(path,&mt);if(hu&&ht&&mt>0){g->gtt_used=(unsigned long long)mu;g->gtt_total=(unsigned long long)mt;g->have_gtt=1;}
 snprintf(path,sizeof path,"%s/gt_cur_freq_mhz",base);if(read_sys_number(path,&v)&&v>0){g->core_clock=v;g->have_core=1;}read_hwmon(g,base);ngpus++;}closedir(d);}
 read_nvidia_stats();}
static void draw_gpu_history(int x,int y,int w,int h,const char*title,const char*value,double*hist,unsigned long color){panel(x,y,w,h);text(x+12,y+20,title,INK);text(x+w-72,y+20,value,color);int gx=x+12,gy=y+31,gw=w-24,gh=h-39;for(int k=1;k<4;k++)ln(gx,gy+gh*k/4,gx+gw,gy+gh*k/4,LINE);if(gpuhistcount>1){for(int i=1;i<gpuhistcount;i++){int a=(gpuhistpos-gpuhistcount+i-1+120)%120,b=(gpuhistpos-gpuhistcount+i+120)%120;int x1=gx+(i-1)*gw/(gpuhistcount-1),x2=gx+i*gw/(gpuhistcount-1);int y1=gy+gh-(int)(hist[a]*gh/100.0),y2=gy+gh-(int)(hist[b]*gh/100.0);ln(x1,y1,x2,y2,color);}}}
static void gpu_bar(int x,int y,int w,double pct,unsigned long color){if(pct<0)pct=0;if(pct>100)pct=100;fill(x,y,w,8,0xE8EDF4);if(pct>0)fill(x,y,(int)(w*pct/100.0),8,color);}
static void gpu_metric(int x,int y,int w,const char*label,const char*value){
text(x,y,label,MUTED);int vx=x+250;cliptext(vx,y,w-250,value,INK);
}
static int gpu_wrap_text(int x,int y,int maxw,const char*s,unsigned long color,int maxlines){
 int limit=maxw/7;if(limit<12)limit=12;const char*p=s?s:"";int lines=0;
 while(*p&&lines<maxlines){char line[256];int n=0,lastspace=-1;while(p[n]&&n<limit&&n<(int)sizeof(line)-1){if(p[n]==' ')lastspace=n;n++;}if(p[n]&&lastspace>0)n=lastspace;if(n<1)n=1;memcpy(line,p,(size_t)n);line[n]=0;text(x,y+lines*15,line,color);p+=n;while(*p==' ')p++;lines++;}
 return lines;
}
static void draw_gpu(void){
 int x=NAVW+26,y=84,w=ww-NAVW-50;text(x,y,"GPU details",INK);draw_page_subtitle(x,y+24,"Live GPU telemetry. Unsupported metrics are shown as N/A.");draw_list_search(8);
 int top=y+54,cw=w,ch=535,available=hh-44-top,visible=available/(ch+12),filtered=0;if(visible<1)visible=1;
 for(int i=0;i<ngpus;i++){char blob[320];snprintf(blob,sizeof blob,"%.15s %.120s %.23s %.63s %.31s",gpus[i].card,gpus[i].name,gpus[i].vendor,gpus[i].driver,gpus[i].pci);if(list_matches(8,blob))filtered++;}
 int maxscroll=filtered>visible?filtered-visible:0;if(gpu_scroll>maxscroll)gpu_scroll=maxscroll;if(gpu_scroll<0)gpu_scroll=0;int shown=0;
 for(int i=0;i<ngpus;i++){
  GpuInfo*g=&gpus[i];char blob[320];snprintf(blob,sizeof blob,"%.15s %.120s %.23s %.63s %.31s",g->card,g->name,g->vendor,g->driver,g->pci);if(!list_matches(8,blob))continue;
  int row=shown-gpu_scroll;if(row<0||row>=visible){shown++;continue;}int px=x,py=top+row*(ch+12);panel(px,py,cw,ch);
  char info[256],v[96],a[96];snprintf(info,sizeof info,"%.15s  |  %.23s",g->card,g->vendor);text(px+16,py+24,info,BLUE);
  int name_lines=gpu_wrap_text(px+16,py+46,cw-32,g->name,INK,2);if(name_lines<1)name_lines=1;
  int driver_y=py+48+name_lines*16;snprintf(info,sizeof info,"Driver: %s",g->driver[0]?g->driver:"N/A");cliptext(px+16,driver_y,cw-32,info,MUTED);snprintf(info,sizeof info,"PCI device: %s",g->pci[0]?g->pci:"N/A");cliptext(px+16,driver_y+17,cw-32,info,MUTED);
  int uy=driver_y+42;if(g->have_util)snprintf(v,sizeof v,"%.1f%%",g->util);else strcpy(v,"N/A");text(px+16,uy,"GPU utilization",INK);cliptext(px+266,uy,cw-282,v,g->have_util?BLUE:MUTED);gpu_bar(px+16,uy+8,cw-32,g->have_util?g->util:0,BLUE);
  int gy=uy+21;draw_gpu_history(px+16,gy,cw-32,138,"GPU utilization history",v,gpu_hist[i],BLUE);
  int mx=px+16,mw=cw-32,yy=gy+159;
  if(g->have_vram)snprintf(a,sizeof a,"%.0f / %.0f MiB",g->vram_used/1048576.0,g->vram_total/1048576.0);else strcpy(a,"N/A");gpu_metric(mx,yy,mw,"VRAM used / total",a);
  if(g->have_vram)snprintf(a,sizeof a,"%.1f%%",g->vram_total?100.0*g->vram_used/g->vram_total:0);else strcpy(a,"N/A");gpu_metric(mx,yy+24,mw,"VRAM utilization",a);if(g->have_vram)gpu_bar(mx+250,yy+30,mw-262,g->vram_total?100.0*g->vram_used/g->vram_total:0,GREEN);
  if(g->have_temp)snprintf(a,sizeof a,"%.0f °C",g->temp);else strcpy(a,"N/A");gpu_metric(mx,yy+52,mw,"Temperature",a);
  if(g->have_power)snprintf(a,sizeof a,"%.1f W",g->power);else strcpy(a,"N/A");gpu_metric(mx,yy+76,mw,"Power draw",a);
  if(g->have_core)snprintf(a,sizeof a,"%.0f MHz",g->core_clock);else strcpy(a,"N/A");gpu_metric(mx,yy+100,mw,"GPU core clock",a);
  if(g->have_memclock)snprintf(a,sizeof a,"%.0f MHz",g->mem_clock);else strcpy(a,"N/A");gpu_metric(mx,yy+124,mw,"Memory clock",a);
  if(g->have_memutil)snprintf(a,sizeof a,"%.0f%%",g->memutil);else strcpy(a,"N/A");gpu_metric(mx,yy+148,mw,"Memory engine utilization",a);
  if(g->have_fan)snprintf(a,sizeof a,g->fan_rpm?"%.0f RPM":"%.0f%%",g->fan);else strcpy(a,"N/A");gpu_metric(mx,yy+172,mw,g->fan_rpm?"Fan speed (RPM)":"Fan speed",a);
  if(g->have_gtt)snprintf(a,sizeof a,"%.0f / %.0f MiB",g->gtt_used/1048576.0,g->gtt_total/1048576.0);else strcpy(a,"N/A");gpu_metric(mx,yy+196,mw,"Shared / GTT memory",a);
  shown++;
 }
 if(!ngpus)text(x+12,top+24,"No GPU devices were found in /sys/class/drm.",MUTED);else if(!filtered)text(x+12,top+24,list_queries[8][0]?"No GPUs match your search.":"No GPU metrics available.",MUTED);if(filtered>visible)text(x,hh-33,"Scroll to view additional GPU devices.",MUTED);
}
static void draw_system_info(void){int x=NAVW+26,y=84,w=ww-NAVW-50;struct sysinfo si;sysinfo(&si);text(x,y,"System info",INK);draw_page_subtitle(x,y+24,"Live operating-system and hardware summary.");draw_list_search(9);y+=54;panel(x,y,w,510);const char*keys[]={"Operating system","Kernel","Architecture","Hostname","Logical CPUs","Page size","Uptime","Process count","Total RAM","Available RAM","CPU model","Boot ID","Root filesystem","Shell","User ID","Home directory","Display server","Process limit"};char vals[18][256]={{0}};FILE*f=fopen("/etc/os-release","r");if(f){while(fgets(vals[0],sizeof vals[0],f))if(!strncmp(vals[0],"PRETTY_NAME=",12)){memmove(vals[0],vals[0]+12,strlen(vals[0]+12)+1);vals[0][strcspn(vals[0],"\r\n\"")]=0;break;}fclose(f);}if(!vals[0][0])strcpy(vals[0],"Linux");struct utsname un;if(uname(&un)==0){snprintf(vals[1],sizeof vals[1],"%s",un.release);snprintf(vals[2],sizeof vals[2],"%s (%s)",un.machine,sizeof(void*)==8?"64-bit":"32-bit");}else{strcpy(vals[1],"Unknown");strcpy(vals[2],sizeof(void*)==8?"64-bit":"32-bit");}if(gethostname(vals[3],sizeof vals[3]-1)<0)strcpy(vals[3],"Unknown");snprintf(vals[4],sizeof vals[4],"%ld",sysconf(_SC_NPROCESSORS_ONLN));snprintf(vals[5],sizeof vals[5],"%ld bytes",sysconf(_SC_PAGESIZE));snprintf(vals[6],sizeof vals[6],"%ld days, %ld hours, %ld minutes",si.uptime/86400,(si.uptime/3600)%24,(si.uptime/60)%60);snprintf(vals[7],sizeof vals[7],"%d",nproc);unsigned long long used,total;read_mem_percent(&used,&total);snprintf(vals[8],sizeof vals[8],"%.2f GiB",total/1073741824.0);snprintf(vals[9],sizeof vals[9],"%.2f GiB",(total-used)/1073741824.0);
 FILE*cpuinfo=fopen("/proc/cpuinfo","r");if(cpuinfo){char l[512];while(fgets(l,sizeof l,cpuinfo))if(!strncmp(l,"model name",10)){char*q=strchr(l,':');if(q){while(*++q==' ');snprintf(vals[10],sizeof vals[10],"%s",q);vals[10][strcspn(vals[10],"\r\n")]=0;}break;}fclose(cpuinfo);}if(!vals[10][0])strcpy(vals[10],"Not exposed");FILE*boot=fopen("/proc/sys/kernel/random/boot_id","r");if(boot){fgets(vals[11],sizeof vals[11],boot);fclose(boot);vals[11][strcspn(vals[11],"\r\n")]=0;}else strcpy(vals[11],"Unavailable");struct statvfs svfs;if(statvfs("/",&svfs)==0){unsigned long long fs_total=(unsigned long long)svfs.f_blocks*svfs.f_frsize,fs_free=(unsigned long long)svfs.f_bavail*svfs.f_frsize;snprintf(vals[12],sizeof vals[12],"/  %.2f GiB free of %.2f GiB",fs_free/1073741824.0,fs_total/1073741824.0);}else strcpy(vals[12],"Unavailable");snprintf(vals[13],sizeof vals[13],"%s",getenv("SHELL")?getenv("SHELL"):"Unknown");snprintf(vals[14],sizeof vals[14],"%u",(unsigned)getuid());snprintf(vals[15],sizeof vals[15],"%s",getenv("HOME")?getenv("HOME"):"Unknown");snprintf(vals[16],sizeof vals[16],"%s",getenv("WAYLAND_DISPLAY")?"Wayland / XWayland":getenv("DISPLAY")?"X11":"Unknown");snprintf(vals[17],sizeof vals[17],"%ld",sysconf(_SC_CHILD_MAX));int shown=0;for(int i=0;i<18;i++){char blob[600];snprintf(blob,sizeof blob,"%s %s",keys[i],vals[i]);if(!list_matches(9,blob))continue;int yy=y+30+shown*25;text(x+20,yy,keys[i],MUTED);cliptext(x+250,yy,w-280,vals[i],INK);shown++;}if(!shown)text(x+20,y+30,"No system information matches your search.",MUTED);}

static void draw_details_page(void){
 int x=NAVW+26,y=84,w=ww-NAVW-50; Proc *p=selected_proc();
 text(x,y,"Process details",INK);draw_page_subtitle(x,y+24,"Select a process on the Processes page, then inspect its live kernel properties here.");draw_list_search(5);y+=54;
 if(!p){panel(x,y,w,112);text(x+18,y+34,"No process selected",INK);text(x+18,y+62,"Go to Processes, click a row, then return to Details.",MUTED);return;}
 panel(x,y,w,300);char val[PATH_MAX+128],path[PATH_MAX],cmd[2048];
 const char *keys[]={"Process name","Process ID","Parent process ID","User","State","CPU estimate","Resident memory","Virtual memory","Threads","Executable","Command line"};
 char values[11][PATH_MAX+128];memset(values,0,sizeof values);snprintf(values[0],sizeof values[0],"%s",p->name);snprintf(values[1],sizeof values[1],"%d",p->pid);snprintf(values[2],sizeof values[2],"%d",p->ppid);snprintf(values[3],sizeof values[3],"%s",p->user);snprintf(values[4],sizeof values[4],"%s (%c)",p->state=='R'?"Running":p->state=='S'?"Sleeping":p->state=='D'?"Uninterruptible sleep":p->state=='T'?"Stopped":"Other",p->state);char cpu_display[32];format_cpu(p->cpu,cpu_display,sizeof cpu_display);snprintf(values[5],sizeof values[5],"%s (sampled estimate)",cpu_display);snprintf(values[6],sizeof values[6],"%.2f MiB",p->rss/1048576.0);snprintf(values[7],sizeof values[7],"%.2f MiB",p->vsize/1048576.0);snprintf(values[8],sizeof values[8],"%lu",p->threads);
 snprintf(path,sizeof path,"/proc/%d/exe",p->pid);ssize_t n=readlink(path,val,sizeof val-1);if(n>=0){val[n]=0;snprintf(values[9],sizeof values[9],"%s",val);}else strcpy(values[9],"Not available (permission or process exited)");
 snprintf(path,sizeof path,"/proc/%d/cmdline",p->pid);FILE*f=fopen(path,"r");size_t used=0;if(f){int ch;while((ch=fgetc(f))!=EOF&&used<sizeof cmd-2){cmd[used++]=ch=='\0'?' ':ch;}fclose(f);}while(used&&cmd[used-1]==' ')used--;cmd[used]=0;snprintf(values[10],sizeof values[10],"%s",used?cmd:"Command line unavailable");
 int shown=0;for(int i=0;i<11;i++){char blob[PATH_MAX+256];snprintf(blob,sizeof blob,"%s %s",keys[i],values[i]);if(!list_matches(5,blob))continue;int yy=y+29+shown*24;if(yy>y+278)break;text(x+18,yy,keys[i],MUTED);cliptext(x+230,yy,w-250,values[i],INK);shown++;}if(!shown)text(x+18,y+29,"No process properties match your search.",MUTED);y+=312;panel(x,y,w,110);text(x+18,y+28,"Available actions",BLUE);text(x+18,y+55,"Right-click the process in Processes for terminate, suspend/resume, priority, path and search actions.",INK);text(x+18,y+80,"Some fields can be hidden by Linux permissions or process exit races.",MUTED);
}
static void draw_core_graph(int x,int y,int w,int h,int id){
 panel(x,y,w,h);char label[32],value[16];snprintf(label,sizeof label,"CPU %d",id);int latest=0;if(core_hist_count)latest=core_hist[id][(core_hist_pos+CORE_HISTORY-1)%CORE_HISTORY];snprintf(value,sizeof value,"%d%%",latest);text(x+9,y+17,label,INK);text(x+w-40,y+17,value,BLUE);
 int gx=x+9,gy=y+24,gw=w-18,gh=h-31;if(gh<8)gh=8;for(int k=1;k<=2;k++)ln(gx,gy+gh*k/3,gx+gw,gy+gh*k/3,LINE);
 if(core_hist_count>1){int count=core_hist_count;for(int j=1;j<count;j++){int a=(core_hist_pos-count+j-1+CORE_HISTORY)%CORE_HISTORY,b=(core_hist_pos-count+j+CORE_HISTORY)%CORE_HISTORY;int x1=gx+(j-1)*gw/(count-1),x2=gx+j*gw/(count-1);int y1=gy+gh-(int)(core_hist[id][a]*gh/100.0),y2=gy+gh-(int)(core_hist[id][b]*gh/100.0);ln(x1,y1,x2,y2,BLUE);}}
}
static void draw_cpu_counters(void){
 int x=NAVW+26,y=84,w=ww-NAVW-50;char line[512],label[64];unsigned long long v[10];
 int hy=y+72;int col[5]={x+12,x+(w*23)/100,x+(w*42)/100,x+(w*61)/100,x+(w*80)/100};fill(x,hy,w,32,0xE8EDF5);text(col[0],hy+21,"Processor",MUTED);text(col[1],hy+21,"User ticks",MUTED);text(col[2],hy+21,"System ticks",MUTED);text(col[3],hy+21,"Idle ticks",MUTED);text(col[4],hy+21,"Total ticks",MUTED);
 int rowbase=hy+54,visible=(hh-44-rowbase)/29;if(visible<1)visible=1;int totalrows=0;FILE*f=fopen("/proc/stat","r");if(f){while(fgets(line,sizeof line,f)){if(strncmp(line,"cpu",3)||line[3]<'0'||line[3]>'9')continue;if(sscanf(line,"%63s",label)==1&&list_matches(10,label))totalrows++;}fclose(f);}int maxscroll=totalrows>visible?totalrows-visible:0;if(cpu_scroll>maxscroll)cpu_scroll=maxscroll;if(cpu_scroll<0)cpu_scroll=0;
 f=fopen("/proc/stat","r");int row=0,drawn=0;while(f&&fgets(line,sizeof line,f)){if(strncmp(line,"cpu",3)||line[3]<'0'||line[3]>'9')continue;int n=sscanf(line,"%63s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",label,&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9]);if(n<5||!list_matches(10,label))continue;if(row++<cpu_scroll)continue;if(drawn>=visible)break;int yy=rowbase+drawn*29;if(drawn%2==0)fill(x,yy-21,w,27,WHITE);unsigned long long total=0;for(int i=0;i<n-1;i++)total+=v[i];char b[80];text(col[0],yy,label,INK);snprintf(b,sizeof b,"%llu",v[0]);text(col[1],yy,b,INK);snprintf(b,sizeof b,"%llu",v[2]);text(col[2],yy,b,INK);snprintf(b,sizeof b,"%llu",v[3]);text(col[3],yy,b,INK);snprintf(b,sizeof b,"%llu",total);text(col[4],yy,b,INK);drawn++;}if(f)fclose(f);if(!drawn)text(x+12,rowbase,list_queries[10][0]?"No CPU cores match your search.":"Per-core counters are unavailable.",MUTED);
}
static int cpu_details_mode=0;
static void cpu_mode_button(int x,int y,int w,const char*label,int active){fill(x,y,w,26,active?PALE:WHITE);stroke(x,y,w,26,active?BLUE:LINE);center(x,y+18,w,label,active?BLUE:INK);}
static void draw_cpu_details(void){
 int x=NAVW+26,y=84,w=ww-NAVW-50;text(x,y,"CPU details",INK);draw_page_subtitle(x,y+24,"Compact per-core utilization graphs, with cumulative tick counters available.");draw_list_search(10);
 cpu_mode_button(x,y+38,126,"Per-core graphs",cpu_details_mode==0);cpu_mode_button(x+134,y+38,126,"Tick counters",cpu_details_mode==1);
 if(cpu_details_mode){draw_cpu_counters();return;}
 char label[32];int ids[MAX_CPU_CORES],count=0;for(int i=0;i<core_count&&i<MAX_CPU_CORES;i++){snprintf(label,sizeof label,"CPU %d",i);if(list_matches(10,label))ids[count++]=i;}
 int gap=9,cols=w>=760?4:(w>=500?3:2),cardw=(w-gap*(cols-1))/cols,cardh=58,rowgap=8,top=y+72;int totalrows=(count+cols-1)/cols,visible=(hh-44-top+rowgap)/(cardh+rowgap);if(visible<1)visible=1;int maxscroll=totalrows>visible?totalrows-visible:0;if(cpu_scroll>maxscroll)cpu_scroll=maxscroll;if(cpu_scroll<0)cpu_scroll=0;
 for(int row=cpu_scroll;row<totalrows&&row<cpu_scroll+visible;row++)for(int col=0;col<cols;col++){int k=row*cols+col;if(k>=count)break;int cw=col==cols-1?w-(cols-1)*(cardw+gap):cardw;draw_core_graph(x+col*(cardw+gap),top+(row-cpu_scroll)*(cardh+rowgap),cw,cardh,ids[k]);}
 if(!count)text(x+12,top+17,list_queries[10][0]?"No CPU cores match your search.":"Per-core counters are unavailable.",MUTED);
 if(totalrows>visible)text(x,hh-32,"Scroll to view additional CPU cores.",MUTED);
}

typedef struct {const char *key,*label;int group;} MemMetric;
static const MemMetric memory_metrics[]={
 {"MemTotal","Total physical memory",0},{"MemAvailable","Available memory",0},{"MemFree","Free memory",0},{"Active","Active pages",0},{"Inactive","Inactive pages",0},{"Active(anon)","Active anonymous memory",0},{"Inactive(anon)","Inactive anonymous memory",0},{"Active(file)","Active file cache",0},{"Inactive(file)","Inactive file cache",0},{"AnonPages","Anonymous pages",0},{"Mapped","Mapped memory",0},{"Cached","File cache",0},{"Buffers","Block-device buffers",0},{"Shmem","Shared memory",0},{"Dirty","Dirty pages",0},{"Writeback","Pages being written",0},{"SwapCached","Cached swap pages",0},{"SwapTotal","Total swap",0},{"SwapFree","Free swap",0},{"Unevictable","Unevictable pages",0},{"Mlocked","Locked memory",0},
 {"CommitLimit","Commit limit",1},{"Committed_AS","Committed virtual memory",1},{"KernelStack","Kernel stacks",1},{"PageTables","Page tables",1},{"SecPageTables","Secondary page tables",1},{"Slab","Kernel slab memory",1},{"SReclaimable","Reclaimable slab",1},{"SUnreclaim","Unreclaimable slab",1},{"VmallocTotal","Virtual address space",1},{"VmallocUsed","Used virtual address space",1},{"VmallocChunk","Largest virtual chunk",1},{"Percpu","Per-CPU allocations",1},{"AnonHugePages","Anonymous huge pages",1},{"ShmemHugePages","Shared huge pages",1},{"ShmemPmdMapped","Shared PMD mapped",1},{"FileHugePages","File-backed huge pages",1},{"CmaTotal","Contiguous memory total",1},{"CmaFree","Contiguous memory free",1},{"HugePages_Total","Huge pages total",1},{"HugePages_Free","Huge pages free",1},{"HugePages_Rsvd","Huge pages reserved",1},{"HugePages_Surp","Surplus huge pages",1},{"Hugepagesize","Huge page size",1},{"DirectMap4k","Direct map (4 KiB)",1},{"DirectMap2M","Direct map (2 MiB)",1},{"DirectMap1G","Direct map (1 GiB)",1}
};
#define MEM_METRIC_COUNT ((int)(sizeof(memory_metrics)/sizeof(memory_metrics[0])))
static void format_mem_metric(unsigned long long value,int bytes,char*out,size_t cap){if(!bytes){snprintf(out,cap,"%llu",value);return;}double b=(double)value;if(b>=1073741824.0)snprintf(out,cap,"%.1f GiB",b/1073741824.0);else if(b>=1048576.0)snprintf(out,cap,"%.1f MiB",b/1048576.0);else if(b>=1024.0)snprintf(out,cap,"%.1f KiB",b/1024.0);else snprintf(out,cap,"<1 KiB");}
static void draw_memory_card(int x,int y,int w,int h,const char*label,const char*value,double ratio,unsigned long accent){panel(x,y,w,h);fill(x+1,y+1,3,h-2,accent);text(x+13,y+20,label,MUTED);cliptext(x+13,y+43,w-26,value,INK);fill(x+13,y+h-10,w-26,4,LINE);if(ratio<0)ratio=0;if(ratio>1)ratio=1;int bw=(int)((w-26)*ratio);if(bw<0)bw=0;fill(x+13,y+h-10,bw,4,accent);}
static void draw_memory_details(void){
 int x=NAVW+26,y=84,w=ww-NAVW-50;char line[512],key[96],unit[16];unsigned long long raw;unsigned long long values[MEM_METRIC_COUNT]={0};unsigned char present[MEM_METRIC_COUNT]={0},isbytes[MEM_METRIC_COUNT]={0};
 text(x,y,"Memory details",INK);draw_page_subtitle(x,y+24,"Live RAM, swap and kernel/virtual-memory counters from /proc/meminfo.");draw_list_search(11);
 FILE*f=fopen("/proc/meminfo","r");while(f&&fgets(line,sizeof line,f)){unit[0]=0;if(sscanf(line,"%95[^:]: %llu %15s",key,&raw,unit)<2)continue;for(int i=0;i<MEM_METRIC_COUNT;i++)if(!strcmp(key,memory_metrics[i].key)){values[i]=raw;if(!strcmp(unit,"kB")){values[i]*=1024ULL;isbytes[i]=1;}else if(strcmp(memory_metrics[i].key,"HugePages_Total")==0||strcmp(memory_metrics[i].key,"HugePages_Free")==0||strcmp(memory_metrics[i].key,"HugePages_Rsvd")==0||strcmp(memory_metrics[i].key,"HugePages_Surp")==0){isbytes[i]=0;}else isbytes[i]=1;present[i]=1;break;}}if(f)fclose(f);
 unsigned long long total=0,avail=0,swaptotal=0,swapfree=0;for(int i=0;i<MEM_METRIC_COUNT;i++)if(present[i]){if(!strcmp(memory_metrics[i].key,"MemTotal"))total=values[i];else if(!strcmp(memory_metrics[i].key,"MemAvailable"))avail=values[i];else if(!strcmp(memory_metrics[i].key,"SwapTotal"))swaptotal=values[i];else if(!strcmp(memory_metrics[i].key,"SwapFree"))swapfree=values[i];}
 unsigned long long used=total>avail?total-avail:0,swapused=swaptotal>swapfree?swaptotal-swapfree:0;char v0[64],v1[64],v2[64],v3[64];format_process_memory(total,v0,sizeof v0);format_process_memory(used,v1,sizeof v1);format_process_memory(avail,v2,sizeof v2);format_process_memory(swapused,v3,sizeof v3);
 int gap=10,cardw=(w-gap*3)/4,lastcardw=w-3*(cardw+gap),cardy=y+54,cardh=66;
 draw_memory_card(x,cardy,cardw,cardh,"Installed RAM",v0,1.0,BLUE);
 char b[96];snprintf(b,sizeof b,"%s (%.1f%%)",v1,total?100.0*(double)used/total:0.0);
 draw_memory_card(x+cardw+gap,cardy,cardw,cardh,"In use",b,total?(double)used/total:0,AMBER);
 draw_memory_card(x+2*(cardw+gap),cardy,cardw,cardh,"Available",v2,total?(double)avail/total:0,GREEN);
 snprintf(b,sizeof b,"%s / ",v3);if(swaptotal){char t[64];format_process_memory(swaptotal,t,sizeof t);snprintf(b,sizeof b,"%s / %s",v3,t);}else snprintf(b,sizeof b,"Not configured");
 draw_memory_card(x+3*(cardw+gap),cardy,lastcardw,cardh,"Swap used",b,swaptotal?(double)swapused/swaptotal:0,RED);
 int listy=cardy+cardh+12,panely=listy,panelh=hh-panely-44;if(panelh<130)panelh=130;int colgap=14,colw=(w-colgap)/2,colw_right=w-colgap-colw;for(int g=0;g<2;g++){int cw=g?colw_right:colw;int px=x+g*(colw+colgap);panel(px,panely,cw,panelh);cliptext(px+12,panely+24,cw-24,g?"Kernel / virtual memory":"Physical memory and swap",BLUE);ln(px+10,panely+34,px+cw-10,panely+34,LINE);}
 int count[2]={0,0};for(int i=0;i<MEM_METRIC_COUNT;i++){int g=memory_metrics[i].group;if(!present[i])continue;char value[64],blob[256];format_mem_metric(values[i],isbytes[i],value,sizeof value);snprintf(blob,sizeof blob,"%s %s %s",memory_metrics[i].label,memory_metrics[i].key,value);if(list_matches(11,blob))count[g]++;}
 int rowh=23,visible=(panelh-52)/rowh;if(visible<1)visible=1;int maxscroll=0;for(int g=0;g<2;g++)if(count[g]>visible&&count[g]-visible>maxscroll)maxscroll=count[g]-visible;if(memory_scroll>maxscroll)memory_scroll=maxscroll;if(memory_scroll<0)memory_scroll=0;
 int at[2]={0,0},drawn[2]={0,0};for(int i=0;i<MEM_METRIC_COUNT;i++){int g=memory_metrics[i].group;if(!present[i])continue;char value[64],blob[256];format_mem_metric(values[i],isbytes[i],value,sizeof value);snprintf(blob,sizeof blob,"%s %s %s",memory_metrics[i].label,memory_metrics[i].key,value);if(!list_matches(11,blob))continue;int idx=at[g]++;if(idx<memory_scroll||idx>=memory_scroll+visible)continue;int cw=g?colw_right:colw,px=x+g*(colw+colgap),yy=panely+57+(idx-memory_scroll)*rowh;if(drawn[g]++%2==1)fill(px+7,yy-17,cw-14,rowh,0xF0F3F8);cliptext(px+12,yy,cw-135,memory_metrics[i].label,MUTED);int valuew=108;righttext(px+cw-119,yy,valuew,value,INK);}
 for(int g=0;g<2;g++){if(count[g]==0){int cw=g?colw_right:colw,px=x+g*(colw+colgap);const char*msg=list_queries[11][0]?(g?"No kernel / virtual memory metrics match your search.":"No physical memory and swap metrics match your search."):(g?"No kernel / virtual memory counters available.":"No physical memory and swap counters available.");cliptext(px+14,panely+57,cw-28,msg,MUTED);}}
 if(count[0]>visible||count[1]>visible)text(x,hh-28,"Scroll to browse additional memory metrics.",MUTED);
}
static void menu_add(const char *label,int action){if(context_count>=12)return;snprintf(context_items[context_count].label,sizeof context_items[context_count].label,"%s",label);context_items[context_count].action=action;context_count++;}
static void build_context_menu(void){
 context_count=0;
 if(service_menu){
  int idx=service_find_unit(selected_service_unit);
  if(idx<0){menu_add("Refresh service list",MA_SERVICE_REFRESH);menu_add("Show service status",MA_SERVICE_STATUS);return;}
  Service *sv=&services[idx];
  if(!strcmp(sv->active,"active")){menu_add("Stop service",MA_SERVICE_STOP);menu_add("Restart service",MA_SERVICE_RESTART);menu_add("Show service status",MA_SERVICE_STATUS);}
  else if(!strcmp(sv->active,"activating")){menu_add("Stop starting service",MA_SERVICE_STOP);menu_add("Show service status",MA_SERVICE_STATUS);}
  else if(!strcmp(sv->active,"deactivating")){menu_add("Start service after stop",MA_SERVICE_START);menu_add("Show service status",MA_SERVICE_STATUS);}
  else{menu_add("Start service",MA_SERVICE_START);menu_add("Show service status",MA_SERVICE_STATUS);}
  return;
 }
 if(startup_menu){static char toggle[48];if(selected_startup>=0&&selected_startup<nstartups){snprintf(toggle,sizeof toggle,"%s startup",startups[selected_startup].disabled?"Enable":"Disable");menu_add("Open containing folder",MA_STARTUP_OPEN);menu_add(toggle,MA_STARTUP_TOGGLE);menu_add("Show file path",MA_STARTUP_PATH);}return;}
 Proc *cp=selected_proc();if(!cp){menu_add("No process selected",MA_NONE);return;}
 if(cp->state!='Z'){menu_add("End task",MA_PROC_TERM);menu_add("Force stop",MA_PROC_KILL);menu_add(cp->state=='T'?"Resume process":"Suspend process",MA_PROC_TOGGLE);menu_add("Lower priority",MA_PROC_NICE_LOW);menu_add("Normal priority",MA_PROC_NICE_NORMAL);}
 menu_add("Open file location",MA_PROC_OPEN_DIR);menu_add("Show executable path",MA_PROC_SHOW_PATH);menu_add("Search online",MA_PROC_SEARCH);menu_add("Copy PID",MA_PROC_COPY_PID);menu_add(cp->state=='Z'?"Properties (exited process)":"Properties",MA_PROC_PROPERTIES);
}
static int menu_height(void){return context_count*31+12;}
static void draw_menu(void){
 if(!menu)return;
 build_context_menu();
 if(!context_count)return;
 int mw=276,mh=menu_height();
 if(menu_x+mw>ww)menu_x=ww-mw-8;
 if(menu_x<4)menu_x=4;
 if(menu_y+mh>hh-30)menu_y=hh-mh-32;
 if(menu_y<64)menu_y=64;
 fill(menu_x+3,menu_y+3,mw,mh,0xD8DDE6);fill(menu_x,menu_y,mw,mh,WHITE);stroke(menu_x,menu_y,mw,mh,LINE);
 for(int i=0;i<context_count;i++){int a=context_items[i].action;unsigned long color=(a==MA_PROC_TERM||a==MA_PROC_KILL||a==MA_SERVICE_STOP||a==MA_STARTUP_TOGGLE)?RED:INK;text(menu_x+14,menu_y+23+i*31,context_items[i].label,color);if(i<context_count-1)ln(menu_x+8,menu_y+31+i*31,menu_x+mw-8,menu_y+31+i*31,0xF0F2F5);}
}
static void dim_workspace_for_dialog(void){
 TaskXRenderPictFormat *fmt=XRenderFindVisualFormat(D,DefaultVisual(D,S));if(!fmt)return;
 TaskPicture dst=XRenderCreatePicture(D,W,fmt,0,NULL);if(!dst)return;
 TaskXRenderColor shade={0,0,0,(unsigned short)(dark_mode?0x2200:0x5200)};
 TaskPicture src=XRenderCreateSolidFill(D,&shade);
 if(src){XRenderComposite(D,3,src,0,dst,0,0,0,0,0,0,(unsigned)ww,(unsigned)hh);XRenderFreePicture(D,src);}
 XRenderFreePicture(D,dst);
}
static void draw_run_dialog(void){
 if(!run_dialog)return;
 int x=run_dialog_x(),y=run_dialog_y(),w=run_dialog_w(),h=run_dialog_h(),rows=run_dialog_rows();
 int list_top=y+158,list_h=rows*RUN_ROW_H,list_x=x+24,list_w=w-48;
 
 dim_workspace_for_dialog();fill(x-5,y-5,w+10,h+10,0xBFC7D4);fill(x,y,w,h,0xF8FAFD);stroke(x,y,w,h,0xAEB8C8);
 fill(x,y,w,66,0x263445);text(x+24,y+30,"Run new task",WHITE);text(x+24,y+51,"Find and launch an application or command",0xD7E0EC);
 text(x+24,y+88,"Click here to type, or start typing anywhere",INK);fill(x+24,y+98,w-48,34,WHITE);stroke(x+24,y+98,w-48,34,run_focus?BLUE:LINE);cliptext(x+36,y+120,w-72,run_command[0]?run_command:"Enter an app name or command…",run_command[0]?INK:MUTED);
 run_update_filtered();text(list_x,list_top-10,"SUGGESTED COMMANDS",MUTED);char countbuf[96];snprintf(countbuf,sizeof countbuf,"%d match%s",run_filtered_count,run_filtered_count==1?"":"es");
 righttext(list_x,list_top-10,list_w,countbuf,MUTED);
 fill(list_x,list_top,list_w,list_h,WHITE);stroke(list_x,list_top,list_w,list_h,LINE);
 for(int row=0;row<rows;row++){int sy=list_top+row*RUN_ROW_H;int idx=row<visible_run_count?visible_run_suggestions[row]:-1;if(idx>=0&&idx==run_suggestion)fill(list_x+1,sy+1,list_w-2,RUN_ROW_H-1,0xDCE8FF);else if(row%2==1)fill(list_x+1,sy+1,list_w-2,RUN_ROW_H-1,0xF5F7FA);if(idx>=0){fill(list_x+12,sy+7,9,9,idx==run_suggestion?BLUE:0xAAB4C4);cliptext(list_x+32,sy+17,list_w-60,run_suggestions[idx],idx==run_suggestion?BLUE:INK);}if(row<rows-1)ln(list_x+1,sy+RUN_ROW_H,list_x+list_w-2,sy+RUN_ROW_H,0xE7EBF1);}

 if(run_filtered_count>rows){int track_h=list_h-8,thumb_h=track_h*rows/run_filtered_count;if(thumb_h<18)thumb_h=18;int maxscroll=run_filtered_count-rows;int thumb_y=list_top+4+(maxscroll?((track_h-thumb_h)*run_scroll/maxscroll):0);fill(list_x+list_w-8,list_top+4,3,track_h,0xE1E6EE);fill(list_x+list_w-8,thumb_y,3,thumb_h,BLUE);}
 if(!run_filtered_count)text(list_x+12,list_top+24,run_command[0]?"No matching command found in PATH.":"No suggestions available; you can type any command.",MUTED);
 int foot=y+h-74;char hint[160];if(run_filtered_count){int a=run_scroll+1,b=run_scroll+visible_run_count;snprintf(hint,sizeof hint,"Showing %d-%d / %d",a,b,run_filtered_count);}else snprintf(hint,sizeof hint,"Type a command, or click Cancel to return.");cliptext(x+24,foot,w-250,hint,MUTED);
 int by=y+h-47;fill(x+w-210,by,88,32,BLUE);center(x+w-210,by+21,88,"Run",WHITE);fill(x+w-112,by,88,32,0xE8EDF4);center(x+w-112,by+21,88,"Cancel",INK);
}
static void draw(void){draw_nav();if(page==0)draw_processes(0);else if(page==1)draw_performance();else if(page==2)draw_history();else if(page==3)draw_startup();else if(page==4)draw_users();else if(page==5)draw_details_page();else if(page==6)draw_services();else if(page==7)draw_disk_network();else if(page==8)draw_gpu();else if(page==9)draw_system_info();else if(page==10)draw_cpu_details();else if(page==11)draw_memory_details();draw_menu();draw_action_banner();draw_run_dialog();XFlush(D);}
static int row_view_pick(int y){int start=84+90+53-20;if(y<start||y>hh-44)return -1;int row=(y-start+scroll*30)/30;if(row<0)return -1;build_process_view();return row<proc_view_n?row:-1;}
static int row_pick(int y){int row=row_view_pick(y);return row>=0?proc_view[row]:-1;}
static void do_menu(int y){
 int i=(y-menu_y-10)/31;menu=0;if(i<0||i>=context_count)return;int action=context_items[i].action;
 if(action==MA_NONE){setstatus("This item has no available action in its current state.");return;}
 switch(action){
  case MA_SERVICE_START:service_action("start");return;
  case MA_SERVICE_STOP:service_action("stop");return;
  case MA_SERVICE_RESTART:service_action("restart");return;
  case MA_SERVICE_REFRESH:services_loaded=0;load_services();setstatus("Completed: service list refreshed from systemd.");return;
  case MA_SERVICE_STATUS:{services_loaded=0;load_services();int idx=service_find_unit(selected_service_unit);if(idx>=0){char b[256];snprintf(b,sizeof b,"%.145s: %s / %s (%s)",services[idx].unit,services[idx].active,services[idx].sub,services[idx].load);setstatus(b);}else setstatus("Service is no longer present in the loaded systemd unit list.");return;}
  case MA_STARTUP_OPEN:startup_action(0);return;
  case MA_STARTUP_TOGGLE:startup_action(1);return;
  case MA_STARTUP_PATH:startup_action(2);return;
 }
 Proc*p=selected_proc();if(!p){setstatus("Select a process first.");return;}
 if(action==MA_PROC_TERM)signal_selected(SIGTERM);
 else if(action==MA_PROC_KILL)signal_selected(SIGKILL);
 else if(action==MA_PROC_TOGGLE)signal_selected(p->state=='T'?SIGCONT:SIGSTOP);
 else if(action==MA_PROC_NICE_LOW||action==MA_PROC_NICE_NORMAL){int niceval=action==MA_PROC_NICE_LOW?10:0;char pending[128];snprintf(pending,sizeof pending,"Changing priority of PID %d",p->pid);begin_action(pending);if(setpriority(PRIO_PROCESS,(id_t)p->pid,niceval)==0){char msg[160];snprintf(msg,sizeof msg,"Set priority for PID %d to %s.",p->pid,action==MA_PROC_NICE_LOW?"low":"normal");setstatus(msg);}else{char msg[160];snprintf(msg,sizeof msg,"Could not change priority for PID %d: %s",p->pid,strerror(errno));setstatus(msg);}}
 else if(action==MA_PROC_OPEN_DIR)open_exe_dir();
 else if(action==MA_PROC_SHOW_PATH)show_exe();
 else if(action==MA_PROC_SEARCH)search_online();
 else if(action==MA_PROC_COPY_PID){begin_action("copying process PID to clipboard");char b[64];snprintf(b,sizeof b,"printf '%d' | xclip -selection clipboard 2>/dev/null",p->pid);if(system(b)==0)setstatus("PID copied to clipboard.");else setstatus("Clipboard copy needs xclip or xsel.");}
 else if(action==MA_PROC_PROPERTIES){begin_action("reading selected process properties");char b[256],cpu_text[32];format_cpu(p->cpu,cpu_text,sizeof cpu_text);snprintf(b,sizeof b,"PID %d | %.75s | user %.40s | state %c | CPU %s | memory %.1f MB",p->pid,p->name,p->user,p->state,cpu_text,p->rss/1048576.0);setstatus(b);}
}
static void add_history(void){unsigned long long u,t;double m=read_mem_percent(&u,&t);cpu_hist[histpos]=read_cpu_percent();mem_hist[histpos]=m;sample_core_history();if(page==8){update_gpu();for(int gi=0;gi<ngpus;gi++)gpu_hist[gi][gpuhistpos]=gpus[gi].have_util?gpus[gi].util:0;gpuhistpos=(gpuhistpos+1)%120;if(gpuhistcount<120)gpuhistcount++;}unsigned long long dr=0,dw=0,nrx=0,ntx=0;FILE*f=fopen("/proc/diskstats","r");if(f){char line[512],name[64];unsigned major,minor;unsigned long long rd,wr;while(fgets(line,sizeof line,f)){if(sscanf(line,"%u %u %63s %*u %*u %llu %*u %*u %*u %*u %llu",&major,&minor,name,&rd,&wr)==5){char path[PATH_MAX];snprintf(path,sizeof path,"/sys/block/%s",name);if(access(path,F_OK)==0){dr+=rd*512ULL;dw+=wr*512ULL;}}}fclose(f);}f=fopen("/proc/net/dev","r");if(f){char line[512],name[64];int skip=0;unsigned long long rx,tx;while(fgets(line,sizeof line,f)){if(skip++<2)continue;if(sscanf(line," %63[^:]: %llu %*u %*u %*u %*u %*u %*u %*u %llu",name,&rx,&tx)==3){nrx+=rx;ntx+=tx;}}fclose(f);}if(io_baseline){disk_read_rate=dr>=prev_disk_read?(double)(dr-prev_disk_read):0;disk_write_rate=dw>=prev_disk_write?(double)(dw-prev_disk_write):0;net_rx_rate=nrx>=prev_net_rx?(double)(nrx-prev_net_rx):0;net_tx_rate=ntx>=prev_net_tx?(double)(ntx-prev_net_tx):0;}prev_disk_read=dr;prev_disk_write=dw;prev_net_rx=nrx;prev_net_tx=ntx;io_baseline=1;histpos=(histpos+1)%120;if(histcount<120)histcount++;}
int main(void){setlocale(LC_CTYPE, "");load_theme();D=XOpenDisplay(NULL);if(!D){fprintf(stderr,"Task Manager requires an X11 display or XWayland.\n");return 1;}S=DefaultScreen(D);W=XCreateSimpleWindow(D,RootWindow(D,S),80,60,SW,SH,1,0xB9C2D0,BG);XStoreName(D,W,"Task Manager");
 XClassHint class_hint; class_hint.res_name=(char*)"taskmgr"; class_hint.res_class=(char*)"TaskManager"; XSetClassHint(D,W,&class_hint);
 XSizeHints size_hints; memset(&size_hints,0,sizeof(size_hints)); size_hints.flags=PMinSize; size_hints.min_width=1000; size_hints.min_height=620; XSetWMNormalHints(D,W,&size_hints);
 Atom net_pid=XInternAtom(D,"_NET_WM_PID",False); unsigned long mypid=(unsigned long)getpid(); XChangeProperty(D,W,net_pid,XA_CARDINAL,32,PropModeReplace,(unsigned char*)&mypid,1);
 XSelectInput(D,W,ExposureMask|KeyPressMask|ButtonPressMask|StructureNotifyMask);G=XCreateGC(D,W,0,NULL);char **missing=NULL;int missing_count=0;char *defstr=NULL;FS=XCreateFontSet(D,"-*-sans-serif-medium-r-normal--14-*-*-*-*-*-*-*",&missing,&missing_count,&defstr);if(missing)XFreeStringList(missing);F=XLoadQueryFont(D,"fixed");if(F)XSetFont(D,G,F->fid);XD=XftDrawCreate(D,W,DefaultVisual(D,S),DefaultColormap(D,S));XF=XftFontOpenName(D,S,"Segoe UI Variable:size=10.5");if(!XF)XF=XftFontOpenName(D,S,"Segoe UI:size=10.5");if(!XF)XF=XftFontOpenName(D,S,"Selawik:size=10.5");if(!XF)XF=XftFontOpenName(D,S,"Noto Sans:size=10.5");if(!XF)XF=XftFontOpenName(D,S,"sans:size=10.5");XFUser=XftFontOpenName(D,S,"Segoe UI Variable:size=12");if(!XFUser)XFUser=XftFontOpenName(D,S,"Segoe UI:size=12");if(!XFUser)XFUser=XftFontOpenName(D,S,"Selawik:size=12");if(!XFUser)XFUser=XftFontOpenName(D,S,"Noto Sans:size=12");if(!XFUser)XFUser=XftFontOpenName(D,S,"sans:size=12");WM_DELETE=XInternAtom(D,"WM_DELETE_WINDOW",False);XSetWMProtocols(D,W,&WM_DELETE,1);XMapWindow(D,W);
 Atom net_state=XInternAtom(D,"_NET_WM_STATE",False), max_v=XInternAtom(D,"_NET_WM_STATE_MAXIMIZED_VERT",False), max_h=XInternAtom(D,"_NET_WM_STATE_MAXIMIZED_HORZ",False);
 XEvent maxev; memset(&maxev,0,sizeof(maxev)); maxev.xclient.type=ClientMessage; maxev.xclient.window=W; maxev.xclient.message_type=net_state; maxev.xclient.format=32; maxev.xclient.data.l[0]=1; maxev.xclient.data.l[1]=max_v; maxev.xclient.data.l[2]=max_h; maxev.xclient.data.l[3]=1; XSendEvent(D,RootWindow(D,S),False,SubstructureRedirectMask|SubstructureNotifyMask,&maxev); XFlush(D);
 refresh();add_history();time_t last=time(NULL);for(;;){while(XPending(D)){XEvent e;XNextEvent(D,&e);if(e.type==ClientMessage&&(Atom)e.xclient.data.l[0]==WM_DELETE)goto done;if(e.type==Expose)draw();else if(e.type==ConfigureNotify){ww=e.xconfigure.width;hh=e.xconfigure.height;draw();}else if(e.type==ButtonPress){int x=e.xbutton.x,y=e.xbutton.y;if(run_dialog){int dx=run_dialog_x(),dy=run_dialog_y(),dw=run_dialog_w(),dh=run_dialog_h(),rows=run_dialog_rows(),listtop=dy+158,listbottom=listtop+rows*RUN_ROW_H;if((e.xbutton.button==Button4||e.xbutton.button==Button5)&&x>=dx+24&&x<=dx+dw-24&&y>=listtop&&y<listbottom){run_update_filtered();int maxscroll=run_filtered_count-rows;if(maxscroll<0)maxscroll=0;run_scroll+=(e.xbutton.button==Button4?-1:1);if(run_scroll<0)run_scroll=0;
 if(run_scroll>maxscroll){run_scroll=maxscroll;}if(run_filtered_count>0)run_suggestion=run_filtered[run_scroll];draw();continue;}if(y>=dy+dh-47&&y<=dy+dh-15&&x>=dx+dw-210&&x<=dx+dw-122)launch_command();else if(y>=dy+dh-47&&y<=dy+dh-15&&x>=dx+dw-112&&x<=dx+dw-24){run_dialog=0;run_focus=0;setstatus("Run task cancelled.");}else if(y>=dy+98&&y<=dy+132&&x>=dx+24&&x<=dx+dw-24){run_focus=1;XSetInputFocus(D,W,RevertToParent,CurrentTime);}else if(y>=listtop&&y<listbottom&&x>=dx+24&&x<=dx+dw-24){run_update_filtered();int wanted=(y-listtop)/RUN_ROW_H;if(wanted>=0&&wanted<visible_run_count){run_suggestion=visible_run_suggestions[wanted];snprintf(run_command,sizeof run_command,"%s",run_suggestions[run_suggestion]);run_scroll=0;run_focus=1;XSetInputFocus(D,W,RevertToParent,CurrentTime);}}draw();continue;}if(menu){if(e.xbutton.button==Button1&&x>=menu_x&&x<=menu_x+276&&y>=menu_y&&y<=menu_y+menu_height()){do_menu(y);draw();continue;}menu=0;service_menu=0;startup_menu=0;draw();}if(e.xbutton.button==Button4||e.xbutton.button==Button5){int delta=e.xbutton.button==Button4?-1:1;if(y>165&&y<hh-40){if(page==2){history_scroll+=delta;if(history_scroll<0)history_scroll=0;}else if(page==8){gpu_scroll+=delta;if(gpu_scroll<0)gpu_scroll=0;}else if(page==10){cpu_scroll+=delta;if(cpu_scroll<0)cpu_scroll=0;}else if(page==11){memory_scroll+=delta;if(memory_scroll<0)memory_scroll=0;}else if(page==7){if(y<disk_panel_bottom){disk_scroll+=delta;if(disk_scroll<0)disk_scroll=0;}else{network_scroll+=delta;if(network_scroll<0)network_scroll=0;}}else if(page==0||page==5||page==3||page==6||page==4){scroll+=delta;if(scroll<0)scroll=0;}}draw();continue;}if(e.xbutton.button==Button3&&page==0){int p=row_pick(y);if(p>=0){selected=p;service_menu=0;menu=1;menu_x=x;menu_y=y;draw();}}else if(e.xbutton.button==Button3&&page==6&&x>=NAVW+26&&x<ww-16){services_loaded=0;load_services();int sy=84+54+52-19;int pos=(y-sy)/28+scroll;int idx=service_filtered_index(pos);if(y>=sy&&y<hh-44&&idx>=0&&idx<nservices){selected_service=idx;snprintf(selected_service_unit,sizeof selected_service_unit,"%s",services[idx].unit);service_menu=1;startup_menu=0;menu=1;menu_x=x;menu_y=y;draw();}}else if(e.xbutton.button==Button3&&page==3&&x>=NAVW+26&&x<ww-16){load_startups();int sy=84+54+52-19;int pos=(y-sy)/28+scroll;int idx=startup_filtered_index(pos);if(y>=sy&&y<hh-44&&idx>=0&&idx<nstartups){selected_startup=idx;startup_menu=1;service_menu=0;menu=1;menu_x=x;menu_y=y;draw();}}else if(e.xbutton.button==Button1){
 if(page_has_search(page)&&x>=list_search_x()&&x<=ww-24&&y>=70&&y<=100){list_search_focus_page=page;if(x>=ww-24-30&&list_queries[page][0]){list_queries[page][0]=0;reset_search_scroll(page);selected_startup=-1;selected_service=-1;selected_appstat=-1;selected_appstat_name[0]=0;}else XSetInputFocus(D,W,RevertToParent,CurrentTime);setstatus(list_queries[page][0]?"Searching this list…":"Search cleared.");draw();continue;}
 if(page_has_search(page))list_search_focus_page=-1; if(page==0)search_focus=0;
 if(list_search_focus_page>=0&&list_search_focus_page!=page)list_search_focus_page=-1;else if(list_search_focus_page==page)list_search_focus_page=-1;
 if(page==0&&!(y>=122&&y<=153&&x>=NAVW+26&&x<=NAVW+366)){search_focus=0;}
 if(x< NAVW && y>=hh-60 && y<=hh-30){dark_mode=!dark_mode;save_theme();setstatus(dark_mode?"Completed: dark theme enabled.":"Completed: light theme enabled.");draw();continue;}if(page==2&&x>=NAVW+26&&x<ww-16){int hy=84+54+52;int visible=(hh-62-hy)/29;if(visible<1)visible=1;int rowtop=hy-19;int pos=history_scroll+(y-rowtop)/29;int idx=history_filtered_index(pos);if(y>=rowtop&&y<rowtop+visible*29&&idx>=0&&idx<nappstats){selected_appstat=idx;snprintf(selected_appstat_name,sizeof selected_appstat_name,"%s",appstats[idx].name);char msg[256];snprintf(msg,sizeof msg,"Selected application history: %.210s",selected_appstat_name);setstatus(msg);}}else if(page==6&&x>=NAVW+26&&x<ww-16){int sy=84+54+52-19;int pos=(y-sy)/28+scroll;int idx=service_filtered_index(pos);if(y>=sy&&y<hh-44&&idx>=0&&idx<nservices){selected_service=idx;snprintf(selected_service_unit,sizeof selected_service_unit,"%s",services[idx].unit);setstatus(services[idx].unit);}}else if(page==3&&x>=NAVW+26&&x<ww-16){load_startups();int sy=84+54+52-19;int pos=(y-sy)/28+scroll;int idx=startup_filtered_index(pos);if(y>=sy&&y<hh-44&&idx>=0&&idx<nstartups){selected_startup=idx;setstatus(startups[idx].name);}}else if(page==0&&y>205&&x>NAVW){int vr=row_view_pick(y);if(vr>=0){int p=proc_view[vr],pp[9];process_column_positions(NAVW+26,ww-24,pp);int d=proc_view_depth[vr];if(d>8)d=8;int ax=pp[0]+d*14+4;if(proc_view_has_children[vr]&&x>=ax-7&&x<=ax+8){proc_toggle_collapsed(proc[p].pid);setstatus(proc_pid_collapsed(proc[p].pid)?"Process collapsed.":"Process expanded.");}else{selected=p;setstatus("Process selected. Right-click for actions.");}}}else if(page==10&&y>=122&&y<=149&&x>=NAVW+26&&x<NAVW+26+260){cpu_details_mode=(x<NAVW+26+128)?0:1;cpu_scroll=0;setstatus(cpu_details_mode?"CPU tick counter view selected.":"Per-core CPU graphs selected.");}
 else if((page==10||page==11||page==7)&&x>NAVW&&y>125&&y<hh-40){setstatus("Row selected. Right-click menus are available for processes, services and startup entries.");}else if(y>=63&&x<NAVW){int n=(y-63)/38;if(n>=0&&n<PAGE_COUNT){page=n;selected_service=-1;selected_startup=-1;selected_service_unit[0]=0;menu=0;service_menu=0;startup_menu=0;scroll=0;query[0]=0;search_focus=0;list_search_focus_page=-1;if(page==0)refresh();else if(page==8)update_gpu();}}else if(page==0&&y>=122&&y<=153&&x>NAVW+26&&x<NAVW+366){search_focus=1;if(x>NAVW+333&&query[0]){query[0]=0;selected=-1;scroll=0;}XSetInputFocus(D,W,RevertToParent,CurrentTime);}else if(page==0&&y>=122&&y<=153&&x>=ww-218&&x<=ww-24){run_task_dialog();}else if(page==0&&y>=174&&y<=206&&x>NAVW){int tx=NAVW+26,pp[9];process_column_positions(tx,ww-24,pp);int new_col=x<pp[1]?0:x<pp[2]?1:x<pp[3]?2:x<pp[4]?-1:x<pp[5]?3:x<pp[6]?4:x<pp[7]?5:x<pp[8]?6:-1;if(new_col>=0){if(new_col==sort_col)sort_desc=!sort_desc;else{sort_col=new_col;sort_desc=(sort_col>=3&&sort_col<=6);}pid_t keep_pid=(selected>=0&&selected<nproc)?proc[selected].pid:0;qsort(proc,nproc,sizeof(Proc),cmp_proc);selected=-1;if(keep_pid)for(int si=0;si<nproc;si++)if(proc[si].pid==keep_pid){selected=si;break;}build_process_view();}}else if(page==0&&y>205&&x>NAVW){int p=row_pick(y);if(p>=0)selected=p;}else if(y<58)search_focus=0;draw();}}else if(e.type==KeyPress){char b[32];KeySym k;int n=XLookupString(&e.xkey,b,sizeof b,&k,NULL);if(run_dialog){if(k==XK_Escape){run_dialog=0;run_focus=0;setstatus("Run task cancelled.");}else if(k==XK_Return||k==XK_KP_Enter){if(run_suggestion>=0&&!strpbrk(run_command," \t")&&(!run_command[0]||strncasecmp(run_suggestions[run_suggestion],run_command,strlen(run_command))==0))snprintf(run_command,sizeof run_command,"%s",run_suggestions[run_suggestion]);launch_command();}else if(k==XK_Up){run_choose_suggestion(-1);}else if(k==XK_Down||k==XK_Tab){run_choose_suggestion(1);}else if(k==XK_Page_Up||k==XK_Page_Down){run_choose_suggestion(k==XK_Page_Up?-run_dialog_rows():run_dialog_rows());}else if(k==XK_BackSpace){size_t q=strlen(run_command);if(q)run_command[q-1]=0;run_suggestion=-1;run_scroll=0;}else if(n>0&&isprint((unsigned char)b[0])&&strlen(run_command)<sizeof run_command-2){strncat(run_command,b,1);run_suggestion=-1;run_scroll=0;}draw();continue;}if(k==XK_Escape){menu=0;search_focus=0;list_search_focus_page=-1;}else if((page==0&&search_focus)||(list_search_focus_page==page&&page_has_search(page))){char*q=(page==0)?query:list_queries[page];if(k==XK_BackSpace){size_t qn=strlen(q);if(qn)q[qn-1]=0;reset_search_scroll(page);selected=-1;selected_startup=-1;selected_service=-1;selected_appstat_name[0]=0;}else if(n>0&&isprint((unsigned char)b[0])&&strlen(q)<sizeof query-2){size_t qn=strlen(q);q[qn]=b[0];q[qn+1]=0;reset_search_scroll(page);selected=-1;selected_startup=-1;selected_service=-1;selected_appstat_name[0]=0;setstatus("Searching list…");}}else if(page==2&&(k==XK_Up||k==XK_Down)){int dir=k==XK_Up?-1:1,pos=-1,seen=0;for(int ai=0;ai<nappstats;ai++)if(list_matches(2,appstats[ai].name)){if(ai==selected_appstat){pos=seen;break;}seen++;}if(pos<0)pos=history_scroll;pos+=dir;int count=0;for(int ai=0;ai<nappstats;ai++)if(list_matches(2,appstats[ai].name))count++;if(pos<0)pos=0;if(pos>=count)pos=count-1;int actual=history_filtered_index(pos);if(actual>=0){selected_appstat=actual;snprintf(selected_appstat_name,sizeof selected_appstat_name,"%s",appstats[actual].name);int visible=(hh-62-(84+54+52))/29;if(visible<1)visible=1;if(pos<history_scroll)history_scroll=pos;if(pos>=history_scroll+visible)history_scroll=pos-visible+1;}}else if(k==XK_F5){refresh();add_history();}else if(k==XK_Delete&&page==0){signal_selected(SIGTERM);}else if(page==10&&(k==XK_Up||k==XK_Down)){cpu_scroll+=(k==XK_Up?-1:1);if(cpu_scroll<0)cpu_scroll=0;}else if(page==11&&(k==XK_Up||k==XK_Down)){memory_scroll+=(k==XK_Up?-1:1);if(memory_scroll<0)memory_scroll=0;}else if(k==XK_Up&&scroll>0)scroll--;else if(k==XK_Down&&scroll<nproc-1)scroll++;else if(search_focus){if(k==XK_BackSpace){size_t q=strlen(query);if(q)query[q-1]=0;selected=-1;scroll=0;}else if(n>0&&isprint((unsigned char)b[0])&&strlen(query)<sizeof query-2){strncat(query,b,1);selected=-1;scroll=0;}}else if(page==0&&n>0&&isprint((unsigned char)b[0])&&!(e.xkey.state&(ControlMask|Mod1Mask))){search_focus=1;query[0]=b[0];query[1]=0;selected=-1;scroll=0;setstatus("Searching processes — type to filter, click the field to edit.");}else if(page_has_search(page)&&n>0&&isprint((unsigned char)b[0])&&!(e.xkey.state&(ControlMask|Mod1Mask))){list_search_focus_page=page;list_queries[page][0]=b[0];list_queries[page][1]=0;reset_search_scroll(page);setstatus("Searching list — type to filter, click the field to edit.");}draw();}}char oldstatus[256];snprintf(oldstatus,sizeof oldstatus,"%s",statusmsg);poll_pending_action();if(strcmp(oldstatus,statusmsg))draw();finish_queued_status();time_t now=time(NULL);if(now!=last){refresh();add_history();if(scroll>nproc-1)scroll=nproc>1?nproc-1:0;draw();last=now;}usleep(50000);}done:if(XFUser)XftFontClose(D,XFUser);if(XF)XftFontClose(D,XF);if(XD)XftDrawDestroy(XD);if(FS)XFreeFontSet(D,FS);if(F)XFreeFont(D,F);XFreeGC(D,G);XDestroyWindow(D,W);XCloseDisplay(D);return 0;}
