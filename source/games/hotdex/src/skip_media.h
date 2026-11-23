/* EX has separate state machines for attract films, story films and minigame
 * instructions. Replay a queued Start through their native exit paths so the
 * visuals and game logic finish together, retaining loading guards and fades. */
extern unsigned g_frame;
extern HleHandler dispatch_lifted(uint32_t va);
static HleHandler skip_demo_main,skip_movie_main,skip_tutorial_main;
static HleHandler skip_advertise_main;
static HleHandler skip_mode_post;
static HleHandler skip_game_main,skip_stage_end;
static HleHandler skip_inner_demo;
static HleHandler skip_rule,skip_start_press;
static uint32_t skip_logo_factory,skip_demo_factory;
static unsigned media_input_frame=~0u,media_previous,media_pressed,media_consumed=~0u;
static int skip_media_scope;
static int skip_start_scope;
static int skip_chain_story;
static int skip_chain_tutorial;
typedef struct { uint32_t self; unsigned frame; int pending,logged,applied; } MediaSkip;
static MediaSkip skip_demo,skip_movie,skip_tutorial;
static MediaSkip skip_game_cinema;
static MediaSkip skip_inner_tutorial;
static MediaSkip skip_rule_tutorial;

static void media_poll_start(void)
{
    if(media_input_frame!=g_frame){
        unsigned buttons=host_cabinet_buttons()&(CAB_P1_START|CAB_P2_START);
        media_pressed=buttons&~media_previous;media_previous=buttons;media_input_frame=g_frame;
    }
}
static int media_start_pressed(void)
{
    media_poll_start();
    if(!media_pressed || media_consumed==g_frame)return 0;
    media_consumed=g_frame;return 1;
}
static int media_skip_request(MediaSkip *request,uint32_t self,const char *name)
{
    if(request->self!=self || g_frame-request->frame>1){
        request->self=self;request->pending=request->logged=request->applied=0;
    }
    request->frame=g_frame;
    if(media_start_pressed())request->pending=1;
    if(request->pending && !request->logged){
        fprintf(stderr,"[hotdex-skip] Start requested: %s at frame %u\n",name,g_frame);
        request->logged=1;
    }
    return request->pending;
}
static void media_call(CPU *c,HleHandler original)
{
    push32(c,rd32(c->esp-4));original(c);
}
static void media_finish_movie(CPU *c)
{
    uint32_t manager=rd32(0x0a9dffacu);
    if(!manager || !rd32(manager+0x34))return;
    CPU call=*c;call.eax=manager;call.edx=0;
    guest_call(&call,guest_symbol("_ZN10CSofdecMgr7ReleaseEi."),NULL,0);
}
static void h_media_start_press(CPU *c)
{
    /* Replay the queued edge only inside a presentation handler. This takes
     * its complete native Start path, including the How to Play overlay,
     * animation clock and render-target cleanup. Gameplay sees real input. */
    uint32_t ret=rd32(c->esp-4);
    int ending_check=(ret>=0x080c3d8fu && ret<=0x080c3e30u) ||
                     (ret>=0x080c4173u && ret<=0x080c4221u) ||
                     (ret>=0x080c42bcu && ret<=0x080c4380u);
    if(skip_start_scope==1 || (skip_start_scope==2 && ending_check)){
        static unsigned logged=~0u;
        if(logged!=g_frame){fprintf(stderr,"[hotdex-skip] queued Start applied at frame %u\n",g_frame);logged=g_frame;}
        c->eax=1;return;
    }
    media_call(c,skip_start_press);
}
static void h_media_stage_end(CPU *c)
{
    if(skip_media_scope){c->eax=1;return;}
    media_call(c,skip_stage_end);
}
static void h_media_game(CPU *c)
{
    uint32_t self=A32(0),mode=rd32(self+0x34);
    int previous=skip_media_scope;
    int previous_start=skip_start_scope;
    if(mode!=8 && mode!=9)skip_chain_tutorial=0;
    /* Stage card, normal ending, ending movie, secret ending. These cases
     * use completion queries inside Main rather than GM_MOVIE_func. */
    if(mode==2 || mode==18 || mode==20 || mode==30){
        int pending=media_skip_request(&skip_game_cinema,self^mode,"stage intro/ending");
        if(mode==2){skip_media_scope=previous||pending;if(pending)skip_chain_story=1;}
        else skip_start_scope=previous_start?previous_start:(pending?2:0);
    }
    media_call(c,skip_game_main);
    skip_media_scope=previous;
    skip_start_scope=previous_start;
}
static void h_media_demo(CPU *c)
{
    uint32_t self=A32(0);
    int pending=media_skip_request(&skip_demo,self,"attract/demo movie");
    if(pending && !skip_demo.applied){media_finish_movie(c);skip_demo.applied=1;}
    media_call(c,skip_demo_main);
}
static void h_media_movie(CPU *c)
{
    uint32_t self=c->eax;
    int pending=media_skip_request(&skip_movie,self,"story movie");
    if(skip_chain_story){skip_movie.pending=pending=1;skip_chain_story=0;}
    int previous=skip_start_scope;skip_start_scope=previous||pending;
    media_call(c,skip_movie_main);
    skip_start_scope=previous;
}
static void h_media_tutorial(CPU *c)
{
    uint32_t self=c->eax;
    int pending=media_skip_request(&skip_tutorial,self,"minigame instructions");
    if(pending)skip_chain_tutorial=1;
    /* Native loading guards remain in charge; retain the edge until ready. */
    int previous=skip_start_scope;skip_start_scope=previous||pending;
    media_call(c,skip_tutorial_main);
    if(c->eax)skip_tutorial.pending=0;
    skip_start_scope=previous;
}
static void h_media_inner_tutorial(CPU *c)
{
    uint32_t self=c->eax;
    int pending=media_skip_request(&skip_inner_tutorial,self,"How to Play / game demo");
    if(skip_chain_tutorial){skip_inner_tutorial.pending=pending=1;skip_chain_tutorial=0;}
    int previous=skip_start_scope;skip_start_scope=previous||pending;
    media_call(c,skip_inner_demo);
    if(rd8(self+0x3c)==13)skip_inner_tutorial.pending=0;
    skip_start_scope=previous;
}
static void h_media_rule(CPU *c)
{
    uint32_t self=c->eax;
    int pending=media_skip_request(&skip_rule_tutorial,self,"How to Play / rules");
    if(skip_chain_tutorial){skip_rule_tutorial.pending=pending=1;skip_chain_tutorial=0;}
    int previous=skip_start_scope;skip_start_scope=previous||pending;
    media_call(c,skip_rule);
    if(rd8(self+0x3c)==6 || rd8(self+0x3c)==7)skip_rule_tutorial.pending=0;
    skip_start_scope=previous;
}
static void h_media_advertise(CPU *c)
{
    if(media_start_pressed()){
        wr32(0x0a9dfde0u,1); /* Advertise_Finish_flag, native exit path */
        fprintf(stderr,"[hotdex-skip] Start requested: advertisement at frame %u\n",g_frame);
    }
    media_call(c,skip_advertise_main);
}
static void h_media_mode_post(CPU *c)
{
    uint32_t self=A32(0),mode=rd32(self+0x34),table=rd32(self+0x44),entry=0,saved=0;
    media_poll_start(); /* Track releases even during gameplay and loading. */
    if(table && mode<32 && !rd32(self+0x3c)){
        uint32_t factory=rd32(table+mode*20);
        if((factory==skip_logo_factory || factory==skip_demo_factory) && media_start_pressed()){
            entry=table+mode*20+4;saved=rd32(entry);
            /* Expire the native mode timer. Post chooses the proper next
             * mode for the cabinet's locale and Pre releases its resources. */
            wr32(entry,1);wr32(self+0x40,1);
            fprintf(stderr,"[hotdex-skip] Start requested: logo/attract mode %u at frame %u\n",mode,g_frame);
        }
    }
    media_call(c,skip_mode_post);
    if(entry)wr32(entry,saved);
}
static int media_bind(const char *name,HleHandler replacement,HleHandler *original)
{
    uint32_t address=guest_symbol(name);
    *original=address?dispatch_lifted(address):NULL;
    return *original && guest_override(name,replacement);
}
static void install_media_skip(void)
{
    skip_logo_factory=guest_symbol("_Z7NewLOGOv");skip_demo_factory=guest_symbol("_Z7NewDEMOv");
    if(!media_bind("_ZN5CDemo4MainEv",h_media_demo,&skip_demo_main) ||
       !media_bind("_ZN8CGameMgr13GM_MOVIE_funcEv.",h_media_movie,&skip_movie_main) ||
       !media_bind("_ZN8CGameMgr20GM_MINIGAMEDEMO_funcEv.",h_media_tutorial,&skip_tutorial_main) ||
       !media_bind("_ZN10CAdvertise4MainEv",h_media_advertise,&skip_advertise_main) ||
       !media_bind("_ZN9CModeCtrl4PostEv",h_media_mode_post,&skip_mode_post) ||
       !media_bind("_ZN8CGameMgr4MainEv",h_media_game,&skip_game_main) ||
       !media_bind("_ZN7CGameUI23CheckStageStartMovieEndEv.",h_media_stage_end,&skip_stage_end) ||
       !media_bind("_ZN9CMiniGame12MG_DEMO_funcEv.",h_media_inner_tutorial,&skip_inner_demo) ||
       !media_bind("_ZN9CMiniGame12MG_RULE_funcEv.",h_media_rule,&skip_rule) ||
       !media_bind("_ZN9CInputJvs10StartPressEa",h_media_start_press,&skip_start_press) ||
       !skip_logo_factory || !skip_demo_factory ||
       !guest_symbol("_ZN10CSofdecMgr7ReleaseEi.")){
        fprintf(stderr,"[hotdex] missing native media skip entry point\n");exit(1);
    }
}
