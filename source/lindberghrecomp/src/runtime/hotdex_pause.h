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
/* Save the completed back buffer before the menu takes focus. BMP rows are
 * bottom-up, like OpenGL, and WPF can read a BMP from a stream. */
static int hotdex_save_pause_frame(const WCHAR *path, int width, int height)
{
    if(width<=0 || height<=0)return 0;
    const size_t stride=((size_t)width*3+3)&~(size_t)3;
    BITMAPFILEHEADER file={0};
    BITMAPINFOHEADER info={0};
    const DWORD offset=sizeof file+sizeof info;
    if(stride>(MAXDWORD-offset)/(size_t)height)return 0;
    const size_t size=stride*(size_t)height;
    unsigned char *pixels=calloc(1,size);
    if(!pixels)return 0;

    GLint pack_buffer=0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack_buffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPushAttrib(GL_PIXEL_MODE_BIT);
    glPixelStorei(GL_PACK_ALIGNMENT,4);
    glPixelStorei(GL_PACK_ROW_LENGTH,0);
    glPixelStorei(GL_PACK_SKIP_ROWS,0);
    glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    glReadBuffer(GL_BACK);
    glReadPixels(0,0,width,height,GL_BGR,GL_UNSIGNED_BYTE,pixels);
    glPopAttrib();
    glPopClientAttrib();
    glBindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)pack_buffer);

    file.bfType=0x4d42;
    file.bfSize=offset+(DWORD)size;
    file.bfOffBits=offset;
    info.biSize=sizeof info;
    info.biWidth=width;
    info.biHeight=height;
    info.biPlanes=1;
    info.biBitCount=24;
    info.biCompression=BI_RGB;
    info.biSizeImage=(DWORD)size;
    FILE *stream=_wfopen(path,L"wb");
    int ok=0;
    if(stream){
        ok=fwrite(&file,sizeof file,1,stream)==1 &&
           fwrite(&info,sizeof info,1,stream)==1 &&
           fwrite(pixels,1,size,stream)==size;
        if(fclose(stream)!=0)ok=0;
    }
    free(pixels);
    return ok;
}

static int hotdex_pause_loop(int width,int height)
{
    WCHAR app[MAX_PATH],script[MAX_PATH],command[4096],powershell[MAX_PATH];
    GetModuleFileNameW(NULL,app,MAX_PATH);WCHAR*slash=wcsrchr(app,L'\\');if(!slash)return 0;*slash=0;
    swprintf(script,MAX_PATH,L"%ls\\config_menu.ps1",app);
    if(GetFileAttributesW(script)==INVALID_FILE_ATTRIBUTES){fprintf(stderr,"[hotdex-pause] missing config_menu.ps1\n");return 0;}
    GetSystemDirectoryW(powershell,MAX_PATH);wcscat(powershell,L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    WCHAR temp[MAX_PATH],snapshot[MAX_PATH]=L"";
    const DWORD temp_length=GetTempPathW(MAX_PATH,temp);
    if(temp_length && temp_length<MAX_PATH && GetTempFileNameW(temp,L"hdx",0,snapshot)){
        if(!hotdex_save_pause_frame(snapshot,width,height)){
            DeleteFileW(snapshot);
            snapshot[0]=0;
        }
    }
    swprintf(command,4096,L"\"%ls\" -NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File \"%ls\" -InGame -GameWindow %llu%ls%ls%ls",
             powershell,script,(unsigned long long)(uintptr_t)raw_window,
             *snapshot?L" -PauseFrame \"":L"",snapshot,*snapshot?L"\"":L"");
    STARTUPINFOW si={.cb=sizeof si};PROCESS_INFORMATION child={0};
    SDL_bool relative=SDL_GetRelativeMouseMode(),grab=SDL_GetWindowGrab(g_win.win);int cursor=SDL_ShowCursor(SDL_QUERY);
    SDL_SetRelativeMouseMode(SDL_FALSE);SDL_SetWindowGrab(g_win.win,SDL_FALSE);SDL_ShowCursor(SDL_ENABLE);
    raw_suspended=1;raw_release();host_guest_clock_pause(1);host_audio_pause_devices(1);
    /* Present the same frame in the game window, then leave its clock stopped
     * while WPF displays the captured frame in the settings menu. */
    SDL_GL_SwapWindow(g_win.win);
    fprintf(stderr,"[hotdex-pause] opened at frame %u (%s)\n",g_frame,*snapshot?"frozen game frame":"background image unavailable");fflush(stderr);
    DWORD result=0;
    if(CreateProcessW(powershell,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,app,&si,&child)){
        CloseHandle(child.hThread);
        const SDL_bool background=SDL_GetHintBoolean(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,SDL_FALSE);
        if(input_gamepad_active)SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
        while(WaitForSingleObject(child.hProcess,20)==WAIT_TIMEOUT){
            SDL_Event event;
            while(SDL_PollEvent(&event)){
                if(event.type==SDL_QUIT){
                    fprintf(stderr,"[hotdex-pause] game window closed while paused\n");
                    TerminateProcess(child.hProcess,0);
                    result=77;
                    break;
                }
                if(event.type==SDL_CONTROLLERDEVICEADDED || event.type==SDL_CONTROLLERDEVICEREMOVED)pad_event(&event);
                if(!input_gamepad_active || event.type!=SDL_CONTROLLERBUTTONDOWN)continue;
                const int key=vk_of(gamepad_menu_key(event.cbutton.button));
                if(!key)continue;
                const HWND menu=GetForegroundWindow();
                DWORD pid=0;
                GetWindowThreadProcessId(menu,&pid);
                if(pid!=child.dwProcessId)continue;
                const LPARAM scan=1 | ((LPARAM)MapVirtualKeyW((UINT)key,MAPVK_VK_TO_VSC) << 16);
                PostMessageW(menu,WM_KEYDOWN,(WPARAM)key,scan);
                PostMessageW(menu,WM_KEYUP,(WPARAM)key,scan | (LPARAM)0xc0000000u);
            }
        }
        if(input_gamepad_active)SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,background?"1":"0");
        if(result!=77)GetExitCodeProcess(child.hProcess,&result);CloseHandle(child.hProcess);
    }else fprintf(stderr,"[hotdex-pause] UI launch failed: Windows error %lu\n",GetLastError());
    if(*snapshot)DeleteFileW(snapshot);
    hotdex_reload_preferences();
    host_audio_pause_devices(0);host_guest_clock_pause(0);raw_suspended=0;raw_release();
    g_held=g_pad_held=g_cab_latch=g_buttons=0;g_in_tail=g_in_head;
    if(input_gamepad_active)gamepad_clear();
    SDL_FlushEvents(SDL_KEYDOWN,SDL_MOUSEWHEEL);
    SDL_SetWindowGrab(g_win.win,grab);SDL_SetRelativeMouseMode(relative);if(cursor>=0)SDL_ShowCursor(cursor);
    if(result!=74 && result!=75 && result!=76 && result!=77)result=0;
    fprintf(stderr,"[hotdex-pause] closed at frame %u (action %lu)\n",g_frame,result);fflush(stderr);
    return (int)result;
}
