#include "pokemon.h"
#include "pokemon_core.h"
#include "pokemon_art.h"
#include "peer_link.h"
#ifdef ESP_PLATFORM
#include "esp_random.h"
#endif
#include <stdio.h>
#include <string.h>
extern void ble_service_snapshot(badge_ble_info_t*);

enum { VIEW_MENU,VIEW_ROOM,VIEW_FIELD,VIEW_RESULT };
enum { PAPER=0xF1E9CD,INK=0x2B3B38,GREEN=0x52785A,LINE=0xB2B99A };
static lv_obj_t *root,*note,*health[2],*hp_text[2],*sprites[2],*skills[4],*particles[3];
static void (*return_to_apps)(void);
static pk_battle_t battle;
static pk_packet_t pending;
static unsigned selected,net_mode,view,link_epoch,link_status,room_key;
static bool starting,valid,has_pending,animating;
static uint32_t link_at,seen_at,sent_at,action_at,anim_at;
static uint8_t before_hp[2];
static unsigned controls_key,drawn_hp[2];
static char message[128];
static const uint32_t type_colors[]={0x919879,0xC66842,0x4C8BAC,0x65904C,0xBA9C30,0x947494};
static void menu(void),room(void),field(void),result(void),input(lv_event_t *e);
static unsigned me(void){return net_mode==PEER_JOIN?1:0;}
static uint32_t seed(void){
#ifdef ESP_PLATFORM
    return esp_random();
#else
    return lv_tick_get()^0xCB251970u;
#endif
}
static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius){
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,255,0);lv_obj_set_style_radius(o,radius,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);return o;
}
static lv_obj_t *text(lv_obj_t *parent,const char *s,int x,int y,int w,const lv_font_t *font,uint32_t color){
    lv_obj_t *o=lv_label_create(parent);lv_label_set_text(o,s);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(o,LV_LABEL_LONG_WRAP);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
static lv_obj_t *button(const char *s,int x,int y,int w,int h,int id){
    lv_obj_t *o=box(root,x,y,w,h,0xFFF9E7,7);
    lv_obj_set_style_border_width(o,2,0);lv_obj_set_style_border_color(o,lv_color_hex(LINE),0);
    lv_obj_set_style_bg_color(o,lv_color_hex(0xDBE3BC),LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(o,100,LV_STATE_DISABLED);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *label=text(o,s,0,0,w-4,&font14,INK);lv_obj_center(label);
    lv_obj_add_event_cb(o,badge_ui_click_guard,LV_EVENT_ALL,NULL);
    lv_obj_add_event_cb(o,input,LV_EVENT_SHORT_CLICKED,(void*)(intptr_t)id);return o;
}
static lv_obj_t *image(lv_obj_t *parent,unsigned species,bool back,int x,int y,int scale){
    lv_obj_t *o=lv_image_create(parent);lv_image_set_src(o,back?pokemon_back[species]:pokemon_front[species]);
    lv_image_set_pivot(o,0,0);lv_image_set_scale(o,scale);lv_image_set_antialias(o,false);
    lv_obj_set_pos(o,x,y);lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
static void heading(const char *title){
    button("‹",80,27,42,40,0);text(root,title,123,30,153,&font24,INK);
}
static void stop_link(void){
    if(net_mode)peer_link_mode(PEER_SINGLE);
    net_mode=0;starting=has_pending=false;
}
static void menu(void){
    stop_link();valid=animating=false;view=VIEW_MENU;lv_obj_clean(root);heading("宝可梦对战");
    for(unsigned i=0;i<4;i++){
        lv_obj_t *card=button("",80+(i%2)*104,79+(i/2)*81,96,77,10+i);
        if(i==selected){lv_obj_set_style_border_color(card,lv_color_hex(GREEN),0);lv_obj_set_style_border_width(card,3,0);}
        image(card,i,false,16,-2,256);text(card,pk_species[i].name,0,56,96,&font14,INK);
    }
    char detail[48];snprintf(detail,sizeof(detail),"Lv.50 · %s属性",pk_types[pk_species[selected].type]);
    text(root,detail,80,244,200,&font14,GREEN);
    button("单机对战",60,270,76,38,1);button("创建房间",142,270,76,38,2);button("加入房间",224,270,76,38,3);
    note=text(root,message[0]?message:"选择精灵后开始",89,315,182,&font14,INK);
}
static void send_state(void){
    pk_packet_t packet={.op=PK_STATE,.state=battle};uint8_t wire[PK_WIRE_BYTES];pk_encode(wire,&packet);
    if(peer_link_send(wire))sent_at=lv_tick_get();
}
static void request(unsigned op,unsigned value){
    pending=(pk_packet_t){.op=op,.value=value,.session=battle.session,.revision=battle.revision,.turn=battle.turn};
    has_pending=true;action_at=lv_tick_get()-500;
}
static void bar(unsigned actor,unsigned hp){
    unsigned local=actor==me()?0:1,max=pk_species[battle.species[actor]].hp;
    if(drawn_hp[local]==hp)return;
    drawn_hp[local]=hp;
    lv_obj_set_width(health[local],hp?100*hp/max+1:0);
    lv_obj_set_style_bg_color(health[local],lv_color_hex(hp*4<max?0xC35B49:hp*2<max?0xBEA044:GREEN),0);
    lv_label_set_text_fmt(hp_text[local],"%u / %u",hp,max);
}
static void controls(void){
    unsigned key=((unsigned)battle.revision<<4)|(animating?1:0)|(has_pending?2:0);
    if(key==controls_key)return;
    controls_key=key;
    bool enabled=!animating&&battle.phase==PK_BATTLE&&!(battle.ready&(1u<<me()))&&!has_pending;
    bool any=false;for(unsigned i=0;i<4;i++)any|=battle.pp[me()][i]!=0;
    for(unsigned i=0;i<4;i++){
        const pk_move_t *move=pk_move(battle.species[me()],i);char s[72];
        if(!any&&i==0)snprintf(s,sizeof(s),"挣扎\n无 PP · 反伤");
        else snprintf(s,sizeof(s),"%s\n%s · %u/%u",move->name,pk_types[move->type],battle.pp[me()][i],move->pp);
        lv_label_set_text(lv_obj_get_child(skills[i],0),s);
        if(enabled&&(any?battle.pp[me()][i]>0:i==0))lv_obj_remove_state(skills[i],LV_STATE_DISABLED);
        else lv_obj_add_state(skills[i],LV_STATE_DISABLED);
    }
    if(!animating)lv_label_set_text(note,enabled?"选择技能":has_pending?"正在确认出招":"等待伙伴出招");
}
static void field(void){
    view=VIEW_FIELD;controls_key=~0u;drawn_hp[0]=drawn_hp[1]=~0u;lv_obj_clean(root);heading(net_mode?"蓝牙对战":"单机对战");
    box(root,186,160,112,15,0xD6DBAF,8);box(root,61,206,127,19,0xD6DBAF,10);
    for(unsigned local=0;local<2;local++){
        unsigned actor=local?me()^1:me(),s=battle.species[actor];
        sprites[local]=image(root,s,!local,local?188:57,local?72:96,512);
        int x=local?66:185,y=local?76:171;
        lv_obj_t *card=box(root,x,y,112,49,0xFFF9E7,6);
        text(card,pk_species[s].name,0,1,112,&font14,INK);
        box(card,5,23,102,5,LINE,0);health[local]=box(card,5,23,101,5,GREEN,0);
        hp_text[local]=text(card,"",0,29,112,&font14,INK);bar(actor,battle.hp[actor]);
    }
    box(root,62,221,236,25,PAPER,3);note=text(root,"选择技能",62,223,236,&font14,INK);
    for(unsigned i=0;i<4;i++)skills[i]=button("",87+(i%2)*96,248+(i/2)*46,90,42,30+i);
    for(unsigned i=0;i<3;i++){particles[i]=box(root,0,0,6,6,0xC9A946,1);lv_obj_add_flag(particles[i],LV_OBJ_FLAG_HIDDEN);}
    controls();
}
static void animate_from(const uint8_t hp[2]){
    before_hp[0]=hp[0];before_hp[1]=hp[1];animating=true;anim_at=lv_tick_get();
    for(unsigned side=0;side<2;side++)bar(side,hp[side]);
    controls();
}
static void room(void){
    peer_link_info_t link;peer_link_snapshot(&link);badge_ble_info_t devices;ble_service_snapshot(&devices);
    unsigned key=link.status+16*devices.count+256*starting+512*valid+1024*battle.ready+
        4096*(valid?battle.species[1]:PK_NONE)+65536*has_pending;
    if(view==VIEW_ROOM&&key==room_key)return;
    room_key=key;view=VIEW_ROOM;lv_obj_clean(root);heading("蓝牙对战");
    if(starting){text(root,"正在开启蓝牙",70,142,220,&font18,INK);return;}
    if(link.status==PEER_READY&&valid){
        unsigned own=me(),other=own^1;
        image(root,battle.species[own]<4?battle.species[own]:selected,false,81,96,256);
        unsigned enemy=battle.species[other];if(enemy<4)image(root,enemy,false,215,96,256);
        text(root,"VS",159,117,42,&font24,GREEN);
        text(root,pk_species[selected].name,64,164,116,&font18,INK);
        text(root,enemy<4?pk_species[enemy].name:"等待选择",182,164,116,&font18,INK);
        text(root,(battle.ready&(1u<<own))?"你已准备，等待伙伴":"双方准备后开始",70,211,220,&font14,GREEN);
        if(battle.species[own]<4&&battle.species[other]<4){
            lv_obj_t *b=button((battle.ready&(1u<<own))?"已准备":"准备对战",113,252,134,43,4);
            if((battle.ready&(1u<<own))||has_pending)lv_obj_add_state(b,LV_STATE_DISABLED);
        }
        return;
    }
    if(link.status==PEER_ERROR){text(root,link.error==15?"先断开其他蓝牙设备":"连接已结束",68,130,224,&font18,INK);text(root,"返回后重新创建或加入",68,189,224,&font14,GREEN);return;}
    if(net_mode==PEER_HOST){
        text(root,link.room,69,113,222,&font24,GREEN);text(root,"另一块选择加入房间",68,167,224,&font18,INK);
        text(root,link.status==PEER_CONNECTING?"伙伴正在连接":"等待伙伴加入",68,219,224,&font14,INK);
    }else if(link.status==PEER_SCANNING||link.status==PEER_CONNECTING||link.status==PEER_READY){
        text(root,link.status==PEER_SCANNING?"正在寻找房间":link.status==PEER_READY?"正在同步精灵":"正在连接伙伴",68,149,224,&font18,INK);
    }else {
        text(root,devices.count?"选择对方的房间":"没有找到房间",68,91,224,&font18,INK);
        for(int i=0;i<devices.count&&i<3;i++)button(devices.devices[i].name,100,133+i*46,160,39,20+i);
    }
}
static void result(void){
    view=VIEW_RESULT;lv_obj_clean(root);heading("对战结束");
    bool draw=(!battle.hp[0]&&!battle.hp[1])||(battle.hp[0]&&battle.hp[1]);
    bool win=battle.hp[me()]&&!battle.hp[me()^1];
    text(root,draw?"平局！":win?"你赢了！":"下次再战！",70,91,220,&font24,GREEN);
    image(root,battle.species[win||draw?me():me()^1],false,116,121,512);
    char s[48];snprintf(s,sizeof(s),"完成 %u 回合",battle.turn);text(root,s,76,242,208,&font18,INK);
    button("重新选精灵",104,286,152,42,5);
}
static void input(lv_event_t *e){
    int id=(int)(intptr_t)lv_event_get_user_data(e);
    if(id==0){if(view==VIEW_MENU){if(return_to_apps)return_to_apps();}else {message[0]=0;menu();}return;}
    if(id>=10&&id<14){selected=id-10;message[0]=0;menu();return;}
    if(id==5){message[0]=0;menu();return;}
    if(id==1){
        stop_link();pk_lobby(&battle,selected,seed());battle.species[1]=pk_random(&battle)%3;
        if(battle.species[1]>=selected)battle.species[1]++;
        pk_start(&battle);valid=true;field();return;
    }
    if(id==2||id==3){
        peer_link_info_t link;peer_link_snapshot(&link);link_epoch=link.epoch;
        unsigned mode=id==2?PEER_HOST:PEER_JOIN;
        if(!peer_link_mode(mode)){snprintf(message,sizeof(message),"蓝牙正忙，请稍后重试");menu();return;}
        net_mode=mode;starting=true;valid=has_pending=animating=false;link_status=PEER_OFF;
        link_at=seen_at=sent_at=action_at=lv_tick_get();room_key=~0u;
        pk_lobby(&battle,selected,seed());room();return;
    }
    if(id>=20&&id<23){if(peer_link_connect(id-20)){link_at=lv_tick_get();room_key=~0u;}return;}
    if(id==4&&valid&&battle.phase==PK_LOBBY&&!has_pending){
        if(net_mode==PEER_HOST){battle.ready|=1;if(battle.ready==3)pk_start(&battle);battle.revision++;send_state();room_key=~0u;}
        else request(PK_READY,selected);
        return;
    }
    if(id>=30&&id<34&&valid&&!animating&&!has_pending){
        unsigned slot=id-30;bool any=false;for(unsigned i=0;i<4;i++)any|=battle.pp[me()][i]!=0;
        if(!any)slot=PK_STRUGGLE;
        if(!pk_choice(&battle,me(),slot)||(battle.ready&(1u<<me())))return;
        if(net_mode==PEER_JOIN){request(PK_ACTION,slot);controls();return;}
        uint8_t hp[2]={battle.hp[0],battle.hp[1]};unsigned turn=battle.turn;
        pk_submit(&battle,0,slot,turn);
        if(!net_mode)pk_submit(&battle,1,pk_cpu(&battle,1),turn);
        if(net_mode)send_state();
        if(battle.turn!=turn)animate_from(hp);else controls();
    }
}
static void animate(uint32_t now){
    unsigned age=now-anim_at,step=age/1000,within=age%1000;
    unsigned count=battle.last[battle.first^1]==PK_SKIP?1:2;
    if(step>=count){
        animating=false;for(unsigned i=0;i<2;i++){lv_obj_set_pos(sprites[i],i?188:57,i?72:96);lv_obj_set_style_opa(sprites[i],255,0);bar(i,battle.hp[i]);}
        for(unsigned i=0;i<3;i++)lv_obj_add_flag(particles[i],LV_OBJ_FLAG_HIDDEN);
        if(battle.phase==PK_FINISHED)result();else controls();return;
    }
    unsigned side=battle.first^step,target=side^1,local=side==me()?0:1;
    const pk_move_t *move=pk_move(battle.species[side],battle.last[side]);
    char s[128];snprintf(s,sizeof(s),"%s · %s%s",pk_species[battle.species[side]].name,move->name,
        (battle.miss&(1u<<side))?" · 未命中":(battle.critical&(1u<<side))?" · 暴击！":"");
    lv_label_set_text(note,s);
    for(unsigned i=0;i<2;i++){
        int x=i?188:57,y=i?72:96;
        if(i==local&&within<240)x+=(i?-1:1)*(int)(within<120?within/12:(240-within)/12);
        lv_obj_set_pos(sprites[i],x,y);
        lv_obj_set_style_opa(sprites[i],i!=local&&within>=280&&within<550&&!(battle.miss&(1u<<side))&&within/65%2?70:255,0);
    }
    uint8_t hp[2]={before_hp[0],before_hp[1]};
    for(unsigned i=0;i<=step;i++){
        unsigned actor=battle.first^i,defender=actor^1;
        unsigned amount=battle.damage[actor],recoil=pk_move(battle.species[actor],battle.last[actor])->recoil?(amount/4?amount/4:amount?1:0):0;
        unsigned progress=i<step?300:within>350?(within>=650?300:within-350):0;
        unsigned hit=amount*progress/300,back=recoil*progress/300;
        hp[defender]=hp[defender]>hit?hp[defender]-hit:0;hp[actor]=hp[actor]>back?hp[actor]-back:0;
    }
    bar(side,hp[side]);bar(target,hp[target]);
    for(unsigned i=0;i<3;i++){
        if(within<160||within>420){lv_obj_add_flag(particles[i],LV_OBJ_FLAG_HIDDEN);continue;}
        int start_x=local?229:111,start_y=local?119:174,end_x=local?111:229,end_y=local?174:119;
        int t=(int)within-160-(int)i*25;if(t<0)t=0;if(t>200)t=200;
        lv_obj_set_pos(particles[i],start_x+(end_x-start_x)*t/200,start_y+(end_y-start_y)*t/200+i*4);
        lv_obj_set_style_bg_color(particles[i],lv_color_hex(type_colors[move->type]),0);lv_obj_remove_flag(particles[i],LV_OBJ_FLAG_HIDDEN);
    }
}
static void disconnected(const char *reason){
    snprintf(message,sizeof(message),"%s",reason);menu();
}
static void network(uint32_t now){
    peer_link_info_t link;peer_link_snapshot(&link);
    if(starting&&link.mode==net_mode&&link.epoch!=link_epoch){starting=false;room_key=~0u;}
    if(link.status!=link_status){link_status=link.status;if(link.status==PEER_CONNECTING||link.status==PEER_READY)link_at=seen_at=now;room_key=~0u;}
    if(starting){if(now-link_at>12000)disconnected("蓝牙开启超时，请重试");else room();return;}
    if(link.mode!=net_mode||link.status==PEER_ERROR){disconnected(link.error==15?"先断开其他蓝牙设备":"连接中断，请重新开局");return;}
    if(link.status!=PEER_READY){
        if(link.status==PEER_CONNECTING&&now-link_at>12000){disconnected("连接超时，请重试");return;}
        room();return;
    }
    if(net_mode==PEER_HOST&&!valid){valid=true;seen_at=now;sent_at=now-1000;room_key=~0u;}
    uint8_t wire[PK_WIRE_BYTES];pk_packet_t packet;
    while(peer_link_receive(wire)){
        if(!pk_decode(wire,sizeof(wire),&packet))continue;
        if(net_mode==PEER_HOST){
            if(packet.session!=battle.session)continue;
            seen_at=now;uint8_t hp[2]={battle.hp[0],battle.hp[1]};unsigned turn=battle.turn;
            if(pk_apply(&battle,&packet)){
                send_state();room_key=~0u;
                if(battle.turn!=turn){if(view!=VIEW_FIELD)field();animate_from(hp);}
            }
        }else if(packet.op==PK_STATE){
            pk_battle_t *next=&packet.state;
            if(valid&&(next->session!=battle.session||(int16_t)(next->revision-battle.revision)<0))continue;
            seen_at=now;bool changed=!valid||next->revision!=battle.revision;
            uint8_t hp[2]={battle.hp[0],battle.hp[1]};unsigned turn=battle.turn;bool had_battle=valid&&battle.phase!=PK_LOBBY;
            battle=*next;valid=true;
            if(has_pending&&((pending.op==PK_SELECT&&battle.species[1]==pending.value)||
                (pending.op==PK_READY&&(battle.phase!=PK_LOBBY||(battle.ready&2)))||
                (pending.op==PK_ACTION&&(battle.turn!=pending.turn||(battle.ready&2)))))has_pending=false;
            if(changed){room_key=~0u;if(had_battle&&battle.turn!=turn){if(view!=VIEW_FIELD)field();animate_from(hp);}}
        }
    }
    if(now-seen_at>15000){disconnected("伙伴没有响应，请重新开局");return;}
    if(net_mode==PEER_HOST){if(now-sent_at>=1000)send_state();}
    else if(valid){
        if(battle.phase==PK_LOBBY&&battle.species[1]!=selected&&!has_pending)request(PK_SELECT,selected);
        if(has_pending&&now-action_at>=500){pk_encode(wire,&pending);if(peer_link_send(wire))action_at=sent_at=now;}
        else if(!has_pending&&now-sent_at>=1000){packet=(pk_packet_t){.op=PK_PING,.session=battle.session,.turn=battle.turn};pk_encode(wire,&packet);if(peer_link_send(wire))sent_at=now;}
    }
    if(valid&&battle.phase!=PK_LOBBY){
        if(view==VIEW_ROOM)field();
        if(view==VIEW_FIELD&&!animating){if(battle.phase==PK_FINISHED)result();else controls();}
    }else room();
}
void pokemon_open(lv_obj_t *parent,void (*back)(void)){
    pokemon_close();return_to_apps=back;root=box(parent,0,0,360,360,PAPER,0);message[0]=0;menu();
}
void pokemon_close(void){
    stop_link();valid=animating=false;if(root){lv_obj_delete(root);root=NULL;}
}
void pokemon_poll(bool active){
    if(!root)return;
    if(!active){if(net_mode)disconnected("已结束蓝牙对战");return;}
    uint32_t now=lv_tick_get();if(net_mode)network(now);
    if(root&&animating)animate(now);
}
void pokemon_info(char *out,size_t size){
    snprintf(out,size,"{\"active\":%s,\"view\":%u,\"mode\":%u,\"valid\":%s,\"session\":%u,\"phase\":%u,\"turn\":%u,\"revision\":%u,\"species\":[%u,%u],\"hp\":[%u,%u],\"ready\":%u,\"pending\":%s,\"animating\":%s}",
        root?"true":"false",view,net_mode,valid?"true":"false",battle.session,battle.phase,battle.turn,battle.revision,
        battle.species[0],battle.species[1],battle.hp[0],battle.hp[1],battle.ready,has_pending?"true":"false",animating?"true":"false");
}
