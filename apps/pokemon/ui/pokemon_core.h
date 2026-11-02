#ifndef CABADGE_POKEMON_CORE_H
#define CABADGE_POKEMON_CORE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

enum { PK_NORMAL,PK_FIRE,PK_WATER,PK_GRASS,PK_ELECTRIC,PK_POISON };
enum { PK_LOBBY,PK_BATTLE,PK_FINISHED };
enum { PK_STATE=1,PK_SELECT,PK_READY,PK_ACTION,PK_PING };
enum { PK_STRUGGLE=4,PK_SKIP=5,PK_NONE=15,PK_WIRE_BYTES=20 };
typedef struct {const char *name;uint8_t type,power,accuracy,pp,priority,recoil,high_crit;} pk_move_t;
typedef struct {const char *name;uint8_t dex,type,type2,hp,atk,def,spa,spd,speed;pk_move_t moves[4];} pk_species_t;
/* Level 50, neutral nature, 31 IV, no EV. Damage-only Gen-3 move subset;
 * shortcut: secondary status effects are deferred until the basic duel is accepted. */
static const pk_species_t pk_species[4]={
    {"妙蛙种子",1,PK_GRASS,PK_POISON,120,69,69,85,85,65,{
        {"撞击",PK_NORMAL,35,95,35,0,0,0},{"藤鞭",PK_GRASS,35,100,10,0,0,0},
        {"飞叶快刀",PK_GRASS,55,95,25,0,0,1},{"猛撞",PK_NORMAL,90,85,20,0,1,0}}},
    {"小火龙",4,PK_FIRE,PK_NONE,114,72,63,80,70,85,{
        {"抓",PK_NORMAL,40,100,35,0,0,0},{"火花",PK_FIRE,40,100,25,0,0,0},
        {"喷射火焰",PK_FIRE,95,100,15,0,0,0},{"劈开",PK_NORMAL,70,100,20,0,0,1}}},
    {"杰尼龟",7,PK_WATER,PK_NONE,119,68,85,70,84,63,{
        {"撞击",PK_NORMAL,35,95,35,0,0,0},{"水枪",PK_WATER,40,100,25,0,0,0},
        {"泡沫光线",PK_WATER,65,100,20,0,0,0},{"水炮",PK_WATER,120,80,5,0,0,0}}},
    {"皮卡丘",25,PK_ELECTRIC,PK_NONE,110,75,60,70,70,110,{
        {"电光一闪",PK_NORMAL,40,100,30,1,0,0},{"电击",PK_ELECTRIC,40,100,30,0,0,0},
        {"十万伏特",PK_ELECTRIC,95,100,15,0,0,0},{"打雷",PK_ELECTRIC,120,70,10,0,0,0}}}
};
static const pk_move_t pk_struggle={"挣扎",PK_NORMAL,50,100,0,0,1,0};
static const char *const pk_types[]={"普通","火","水","草","电","毒"};
typedef struct {
    uint32_t rng;
    uint16_t session,revision;
    uint8_t phase,turn,species[2],hp[2],pp[2][4],ready,picks[2],last[2],damage[2],first,miss,critical;
} pk_battle_t;
typedef struct {uint16_t session,revision;uint8_t op,turn,value;pk_battle_t state;} pk_packet_t;

