/* EX-only pause bridge. No HOTD4 guest-memory addresses or game artwork. */
static int hotdex_extra_loaded,hotdex_p2_bind[3];
static void hotdex_extra_controls(void)
{
    if(hotdex_extra_loaded)return;hotdex_extra_loaded=1;hc_init();
    hotdex_p2_bind[0]=SDLK_KP_0;hotdex_p2_bind[1]=SDLK_KP_PERIOD;hotdex_p2_bind[2]=SDLK_RCTRL;
    FILE*f=fopen(hc_path,"rb");if(!f)return;char name[64],line[256];int value;
    while(fgets(line,sizeof line,f)){if(sscanf(line,"%63s %d",name,&value)!=2)continue;const char*names[]={"trigger2","reload2","action2"};for(int i=0;i<3;i++)if(!strcmp(name,names[i]))hotdex_p2_bind[i]=value;}
    fclose(f);
}
static int hotdex_extra_input(int bind,int down)
{
    if(!hotdex_desktop())return 0;hotdex_extra_controls();int hit=0;
    const unsigned bits[]={CAB_P2_TRIGGER,CAB_P2_RELOAD,CAB_P2_ACTION};
    for(int i=0;i<3;i++)if(bind && hotdex_p2_bind[i]==bind){if(down){g_held|=bits[i];g_cab_latch|=bits[i];}else g_held&=~bits[i];hit=1;}
    return hit;
}
static void hotdex_reload_preferences(void)
{
    HostSettings active=*settings();settings_reload();
    settings()->window_w=active.window_w;settings()->window_h=active.window_h;
    settings()->aa=active.aa;settings()->fullscreen=active.fullscreen;
    settings()->raw_mouse=active.raw_mouse;settings()->mouse_grab=active.mouse_grab;
    /* Device ownership/relative mode changes are applied by Restart Arcade. */
    hc_loaded=0;hc_init();hotdex_extra_loaded=0;
    sinden_enabled=-1;sinden_width=0;gameplay_options_load();
    input_mouse_speed=settings()->mouse_sens;
    fprintf(stderr,"[hotdex-pause] saved controls, border and audio reloaded; display/device changes wait for restart\n");
}
static int hotdex_pause_loop(int width,int height)
{
    (void)width;(void)height;
    WCHAR app[MAX_PATH],script[MAX_PATH],command[4096],powershell[MAX_PATH];
    GetModuleFileNameW(NULL,app,MAX_PATH);WCHAR*slash=wcsrchr(app,L'\\');if(!slash)return 0;*slash=0;
    swprintf(script,MAX_PATH,L"%ls\\config_menu.ps1",app);
    if(GetFileAttributesW(script)==INVALID_FILE_ATTRIBUTES){fprintf(stderr,"[hotdex-pause] missing config_menu.ps1\n");return 0;}
    GetSystemDirectoryW(powershell,MAX_PATH);wcscat(powershell,L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    swprintf(command,4096,L"\"%ls\" -NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File \"%ls\" -InGame -GameWindow %llu",powershell,script,(unsigned long long)(uintptr_t)raw_window);
    STARTUPINFOW si={.cb=sizeof si};PROCESS_INFORMATION child={0};
    SDL_bool relative=SDL_GetRelativeMouseMode(),grab=SDL_GetWindowGrab(g_win.win);int cursor=SDL_ShowCursor(SDL_QUERY);
    SDL_SetRelativeMouseMode(SDL_FALSE);SDL_SetWindowGrab(g_win.win,SDL_FALSE);SDL_ShowCursor(SDL_ENABLE);
    raw_suspended=1;raw_release();host_guest_clock_pause(1);host_audio_pause_devices(1);
    /* Present this completed frame once. WPF's translucent owned window uses
     * the frozen game beneath it, without Windows screen-capture APIs. */
    SDL_GL_SwapWindow(g_win.win);
    fprintf(stderr,"[hotdex-pause] opened at frame %u\n",g_frame);fflush(stderr);
    DWORD result=0;
    if(CreateProcessW(powershell,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,app,&si,&child)){
        CloseHandle(child.hThread);
        while(WaitForSingleObject(child.hProcess,20)==WAIT_TIMEOUT){
            SDL_Event event;while(SDL_PollEvent(&event))if(event.type==SDL_QUIT){TerminateProcess(child.hProcess,0);result=77;}
        }
        if(result!=77)GetExitCodeProcess(child.hProcess,&result);CloseHandle(child.hProcess);
    }else fprintf(stderr,"[hotdex-pause] UI launch failed: Windows error %lu\n",GetLastError());
    hotdex_reload_preferences();
    host_audio_pause_devices(0);host_guest_clock_pause(0);raw_suspended=0;raw_release();
    g_held=g_pad_held=g_cab_latch=g_buttons=0;g_in_tail=g_in_head;
    SDL_FlushEvents(SDL_KEYDOWN,SDL_MOUSEWHEEL);
    SDL_SetWindowGrab(g_win.win,grab);SDL_SetRelativeMouseMode(relative);if(cursor>=0)SDL_ShowCursor(cursor);
    if(result!=74 && result!=75 && result!=76 && result!=77)result=0;
    fprintf(stderr,"[hotdex-pause] closed at frame %u (action %lu)\n",g_frame,result);fflush(stderr);
    return (int)result;
}
