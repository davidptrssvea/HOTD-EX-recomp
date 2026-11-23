#ifndef HOTD4_INPUT_OPTIONS_H
#define HOTD4_INPUT_OPTIONS_H
/* Windows desktop preferences and per-device lightgun input. Included by host_sdl. */
#include <SDL_syswm.h>
#include "coop_mouse.h"
static int input_pref_mode, input_active_mode, input_pref_aa, input_prefs_loaded;
static int raw_suspended;
static float input_mouse_speed;
static int input_mouse_smoothing;
static const float mouse_speeds[] = {0.50f, 0.75f, 1.00f, 1.25f, 1.50f, 2.00f, 2.50f, 3.00f, 4.00f, 5.00f, 6.00f, 8.00f};
static const wchar_t *mouse_smoothing_names[] = {L"Off", L"Low", L"High"};
static unsigned raw_held, remote_held;
static CoopMouse remote_aim;
static HHOOK remote_hook;
static HANDLE remote_thread,remote_ready;
static DWORD remote_thread_id;
typedef struct {UINT msg;MSLLHOOKSTRUCT event;} RemotePacket;
static RemotePacket remote_packets[512];
static unsigned remote_read,remote_write;
static int remote_overflow;
static SRWLOCK remote_lock=SRWLOCK_INIT;
#define REMOTE_WARP_TAG ((ULONG_PTR)0x48345032u)
static char input_guns[2][1024];
static char raw_active_guns[2][1024];
static HANDLE raw_assigned[2];
static WNDPROC raw_previous_proc;
static HWND raw_window;
static const wchar_t *input_mode_names[] = {L"Mouse (Absolute)", L"Mouse (Raw Relative)", L"Lightgun (Raw Input)", L"Co-op: Gun P1 + Mouse P2", L"Co-op: Gun P1 + Mouse P2 (Relative)"};
static void input_path(char *out, size_t cap, const char *file)
{
    const char *dir = getenv("HOTD4_SETTINGS_DIR");
    if (dir && *dir) snprintf(out,cap,"%s\\%s",dir,file);
    else {
        GetModuleFileNameA(NULL,out,(DWORD)cap);
        char *slash = strrchr(out,'\\');
        if (slash) snprintf(slash+1,cap-(size_t)(slash+1-out),"%s",file);
    }
}
static int input_read(const char *file, char *out, size_t cap)
{
    char path[2048]; input_path(path,sizeof path,file);
    FILE *f=fopen(path,"rb"); if(!f)return 0;
    int ok=fgets(out,(int)cap,f)!=NULL; fclose(f);
    if(ok)out[strcspn(out,"\r\n")]=0;
    return ok;
}
static void input_write(const char *file, const char *value)
{
    char path[2048]; input_path(path,sizeof path,file);
    FILE *f=fopen(path,"wb");
    if(f){fputs(value,f);fclose(f);}else fprintf(stderr,"[input] Cannot save %s\n",path);
}
static void input_options_load(void)
{
    if(input_prefs_loaded)return;
    input_prefs_loaded=1;
    input_pref_mode=settings()->raw_mouse?1:0;
    input_pref_aa=settings()->aa;
    input_mouse_speed = settings()->mouse_sens;
    if (!isfinite(input_mouse_speed) || input_mouse_speed <= 0.0f) input_mouse_speed = 1.0f;
    char value[32];
    if (input_read("mouse-speed.txt", value, sizeof value)) {
        float speed = (float)atof(value);
        if (speed >= 0.5f && speed <= 8.0f) input_mouse_speed = speed;
    }
    if (input_read("mouse-smoothing.txt", value, sizeof value) &&
        strlen(value) == 1 && value[0] >= '0' && value[0] <= '2')
        input_mouse_smoothing = value[0] - '0';
    if(input_read("input-profile.txt",value,sizeof value) && strlen(value)==1 && value[0]>='0' && value[0]<='4')input_pref_mode=atoi(value);
    if(input_read("aa-profile.txt",value,sizeof value)){
        int n=atoi(value); if(n==0 || n==2 || n==4 || n==8)input_pref_aa=n;
    }
    input_read("raw-gun1.txt",input_guns[0],sizeof input_guns[0]);
    input_read("raw-gun2.txt",input_guns[1],sizeof input_guns[1]);
    if(!*input_guns[0])strcpy(input_guns[0],"auto");
    if(!*input_guns[1])strcpy(input_guns[1],"disabled");
    memcpy(raw_active_guns,input_guns,sizeof raw_active_guns);
    input_active_mode=input_pref_mode;
    settings()->raw_mouse=input_active_mode==1;
    if(input_active_mode>=2)settings()->mouse_grab=0;
    settings()->aa=input_pref_aa;
}
static void input_option_cycle(int row, int delta)
{
    char value[32]; delta=delta<0?-1:1;
    if(row==0){input_pref_mode=(input_pref_mode+5+delta)%5;snprintf(value,sizeof value,"%d",input_pref_mode);input_write("input-profile.txt",value);}
    if (row == 4 && (input_pref_mode == 1 || input_pref_mode == 4)) {
        int count = sizeof mouse_speeds / sizeof *mouse_speeds;
        int at = 0;
        for (int i = 1; i < count; ++i)
            if (fabsf(mouse_speeds[i] - input_mouse_speed) < fabsf(mouse_speeds[at] - input_mouse_speed)) at = i;
        input_mouse_speed = mouse_speeds[(at + count + delta) % count];
        snprintf(value, sizeof value, "%.2f", (double)input_mouse_speed);
        input_write("mouse-speed.txt", value);
    }
    if (row == 5 && (input_pref_mode == 1 || input_pref_mode == 4)) {
        input_mouse_smoothing = (input_mouse_smoothing + 3 + delta) % 3;
        snprintf(value, sizeof value, "%d", input_mouse_smoothing);
        input_write("mouse-smoothing.txt", value);
    }
    if(row==3){
        const int samples[]={0,2,4,8}; int at=0;
        for(int i=0;i<4;i++)if(samples[i]==input_pref_aa)at=i;
        input_pref_aa=samples[(at+4+delta)%4];snprintf(value,sizeof value,"%d",input_pref_aa);input_write("aa-profile.txt",value);
    }
    if(row==1 || row==2){
        if(input_pref_mode<2 || (row==2 && input_pref_mode>=3))return;
        int pl=row-1; UINT count=0;
        if(GetRawInputDeviceList(NULL,&count,sizeof(RAWINPUTDEVICELIST))==(UINT)-1)return;
        RAWINPUTDEVICELIST *list=calloc(count?count:1,sizeof *list);
        if(!list)return;
        UINT got=GetRawInputDeviceList(list,&count,sizeof *list);
        char paths[66][1024]; int n=2,at=0;
        strcpy(paths[0],"auto");strcpy(paths[1],"disabled");
        if(got!=(UINT)-1)for(UINT i=0;i<got && n<66;i++)if(list[i].dwType==RIM_TYPEMOUSE){
            UINT len=sizeof paths[0];
            if(GetRawInputDeviceInfoA(list[i].hDevice,RIDI_DEVICENAME,paths[n],&len)!=(UINT)-1){paths[n][1023]=0;n++;}
        }
        for(int i=0;i<n;i++)if(!_stricmp(paths[i],input_guns[pl]))at=i;
        strcpy(input_guns[pl],paths[(at+n+delta)%n]);
        input_write(pl?"raw-gun2.txt":"raw-gun1.txt",input_guns[pl]);free(list);
    }
}
static void input_gun_label(int pl, wchar_t *out, size_t cap)
{
    if(pl==1 && input_pref_mode>=3){swprintf(out,cap,L"P2 Device  < Parsec / injected mouse >");return;}
    if(!_stricmp(input_guns[pl],"auto")){swprintf(out,cap,L"P%d Device  < Auto: absolute gun >",pl+1);return;}
    if(!_stricmp(input_guns[pl],"disabled")){swprintf(out,cap,L"P%d Device  < Disabled >",pl+1);return;}
    /* VID/PID and interface identify the same device in both menus. */
    const char *id=strstr(input_guns[pl],"VID_"); if(!id)id=input_guns[pl];
    swprintf(out,cap,L"P%d Device  < %.32hs >",pl+1,id);
}
static int raw_select_player(HANDLE device, const char *path, int absolute)
{
    if(!device)return -1;
    int players=input_active_mode>=3?1:2;
    /* Explicit assignments always take priority over automatic assignment. */
    for(int p=0;p<players;p++)if(!_stricmp(raw_active_guns[p],path))return p;
    for(int p=0;p<players;p++)if(raw_assigned[p]==device)return p;
    if(!absolute)return -1;
    for(int p=0;p<players;p++)if(!_stricmp(raw_active_guns[p],"auto") && !raw_assigned[p]){
        raw_assigned[p]=device;fprintf(stderr,"[raw-gun] P%d assigned %s\n",p+1,path);return p;
    }
    return -1;
}
static int raw_player(HANDLE device, int absolute)
{
    if(!device)return -1;
    char path[1024];UINT len=sizeof path;
    if(GetRawInputDeviceInfoA(device,RIDI_DEVICENAME,path,&len)==(UINT)-1)return -1;
    path[1023]=0;
    return raw_select_player(device,path,absolute);
}
static int raw_normalized(LONG value, int origin, int span)
{
    return origin+(int)((int64_t)value*(span-1)/65535);
}
static void raw_release(void)
{
    raw_held=remote_held=0;g_buttons=0;
    if(input_active_mode>=3)g_held &= ~(CAB_P2_TRIGGER|CAB_P2_RELOAD|CAB_P2_ACTION|CAB_P2_FIREMODE);
    remote_aim.have_screen=0;
    AcquireSRWLockExclusive(&remote_lock);
    remote_read=remote_write=0;remote_overflow=0;
    ReleaseSRWLockExclusive(&remote_lock);
    g_cab_latch &= ~(CAB_P1_TRIGGER|CAB_P1_RELOAD|CAB_P1_ACTION|CAB_P1_FIREMODE|
                     CAB_P2_TRIGGER|CAB_P2_RELOAD|CAB_P2_ACTION|CAB_P2_FIREMODE);
}
static void raw_decode(int player, const RAWMOUSE *mouse)
{
    int x=player?g_gun2_px:g_mouse_x,y=player?g_gun2_py:g_mouse_y;
    if(mouse->usFlags&MOUSE_MOVE_ABSOLUTE){
        int virt=(mouse->usFlags&MOUSE_VIRTUAL_DESKTOP)!=0;
        POINT point={raw_normalized(mouse->lLastX,virt?GetSystemMetrics(SM_XVIRTUALSCREEN):0,GetSystemMetrics(virt?SM_CXVIRTUALSCREEN:SM_CXSCREEN)),
                     raw_normalized(mouse->lLastY,virt?GetSystemMetrics(SM_YVIRTUALSCREEN):0,GetSystemMetrics(virt?SM_CYVIRTUALSCREEN:SM_CYSCREEN))};
        ScreenToClient(raw_window,&point);
        RECT rect;GetClientRect(raw_window,&rect);
        int dw,dh;SDL_GL_GetDrawableSize(g_win.win,&dw,&dh);
        /* Native client coordinates -> drawable -> game viewport, without SDL's DPI conversion. */
        double px=(double)point.x*dw/(rect.right?rect.right:1),py=(double)point.y*dh/(rect.bottom?rect.bottom:1);
        x=(int)floor((px-g_vp_x)*g_win.w/(g_vp_w?g_vp_w:dw));
        y=(int)floor((py-g_vp_y)*g_win.h/(g_vp_h?g_vp_h:dh));
    }else if(mouse->lLastX || mouse->lLastY){
        if(x<0 || x>=g_win.w)x=g_win.w/2;if(y<0 || y>=g_win.h)y=g_win.h/2;
        x+=(int)(mouse->lLastX*settings()->mouse_sens);y+=(int)(mouse->lLastY*settings()->mouse_sens);
        x=x<0?0:x>=g_win.w?g_win.w-1:x;y=y<0?0:y>=g_win.h?g_win.h-1:y;
    }
    if(player){g_gun2_on=1;g_gun2_px=x;g_gun2_py=y;}else{g_mouse_x=x;g_mouse_y=y;}
    const unsigned bits[3]={player?CAB_P2_TRIGGER:CAB_P1_TRIGGER,player?CAB_P2_RELOAD:CAB_P1_RELOAD,player?CAB_P2_ACTION:CAB_P1_ACTION};
    for(int b=0;b<3;b++){
        if(mouse->usButtonFlags&(1u<<(b*2))){raw_held|=bits[b];g_cab_latch|=bits[b];}
        if(mouse->usButtonFlags&(2u<<(b*2)))raw_held&=~bits[b];
    }

    if(!player)g_buttons=(raw_held&CAB_P1_TRIGGER?1:0)|(raw_held&CAB_P1_RELOAD?4:0);
}
static void remote_mouse_publish(void)
{
    if(input_active_mode<3)return;
    coop_mouse_publish(&remote_aim,input_active_mode==4?input_mouse_smoothing:0,SDL_GetTicks());
    g_gun2_px=(int)lroundf(remote_aim.x);g_gun2_py=(int)lroundf(remote_aim.y);g_gun2_on=1;
}
static void remote_mouse_event(UINT msg,const MSLLHOOKSTRUCT *event,int active)
{
    int move=msg==WM_MOUSEMOVE;
    int remote=(event->flags&LLMHF_INJECTED)!=0 && event->dwExtraInfo!=REMOTE_WARP_TAG;
    if(move) {
        POINT p=event->pt;ScreenToClient(raw_window,&p);
        RECT rect;GetClientRect(raw_window,&rect);int dw=0,dh=0;
        SDL_GL_GetDrawableSize(g_win.win,&dw,&dh);
        if(rect.right<=0 || rect.bottom<=0 || dw<=0 || dh<=0)return;
        float sx=(float)dw/rect.right*g_win.w/(g_vp_w?g_vp_w:dw);
        float sy=(float)dh/rect.bottom*g_win.h/(g_vp_h?g_vp_h:dh);
        float gx=p.x*sx-g_vp_x*(float)g_win.w/(g_vp_w?g_vp_w:dw);
        float gy=p.y*sy-g_vp_y*(float)g_win.h/(g_vp_h?g_vp_h:dh);
        int was_on=remote_aim.target_x>=0 && remote_aim.target_y>=0 && remote_aim.target_x<g_win.w && remote_aim.target_y<g_win.h;
        coop_mouse_motion(&remote_aim,active&&remote,input_active_mode==4,event->pt.x,event->pt.y,
                          gx,gy,sx,sy,input_mouse_speed,g_win.w,g_win.h);
        if(active&&remote) {
            remote_mouse_publish();
            int on=g_gun2_px>=0 && g_gun2_py>=0 && g_gun2_px<g_win.w && g_gun2_py<g_win.h;
            if(was_on && !on && settings()->leave_reload)g_cab_latch|=CAB_P2_RELOAD;
            if(input_active_mode==4 && (p.x<24 || p.y<24 || p.x>rect.right-24 || p.y>rect.bottom-24)) {
                /* Tag our own warp so it never becomes another player's movement. */
                POINT center={rect.right/2,rect.bottom/2};ClientToScreen(raw_window,&center);
                int ox=GetSystemMetrics(SM_XVIRTUALSCREEN),oy=GetSystemMetrics(SM_YVIRTUALSCREEN);
                int w=GetSystemMetrics(SM_CXVIRTUALSCREEN),h=GetSystemMetrics(SM_CYVIRTUALSCREEN);
                if(w>1 && h>1){INPUT input={0};input.type=INPUT_MOUSE;
                    input.mi.dx=(LONG)((int64_t)(center.x-ox)*65535/(w-1));input.mi.dy=(LONG)((int64_t)(center.y-oy)*65535/(h-1));
                    input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
                    input.mi.dwExtraInfo=REMOTE_WARP_TAG;SendInput(1,&input,sizeof input);}
            }
        }
        return;
    }
    if(!active || !remote)return;
    int button=msg==WM_LBUTTONDOWN || msg==WM_LBUTTONUP?SDL_BUTTON_LEFT:
               msg==WM_RBUTTONDOWN || msg==WM_RBUTTONUP?SDL_BUTTON_RIGHT:
               msg==WM_MBUTTONDOWN || msg==WM_MBUTTONUP?SDL_BUTTON_MIDDLE:
               msg==WM_XBUTTONDOWN || msg==WM_XBUTTONUP?(HIWORD(event->mouseData)==XBUTTON1?SDL_BUTTON_X1:SDL_BUTTON_X2):0;
    if(!button)return;
    int down=msg==WM_LBUTTONDOWN || msg==WM_RBUTTONDOWN || msg==WM_MBUTTONDOWN || msg==WM_XBUTTONDOWN;
    const unsigned bits[]={CAB_P2_TRIGGER,CAB_P2_RELOAD,CAB_P2_ACTION,CAB_P2_FIREMODE};
    for(int a=ACT_TRIGGER;a<=ACT_FIREMODE;a++)if(hc_get(a)==MOUSE_BIND(button)) {
        unsigned bit=bits[a-ACT_TRIGGER];
        if(down){remote_held|=bit;g_cab_latch|=bit;}else remote_held&=~bit;
    }
}
/* A dedicated message thread keeps Windows' low-level hook alive while the
 * game loads assets or compiles shaders. It never touches SDL or game state. */