static inline uint32_t pk_random(pk_battle_t *b){
    uint32_t x=b->rng?b->rng:0x6D2B79F5u;x^=x<<13;x^=x>>17;x^=x<<5;return b->rng=x;
}
/* Multiplier in quarters, including the secondary Poison type. */
static inline unsigned pk_effect(unsigned attack,unsigned defend){
    if(attack==PK_FIRE)return defend==PK_GRASS?8:defend==PK_FIRE||defend==PK_WATER?2:4;
    if(attack==PK_WATER)return defend==PK_FIRE?8:defend==PK_WATER||defend==PK_GRASS?2:4;
    if(attack==PK_GRASS)return defend==PK_WATER?8:defend==PK_FIRE||defend==PK_GRASS||defend==PK_POISON?2:4;
    if(attack==PK_ELECTRIC)return defend==PK_WATER?8:defend==PK_GRASS||defend==PK_ELECTRIC?2:4;
    return 4;
}
static inline const pk_move_t *pk_move(unsigned species,unsigned slot){
    return slot<4?&pk_species[species].moves[slot]:&pk_struggle;
}
static inline bool pk_choice(const pk_battle_t *b,unsigned side,unsigned slot){
    if(side>1||b->phase!=PK_BATTLE||b->species[side]>3||!b->hp[side])return false;
    if(slot<4)return b->pp[side][slot]>0;
    if(slot!=PK_STRUGGLE)return false;
    return !(b->pp[side][0]|b->pp[side][1]|b->pp[side][2]|b->pp[side][3]);
}
static inline void pk_lobby(pk_battle_t *b,unsigned species,uint32_t seed){
    *b=(pk_battle_t){.session=(seed&65535)?seed&65535:1,.rng=seed^0xA5A55A5Au,.revision=1,
        .species={(uint8_t)species,PK_NONE},.last={PK_SKIP,PK_SKIP}};
}
static inline void pk_start(pk_battle_t *b){
    b->phase=PK_BATTLE;b->ready=0;b->turn=0;b->miss=b->critical=0;
    for(unsigned side=0;side<2;side++){
        b->hp[side]=pk_species[b->species[side]].hp;
        for(unsigned i=0;i<4;i++)b->pp[side][i]=pk_species[b->species[side]].moves[i].pp;
    }
}
static inline unsigned pk_damage(const pk_species_t *a,const pk_species_t *d,const pk_move_t *m,unsigned variation,bool crit){
    bool special=m->type!=PK_NORMAL;
    unsigned atk=special?a->spa:a->atk,def=special?d->spd:d->def;
    unsigned value=((22u*m->power*atk/def)/50u)+2u;
    if(crit)value*=2;
    if(m!=&pk_struggle){
        if(m->type==a->type||m->type==a->type2)value=value*3/2;
        value=value*pk_effect(m->type,d->type)/4;
        value=value*pk_effect(m->type,d->type2)/4;
    }
    value=value*(85+variation%16)/100;return value?value:1;
}
static inline void pk_resolve(pk_battle_t *b){
    const pk_move_t *moves[2]={pk_move(b->species[0],b->picks[0]),pk_move(b->species[1],b->picks[1])};
    unsigned s0=pk_species[b->species[0]].speed,s1=pk_species[b->species[1]].speed;
    b->first=moves[0]->priority!=moves[1]->priority?moves[1]->priority>moves[0]->priority:
        s0==s1?(pk_random(b)&1):s1>s0;
    b->miss=b->critical=0;b->last[0]=b->last[1]=PK_SKIP;b->damage[0]=b->damage[1]=0;
    for(unsigned i=0;i<2;i++){
        unsigned side=b->first^i,target=side^1;
        if(!b->hp[side]||!b->hp[target])break;
        unsigned slot=b->picks[side];const pk_move_t *m=moves[side];b->last[side]=slot;
        if(slot<4)b->pp[side][slot]--;
        if(pk_random(b)%100>=m->accuracy){b->miss|=1u<<side;continue;}
        bool crit=pk_random(b)%(m->high_crit?8:16)==0;if(crit)b->critical|=1u<<side;
        unsigned damage=pk_damage(&pk_species[b->species[side]],&pk_species[b->species[target]],m,pk_random(b),crit);
        if(damage>b->hp[target])damage=b->hp[target];
        b->damage[side]=damage;b->hp[target]-=damage;
        if(m->recoil){unsigned recoil=damage/4;if(!recoil)recoil=1;b->hp[side]=b->hp[side]>recoil?b->hp[side]-recoil:0;}
    }
    b->ready=0;b->turn++;
    if(!b->hp[0]||!b->hp[1]||b->turn>=100)b->phase=PK_FINISHED;
}
static inline bool pk_submit(pk_battle_t *b,unsigned side,unsigned slot,unsigned turn){
    if(turn!=b->turn||!pk_choice(b,side,slot)||(b->ready&(1u<<side)))return false;
    b->picks[side]=slot;b->ready|=1u<<side;
    if(b->ready==3)pk_resolve(b);
    b->revision++;return true;
}
/* Remote input changes only the guest's side, never HP, RNG or the host's move. */
static inline bool pk_apply(pk_battle_t *b,const pk_packet_t *p){
    if(p->session!=b->session)return false;
    if(p->op==PK_ACTION)return pk_submit(b,1,p->value,p->turn);
    if(b->phase!=PK_LOBBY)return false;
    if(p->op==PK_SELECT&&p->value<4){
        if(b->species[1]==p->value)return false;
        b->species[1]=p->value;b->ready&=1;
    }else if(p->op==PK_READY&&p->value==b->species[1]&&p->value<4&&!(b->ready&2)){
        b->ready|=2;if(b->ready==3)pk_start(b);
    }else return false;
    b->revision++;return true;
}
static inline unsigned pk_cpu(const pk_battle_t *b,unsigned side){
    unsigned best=PK_STRUGGLE,value=0;
    for(unsigned i=0;i<4;i++)if(pk_choice(b,side,i)){
        const pk_move_t *m=pk_move(b->species[side],i);
        unsigned v=pk_damage(&pk_species[b->species[side]],&pk_species[b->species[side^1]],m,7,false)*m->accuracy;
        if(v>value){value=v;best=i;}
    }
    return best;
}
static inline void pk_encode(uint8_t data[PK_WIRE_BYTES],const pk_packet_t *p){
    memset(data,0,PK_WIRE_BYTES);data[0]=0xD7;data[1]=0x10|p->op;
    const pk_battle_t *b=&p->state;uint16_t session=p->op==PK_STATE?b->session:p->session;
    uint16_t revision=p->op==PK_STATE?b->revision:p->revision;
    data[4]=session;data[5]=session>>8;data[6]=revision;data[7]=revision>>8;
    if(p->op!=PK_STATE){data[2]=p->value;data[3]=p->turn;return;}
    data[2]=b->phase|(b->ready<<2)|(b->miss<<4)|(b->critical<<6);data[3]=b->turn;
    data[8]=b->species[0]|(b->species[1]<<4);data[9]=b->hp[0];data[10]=b->hp[1];
    uint64_t pp=0;for(unsigned side=0;side<2;side++)for(unsigned i=0;i<4;i++)pp|=(uint64_t)b->pp[side][i]<<((side*4+i)*6);
    for(unsigned i=0;i<6;i++)data[11+i]=pp>>(i*8);
    data[17]=b->last[0]|(b->last[1]<<3)|(b->first<<6);data[18]=b->damage[0];data[19]=b->damage[1];
}
static inline bool pk_decode(const uint8_t *data,size_t n,pk_packet_t *p){
    if(!data||n!=PK_WIRE_BYTES||data[0]!=0xD7||data[1]<0x11||data[1]>0x15)return false;
    *p=(pk_packet_t){.op=data[1]&15,.turn=data[3],.value=data[2],.revision=data[6]|(uint16_t)data[7]<<8,
        .session=data[4]|(uint16_t)data[5]<<8};
    if(!p->session||p->turn>100)return false;
    if(p->op!=PK_STATE){
        if(p->value>(p->op==PK_ACTION?4:p->op==PK_PING?0:3))return false;
        for(unsigned i=8;i<20;i++)if(data[i])return false;
        return true;
    }
    pk_battle_t *b=&p->state;
    *b=(pk_battle_t){.session=p->session,.revision=p->revision,.turn=p->turn,.phase=data[2]&3,
        .ready=(data[2]>>2)&3,.miss=(data[2]>>4)&3,.critical=data[2]>>6,
        .species={data[8]&15,data[8]>>4},.hp={data[9],data[10]},.damage={data[18],data[19]},
        .last={data[17]&7,(data[17]>>3)&7},.first=(data[17]>>6)&1};
    if(b->phase>PK_FINISHED||b->species[0]>3||(b->species[1]>3&&b->species[1]!=PK_NONE)||
       b->last[0]>PK_SKIP||b->last[1]>PK_SKIP||(data[17]&128))return false;
    if(b->phase!=PK_LOBBY&&(b->species[1]>3||b->hp[0]>pk_species[b->species[0]].hp||b->hp[1]>pk_species[b->species[1]].hp||
       (b->phase==PK_BATTLE&&(!b->hp[0]||!b->hp[1]||b->turn>=100))))return false;
    uint64_t pp=0;for(unsigned i=0;i<6;i++)pp|=(uint64_t)data[11+i]<<(i*8);
    for(unsigned side=0;side<2;side++)for(unsigned i=0;i<4;i++){
        b->pp[side][i]=(pp>>((side*4+i)*6))&63;
        if(b->species[side]<4&&b->pp[side][i]>pk_species[b->species[side]].moves[i].pp)return false;
    }
    return true;
}
#endif
