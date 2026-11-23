/* Native score display. The arcade remains the owner of ranking and saving. */
static void pause_texture_setup(GLuint texture);
static void pause_quad(float x,float y,float w,float h,int flip);
static int high_score_display=-1;
static int high_scores_enabled(void)
{
    if(high_score_display<0){char value[8];high_score_display=1;
        if(input_read("high-score-display.txt",value,sizeof value) && !strcmp(value,"0"))high_score_display=0;}
    return high_score_display;
}
static void high_scores_toggle(void)
{
    high_score_display=!high_scores_enabled();input_write("high-score-display.txt",high_score_display?"1":"0");
}
static int high_scores_snapshot(HighScoreState *state)
{
    memset(state,0,sizeof *state);
    return high_score_reader && high_score_reader(state) && state->count==HOTD4_SCORE_ROWS;
}
static int high_scores_texture(GLuint texture,const HighScoreState *state,int compact)
{
    int width=compact?246:PANEL_W,height=compact?66:PANEL_H;
    BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(NULL);if(!dc)return 0;
    void *pixels=NULL;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0);
    HFONT title=CreateFontW(compact?-15:-28,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");
    HFONT number=CreateFontW(compact?-28:-22,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Consolas");
    HFONT small=CreateFontW(-14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");
    int ok=bitmap && pixels && title && number && small;
    if(ok){
        HGDIOBJ old_bitmap=SelectObject(dc,bitmap),old_font=GetCurrentObject(dc,OBJ_FONT);SetBkMode(dc,TRANSPARENT);
        pause_fill(dc,0,0,width,height,RGB(17,19,25));pause_fill(dc,0,0,width,3,RGB(207,162,84));
        if(compact){
            wchar_t score[32];swprintf(score,32,L"%08u",state->rows[0].score);
            pause_text(dc,title,16,9,L"HI-SCORE",RGB(224,186,115));
            pause_text(dc,number,15,28,score,RGB(248,245,237));
        }else{
            pause_text(dc,title,31,27,L"High Scores",RGB(248,245,237));
            pause_text(dc,small,32,70,state->practice?L"Practice copy - your arcade records stay unchanged.":L"Your arcade ranking - saved locally.",RGB(176,181,193));
            pause_text(dc,small,46,108,L"RANK",RGB(179,185,198));pause_text(dc,small,132,108,L"NAME",RGB(179,185,198));
            pause_text(dc,small,455,108,L"SCORE",RGB(179,185,198));
            if(state->count){for(int i=0;i<state->count;i++){
                int y=136+i*31;wchar_t rank[12],name[8],score[32];
                swprintf(rank,12,L"%02d",i+1);swprintf(name,8,L"%hs",state->rows[i].initials);swprintf(score,32,L"%08u",state->rows[i].score);
                pause_fill(dc,31,y-2,PANEL_W-62,29,i==0?RGB(53,44,30):i%2?RGB(24,27,35):RGB(29,32,41));
                COLORREF color=i==0?RGB(240,201,129):RGB(230,233,240);
                pause_text(dc,number,46,y,rank,color);pause_text(dc,number,131,y,name,color);
                SelectObject(dc,number);SetTextColor(dc,color);RECT right={300,y,550,y+28};DrawTextW(dc,score,-1,&right,DT_RIGHT|DT_SINGLELINE|DT_NOPREFIX);
            }}else pause_text(dc,small,45,161,L"Ranking is loading. Resume the game and try again.",RGB(195,200,212));
            pause_text(dc,small,32,464,L"Qualify for the ranking and confirm your initials to save a score.",RGB(171,178,190));
            pause_fill(dc,31,499,PANEL_W-62,35,RGB(116,34,39));pause_text(dc,small,49,507,L"Back   -   Enter / Esc",RGB(250,245,240));
        }
        GdiFlush();pause_texture_setup(texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,width,height,0,GL_BGRA,GL_UNSIGNED_BYTE,pixels);
        SelectObject(dc,old_font);SelectObject(dc,old_bitmap);
    }
    if(title)DeleteObject(title);if(number)DeleteObject(number);if(small)DeleteObject(small);
    if(bitmap)DeleteObject(bitmap);DeleteDC(dc);return ok;
}
static int high_scores_panel(PauseMenu *menu)
{
    HighScoreState state;high_scores_snapshot(&state);return high_scores_texture(menu->panel,&state,0);
}
static void high_scores_draw(PauseMenu *menu,int w,int h)
{
    float scale=fminf(w/960.0f,h/650.0f);glColor4f(1,1,1,1);glBindTexture(GL_TEXTURE_2D,menu->panel);
    pause_quad((w-PANEL_W*scale)/2,(h-PANEL_H*scale)/2,PANEL_W*scale,PANEL_H*scale,0);
}
static int high_scores_back(const SDL_Event *event)
{
    if(event->type==SDL_KEYDOWN && !event->key.repeat &&
       (event->key.keysym.sym==SDLK_RETURN || event->key.keysym.sym==SDLK_KP_ENTER || event->key.keysym.sym==SDLK_ESCAPE || hc_matches(HC_PAUSE,event->key.keysym.sym)))return 1;
    if(event->type==SDL_MOUSEBUTTONUP && event->button.button==SDL_BUTTON_LEFT){
        int w,h;SDL_GetWindowSize(g_win.win,&w,&h);float scale=fminf(w/960.0f,h/650.0f);if(scale<=0)return 0;
        float x=(event->button.x-(w-PANEL_W*scale)/2)/scale,y=(event->button.y-(h-PANEL_H*scale)/2)/scale;
        return x>=31 && x<PANEL_W-31 && y>=499 && y<534;
    }
    return 0;
}
static void high_scores_hud_draw(int w,int h)
{
    HighScoreState state;if(!high_scores_enabled() || !high_scores_snapshot(&state) || !state.playing || w<=0 || h<=0)return;
    static GLuint panel;static uint32_t cached=~0u;
    GLint program,unpack;glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);
    glPushAttrib(GL_ALL_ATTRIB_BITS);glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glUseProgram(0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    if(glBindProgramARB){glBindProgramARB(GL_VERTEX_PROGRAM_ARB,0);glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB,0);}
    glDisable(GL_VERTEX_PROGRAM_ARB);glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);glDisable(GL_LIGHTING);glDisable(GL_ALPHA_TEST);glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    for(int i=7;i>=0;i--){glActiveTexture(GL_TEXTURE0+i);glDisable(GL_TEXTURE_2D);glDisable(GL_TEXTURE_CUBE_MAP);}
    glViewport(0,0,w,h);glMatrixMode(GL_TEXTURE);glPushMatrix();glLoadIdentity();
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(0,w,h,0,-1,1);
    glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
    if(!panel)glGenTextures(1,&panel);
    if(cached!=state.rows[0].score && high_scores_texture(panel,&state,1))cached=state.rows[0].score;
    int vw=g_vp_w>0?g_vp_w:w,vh=g_vp_h>0?g_vp_h:h;
    float scale=fmaxf(.5f,vh/1080.0f),x=g_vp_x+vw-274*scale,y=h-g_vp_y-vh+80*scale;
    glEnable(GL_TEXTURE_2D);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);glColor4f(1,1,1,1);
    glBindTexture(GL_TEXTURE_2D,panel);pause_quad(x,y,246*scale,66*scale,0);
    glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_TEXTURE);glPopMatrix();glMatrixMode(GL_MODELVIEW);
    glPopClientAttrib();glPopAttrib();glBindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)unpack);glUseProgram((GLuint)program);
}
