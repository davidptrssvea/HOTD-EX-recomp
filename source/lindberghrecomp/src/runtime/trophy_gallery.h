/* Native in-game gallery. The game/audio/guest clock stay inside the pause loop. */
enum { TG_W=1280,TG_H=720,TG_X=327,TG_Y=211,TG_BOTTOM=584,TG_CARD_W=406,TG_CARD_H=168,TG_STEP=180,TG_BACK=15 };
static unsigned long long achievement_unlocked_time(int id);
static int achievement_ending_count(void);
static void pause_quad(float x,float y,float w,float h,int flip);
static int trophy_ids(const PauseMenu *menu,int ids[12])
{
    int count=0;
    for(int id=0;id<12;id++){
        int earned=achievement_is_unlocked(id);
        if(menu->trophy_filter==1 && !earned)continue;
        if(menu->trophy_filter==2 && earned)continue;
        ids[count++]=id;
    }
    return count;
}
static int trophy_scroll_max(const PauseMenu *menu)
{
    int ids[12],count=trophy_ids(menu,ids);
    int max=((count+1)/2)*TG_STEP-12-(TG_BOTTOM-TG_Y);
    return max>0?max:0;
}
static void trophy_clamp(PauseMenu *menu)
{
    int max=trophy_scroll_max(menu);
    if(menu->trophy_scroll<0)menu->trophy_scroll=0;
    if(menu->trophy_scroll>max)menu->trophy_scroll=max;
}
static void trophy_thumb(const PauseMenu *menu,float *top,float *height)
{
    float viewport=TG_BOTTOM-TG_Y,max=trophy_scroll_max(menu);
    *height=max>0?fmaxf(30,viewport*viewport/(viewport+max)):viewport;
    *top=TG_Y+(max>0?(viewport-*height)*menu->trophy_scroll/max:0);
}
static void trophy_coords(SDL_Window *window,int x,int y,float *dx,float *dy)
{
    int w,h;SDL_GetWindowSize(window,&w,&h);
    float scale=fminf(w/(float)TG_W,h/(float)TG_H);
    if(scale<=0){*dx=*dy=-10000;return;}
    *dx=(x-(w-TG_W*scale)/2)/scale;*dy=(y-(h-TG_H*scale)/2)/scale;
}
static int trophy_hit(const PauseMenu *menu,float x,float y)
{
    const int tabs[3][2]={{327,74},{409,112},{529,98}};
    if(y>=171 && y<205)for(int i=0;i<3;i++)if(x>=tabs[i][0] && x<tabs[i][0]+tabs[i][1])return i;
    if(x>=1041 && x<1189 && y>=597 && y<635)return TG_BACK;
    if(x<TG_X || x>=1151 || y<TG_Y || y>=TG_BOTTOM)return -1;
    int column=x>=745;float localx=x-(column?745:TG_X);
    if(localx<0 || localx>=TG_CARD_W)return -1;
    int row=(int)(y-TG_Y+menu->trophy_scroll)/TG_STEP;
    int localy=(int)(y-TG_Y+menu->trophy_scroll)%TG_STEP;
    int ids[12],index=row*2+column,count=trophy_ids(menu,ids);
    return localy<TG_CARD_H && index<count?3+index:-1;
}
static void trophy_set_filter(PauseMenu *menu,int filter)
{
    menu->trophy_filter=(filter+3)%3;menu->trophy_scroll=0;menu->selected=menu->trophy_filter;menu->trophy_drag=0;
}
static void trophy_focus_visible(PauseMenu *menu)
{
    if(menu->selected>=3 && menu->selected<TG_BACK){
        int top=((menu->selected-3)/2)*TG_STEP;
        if(top<menu->trophy_scroll)menu->trophy_scroll=top;
        if(top+TG_CARD_H>menu->trophy_scroll+TG_BOTTOM-TG_Y)menu->trophy_scroll=top+TG_CARD_H-(TG_BOTTOM-TG_Y);
    }
    trophy_clamp(menu);
}
/* 1: redraw; 2: return to pause menu; 0: consumed without change. */
static int trophy_event(PauseMenu *menu,const SDL_Event *event)
{
    int ids[12],count=trophy_ids(menu,ids);
    if(event->type==SDL_WINDOWEVENT)return 1;
    if(event->type==SDL_MOUSEWHEEL){
        int delta=event->wheel.y;if(event->wheel.direction==SDL_MOUSEWHEEL_FLIPPED)delta=-delta;
        menu->trophy_scroll-=delta*130;trophy_clamp(menu);return 1;
    }
    if(event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEBUTTONUP || event->type==SDL_MOUSEMOTION){
        int motion=event->type==SDL_MOUSEMOTION;
        float x,y;trophy_coords(g_win.win,motion?event->motion.x:event->button.x,motion?event->motion.y:event->button.y,&x,&y);
        if(menu->trophy_drag){
            if(event->type==SDL_MOUSEBUTTONUP){menu->trophy_drag=0;return 1;}
            if(motion){
                float top,height;trophy_thumb(menu,&top,&height);
                float span=TG_BOTTOM-TG_Y-height;
                if(span>0)menu->trophy_scroll=(int)((y-menu->trophy_drag_offset-TG_Y)*trophy_scroll_max(menu)/span);
                trophy_clamp(menu);return 1;
            }
        }
        if(event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT && x>=1167 && x<1189 && y>=TG_Y && y<TG_BOTTOM && trophy_scroll_max(menu)>0){
            float top,height;trophy_thumb(menu,&top,&height);
            menu->trophy_drag=1;menu->trophy_drag_offset=(y>=top && y<top+height)?y-top:height/2;
            float span=TG_BOTTOM-TG_Y-height;
            if(span>0)menu->trophy_scroll=(int)((y-menu->trophy_drag_offset-TG_Y)*trophy_scroll_max(menu)/span);
            trophy_clamp(menu);return 1;
        }
        int hit=trophy_hit(menu,x,y);
        if(hit<0)return 0;
        int changed=menu->selected!=hit;menu->selected=hit;
        if(event->type==SDL_MOUSEBUTTONUP && event->button.button==SDL_BUTTON_LEFT){
            if(hit<3){trophy_set_filter(menu,hit);return 1;}
            if(hit==TG_BACK)return 2;
        }
        return changed;
    }
    if(event->type!=SDL_KEYDOWN)return 0;
    SDL_Keycode key=event->key.keysym.sym;
    if(key==SDLK_ESCAPE)return event->key.repeat?0:2;
    if(key==SDLK_PAGEDOWN || key==SDLK_PAGEUP){menu->trophy_scroll+=(key==SDLK_PAGEDOWN?1:-1)*(TG_BOTTOM-TG_Y);trophy_clamp(menu);return 1;}
    if(key==SDLK_HOME || key==SDLK_END){menu->trophy_scroll=key==SDLK_HOME?0:trophy_scroll_max(menu);return 1;}
    if(key==SDLK_TAB){
        int slots[16],n=0;for(int i=0;i<3+count;i++)slots[n++]=i;slots[n++]=TG_BACK;
        int at=0;for(int i=0;i<n;i++)if(slots[i]==menu->selected)at=i;
        menu->selected=slots[(at+n+((event->key.keysym.mod&KMOD_SHIFT)?-1:1))%n];trophy_focus_visible(menu);return 1;
    }
    if(key==SDLK_LEFT || key==SDLK_RIGHT){
        int delta=key==SDLK_LEFT?-1:1;
        if(menu->selected<3)trophy_set_filter(menu,menu->trophy_filter+delta);
        else if(menu->selected<TG_BACK){int index=menu->selected-3+delta;if(index>=0 && index<count)menu->selected=3+index;trophy_focus_visible(menu);}
        return 1;
    }
    if(key==SDLK_UP || key==SDLK_DOWN){
        int down=key==SDLK_DOWN;
        if(menu->selected<3)menu->selected=down && count?3:TG_BACK;
        else if(menu->selected==TG_BACK)menu->selected=!down && count?count+2:menu->trophy_filter;
        else {int index=menu->selected-3+(down?2:-2);menu->selected=index<0?menu->trophy_filter:index>=count?TG_BACK:index+3;}
        trophy_focus_visible(menu);return 1;
    }
    if((key==SDLK_RETURN || key==SDLK_KP_ENTER || key==SDLK_SPACE) && !event->key.repeat){
        if(menu->selected==TG_BACK)return 2;
        if(menu->selected<3){trophy_set_filter(menu,menu->selected);return 1;}
    }
    return 0;
}
static void trophy_box(HDC dc,int x,int y,int w,int h,int radius,COLORREF fill,COLORREF edge)
{
    HBRUSH brush=CreateSolidBrush(fill);HPEN pen=CreatePen(PS_SOLID,1,edge);
    HGDIOBJ oldb=SelectObject(dc,brush),oldp=SelectObject(dc,pen);
    RoundRect(dc,x,y,x+w,y+h,radius*2,radius*2);
    SelectObject(dc,oldb);SelectObject(dc,oldp);DeleteObject(brush);DeleteObject(pen);
}
static void trophy_right(HDC dc,HFONT font,int x,int y,const wchar_t *text,COLORREF color)
{
    SelectObject(dc,font);SIZE size;GetTextExtentPoint32W(dc,text,(int)wcslen(text),&size);pause_text(dc,font,x-size.cx,y,text,color);
}
static void trophy_date(int id,wchar_t out[64])
{
    out[0]=0;unsigned long long time=achievement_unlocked_time(id);if(!time)return;
    ULARGE_INTEGER ticks;ticks.QuadPart=time*10000000ull+116444736000000000ull;
    FILETIME utc={ticks.LowPart,ticks.HighPart},local;SYSTEMTIME calendar;
    if(FileTimeToLocalFileTime(&utc,&local) && FileTimeToSystemTime(&local,&calendar))
        swprintf(out,64,L"%04u-%02u-%02u %02u:%02u",calendar.wYear,calendar.wMonth,calendar.wDay,calendar.wHour,calendar.wMinute);
}
static int trophy_gallery_panel(PauseMenu *menu)
{
    int dw,dh;SDL_GL_GetDrawableSize(g_win.win,&dw,&dh);
    float factor=fminf(4,fmaxf(.5f,fminf(dw/1280.0f,dh/720.0f)));
    int width=(int)ceilf(TG_W*factor),height=(int)ceilf(TG_H*factor);
    BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(NULL);if(!dc)return 0;
    void *pixels=NULL;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0);
    HFONT fonts[8];const int sizes[]={34,12,17,13,11,10,43,20};
    int valid=bitmap && pixels;
    for(int i=0;i<8;i++){fonts[i]=CreateFontW(-sizes[i],0,0,0,(i==0 || i==6)?FW_BOLD:(i==2 || i==4 || i==7)?FW_SEMIBOLD:FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");valid=valid && fonts[i];}
    if(valid){
        memset(pixels,0,(size_t)width*height*4);
        HGDIOBJ oldbitmap=SelectObject(dc,bitmap),oldfont=GetCurrentObject(dc,OBJ_FONT);
        SetGraphicsMode(dc,GM_ADVANCED);XFORM transform={factor,0,0,factor,0,0};SetWorldTransform(dc,&transform);SetBkMode(dc,TRANSPARENT);
        COLORREF white=RGB(223,228,236),muted=RGB(165,173,187),gold=RGB(233,195,138);
        trophy_box(dc,64,60,1152,600,0,RGB(11,14,20),RGB(104,96,82));
        pause_text(dc,fonts[1],91,85,L"THE HOUSE OF THE DEAD 4",RGB(182,165,139));
        pause_text(dc,fonts[0],91,107,L"TROPHIES",white);pause_fill(dc,91,149,52,2,RGB(188,69,68));
        int earned=achievement_count(),bronze=0;for(int i=0;i<10;i++)bronze+=achievement_is_unlocked(i);
        wchar_t text[128];swprintf(text,128,L"%d / 12 unlocked",earned);trophy_right(dc,fonts[2],1188,94,text,gold);
        trophy_right(dc,fonts[3],1188,122,L"Your collection - Saved automatically",muted);
        trophy_box(dc,91,165,212,419,5,RGB(20,26,36),RGB(52,62,78));
        pause_text(dc,fonts[1],106,181,L"COLLECTION",muted);swprintf(text,128,L"%d / 12",earned);pause_text(dc,fonts[6],106,208,text,white);
        pause_fill(dc,106,259,180,5,RGB(42,48,59));pause_fill(dc,106,259,180*earned/12,5,RGB(225,178,103));
        swprintf(text,128,L"%d%% complete",(int)lroundf(earned*100.0f/12));pause_text(dc,fonts[3],106,275,text,gold);
        const wchar_t *metals[]={L"BRONZE",L"SILVER",L"GOLD"};COLORREF colors[]={RGB(214,155,112),RGB(197,210,225),RGB(234,196,125)};
        int counts[]={bronze,achievement_is_unlocked(11),achievement_is_unlocked(10)};
        for(int i=0;i<3;i++){pause_text(dc,fonts[4],162,309+i*54,metals[i],colors[i]);swprintf(text,128,L"%d / %d",counts[i],i?1:10);pause_text(dc,fonts[7],162,325+i*54,text,white);}
        pause_fill(dc,106,484,180,1,RGB(52,62,78));pause_text(dc,fonts[4],106,498,L"PANDORA'S BOX",muted);
        swprintf(text,128,L"%d / 4 endings seen",achievement_ending_count());pause_text(dc,fonts[3],106,521,text,white);
        const int tabs[3][2]={{327,74},{409,112},{529,98}};
        for(int i=0;i<3;i++){
            int active=menu->trophy_filter==i;
            trophy_box(dc,tabs[i][0],171,tabs[i][1],34,3,active?RGB(104,76,43):RGB(27,32,43),menu->selected==i?RGB(247,219,162):active?RGB(222,180,118):RGB(65,70,83));
            swprintf(text,128,i==0?L"All (12)":i==1?L"Unlocked (%d)":L"Locked (%d)",i==1?earned:12-earned);
            SelectObject(dc,fonts[3]);SetTextColor(dc,active?RGB(255,240,209):white);RECT rect={tabs[i][0],171,tabs[i][0]+tabs[i][1],205};DrawTextW(dc,text,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        int ids[12],count=trophy_ids(menu,ids);int saved=SaveDC(dc);IntersectClipRect(dc,TG_X,TG_Y,1152,TG_BOTTOM);
        for(int i=0;i<count;i++){
            int id=ids[i],unlocked=achievement_is_unlocked(id),x=TG_X+(i%2)*418,y=TG_Y+(i/2)*TG_STEP-menu->trophy_scroll;
            if(y+TG_CARD_H<TG_Y || y>=TG_BOTTOM)continue;
            trophy_box(dc,x,y,TG_CARD_W,TG_CARD_H,5,unlocked?RGB(32,35,43):RGB(19,25,35),menu->selected==i+3?RGB(233,195,138):unlocked?RGB(169,137,84):RGB(48,58,73));
            SelectObject(dc,fonts[2]);SetTextColor(dc,unlocked?RGB(246,212,155):white);RECT title={x+79,y+19,x+393,y+45};DrawTextW(dc,achievement_names[id],-1,&title,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
            pause_text(dc,fonts[4],x+79,y+46,unlocked?L"UNLOCKED":L"LOCKED",unlocked?RGB(215,179,120):RGB(141,153,173));
            SelectObject(dc,fonts[3]);SetTextColor(dc,RGB(185,194,208));RECT desc={x+13,y+85,x+393,y+135};DrawTextW(dc,achievement_descriptions[id],-1,&desc,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
            int metal=id==10?2:id==11?1:0;swprintf(text,128,L"%ls STAR",metals[metal]);pause_text(dc,fonts[5],x+13,y+145,text,colors[metal]);
            trophy_date(id,text);trophy_right(dc,fonts[5],x+393,y+145,text,RGB(152,165,184));
        }
        RestoreDC(dc,saved);
        if(!count){
            const wchar_t *empty=menu->trophy_filter==2?L"All trophies unlocked":L"No trophies unlocked yet";
            const wchar_t *hint=menu->trophy_filter==2?L"Your base-game collection is complete.":L"Clear a chapter to earn your first star.";
            RECT rect={TG_X,432,1151,464};SelectObject(dc,fonts[7]);SetTextColor(dc,white);DrawTextW(dc,empty,-1,&rect,DT_CENTER|DT_SINGLELINE);
            rect.top=474;rect.bottom=496;SelectObject(dc,fonts[3]);SetTextColor(dc,muted);DrawTextW(dc,hint,-1,&rect,DT_CENTER|DT_SINGLELINE);
        }
        if(trophy_scroll_max(menu)>0){float top,size;trophy_thumb(menu,&top,&size);pause_fill(dc,1171,TG_Y,17,TG_BOTTOM-TG_Y,RGB(20,25,35));trophy_box(dc,1171,(int)top,8,(int)size,4,RGB(132,113,87),RGB(132,113,87));}
        pause_text(dc,fonts[3],91,612,L"Tab / Enter: select   Arrows / Wheel / PgUp / PgDn: browse",muted);
        trophy_box(dc,1041,597,148,38,0,RGB(22,25,32),menu->selected==TG_BACK?gold:RGB(65,70,83));
        SelectObject(dc,fonts[2]);SetTextColor(dc,white);RECT back={1041,597,1189,635};DrawTextW(dc,L"Back",-1,&back,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        GdiFlush();
        unsigned char *bytes=pixels;
        for(int y=(int)ceilf(60*factor);y<(int)ceilf(660*factor) && y<height;y++)for(int x=(int)ceilf(64*factor);x<(int)ceilf(1216*factor) && x<width;x++)bytes[((size_t)y*width+x)*4+3]=255;
        glBindTexture(GL_TEXTURE_2D,menu->panel);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_BGRA,GL_UNSIGNED_BYTE,pixels);
        SelectObject(dc,oldfont);SelectObject(dc,oldbitmap);
    }
    for(int i=0;i<8;i++)if(fonts[i])DeleteObject(fonts[i]);if(bitmap)DeleteObject(bitmap);DeleteDC(dc);return valid;
}
static void trophy_gallery_draw(const PauseMenu *menu,int w,int h)
{
    float scale=fminf(w/(float)TG_W,h/(float)TG_H),left=(w-TG_W*scale)/2,top=(h-TG_H*scale)/2;
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,menu->panel);glColor4f(1,1,1,1);pause_quad(left,top,TG_W*scale,TG_H*scale,0);
    int counts[3]={0,achievement_is_unlocked(11),achievement_is_unlocked(10)};for(int i=0;i<10;i++)counts[0]+=achievement_is_unlocked(i);
    for(int i=0;i<3;i++){GLuint star=achievement_star_texture(i);if(star){glBindTexture(GL_TEXTURE_2D,star);glColor4f(1,1,1,counts[i]?1:.38f);pause_quad(left+106*scale,top+(305+i*54)*scale,42*scale,42*scale,0);}}
    int ids[12],count=trophy_ids(menu,ids);
    glEnable(GL_SCISSOR_TEST);glScissor((int)(left+TG_X*scale),(int)(h-top-TG_BOTTOM*scale),(int)((1152-TG_X)*scale),(int)((TG_BOTTOM-TG_Y)*scale));
    for(int i=0;i<count;i++){
        int y=TG_Y+(i/2)*TG_STEP-menu->trophy_scroll;if(y+TG_CARD_H<TG_Y || y>=TG_BOTTOM)continue;
        int id=ids[i];GLuint star=achievement_star_texture(id==10?2:id==11?1:0);
        if(star){glBindTexture(GL_TEXTURE_2D,star);glColor4f(1,1,1,achievement_is_unlocked(id)?1:.28f);pause_quad(left+(TG_X+(i%2)*418+13)*scale,top+(y+13)*scale,56*scale,56*scale,0);}
    }
    if(!count){GLuint star=achievement_star_texture(0);if(star){glBindTexture(GL_TEXTURE_2D,star);glColor4f(1,1,1,.3f);pause_quad(left+695*scale,top+326*scale,88*scale,88*scale,0);}}
    glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glColor4f(1,1,1,1);
}
