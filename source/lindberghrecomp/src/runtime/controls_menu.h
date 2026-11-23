static int controls_hit(float x,float y)
{
    if(y>=600&&y<638){if(x>=91&&x<269)return 16;if(x>=1039&&x<1188)return 17;if(x>=860&&x<1010)return 18;}
    for(int i=0;i<HC_COUNT;i++){int px=91+(i/8)*558,py=174+(i%8)*46;if(x>=px&&x<px+539&&y>=py&&y<py+40)return i;}
    return -1;
}
static int controls_panel(PauseMenu*m)
{
    int dw,dh;SDL_GL_GetDrawableSize(g_win.win,&dw,&dh);float factor=fminf(4,fmaxf(.5f,fminf(dw/1280.f,dh/720.f)));
    int w=(int)ceilf(1280*factor),h=(int)ceilf(720*factor);BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(NULL);if(!dc)return 0;void*pixels=NULL;HBITMAP bm=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0);
    HFONT title=CreateFontW(-32,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI"),font=CreateFontW(-16,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI"),small=CreateFontW(-13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");
    int valid=bm&&pixels&&title&&font&&small;
    if(valid){memset(pixels,0,(size_t)w*h*4);HGDIOBJ old=SelectObject(dc,bm),oldfont=SelectObject(dc,font);SetGraphicsMode(dc,GM_ADVANCED);XFORM xf={factor,0,0,factor,0,0};SetWorldTransform(dc,&xf);SetBkMode(dc,TRANSPARENT);
        trophy_box(dc,64,60,1152,600,0,RGB(11,14,20),RGB(104,96,82));pause_text(dc,title,91,90,L"KEY BINDINGS",RGB(229,232,239));pause_text(dc,small,91,135,L"Changes apply immediately and are saved automatically.",RGB(165,173,187));
        for(int i=0;i<HC_COUNT;i++){int x=91+(i/8)*558,y=174+(i%8)*46;wchar_t name[96];hc_name(i,name,96);int sel=m->selected==i;
            trophy_box(dc,x,y,539,40,3,sel?RGB(70,43,35):RGB(25,30,40),sel?RGB(230,177,104):RGB(58,64,78));pause_text(dc,font,x+12,y+10,hc_labels[i],RGB(225,228,235));trophy_right(dc,font,x+525,y+10,m->binding_capture==i?L"Press a key...":name,RGB(237,199,141));
        }
        const wchar_t *hint=m->binding_capture>=0?L"Press a key. Esc cancels (or binds Pause). Mouse buttons are accepted for the four shooting actions.":L"Select an action and press Enter or click its row. Tab / arrow keys navigate. Esc goes back.";
        pause_text(dc,small,91,555,m->binding_message[0]?m->binding_message:hint,RGB(199,185,160));
        const int x[3]={91,1039,860},width[3]={178,149,150};const wchar_t*names[3]={L"Reset Defaults",L"Back",L"Cancel"};
        for(int i=0;i<3;i++){if(i==2&&m->binding_capture<0)continue;trophy_box(dc,x[i],600,width[i],38,0,RGB(29,34,43),m->selected==16+i?RGB(233,195,138):RGB(65,70,83));pause_text(dc,font,x[i]+12,609,names[i],RGB(225,228,235));}
        GdiFlush();for(int y=0;y<h;y++)for(int x=0;x<w;x++)((unsigned char*)pixels)[((size_t)y*w+x)*4+3]=(x>=64*factor&&x<1216*factor&&y>=60*factor&&y<660*factor)?255:0;
        glBindTexture(GL_TEXTURE_2D,m->panel);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_BGRA,GL_UNSIGNED_BYTE,pixels);SelectObject(dc,oldfont);SelectObject(dc,old);
    }
    if(title)DeleteObject(title);if(font)DeleteObject(font);if(small)DeleteObject(small);if(bm)DeleteObject(bm);DeleteDC(dc);return valid;
}
static void controls_draw(const PauseMenu*m,int w,int h)
{
    float scale=fminf(w/1280.f,h/720.f);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(1,1,1,1);glBindTexture(GL_TEXTURE_2D,m->panel);pause_quad((w-1280*scale)/2,(h-720*scale)/2,1280*scale,720*scale,0);glDisable(GL_BLEND);
}
static int controls_event(PauseMenu*m,const SDL_Event*e)
{
    int hit=-1,value=0,activate=0;
    if(e->type==SDL_MOUSEBUTTONDOWN||e->type==SDL_MOUSEBUTTONUP||e->type==SDL_MOUSEMOTION){float x,y;int px=e->type==SDL_MOUSEMOTION?e->motion.x:e->button.x,py=e->type==SDL_MOUSEMOTION?e->motion.y:e->button.y;trophy_coords(g_win.win,px,py,&x,&y);hit=controls_hit(x,y);}
    if(m->binding_capture>=0){
        if(hit==18&&e->type==SDL_MOUSEBUTTONDOWN){m->binding_capture=-1;m->binding_message[0]=0;return 1;}
        if(e->type==SDL_KEYDOWN&&!e->key.repeat){value=e->key.keysym.sym;if(value==SDLK_ESCAPE&&m->binding_capture!=HC_PAUSE){m->binding_capture=-1;m->binding_message[0]=0;return 1;}}
        if(e->type==SDL_MOUSEBUTTONDOWN)value=MOUSE_BIND(e->button.button);
        if(!value)return 0;
        int result=hc_set(m->binding_capture,value);
        if(result==-1)wcscpy(m->binding_message,L"Choose a keyboard key for this action.");
        else if(result==-2)wcscpy(m->binding_message,L"Could not save the controls profile. Check the installation folder is writable.");
        else if(result>0)swprintf(m->binding_message,200,L"Already assigned to %ls. Choose another key.",hc_labels[result-1]);
        else {m->binding_capture=-1;wcscpy(m->binding_message,L"Binding saved.");}
        return 1;
    }
    if(e->type==SDL_MOUSEMOTION&&hit>=0&&hit<18){m->selected=hit;return 1;}
    if(e->type==SDL_MOUSEBUTTONUP&&e->button.button==SDL_BUTTON_LEFT&&hit>=0&&hit<18){m->selected=hit;activate=1;}
    if(e->type==SDL_KEYDOWN&&!e->key.repeat){SDL_Keycode k=e->key.keysym.sym;if(k==SDLK_ESCAPE)return 2;
        if(k==SDLK_TAB||k==SDLK_DOWN)m->selected=(m->selected+1)%18;
        else if(k==SDLK_UP)m->selected=(m->selected+17)%18;
        else if(k==SDLK_LEFT||k==SDLK_RIGHT)m->selected=m->selected<16?(m->selected+8)%16:33-m->selected;
        else if(k==SDLK_RETURN||k==SDLK_KP_ENTER)activate=1;
        else return 0;
    }
    if(activate){if(m->selected==17)return 2;if(m->selected==16){wcscpy(m->binding_message,hc_reset()==0?L"Default bindings restored.":L"Could not save the controls profile.");}else{m->binding_capture=m->selected;m->binding_message[0]=0;}}
    return e->type==SDL_KEYDOWN||activate;
}
