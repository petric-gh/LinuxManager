#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <locale.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
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
#define BLUE 0x2563EB
#define PALE 0xE7EEFF
#define LINE 0xE1E6EF
#define GREEN 0x16845B
#define RED 0xC0392B
#define AMBER 0xB7791F
#define PANEL 0xFFFFFF

typedef struct { pid_t pid,ppid; char name[256],user[64]; char state; unsigned long long rss,vsize,ticks,start_ticks; unsigned long threads; double cpu; } Proc;
typedef struct { char name[256]; double cpu_sum; unsigned long samples; unsigned long long peak_rss; } AppStat;
typedef struct { char unit[160],load[32],active[32],sub[32],desc[256]; } Service;
typedef struct { char name[256],path[PATH_MAX]; int disabled,user_owned,hidden_override,system_entry; } Startup;
static Display *D; static int S; static Window W; static GC G; static XFontStruct *F; static XFontSet FS; static XftDraw *XD; static XftFont *XF; static Atom WM_DELETE;
static int ww=SW,hh=SH,page=0,selected=-1,scroll=0,menu=0,menu_x=0,menu_y=0,sort_col=0,sort_desc=0,search_focus=0,run_dialog=0,run_focus=0;
static int dark_mode=0, history_scroll=0, cpu_scroll=0, network_scroll=0, selected_appstat=-1;
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
static Proc proc[MAXP],oldproc[MAXP]; static int nproc=0,nold=0;
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
static const char *pages[]={"Processes","Performance","App history","Startup apps","Users","Details","Services","Disk & Network","GPU","System info","CPU details","Memory details","Network"};
#define PAGE_COUNT ((int)(sizeof(pages)/sizeof(pages[0])))
static char list_queries[PAGE_COUNT][128];
static int list_search_focus_page=-1;
static unsigned long CFill(unsigned long c){
 if(!dark_mode)return c;
 if(c==WHITE||c==PANEL||c==BG||c==0xF4F6FA||c==0xF8FAFD)return 0x202833;
 if(c==0xEDF1F7||c==0xF0F3F8||c==0xE8EDF5||c==0xE8EDF4||c==0xF5F7FA||c==0xFAFBFD||c==0xF0F2F5)return 0x27313F;
 if(c==LINE||c==0xE1E6EF||c==0xE7EBF1)return 0x374354;
 if(c==PALE||c==0xDFE8FF||c==0xE7EEFF||c==0xDCE8FF)return 0x263B59;
 if(c==0xFFF1D6)return 0x49391F;
 if(c==0xFDE7E7)return 0x48292D;
 if(c==0xE1F5EA)return 0x203B31;
 if(c==0xD8DDE6)return 0x151C27;
 if(c==0xBFC7D4)return 0x111720;
 if(c==0xD7DCE5)return 0x202833;
 if(c==0xE7E8EC)return 0x303B4A;

 unsigned r=(c>>16)&255,g=(c>>8)&255,b=c&255;
 if(r>218&&g>218&&b>218&&(r>g-16&&g>b-18&&b>r-18))return 0x2A3442;
 return c;
}
static unsigned long CLine(unsigned long c){if(!dark_mode)return c;if(c==LINE||c==0xE1E6EF||c==0xE7EBF1||c==0xAEB8C8||c==0xE7EBF1)return 0x3A4658;return c;}
static unsigned long CText(unsigned long c){
 if(!dark_mode)return c;
 if(c==INK)return 0xE7ECF4;
 if(c==MUTED||c==0x687386)return 0xA9B4C5;
 if(c==BLUE)return 0x8CB2FF;
 if(c==GREEN)return 0x5CD6A2;
 if(c==RED)return 0xFF8585;
 if(c==AMBER)return 0xFFC266;
 if(c==WHITE)return 0xF8FAFC;
 return c;
}
static void fill(int x,int y,int w,int h,unsigned long c){XSetForeground(D,G,CFill(c));if(w>0&&h>0)XFillRectangle(D,W,G,x,y,(unsigned)w,(unsigned)h);}
static void stroke(int x,int y,int w,int h,unsigned long c){XSetForeground(D,G,CLine(c));if(w>0&&h>0)XDrawRectangle(D,W,G,x,y,(unsigned)w,(unsigned)h);}
static void ln(int x,int y,int xx,int yy,unsigned long c){XSetForeground(D,G,CLine(c));XDrawLine(D,W,G,x,y,xx,yy);}
static void text(int x,int y,const char *s,unsigned long c){unsigned long out=CText(c);if(XD&&XF){TaskXftColor fc;fc.pixel=out;fc.color.red=(unsigned short)(((out>>16)&255)*257);fc.color.green=(unsigned short)(((out>>8)&255)*257);fc.color.blue=(unsigned short)((out&255)*257);fc.color.alpha=65535;XftDrawStringUtf8(XD,&fc,XF,x,y,(const unsigned char*)s,(int)strlen(s));}else{XSetForeground(D,G,out);if(FS)Xutf8DrawString(D,W,FS,G,x,y,s,(int)strlen(s));else XDrawString(D,W,G,x,y,s,(int)strlen(s));}}
static void load_theme(void){const char *home=getenv("HOME");if(!home)return;char path[PATH_MAX];size_t n=strnlen(home,sizeof path-32);if(n>=sizeof path-32)return;memcpy(path,home,n);strcpy(path+n,"/.config/wintask-linux/theme");FILE*f=fopen(path,"r");if(f){int v=0;if(fscanf(f,"%d",&v)==1)dark_mode=v?1:0;fclose(f);}}
static void save_theme(void){const char *home=getenv("HOME");if(!home)return;char dir[PATH_MAX],path[PATH_MAX];size_t n=strnlen(home,sizeof dir-32);if(n>=sizeof dir-32)return;memcpy(dir,home,n);strcpy(dir+n,"/.config/wintask-linux");mkdir(dir,0700);size_t d=strlen(dir);if(d+7>=sizeof path)return;memcpy(path,dir,d);strcpy(path+d,"/theme");FILE*f=fopen(path,"w");if(f){fprintf(f,"%d\n",dark_mode?1:0);fclose(f);}}
static void format_cpu(double value,char *out,size_t size){if(value<0.001)snprintf(out,size,"<0.001%%");else snprintf(out,size,"%.3f%%",value);}
static void cliptext(int x,int y,int max,const char *s,unsigned long c){
 char b[1024]; snprintf(b,sizeof b,"%s",s?s:"");
 int lim=max/8; if(lim<1)lim=1;
 if((int)strlen(b)>lim){
  int cut=lim>3?lim-3:lim;
  while(cut>0 && (((unsigned char)b[cut]&0xC0)==0x80))cut--;
  if(cut<1){b[0]=0;} else if(lim>3){b[cut]='.';b[cut+1]='.';b[cut+2]='.';b[cut+3]=0;} else b[cut]=0;
 }
 text(x,y,b,c);
}
static void center(int x,int y,int w,const char*s,unsigned long c){int tw=(int)strlen(s)*8;text(x+(w-tw)/2,y,s,c);}
static void panel(int x,int y,int w,int h){fill(x,y,w,h,PANEL);stroke(x,y,w,h,LINE);}
static unsigned long long read_cpu_total(void){FILE*f=fopen("/proc/stat","r");if(!f)return 0;char b[512];unsigned long long v[10]={0},sum=0;if(fgets(b,sizeof b,f)){char*p=b+3;int n=sscanf(p,"%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9]);for(int i=0;i<n;i++)sum+=v[i];}fclose(f);return sum;}
static double read_cpu_percent(void){FILE*f=fopen("/proc/stat","r");if(!f)return 0;char b[512];unsigned long long v[10]={0},idle=0,total=0; if(fgets(b,sizeof b,f)){int n=sscanf(b+3,"%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9]);for(int i=0;i<n;i++)total+=v[i];idle=v[3]+v[4];}fclose(f);static unsigned long long pt=0,pi=0;double p=pt&&total>pt?100.0*(double)((total-pt)-(idle-pi))/(double)(total-pt):0;pt=total;pi=idle;if(p<0)p=0;if(p>100)p=100;return p;}
static double read_mem_percent(unsigned long long *used,unsigned long long *total){FILE*f=fopen("/proc/meminfo","r");unsigned long long t=0,a=0;char l[256];if(f){while(fgets(l,sizeof l,f)){if(sscanf(l,"MemTotal: %llu",&t)==1){}else if(sscanf(l,"MemAvailable: %llu",&a)==1){}}fclose(f);}*total=t*1024;*used=(t-a)*1024;return t?100.0*(double)(t-a)/(double)t:0;}
static int cmp_proc(const void*,const void*);
static int cmp_appstat(const void*a,const void*b){return strcasecmp(((const AppStat*)a)->name,((const AppStat*)b)->name);}
static void refresh(void){pid_t keep_pid=(selected>=0&&selected<nproc)?proc[selected].pid:0;DIR*d=opendir("/proc");if(!d)return;int oldn=nproc;nold=oldn;if(nold>MAXP)nold=MAXP;memcpy(oldproc,proc,(size_t)nold*sizeof(Proc));tickhash_clear();for(int j=0;j<nold;j++)tickhash_put(oldproc[j].pid,oldproc[j].ticks,oldproc[j].cpu);nproc=0;unsigned long long total=read_cpu_total();struct dirent*e;long pagesize=sysconf(_SC_PAGESIZE);while((e=readdir(d))&&nproc<MAXP){if(!isdigit((unsigned char)e->d_name[0]))continue;pid_t pid=(pid_t)atoi(e->d_name);char path[128],buf[4096];snprintf(path,sizeof path,"/proc/%d/stat",pid);FILE*f=fopen(path,"r");if(!f)continue;if(!fgets(buf,sizeof buf,f)){fclose(f);continue;}fclose(f);char*l=strchr(buf,'('),*r=strrchr(buf,')');if(!l||!r)continue;Proc*p=&proc[nproc];memset(p,0,sizeof(*p));p->pid=pid;size_t len=(size_t)(r-l-1);if(len>=sizeof p->name)len=sizeof p->name-1;memcpy(p->name,l+1,len);char tail[2048];snprintf(tail,sizeof tail,"%s",r+2);char *save=NULL,*tok=strtok_r(tail," ",&save);int field=0;unsigned long long ut=0,st=0,rss=0;while(tok){field++;if(field==1)p->state=tok[0];if(field==2)p->ppid=(pid_t)strtol(tok,NULL,10);if(field==12)ut=strtoull(tok,NULL,10);if(field==13)st=strtoull(tok,NULL,10);if(field==18)p->threads=strtoul(tok,NULL,10);if(field==20)p->start_ticks=strtoull(tok,NULL,10);if(field==21)p->vsize=strtoull(tok,NULL,10);if(field==22)rss=strtoull(tok,NULL,10);tok=strtok_r(NULL," ",&save);}p->ticks=ut+st;p->rss=rss*(unsigned long long)pagesize;unsigned long long oldticks=tickhash_get(pid);if(oldticks){unsigned long long dt=p->ticks>=oldticks?p->ticks-oldticks:0,dd=total>=prev_total?total-prev_total:0;double instant=dd?100.0*(double)dt/(double)dd:0.0;double recent=tickhash_get_cpu(pid);p->cpu=recent*0.70+instant*0.30;}snprintf(path,sizeof path,"/proc/%d/status",pid);f=fopen(path,"r");if(f){char l2[256];while(fgets(l2,sizeof l2,f))if(!strncmp(l2,"Uid:",4)){unsigned uid=(unsigned)atoi(l2+4);snprintf(p->user,sizeof p->user,"%s",username_for_uid(uid));break;}fclose(f);}if(!p->user[0])strcpy(p->user,"?");
 int ai=-1;for(int k=0;k<nappstats;k++)if(!strcmp(appstats[k].name,p->name)){ai=k;break;}
 if(ai<0&&nappstats<512){ai=nappstats++;snprintf(appstats[ai].name,sizeof appstats[ai].name,"%s",p->name);}
 if(ai>=0){appstats[ai].cpu_sum+=p->cpu;appstats[ai].samples++;if(p->rss>appstats[ai].peak_rss)appstats[ai].peak_rss=p->rss;}
 nproc++;}closedir(d);prev_total=total;qsort(proc,nproc,sizeof(Proc),cmp_proc);qsort(appstats,nappstats,sizeof(appstats[0]),cmp_appstat);selected=-1;if(keep_pid)for(int k=0;k<nproc;k++)if(proc[k].pid==keep_pid){selected=k;break;}}
static int matches(const char*s){if(!query[0])return 1;char a[512],b[128];snprintf(a,sizeof a,"%s",s);snprintf(b,sizeof b,"%s",query);for(char*p=a;*p;p++)*p=(char)tolower((unsigned char)*p);for(char*p=b;*p;p++)*p=(char)tolower((unsigned char)*p);return strstr(a,b)!=NULL;}
static int page_has_search(int pg){return pg==2||pg==3||pg==4||pg==5||pg==6||pg==7||pg==8||pg==9||pg==10||pg==11||pg==12;}
static int text_matches_query(const char *q,const char *s){if(!q||!q[0])return 1;if(!s)return 0;char a[1024],b[128];snprintf(a,sizeof a,"%s",s);snprintf(b,sizeof b,"%s",q);for(char*p=a;*p;p++)*p=(char)tolower((unsigned char)*p);for(char*p=b;*p;p++)*p=(char)tolower((unsigned char)*p);return strstr(a,b)!=NULL;}
static int list_matches(int pg,const char *s){return pg==0?matches(s):text_matches_query(pg>=0&&pg<PAGE_COUNT?list_queries[pg]:"",s);}
static int list_search_x(void){return ww-24-300;}
static void draw_list_search(int pg){if(!page_has_search(pg))return;int x=list_search_x(),y=70,w=300;fill(x,y,w,30,WHITE);stroke(x,y,w,30,list_search_focus_page==pg?BLUE:0xAEB8C8);XSetForeground(D,G,CText(MUTED));XDrawArc(D,W,G,x+10,y+8,9,9,0,360*64);ln(x+18,y+17,x+23,y+22,list_search_focus_page==pg?BLUE:MUTED);const char*q=list_queries[pg];cliptext(x+31,y+20,w-62,q[0]?q:"Search this list…",q[0]?INK:MUTED);if(q[0]){fill(x+w-29,y+6,22,18,0xEDF1F7);text(x+w-23,y+20,"×",MUTED);}}
static void reset_search_scroll(int pg){if(pg==2)history_scroll=0;else if(pg==10)cpu_scroll=0;else if(pg==12)network_scroll=0;else scroll=0;}

static int cmp_proc(const void*aa,const void*bb){const Proc*a=aa,*b=bb;int r=0;switch(sort_col){case 1:r=(a->pid>b->pid)-(a->pid<b->pid);break;case 2:r=(a->state>b->state)-(a->state<b->state);break;case 3:r=(a->cpu>b->cpu)-(a->cpu<b->cpu);break;case 4:r=(a->rss>b->rss)-(a->rss<b->rss);break;default:r=strcasecmp(a->name,b->name);}return sort_desc?-r:r;}
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
static void draw_action_banner(void){
 long long now=monotonic_ms();
 if(!status_processing()&&!status_failed()&&!status_action_result())return;
 if(!status_processing()&&status_changed_ms>0&&now-status_changed_ms>5000)return;
 int processing=status_processing(),failed=status_failed();
 unsigned long bg=processing?0xFFF1D6:(failed?0xFDE7E7:0xE1F5EA);
 unsigned long fg=processing?0xA65C00:(failed?RED:GREEN);
 int bx=ww-222,by=11,bw=198,bh=36;
 fill(bx,by,bw,bh,bg);stroke(bx,by,bw,bh,fg);fill(bx+10,by+13,9,9,fg);
 text(bx+26,by+23,processing?"PROCESSING…":(failed?"ACTION FAILED":"ACTION COMPLETE"),fg);
}
static void draw_nav(void){
 fill(0,0,ww,hh,0xF4F6FA);fill(0,0,ww,62,WHITE);fill(0,0,5,62,BLUE);text(24,39,"LinuxManager",INK);text(ww-190,37,"petric-gh",MUTED);ln(0,61,ww,61,LINE);
 fill(0,62,NAVW,hh-86,0xEDF1F7);
 for(int i=0;i<PAGE_COUNT;i++){int y=86+i*38;if(page==i){fill(9,y-23,NAVW-18,33,0xDFE8FF);fill(9,y-23,4,33,BLUE);}text(28,y,pages[i],page==i?BLUE:INK);}
 int ty=hh-60;fill(12,ty,NAVW-24,28,dark_mode?0x34445A:0xFFFFFF);stroke(12,ty,NAVW-24,28,dark_mode?0x50617A:LINE);fill(23,ty+9,10,10,dark_mode?0xFFC266:BLUE);text(42,ty+19,dark_mode?"Light mode":"Dark mode",dark_mode?0xFFC266:BLUE);
 ln(NAVW,62,NAVW,hh-24,LINE);fill(0,hh-24,ww,24,WHITE);ln(0,hh-24,ww,hh-24,LINE);
 unsigned long sc=MUTED;if(status_failed())sc=RED;else if(status_processing())sc=AMBER;else if(status_action_result()||!strncasecmp(statusmsg,"Selected",8)||!strncasecmp(statusmsg,"Ready",5))sc=GREEN;
 fill(12,hh-15,7,7,sc);cliptext(28,hh-7,ww-44,statusmsg,sc);
}
static void draw_processes(int details){int x=NAVW+26,top=84,right=ww-24;char b[256];text(x,top,pages[page],INK);text(x,top+24,"Live processes with a smoothed CPU estimate. Select a row; right-click for actions. F5 refreshes.",MUTED);int barY=top+38;fill(x,barY,340,30,WHITE);stroke(x,barY,340,30,search_focus?BLUE:0xAEB8C8);XSetForeground(D,G,CText(MUTED));XDrawArc(D,W,G,x+10,barY+8,9,9,0,360*64);ln(x+18,barY+17,x+23,barY+22,search_focus?BLUE:MUTED);cliptext(x+31,barY+20,270,query[0]?query:"Click or type to search…",query[0]?INK:MUTED);if(query[0]){fill(x+307,barY+7,24,16,0xEDF1F7);text(x+314,barY+20,"×",MUTED);}fill(right-220,barY,194,30,BLUE);center(right-220,barY+20,194,"Run new task",WHITE);int y=top+90;fill(x,y,right-x,32,0xF0F3F8);const char*heads1[]={"Name","PID","Status","User","CPU","Memory"};const int pos1[]={x+12,x+360,x+440,x+520,x+700,x+810};const char*heads2[]={"PID","Name","User","State","CPU","Memory"};const int pos2[]={x+12,x+100,x+450,x+650,x+740,x+830};for(int j=0;j<6;j++)text(details?pos2[j]:pos1[j],y+21,details?heads2[j]:heads1[j],MUTED);ln(x,y+32,right,y+32,LINE);y+=53;int row=0;for(int i=0;i<nproc;i++){if(!(matches(proc[i].name)||matches(proc[i].user)))continue;int yy=y+row*30-scroll*30;if(yy>hh-44)break;if(yy+5<y) { row++; continue; } if(yy>=y-20 && yy<=hh-42){if(i==selected)fill(x,yy-20,right-x,29,status_processing()?0xFFF1D6:PALE);if(row%2==1&&i!=selected)fill(x,yy-20,right-x,29,0xFAFBFD);Proc*p=&proc[i];if(!details){cliptext(pos1[0],yy,p->pid?330:330,p->name,INK);snprintf(b,sizeof b,"%d",p->pid);text(pos1[1],yy,b,INK);text(pos1[2],yy,p->state=='R'?"Running":p->state=='S'?"Sleeping":p->state=='D'?"Uninterruptible":p->state=='T'?"Stopped":p->state=='Z'?"Zombie":"Background",p->state=='R'?GREEN:MUTED);cliptext(pos1[3],yy,140,p->user,MUTED);format_cpu(p->cpu,b,sizeof b);text(pos1[4],yy,b,INK);snprintf(b,sizeof b,"%.1f MB",p->rss/1048576.0);text(pos1[5],yy,b,INK);}else{snprintf(b,sizeof b,"%d",p->pid);text(pos2[0],yy,b,INK);cliptext(pos2[1],yy,340,p->name,INK);cliptext(pos2[2],yy,170,p->user,MUTED);char state[2]={p->state,0};text(pos2[3],yy,state,INK);format_cpu(p->cpu,b,sizeof b);text(pos2[4],yy,b,INK);snprintf(b,sizeof b,"%.1f MB",p->rss/1048576.0);text(pos2[5],yy,b,INK);} }row++;}if(row==0)text(x+15,y+32,"No matching processes.",MUTED);}
static void draw_graph(int x,int y,int w,int h,const char*title,const char*value,double*hist,int count,unsigned long color){panel(x,y,w,h);text(x+16,y+24,title,INK);text(x+w-90,y+24,value,color);int gx=x+16,gy=y+40,gw=w-32,gh=h-58;for(int k=1;k<4;k++)ln(gx,gy+gh*k/4,gx+gw,gy+gh*k/4,LINE);if(count>1){for(int i=1;i<count;i++){int a=(histpos-count+i-1+120)%120,b=(histpos-count+i+120)%120;int x1=gx+(i-1)*gw/(count-1),x2=gx+i*gw/(count-1);int y1=gy+gh-(int)(hist[a]*gh/100.0),y2=gy+gh-(int)(hist[b]*gh/100.0);ln(x1,y1,x2,y2,color);}}}
static void draw_performance(void){int x=NAVW+26,y=84,w=ww-NAVW-52;unsigned long long used,total;double mp=read_mem_percent(&used,&total);double cp=cpu_hist[(histpos+119)%120];char a[64],b[64];format_cpu(cp,a,sizeof a);snprintf(b,sizeof b,"%.1f%%",mp);text(x,y,"Performance",INK);text(x,y+24,"Real-time CPU and memory telemetry from Linux kernel interfaces.",MUTED);int gw=(w-24)/2,gh=(hh-y-100)/2;draw_graph(x,y+42,gw,gh,"CPU usage",a,cpu_hist,histcount,BLUE);draw_graph(x+gw+24,y+42,gw,gh,"Memory usage",b,mem_hist,histcount,GREEN);struct sysinfo si;sysinfo(&si);char z[256];snprintf(z,sizeof z,"Logical processors: %ld",sysconf(_SC_NPROCESSORS_ONLN));text(x,y+gh+65,z,INK);snprintf(z,sizeof z,"Memory in use: %.2f GiB / %.2f GiB",used/1073741824.0,total/1073741824.0);text(x,y+gh+91,z,INK);snprintf(z,sizeof z,"System uptime: %ld days, %ld hours, %ld minutes",si.uptime/86400,(si.uptime/3600)%24,(si.uptime/60)%60);text(x,y+gh+117,z,INK);snprintf(z,sizeof z,"Processes: %d",nproc);text(x,y+gh+143,z,INK);}
typedef struct {char name[128],home[PATH_MAX],shell[PATH_MAX];unsigned uid;} UserEntry;
static int cmp_user_entry(const void *a,const void *b){return strcasecmp(((const UserEntry*)a)->name,((const UserEntry*)b)->name);}
static void draw_users(void){int x=NAVW+26,y=84,right=ww-24;text(x,y,"Users",INK);text(x,y+24,"Accounts known to the system, sorted alphabetically by username.",MUTED);draw_list_search(4);y+=54;fill(x,y,right-x,32,0xF0F3F8);text(x+12,y+21,"Username",MUTED);text(x+300,y+21,"UID",MUTED);text(x+430,y+21,"Home directory",MUTED);text(x+760,y+21,"Login shell",MUTED);y+=52;static UserEntry items[2048];int count=0;struct passwd*pw;setpwent();while((pw=getpwent())&&count<2048){snprintf(items[count].name,sizeof items[count].name,"%s",pw->pw_name?pw->pw_name:"");snprintf(items[count].home,sizeof items[count].home,"%s",pw->pw_dir?pw->pw_dir:"");snprintf(items[count].shell,sizeof items[count].shell,"%s",pw->pw_shell?pw->pw_shell:"");items[count].uid=(unsigned)pw->pw_uid;count++;}endpwent();qsort(items,count,sizeof(items[0]),cmp_user_entry);int matches_count=0;for(int i=0;i<count;i++){char blob[PATH_MAX*2+256];snprintf(blob,sizeof blob,"%.120s %.4095s %.4095s %u",items[i].name,items[i].home,items[i].shell,items[i].uid);if(list_matches(4,blob))matches_count++;}int visible=(hh-44-y)/28;if(visible<0)visible=0;if(scroll>matches_count-visible)scroll=matches_count>visible?matches_count-visible:0;int pos=0,drawn=0;for(int i=0;i<count;i++){char blob[PATH_MAX*2+256];snprintf(blob,sizeof blob,"%.120s %.4095s %.4095s %u",items[i].name,items[i].home,items[i].shell,items[i].uid);if(!list_matches(4,blob))continue;if(pos++<scroll||drawn>=visible)continue;int yy=y+drawn*28;if(drawn%2==0)fill(x,yy-19,right-x,26,WHITE);cliptext(x+12,yy,270,items[i].name,INK);char b[32];snprintf(b,sizeof b,"%u",items[i].uid);text(x+300,yy,b,INK);cliptext(x+430,yy,300,items[i].home,MUTED);cliptext(x+760,yy,300,items[i].shell,MUTED);drawn++;}if(!drawn)text(x+12,y+10,list_queries[4][0]?"No accounts match your search.":"No local accounts found.",MUTED);}
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
static void draw_startup(void){int x=NAVW+26,y=84,right=ww-24;text(x,y,"Startup apps",INK);text(x,y+24,"XDG autostart entries. Right-click to enable or disable for your account.",MUTED);draw_list_search(3);y+=54;fill(x,y,right-x,32,0xF0F3F8);text(x+12,y+21,"Startup entry",MUTED);text(x+390,y+21,"Status",MUTED);text(x+570,y+21,"Location",MUTED);y+=52;load_startups();int count=0;for(int i=0;i<nstartups;i++)if(list_matches(3,startups[i].name)||list_matches(3,startups[i].path))count++;int visible=(hh-44-y)/28;if(visible<0)visible=0;if(scroll>count-visible)scroll=count>visible?count-visible:0;for(int pos=scroll;pos<count&&pos<scroll+visible;pos++){int i=startup_filtered_index(pos);if(i<0)continue;Startup*st=&startups[i];int yy=y+(pos-scroll)*28;if(i==selected_startup)fill(x,yy-19,right-x,26,status_processing()?0xFFF1D6:PALE);else if((pos-scroll)%2==0)fill(x,yy-19,right-x,26,WHITE);cliptext(x+12,yy,350,st->name,INK);text(x+390,yy,st->disabled?"Disabled":"Enabled",st->disabled?AMBER:GREEN);cliptext(x+570,yy,right-x-580,st->path,MUTED);}if(!count)text(x+12,y+10,list_queries[3][0]?"No startup entries match your search.":"No XDG autostart entries found.",MUTED);}
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
 int x=NAVW+26,y=84,right=ww-24;char b[256];text(x,y,"App history",INK);text(x,y+24,"Select rows with a click; scroll to browse session CPU and peak-memory history.",MUTED);draw_list_search(2);y+=54;
 fill(x,y,right-x,32,0xF0F3F8);text(x+12,y+21,"Application / process",MUTED);text(x+450,y+21,"Average CPU",MUTED);text(x+620,y+21,"Peak memory",MUTED);text(x+790,y+21,"Samples",MUTED);y+=52;
 int count=0;for(int i=0;i<nappstats;i++)if(list_matches(2,appstats[i].name))count++;int visible=(hh-62-y)/29;if(visible<1)visible=1;int maxscroll=count>visible?count-visible:0;if(history_scroll>maxscroll)history_scroll=maxscroll;if(history_scroll<0)history_scroll=0;
 int end=history_scroll+visible;if(end>count)end=count;int shown=0;
 for(int pos=history_scroll;pos<end;pos++){int i=history_filtered_index(pos);if(i<0||!appstats[i].samples)continue;int yy=y+shown*29;int picked=selected_appstat_name[0]&&!strcmp(selected_appstat_name,appstats[i].name);if(picked)fill(x,yy-19,right-x,27,status_processing()?0xFFF1D6:PALE);else if(shown%2==0)fill(x,yy-19,right-x,27,WHITE);
  cliptext(x+12,yy,410,appstats[i].name,INK);format_cpu(appstats[i].cpu_sum/(double)appstats[i].samples,b,sizeof b);text(x+450,yy,b,INK);snprintf(b,sizeof b,"%.1f MB",appstats[i].peak_rss/1048576.0);text(x+620,yy,b,INK);snprintf(b,sizeof b,"%lu",appstats[i].samples);text(x+790,yy,b,MUTED);shown++;}
 if(!shown)text(x+12,y+10,list_queries[2][0]?"No history entries match your search.":"Collecting application usage…",MUTED);
 text(x,y<hh-24?hh-36:y,"History is session-only; it resets when the app closes.",MUTED);
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
static void draw_services(void){int x=NAVW+26,y=84,right=ww-24;text(x,y,"Services",INK);text(x,y+24,"Systemd units. Right-click for state-appropriate actions; results are verified.",MUTED);draw_list_search(6);y+=54;fill(x,y,right-x,32,0xF0F3F8);text(x+12,y+21,"Unit",MUTED);text(x+430,y+21,"Load",MUTED);text(x+570,y+21,"Active",MUTED);text(x+740,y+21,"Description",MUTED);y+=51;load_services();int count=0;for(int i=0;i<nservices;i++){char blob[600];snprintf(blob,sizeof blob,"%.150s %.30s %.30s %.30s %.250s",services[i].unit,services[i].load,services[i].active,services[i].sub,services[i].desc);if(list_matches(6,blob))count++;}int visible=(hh-44-y)/28;if(visible<0)visible=0;if(scroll>count-visible)scroll=count>visible?count-visible:0;for(int pos=scroll;pos<count&&pos<scroll+visible;pos++){int i=service_filtered_index(pos);if(i<0)continue;Service*sv=&services[i];int yy=y+(pos-scroll)*28;if(selected_service_unit[0]&&!strcmp(sv->unit,selected_service_unit))fill(x,yy-19,right-x,26,status_processing()?0xFFF1D6:PALE);else if((pos-scroll)%2==0)fill(x,yy-19,right-x,26,WHITE);cliptext(x+12,yy,400,sv->unit,INK);cliptext(x+430,yy,120,sv->load,MUTED);cliptext(x+570,yy,150,sv->active,!strcmp(sv->active,"active")?GREEN:INK);cliptext(x+740,yy,right-x-750,sv->desc,MUTED);}if(!count)text(x+12,y,list_queries[6][0]?"No services match your search.":"No systemd service units available.",MUTED);}


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

static void draw_disk_network(void){int x=NAVW+26,y=84,w=ww-NAVW-52;char b[256],line[512];text(x,y,"Disk & Network",INK);text(x,y+24,"Cumulative disk and network counters since boot; see Network for packet totals.",MUTED);draw_list_search(7);y+=52;panel(x,y,w,150);text(x+18,y+28,"Storage devices",BLUE);text(x+18,y+55,"Device       Read bytes       Write bytes",MUTED);FILE*f=fopen("/proc/diskstats","r");int rows=0;while(f&&fgets(line,sizeof line,f)&&rows<4){unsigned major,minor;char name[64];unsigned long long rd=0,wr=0;int n=sscanf(line,"%u %u %63s %*u %*u %llu %*u %*u %*u %*u %llu",&major,&minor,name,&rd,&wr);if(n>=5){char sys[PATH_MAX];snprintf(sys,sizeof sys,"/sys/block/%s",name);if(access(sys,F_OK)!=0||!list_matches(7,name))continue;snprintf(b,sizeof b,"%-12s  %llu KiB       %llu KiB",name,rd*512/1024,wr*512/1024);text(x+18,y+81+rows*20,b,INK);rows++;}}if(f)fclose(f);if(!rows)text(x+18,y+84,list_queries[7][0]?"No storage devices match your search.":"No storage device counters available.",MUTED);y+=168;panel(x,y,w,180);text(x+18,y+28,"Network interfaces",GREEN);text(x+18,y+55,"Interface        Received       Transmitted",MUTED);f=fopen("/proc/net/dev","r");int nr=0,drawn=0;while(f&&fgets(line,sizeof line,f)){if(nr++<2)continue;char name[64];unsigned long long rx=0,tx=0;if(sscanf(line," %63[^:]: %llu %*u %*u %*u %*u %*u %*u %*u %llu",name,&rx,&tx)!=3||!list_matches(7,name))continue;if(drawn>=4)break;snprintf(b,sizeof b,"%-14s  %.2f MiB      %.2f MiB",name,rx/1048576.0,tx/1048576.0);text(x+18,y+82+drawn*22,b,INK);drawn++;}if(f)fclose(f);if(!drawn&&list_queries[7][0])text(x+18,y+82,"No network interfaces match your search.",MUTED);}
static void draw_gpu(void){int x=NAVW+26,y=84,w=ww-NAVW-52;text(x,y,"GPU",INK);text(x,y+24,"GPU telemetry depends on the installed driver; no fake values are shown.",MUTED);draw_list_search(8);y+=52;panel(x,y,w,250);text(x+18,y+30,"Detected graphics devices",BLUE);int row=0;DIR*d=opendir("/sys/class/drm");struct dirent*e;while(d&&(e=readdir(d))&&row<8){if(strncmp(e->d_name,"card",4)||strchr(e->d_name,'-'))continue;char p[PATH_MAX],buf[256];snprintf(p,sizeof p,"/sys/class/drm/%s/device/vendor",e->d_name);FILE*f=fopen(p,"r");if(!f)continue;if(fgets(buf,sizeof buf,f)){buf[strcspn(buf,"\r\n")]=0;char out[320];snprintf(out,sizeof out,"%.80s  -  PCI vendor %.80s",e->d_name,buf);if(list_matches(8,out)){text(x+18,y+62+row*22,out,INK);row++;}}fclose(f);}if(d)closedir(d);if(!row)text(x+18,y+66,list_queries[8][0]?"No GPU devices match your search.":"No DRM GPU device exposed by the current driver.",MUTED);text(x+18,y+220,"GPU utilization, temperature and VRAM are driver-specific and unavailable through a universal Linux interface.",MUTED);}
static void draw_system_info(void){int x=NAVW+26,y=84,w=ww-NAVW-52;struct sysinfo si;sysinfo(&si);text(x,y,"System info",INK);text(x,y+24,"Live operating-system and hardware summary.",MUTED);draw_list_search(9);y+=52;panel(x,y,w,510);const char*keys[]={"Operating system","Kernel","Architecture","Hostname","Logical CPUs","Page size","Uptime","Process count","Total RAM","Available RAM","CPU model","Boot ID","Root filesystem","Shell","User ID","Home directory","Display server","Process limit"};char vals[18][256]={{0}};FILE*f=fopen("/etc/os-release","r");if(f){while(fgets(vals[0],sizeof vals[0],f))if(!strncmp(vals[0],"PRETTY_NAME=",12)){memmove(vals[0],vals[0]+12,strlen(vals[0]+12)+1);vals[0][strcspn(vals[0],"\r\n\"")]=0;break;}fclose(f);}if(!vals[0][0])strcpy(vals[0],"Linux");struct utsname un;if(uname(&un)==0){snprintf(vals[1],sizeof vals[1],"%s",un.release);snprintf(vals[2],sizeof vals[2],"%s (%s)",un.machine,sizeof(void*)==8?"64-bit":"32-bit");}else{strcpy(vals[1],"Unknown");strcpy(vals[2],sizeof(void*)==8?"64-bit":"32-bit");}if(gethostname(vals[3],sizeof vals[3]-1)<0)strcpy(vals[3],"Unknown");snprintf(vals[4],sizeof vals[4],"%ld",sysconf(_SC_NPROCESSORS_ONLN));snprintf(vals[5],sizeof vals[5],"%ld bytes",sysconf(_SC_PAGESIZE));snprintf(vals[6],sizeof vals[6],"%ld days, %ld hours, %ld minutes",si.uptime/86400,(si.uptime/3600)%24,(si.uptime/60)%60);snprintf(vals[7],sizeof vals[7],"%d",nproc);unsigned long long used,total;read_mem_percent(&used,&total);snprintf(vals[8],sizeof vals[8],"%.2f GiB",total/1073741824.0);snprintf(vals[9],sizeof vals[9],"%.2f GiB",(total-used)/1073741824.0);
 FILE*cpuinfo=fopen("/proc/cpuinfo","r");if(cpuinfo){char l[512];while(fgets(l,sizeof l,cpuinfo))if(!strncmp(l,"model name",10)){char*q=strchr(l,':');if(q){while(*++q==' ');snprintf(vals[10],sizeof vals[10],"%s",q);vals[10][strcspn(vals[10],"\r\n")]=0;}break;}fclose(cpuinfo);}if(!vals[10][0])strcpy(vals[10],"Not exposed");FILE*boot=fopen("/proc/sys/kernel/random/boot_id","r");if(boot){fgets(vals[11],sizeof vals[11],boot);fclose(boot);vals[11][strcspn(vals[11],"\r\n")]=0;}else strcpy(vals[11],"Unavailable");struct statvfs svfs;if(statvfs("/",&svfs)==0){unsigned long long fs_total=(unsigned long long)svfs.f_blocks*svfs.f_frsize,fs_free=(unsigned long long)svfs.f_bavail*svfs.f_frsize;snprintf(vals[12],sizeof vals[12],"/  %.2f GiB free of %.2f GiB",fs_free/1073741824.0,fs_total/1073741824.0);}else strcpy(vals[12],"Unavailable");snprintf(vals[13],sizeof vals[13],"%s",getenv("SHELL")?getenv("SHELL"):"Unknown");snprintf(vals[14],sizeof vals[14],"%u",(unsigned)getuid());snprintf(vals[15],sizeof vals[15],"%s",getenv("HOME")?getenv("HOME"):"Unknown");snprintf(vals[16],sizeof vals[16],"%s",getenv("WAYLAND_DISPLAY")?"Wayland / XWayland":getenv("DISPLAY")?"X11":"Unknown");snprintf(vals[17],sizeof vals[17],"%ld",sysconf(_SC_CHILD_MAX));int shown=0;for(int i=0;i<18;i++){char blob[600];snprintf(blob,sizeof blob,"%s %s",keys[i],vals[i]);if(!list_matches(9,blob))continue;int yy=y+30+shown*25;text(x+20,yy,keys[i],MUTED);cliptext(x+250,yy,w-280,vals[i],INK);shown++;}if(!shown)text(x+20,y+35,"No system information matches your search.",MUTED);}

static void draw_details_page(void){
 int x=NAVW+26,y=84,w=ww-NAVW-52; Proc *p=selected_proc();
 text(x,y,"Process details",INK);text(x,y+24,"Select a process on the Processes page, then inspect its live kernel properties here.",MUTED);draw_list_search(5);y+=48;
 if(!p){panel(x,y,w,112);text(x+18,y+34,"No process selected",INK);text(x+18,y+62,"Go to Processes, click a row, then return to Details.",MUTED);return;}
 panel(x,y,w,300);char val[PATH_MAX+128],path[PATH_MAX],cmd[2048];
 const char *keys[]={"Process name","Process ID","Parent process ID","User","State","CPU estimate","Resident memory","Virtual memory","Threads","Executable","Command line"};
 char values[11][PATH_MAX+128];memset(values,0,sizeof values);snprintf(values[0],sizeof values[0],"%s",p->name);snprintf(values[1],sizeof values[1],"%d",p->pid);snprintf(values[2],sizeof values[2],"%d",p->ppid);snprintf(values[3],sizeof values[3],"%s",p->user);snprintf(values[4],sizeof values[4],"%s (%c)",p->state=='R'?"Running":p->state=='S'?"Sleeping":p->state=='D'?"Uninterruptible sleep":p->state=='T'?"Stopped":"Other",p->state);char cpu_display[32];format_cpu(p->cpu,cpu_display,sizeof cpu_display);snprintf(values[5],sizeof values[5],"%s (sampled estimate)",cpu_display);snprintf(values[6],sizeof values[6],"%.2f MiB",p->rss/1048576.0);snprintf(values[7],sizeof values[7],"%.2f MiB",p->vsize/1048576.0);snprintf(values[8],sizeof values[8],"%lu",p->threads);
 snprintf(path,sizeof path,"/proc/%d/exe",p->pid);ssize_t n=readlink(path,val,sizeof val-1);if(n>=0){val[n]=0;snprintf(values[9],sizeof values[9],"%s",val);}else strcpy(values[9],"Not available (permission or process exited)");
 snprintf(path,sizeof path,"/proc/%d/cmdline",p->pid);FILE*f=fopen(path,"r");size_t used=0;if(f){int ch;while((ch=fgetc(f))!=EOF&&used<sizeof cmd-2){cmd[used++]=ch=='\0'?' ':ch;}fclose(f);}while(used&&cmd[used-1]==' ')used--;cmd[used]=0;snprintf(values[10],sizeof values[10],"%s",used?cmd:"Command line unavailable");
 int shown=0;for(int i=0;i<11;i++){char blob[PATH_MAX+256];snprintf(blob,sizeof blob,"%s %s",keys[i],values[i]);if(!list_matches(5,blob))continue;int yy=y+29+shown*24;if(yy>y+278)break;text(x+18,yy,keys[i],MUTED);cliptext(x+230,yy,w-250,values[i],INK);shown++;}if(!shown)text(x+18,y+38,"No process properties match your search.",MUTED);y+=330;panel(x,y,w,110);text(x+18,y+28,"Available actions",BLUE);text(x+18,y+55,"Right-click the process in Processes for terminate, suspend/resume, priority, path and search actions.",INK);text(x+18,y+80,"Some fields can be hidden by Linux permissions or process exit races.",MUTED);
}
static void draw_cpu_details(void){
 int x=NAVW+26,y=84,w=ww-NAVW-52;char line[512],label[64];unsigned long long v[10];text(x,y,"CPU details",INK);text(x,y+24,"Cumulative per-core scheduler ticks from /proc/stat; counters rise each second.",MUTED);draw_list_search(10);
 int hy=y+48;fill(x,hy,w,32,0xE8EDF5);text(x+12,hy+21,"Processor",MUTED);text(x+160,hy+21,"User ticks",MUTED);text(x+280,hy+21,"System ticks",MUTED);text(x+410,hy+21,"Idle ticks",MUTED);int totalx=x+(w>650?530:w-110);text(totalx,hy+21,"Total ticks",MUTED);
 int rowbase=hy+59,visible=(hh-44-rowbase)/29;if(visible<1)visible=1;int totalrows=0;FILE*f=fopen("/proc/stat","r");if(f){while(fgets(line,sizeof line,f)){if(strncmp(line,"cpu",3)||line[3]<'0'||line[3]>'9')continue;if(sscanf(line,"%63s",label)==1&&list_matches(10,label))totalrows++;}fclose(f);}int maxscroll=totalrows>visible?totalrows-visible:0;if(cpu_scroll>maxscroll)cpu_scroll=maxscroll;if(cpu_scroll<0)cpu_scroll=0;
 f=fopen("/proc/stat","r");int row=0,drawn=0;while(f&&fgets(line,sizeof line,f)){if(strncmp(line,"cpu",3)||line[3]<'0'||line[3]>'9')continue;int n=sscanf(line,"%63s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",label,&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9]);if(n<5||!list_matches(10,label))continue;if(row++<cpu_scroll)continue;if(drawn>=visible)break;int yy=rowbase+drawn*29;if(drawn%2==0)fill(x,yy-21,w,27,WHITE);unsigned long long total=0;for(int i=0;i<n-1;i++)total+=v[i];char b[80];text(x+12,yy,label,INK);snprintf(b,sizeof b,"%llu",v[0]);text(x+160,yy,b,INK);snprintf(b,sizeof b,"%llu",v[2]);text(x+280,yy,b,INK);snprintf(b,sizeof b,"%llu",v[3]);text(x+410,yy,b,INK);snprintf(b,sizeof b,"%llu",total);text(totalx,yy,b,INK);drawn++;}if(f)fclose(f);if(!drawn)text(x+12,rowbase,list_queries[10][0]?"No CPU cores match your search.":"Per-core counters are unavailable.",MUTED);
}
static void draw_memory_details(void){
 int x=NAVW+26,y=84,w=ww-NAVW-52;char line[512],key[96];unsigned long long val;const char*names[]={"MemTotal","MemAvailable","MemFree","Buffers","Cached","SwapTotal","SwapFree","SReclaimable","Shmem","Dirty","Writeback","PageTables","Slab","Committed_AS","VmallocTotal","VmallocUsed"};
 text(x,y,"Memory details",INK);cliptext(x,y+24,w,"Live kernel counters from /proc/meminfo. Search keys or values to find a specific metric.",MUTED);draw_list_search(11);y+=48;int gap=16,colw=(w-gap)/2;if(colw<120)colw=120;int panelh=hh-y-62;if(panelh<100)panelh=100;for(int c=0;c<2;c++){int px=x+c*(colw+gap);panel(px,y,colw,panelh);cliptext(px+14,y+28,colw-28,c?"Kernel / virtual memory":"Physical memory and swap",BLUE);}
 FILE*f=fopen("/proc/meminfo","r");int rows[2]={0,0},shown=0;while(f&&fgets(line,sizeof line,f)){if(sscanf(line,"%95[^:]: %llu",key,&val)!=2)continue;int found=-1;for(int i=0;i<16;i++)if(!strcmp(key,names[i])){found=i;break;}if(found<0)continue;char b[128],blob[256];if(val>=1048576)snprintf(b,sizeof b,"%.2f GiB",val/1048576.0);else snprintf(b,sizeof b,"%.1f MiB",val/1024.0);snprintf(blob,sizeof blob,"%s %s",key,b);if(!list_matches(11,blob))continue;int c=found>=8?1:0;int px=x+c*(colw+gap);int yy=y+56+rows[c]*30;if(yy>y+panelh-15)continue;int valuew=112;int labelw=colw-24-valuew-12;cliptext(px+12,yy,labelw,key,MUTED);cliptext(px+colw-valuew-12,yy,valuew,b,INK);rows[c]++;shown++;}if(f)fclose(f);if(!shown)text(x+14,y+70,list_queries[11][0]?"No memory metrics match your search.":"Memory counters unavailable.",MUTED);
}
static void draw_network(void){
 int x=NAVW+26,y=84,w=ww-NAVW-52;char line[512];text(x,y,"Network",INK);cliptext(x,y+24,w,"Per-interface received/transmitted bytes and packet counts from /proc/net/dev.",MUTED);draw_list_search(12);
 int hy=y+48;fill(x,hy,w,32,0xE8EDF5);text(x+12,hy+21,"Interface",MUTED);text(x+175,hy+21,"Received",MUTED);text(x+300,hy+21,"RX packets",MUTED);text(x+430,hy+21,"Transmitted",MUTED);int txpos=x+(w>650?590:w-100);text(txpos,hy+21,"TX packets",MUTED);
 int rowbase=hy+59,visible=(hh-44-rowbase)/34;if(visible<1)visible=1;int totalrows=0;FILE*f=fopen("/proc/net/dev","r");int skip=0;if(f){while(fgets(line,sizeof line,f)){if(skip++<2)continue;char name[64];if(sscanf(line," %63[^:]:",name)==1&&list_matches(12,name))totalrows++;}fclose(f);}int maxscroll=totalrows>visible?totalrows-visible:0;if(network_scroll>maxscroll)network_scroll=maxscroll;if(network_scroll<0)network_scroll=0;
 f=fopen("/proc/net/dev","r");skip=0;int row=0,drawn=0;while(f&&fgets(line,sizeof line,f)){if(skip++<2)continue;char name[64];unsigned long long rx=0,rp=0,tx=0,tp=0;if(sscanf(line," %63[^:]: %llu %llu %*u %*u %*u %*u %*u %*u %llu %llu",name,&rx,&rp,&tx,&tp)!=5||!list_matches(12,name))continue;if(row++<network_scroll)continue;if(drawn>=visible)break;int yy=rowbase+drawn*34;if(drawn%2==0)fill(x,yy-22,w,31,WHITE);char b[96];int namew=(w>650)?150:130;cliptext(x+12,yy,namew,name,INK);snprintf(b,sizeof b,"%.2f MiB",rx/1048576.0);cliptext(x+175,yy,w>650?105:90,b,INK);snprintf(b,sizeof b,"%llu",rp);cliptext(x+300,yy,90,b,INK);snprintf(b,sizeof b,"%.2f MiB",tx/1048576.0);cliptext(x+430,yy,w>650?120:90,b,INK);snprintf(b,sizeof b,"%llu",tp);cliptext(txpos,yy,w>650?100:90,b,INK);drawn++;}if(f)fclose(f);if(!drawn)text(x+12,rowbase,list_queries[12][0]?"No interfaces match your search.":"No network interfaces reported.",MUTED);
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
 text(x+24,y+88,"Click here to type, or start typing immediately",INK);fill(x+24,y+98,w-48,34,WHITE);stroke(x+24,y+98,w-48,34,run_focus?BLUE:LINE);cliptext(x+36,y+120,w-72,run_command[0]?run_command:"Enter an app name or command…",run_command[0]?INK:MUTED);
 run_update_filtered();text(list_x,list_top-10,"SUGGESTED COMMANDS",MUTED);char countbuf[96];snprintf(countbuf,sizeof countbuf,"%d match%s",run_filtered_count,run_filtered_count==1?"":"es");text(x+w-112,list_top-10,countbuf,MUTED);
 fill(list_x,list_top,list_w,list_h,WHITE);stroke(list_x,list_top,list_w,list_h,LINE);
 for(int row=0;row<rows;row++){int sy=list_top+row*RUN_ROW_H;int idx=row<visible_run_count?visible_run_suggestions[row]:-1;if(idx>=0&&idx==run_suggestion)fill(list_x+1,sy+1,list_w-2,RUN_ROW_H-1,0xDCE8FF);else if(row%2==1)fill(list_x+1,sy+1,list_w-2,RUN_ROW_H-1,0xF5F7FA);if(idx>=0){fill(list_x+12,sy+7,9,9,idx==run_suggestion?BLUE:0xAAB4C4);cliptext(list_x+32,sy+17,list_w-60,run_suggestions[idx],idx==run_suggestion?BLUE:INK);}if(row<rows-1)ln(list_x+1,sy+RUN_ROW_H,list_x+list_w-2,sy+RUN_ROW_H,0xE7EBF1);}

 if(run_filtered_count>rows){int track_h=list_h-8,thumb_h=track_h*rows/run_filtered_count;if(thumb_h<18)thumb_h=18;int maxscroll=run_filtered_count-rows;int thumb_y=list_top+4+(maxscroll?((track_h-thumb_h)*run_scroll/maxscroll):0);fill(list_x+list_w-8,list_top+4,3,track_h,0xE1E6EE);fill(list_x+list_w-8,thumb_y,3,thumb_h,BLUE);}
 if(!run_filtered_count)text(list_x+12,list_top+24,run_command[0]?"No matching command found in PATH.":"No suggestions available; you can type any command.",MUTED);
 int foot=y+h-74;char hint[160];if(run_filtered_count){int a=run_scroll+1,b=run_scroll+visible_run_count;snprintf(hint,sizeof hint,"Showing %d-%d / %d",a,b,run_filtered_count);}else snprintf(hint,sizeof hint,"Type a command, or click Cancel to return.");cliptext(x+24,foot,w-250,hint,MUTED);
 int by=y+h-47;fill(x+w-210,by,88,32,BLUE);center(x+w-210,by+21,88,"Run",WHITE);fill(x+w-112,by,88,32,0xE8EDF4);center(x+w-112,by+21,88,"Cancel",INK);
}
static void draw(void){draw_nav();if(page==0)draw_processes(0);else if(page==1)draw_performance();else if(page==2)draw_history();else if(page==3)draw_startup();else if(page==4)draw_users();else if(page==5)draw_details_page();else if(page==6)draw_services();else if(page==7)draw_disk_network();else if(page==8)draw_gpu();else if(page==9)draw_system_info();else if(page==10)draw_cpu_details();else if(page==11)draw_memory_details();else if(page==12)draw_network();draw_menu();draw_action_banner();draw_run_dialog();XFlush(D);}
static int row_pick(int y){int start=84+90+53-20; if(y<start||y>hh-44)return -1; int row=(y-start+scroll*30)/30;if(row<0)return -1;int n=0;for(int i=0;i<nproc;i++)if(matches(proc[i].name)||matches(proc[i].user)){if(n++==row)return i;}return -1;}
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
static void add_history(void){unsigned long long u,t;double m=read_mem_percent(&u,&t);cpu_hist[histpos]=read_cpu_percent();mem_hist[histpos]=m;histpos=(histpos+1)%120;if(histcount<120)histcount++;}
int main(void){setlocale(LC_CTYPE, "");load_theme();D=XOpenDisplay(NULL);if(!D){fprintf(stderr,"Task Manager requires an X11 display or XWayland.\n");return 1;}S=DefaultScreen(D);W=XCreateSimpleWindow(D,RootWindow(D,S),80,60,SW,SH,1,0xB9C2D0,BG);XStoreName(D,W,"Task Manager");
 XClassHint class_hint; class_hint.res_name=(char*)"taskmgr"; class_hint.res_class=(char*)"TaskManager"; XSetClassHint(D,W,&class_hint);
 XSizeHints size_hints; memset(&size_hints,0,sizeof(size_hints)); size_hints.flags=PMinSize; size_hints.min_width=1000; size_hints.min_height=620; XSetWMNormalHints(D,W,&size_hints);
 Atom net_pid=XInternAtom(D,"_NET_WM_PID",False); unsigned long mypid=(unsigned long)getpid(); XChangeProperty(D,W,net_pid,XA_CARDINAL,32,PropModeReplace,(unsigned char*)&mypid,1);
 XSelectInput(D,W,ExposureMask|KeyPressMask|ButtonPressMask|StructureNotifyMask);G=XCreateGC(D,W,0,NULL);char **missing=NULL;int missing_count=0;char *defstr=NULL;FS=XCreateFontSet(D,"-*-sans-serif-medium-r-normal--14-*-*-*-*-*-*-*",&missing,&missing_count,&defstr);if(missing)XFreeStringList(missing);F=XLoadQueryFont(D,"fixed");if(F)XSetFont(D,G,F->fid);XD=XftDrawCreate(D,W,DefaultVisual(D,S),DefaultColormap(D,S));XF=XftFontOpenName(D,S,"Noto Sans:size=10.5");if(!XF)XF=XftFontOpenName(D,S,"sans:size=10.5");WM_DELETE=XInternAtom(D,"WM_DELETE_WINDOW",False);XSetWMProtocols(D,W,&WM_DELETE,1);XMapWindow(D,W);
 Atom net_state=XInternAtom(D,"_NET_WM_STATE",False), max_v=XInternAtom(D,"_NET_WM_STATE_MAXIMIZED_VERT",False), max_h=XInternAtom(D,"_NET_WM_STATE_MAXIMIZED_HORZ",False);
 XEvent maxev; memset(&maxev,0,sizeof(maxev)); maxev.xclient.type=ClientMessage; maxev.xclient.window=W; maxev.xclient.message_type=net_state; maxev.xclient.format=32; maxev.xclient.data.l[0]=1; maxev.xclient.data.l[1]=max_v; maxev.xclient.data.l[2]=max_h; maxev.xclient.data.l[3]=1; XSendEvent(D,RootWindow(D,S),False,SubstructureRedirectMask|SubstructureNotifyMask,&maxev); XFlush(D);
 refresh();add_history();time_t last=time(NULL);for(;;){while(XPending(D)){XEvent e;XNextEvent(D,&e);if(e.type==ClientMessage&&(Atom)e.xclient.data.l[0]==WM_DELETE)goto done;if(e.type==Expose)draw();else if(e.type==ConfigureNotify){ww=e.xconfigure.width;hh=e.xconfigure.height;draw();}else if(e.type==ButtonPress){int x=e.xbutton.x,y=e.xbutton.y;if(run_dialog){int dx=run_dialog_x(),dy=run_dialog_y(),dw=run_dialog_w(),dh=run_dialog_h(),rows=run_dialog_rows(),listtop=dy+158,listbottom=listtop+rows*RUN_ROW_H;if((e.xbutton.button==Button4||e.xbutton.button==Button5)&&x>=dx+24&&x<=dx+dw-24&&y>=listtop&&y<listbottom){run_update_filtered();int maxscroll=run_filtered_count-rows;if(maxscroll<0)maxscroll=0;run_scroll+=(e.xbutton.button==Button4?-1:1);if(run_scroll<0)run_scroll=0;
 if(run_scroll>maxscroll){run_scroll=maxscroll;}if(run_filtered_count>0)run_suggestion=run_filtered[run_scroll];draw();continue;}if(y>=dy+dh-47&&y<=dy+dh-15&&x>=dx+dw-210&&x<=dx+dw-122)launch_command();else if(y>=dy+dh-47&&y<=dy+dh-15&&x>=dx+dw-112&&x<=dx+dw-24){run_dialog=0;run_focus=0;setstatus("Run task cancelled.");}else if(y>=dy+98&&y<=dy+132&&x>=dx+24&&x<=dx+dw-24){run_focus=1;XSetInputFocus(D,W,RevertToParent,CurrentTime);}else if(y>=listtop&&y<listbottom&&x>=dx+24&&x<=dx+dw-24){run_update_filtered();int wanted=(y-listtop)/RUN_ROW_H;if(wanted>=0&&wanted<visible_run_count){run_suggestion=visible_run_suggestions[wanted];snprintf(run_command,sizeof run_command,"%s",run_suggestions[run_suggestion]);run_scroll=0;run_focus=1;XSetInputFocus(D,W,RevertToParent,CurrentTime);}}draw();continue;}if(menu){if(e.xbutton.button==Button1&&x>=menu_x&&x<=menu_x+276&&y>=menu_y&&y<=menu_y+menu_height()){do_menu(y);draw();continue;}menu=0;service_menu=0;startup_menu=0;draw();}if(e.xbutton.button==Button4||e.xbutton.button==Button5){int delta=e.xbutton.button==Button4?-1:1;if(y>165&&y<hh-40){if(page==2){history_scroll+=delta;if(history_scroll<0)history_scroll=0;}else if(page==10){cpu_scroll+=delta;if(cpu_scroll<0)cpu_scroll=0;}else if(page==12){network_scroll+=delta;if(network_scroll<0)network_scroll=0;}else if(page==0||page==5||page==3||page==6||page==4){scroll+=delta;if(scroll<0)scroll=0;}}draw();continue;}if(e.xbutton.button==Button3&&page==0){int p=row_pick(y);if(p>=0){selected=p;service_menu=0;menu=1;menu_x=x;menu_y=y;draw();}}else if(e.xbutton.button==Button3&&page==6&&x>=NAVW+26&&x<ww-16){services_loaded=0;load_services();int sy=84+54+52-20;int pos=(y-sy)/28+scroll;int idx=service_filtered_index(pos);if(y>=sy&&y<hh-44&&idx>=0&&idx<nservices){selected_service=idx;snprintf(selected_service_unit,sizeof selected_service_unit,"%s",services[idx].unit);service_menu=1;startup_menu=0;menu=1;menu_x=x;menu_y=y;draw();}}else if(e.xbutton.button==Button3&&page==3&&x>=NAVW+26&&x<ww-16){load_startups();int sy=84+54+52-20;int pos=(y-sy)/28+scroll;int idx=startup_filtered_index(pos);if(y>=sy&&y<hh-44&&idx>=0&&idx<nstartups){selected_startup=idx;startup_menu=1;service_menu=0;menu=1;menu_x=x;menu_y=y;draw();}}else if(e.xbutton.button==Button1){
 if(page_has_search(page)&&x>=list_search_x()&&x<=ww-24&&y>=70&&y<=100){list_search_focus_page=page;if(x>=ww-24-30&&list_queries[page][0]){list_queries[page][0]=0;reset_search_scroll(page);selected_startup=-1;selected_service=-1;selected_appstat=-1;selected_appstat_name[0]=0;}else XSetInputFocus(D,W,RevertToParent,CurrentTime);setstatus(list_queries[page][0]?"Searching this list…":"Search cleared. Type to filter this list.");draw();continue;}
 if(page_has_search(page))list_search_focus_page=-1; if(page==0)search_focus=0;
 if(list_search_focus_page>=0&&list_search_focus_page!=page)list_search_focus_page=-1;else if(list_search_focus_page==page)list_search_focus_page=-1;
 if(page==0&&!(y>=122&&y<=153&&x>=NAVW+26&&x<=NAVW+366)){search_focus=0;}
 if(x< NAVW && y>=hh-60 && y<=hh-30){dark_mode=!dark_mode;save_theme();setstatus(dark_mode?"Completed: dark theme enabled.":"Completed: light theme enabled.");draw();continue;}if(page==2&&x>=NAVW+26&&x<ww-16){int hy=84+54+52;int visible=(hh-62-hy)/29;if(visible<1)visible=1;int rowtop=hy-19;int pos=history_scroll+(y-rowtop)/29;int idx=history_filtered_index(pos);if(y>=rowtop&&y<rowtop+visible*29&&idx>=0&&idx<nappstats){selected_appstat=idx;snprintf(selected_appstat_name,sizeof selected_appstat_name,"%s",appstats[idx].name);char msg[256];snprintf(msg,sizeof msg,"Selected application history: %.210s",selected_appstat_name);setstatus(msg);}}else if(page==6&&x>=NAVW+26&&x<ww-16){int sy=84+54+52-20;int pos=(y-sy)/28+scroll;int idx=service_filtered_index(pos);if(y>=sy&&y<hh-44&&idx>=0&&idx<nservices){selected_service=idx;snprintf(selected_service_unit,sizeof selected_service_unit,"%s",services[idx].unit);setstatus(services[idx].unit);}}else if(page==3&&x>=NAVW+26&&x<ww-16){load_startups();int sy=84+54+52-20;int pos=(y-sy)/28+scroll;int idx=startup_filtered_index(pos);if(y>=sy&&y<hh-44&&idx>=0&&idx<nstartups){selected_startup=idx;setstatus(startups[idx].name);}}else if(page==0&&y>205&&x>NAVW){int p=row_pick(y);if(p>=0){selected=p;setstatus("Process selected. Right-click for actions.");}}else if((page==10||page==11||page==12||page==7)&&x>NAVW&&y>125&&y<hh-40){setstatus("Row selected. Right-click menus are available for processes, services and startup entries.");}else if(y>=63&&x<NAVW){int n=(y-63)/38;if(n>=0&&n<PAGE_COUNT){page=n;selected_service=-1;selected_startup=-1;selected_service_unit[0]=0;menu=0;service_menu=0;startup_menu=0;scroll=0;query[0]=0;search_focus=0;list_search_focus_page=-1;}}else if(page==0&&y>=122&&y<=153&&x>NAVW+26&&x<NAVW+366){search_focus=1;if(x>NAVW+333&&query[0]){query[0]=0;selected=-1;scroll=0;}XSetInputFocus(D,W,RevertToParent,CurrentTime);}else if(page==0&&y>=122&&y<=153&&x>ww-260){run_task_dialog();}else if(page==0&&y>=174&&y<=206&&x>NAVW){int tx=NAVW+26;int new_col=x<tx+360?0:x<tx+440?1:x<tx+520?2:x>=tx+810?4:x>=tx+700?3:0;if(new_col==sort_col)sort_desc=!sort_desc;else{sort_col=new_col;sort_desc=(sort_col==3||sort_col==4);}qsort(proc,nproc,sizeof(Proc),cmp_proc);}else if(page==0&&y>205&&x>NAVW){int p=row_pick(y);if(p>=0)selected=p;}else if(y<58)search_focus=0;draw();}}else if(e.type==KeyPress){char b[32];KeySym k;int n=XLookupString(&e.xkey,b,sizeof b,&k,NULL);if(run_dialog){if(k==XK_Escape){run_dialog=0;run_focus=0;setstatus("Run task cancelled.");}else if(k==XK_Return||k==XK_KP_Enter){if(run_suggestion>=0&&!strpbrk(run_command," \t")&&(!run_command[0]||strncasecmp(run_suggestions[run_suggestion],run_command,strlen(run_command))==0))snprintf(run_command,sizeof run_command,"%s",run_suggestions[run_suggestion]);launch_command();}else if(k==XK_Up){run_choose_suggestion(-1);}else if(k==XK_Down||k==XK_Tab){run_choose_suggestion(1);}else if(k==XK_Page_Up||k==XK_Page_Down){run_choose_suggestion(k==XK_Page_Up?-run_dialog_rows():run_dialog_rows());}else if(k==XK_BackSpace){size_t q=strlen(run_command);if(q)run_command[q-1]=0;run_suggestion=-1;run_scroll=0;}else if(n>0&&isprint((unsigned char)b[0])&&strlen(run_command)<sizeof run_command-2){strncat(run_command,b,1);run_suggestion=-1;run_scroll=0;}draw();continue;}if(k==XK_Escape){menu=0;search_focus=0;list_search_focus_page=-1;}else if((page==0&&search_focus)||(list_search_focus_page==page&&page_has_search(page))){char*q=(page==0)?query:list_queries[page];if(k==XK_BackSpace){size_t qn=strlen(q);if(qn)q[qn-1]=0;reset_search_scroll(page);selected=-1;selected_startup=-1;selected_service=-1;selected_appstat_name[0]=0;}else if(n>0&&isprint((unsigned char)b[0])&&strlen(q)<sizeof query-2){size_t qn=strlen(q);q[qn]=b[0];q[qn+1]=0;reset_search_scroll(page);selected=-1;selected_startup=-1;selected_service=-1;selected_appstat_name[0]=0;setstatus("Searching list…");}}else if(page==2&&(k==XK_Up||k==XK_Down)){int dir=k==XK_Up?-1:1,pos=-1,seen=0;for(int ai=0;ai<nappstats;ai++)if(list_matches(2,appstats[ai].name)){if(ai==selected_appstat){pos=seen;break;}seen++;}if(pos<0)pos=history_scroll;pos+=dir;int count=0;for(int ai=0;ai<nappstats;ai++)if(list_matches(2,appstats[ai].name))count++;if(pos<0)pos=0;if(pos>=count)pos=count-1;int actual=history_filtered_index(pos);if(actual>=0){selected_appstat=actual;snprintf(selected_appstat_name,sizeof selected_appstat_name,"%s",appstats[actual].name);int visible=(hh-62-(84+54+52))/29;if(visible<1)visible=1;if(pos<history_scroll)history_scroll=pos;if(pos>=history_scroll+visible)history_scroll=pos-visible+1;}}else if(k==XK_F5){refresh();add_history();}else if(k==XK_Delete&&page==0){signal_selected(SIGTERM);}else if(k==XK_Up&&scroll>0)scroll--;else if(k==XK_Down&&scroll<nproc-1)scroll++;else if(search_focus){if(k==XK_BackSpace){size_t q=strlen(query);if(q)query[q-1]=0;selected=-1;scroll=0;}else if(n>0&&isprint((unsigned char)b[0])&&strlen(query)<sizeof query-2){strncat(query,b,1);selected=-1;scroll=0;}}else if(page==0&&n>0&&isprint((unsigned char)b[0])&&!(e.xkey.state&(ControlMask|Mod1Mask))){search_focus=1;query[0]=b[0];query[1]=0;selected=-1;scroll=0;setstatus("Searching processes — type to filter, click the field to edit.");}else if(page_has_search(page)&&n>0&&isprint((unsigned char)b[0])&&!(e.xkey.state&(ControlMask|Mod1Mask))){list_search_focus_page=page;list_queries[page][0]=b[0];list_queries[page][1]=0;reset_search_scroll(page);setstatus("Searching list — type to filter, click the field to edit.");}draw();}}char oldstatus[256];snprintf(oldstatus,sizeof oldstatus,"%s",statusmsg);poll_pending_action();if(strcmp(oldstatus,statusmsg))draw();finish_queued_status();time_t now=time(NULL);if(now!=last){refresh();add_history();if(scroll>nproc-1)scroll=nproc>1?nproc-1:0;draw();last=now;}usleep(50000);}done:if(XF)XftFontClose(D,XF);if(XD)XftDrawDestroy(XD);if(FS)XFreeFontSet(D,FS);if(F)XFreeFont(D,F);XFreeGC(D,G);XDestroyWindow(D,W);XCloseDisplay(D);return 0;}