static LRESULT CALLBACK remote_mouse_hook(int code,WPARAM wp,LPARAM lp)
{
    if(code>=0 && GetForegroundWindow()==raw_window) {
        const MSLLHOOKSTRUCT *event=(const MSLLHOOKSTRUCT *)lp;
        if(wp==WM_MOUSEMOVE || (event->flags&LLMHF_INJECTED)) {
            AcquireSRWLockExclusive(&remote_lock);
            if(remote_write-remote_read==512){remote_read++;remote_overflow=1;}
            remote_packets[remote_write++%512]=(RemotePacket){(UINT)wp,*event};
            ReleaseSRWLockExclusive(&remote_lock);
        }
    }
    return CallNextHookEx(NULL,code,wp,lp);
}
static DWORD WINAPI remote_mouse_thread(void *unused)
{
    (void)unused;MSG msg;
    PeekMessage(&msg,NULL,WM_USER,WM_USER,PM_NOREMOVE);
    remote_hook=SetWindowsHookExW(WH_MOUSE_LL,remote_mouse_hook,GetModuleHandle(NULL),0);
    SetEvent(remote_ready);
    if(remote_hook){while(GetMessage(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessage(&msg);}
        UnhookWindowsHookEx(remote_hook);remote_hook=NULL;}
    return 0;
}
static void remote_mouse_drain(void)
{
    if(input_active_mode<3)return;
    for(;;){RemotePacket batch[32];unsigned count=0;int overflow;
        AcquireSRWLockExclusive(&remote_lock);
        overflow=remote_overflow;remote_overflow=0;
        while(remote_read!=remote_write && count<32)batch[count++]=remote_packets[remote_read++%512];
        ReleaseSRWLockExclusive(&remote_lock);
        if(overflow){remote_held=0;remote_aim.have_screen=0;
            g_cab_latch&=~(CAB_P2_TRIGGER|CAB_P2_RELOAD|CAB_P2_ACTION|CAB_P2_FIREMODE);}
        if(!count)break;
        int active=!raw_suspended && GetForegroundWindow()==raw_window;
        for(unsigned i=0;i<count;i++)remote_mouse_event(batch[i].msg,&batch[i].event,active);
    }
}
static void remote_mouse_uninstall(void)
{
    if(remote_thread){PostThreadMessage(remote_thread_id,WM_QUIT,0,0);
        WaitForSingleObject(remote_thread,INFINITE);CloseHandle(remote_thread);remote_thread=NULL;}
    if(remote_ready){CloseHandle(remote_ready);remote_ready=NULL;}
}

static LRESULT CALLBACK raw_window_proc(HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
    if(msg==WM_INPUT && !raw_suspended && GetForegroundWindow()==window){
        RAWINPUT input;UINT size=sizeof input;
        if(GetRawInputData((HRAWINPUT)lp,RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER))!=(UINT)-1 && input.header.dwType==RIM_TYPEMOUSE){
            int p=raw_player(input.header.hDevice,input.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE);
            if(p>=0)raw_decode(p,&input.data.mouse);
        }
    }
    if(msg==WM_KILLFOCUS)raw_release();
    if(msg==WM_INPUT_DEVICE_CHANGE && wp==GIDC_REMOVAL){
        raw_release();for(int p=0;p<2;p++)if(raw_assigned[p]==(HANDLE)lp)raw_assigned[p]=NULL;
    }
    /* SDL/DefWindowProc must receive WM_INPUT for Windows to free the packet. */
    return CallWindowProc(raw_previous_proc,window,msg,wp,lp);
}
static int raw_install(void)
{
    if(input_active_mode<2)return 1;
    SDL_SysWMinfo info;SDL_VERSION(&info.version);
    if(!SDL_GetWindowWMInfo(g_win.win,&info))return 0;
    raw_window=info.info.win.window;
    SetLastError(0);
    raw_previous_proc=(WNDPROC)SetWindowLongPtr(raw_window,GWLP_WNDPROC,(LONG_PTR)raw_window_proc);
    if(!raw_previous_proc)return 0;
    RAWINPUTDEVICE device={1,2,RIDEV_DEVNOTIFY,raw_window};
    if(!RegisterRawInputDevices(&device,1,sizeof device)){
        SetWindowLongPtr(raw_window,GWLP_WNDPROC,(LONG_PTR)raw_previous_proc);return 0;
    }
    if(input_active_mode>=3) {
        coop_mouse_init(&remote_aim,g_win.w,g_win.h,SDL_GetTicks());
        remote_ready=CreateEvent(NULL,TRUE,FALSE,NULL);
        if(remote_ready)remote_thread=CreateThread(NULL,0,remote_mouse_thread,NULL,0,&remote_thread_id);
        if(remote_thread)WaitForSingleObject(remote_ready,INFINITE);
        if(!remote_hook){remote_mouse_uninstall();SetWindowLongPtr(raw_window,GWLP_WNDPROC,(LONG_PTR)raw_previous_proc);return 0;}
        atexit(remote_mouse_uninstall);remote_mouse_publish();
        fprintf(stderr,"[coop] P1 selected Raw Input gun; P2 injected remote mouse (%s). Coin/Start P2: use your P2 key bindings.\n",input_active_mode==4?"relative":"absolute");
    }
    g_mouse_x=g_mouse_y=-100000;
    fprintf(stderr,"[raw-gun] Windows Raw Input active; absolute coordinates, per-device players, no mouse lock\n");
    return 1;
}
#endif
