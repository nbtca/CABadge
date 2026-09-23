/* Renderer-only test, no simulator window and no replacement of pixel code. */
#define GROK_RENDER_TEST
#include "grok.c"
#include <assert.h>
#include <stdio.h>
int main(void){
    uint16_t *allocation=malloc((SIDE*SIDE+2)*2);
    base=malloc(SIDE*SIDE*2);assert(allocation&&base);pixels=allocation+1;
    for(int mode=0;mode<2;mode++)for(int state=0;state<32;state++){
        theme=mode;emotion=state;background();
        for(int r=0;r<25;r++)for(int offset=-300;offset<=300;offset+=150){
            allocation[0]=0xCAFE;allocation[SIDE*SIDE+1]=0xBEEF;
            memcpy(pixels,base,SIDE*SIDE*2);
            for(int e=0;e<2;e++){
                float p[48][2];for(int i=0;i<48;i++){p[i][0]=rings[r][e][i][0]*.01f+offset;p[i][1]=rings[r][e][i][1]*.01f+offset;}
                eye(p,0);
            }
            assert(allocation[0]==0xCAFE&&allocation[SIDE*SIDE+1]==0xBEEF);
        }
    }
    FILE *f=fopen("F:/CABadgeBuild/temp/grok-review/native.ppm","wb");assert(f);
    emotion=2;theme=0;background();memcpy(pixels,base,SIDE*SIDE*2);
    for(int e=0;e<2;e++){float p[48][2];for(int i=0;i<48;i++){p[i][0]=14+rings[0][e][i][0]*.01f;p[i][1]=14+rings[0][e][i][1]*.01f;}eye(p,0);}
    fprintf(f,"P6\n%d %d\n255\n",SIDE,SIDE);for(int i=0;i<SIDE*SIDE;i++){unsigned char c[3]={(pixels[i]>>11)*8,((pixels[i]>>5)&63)*4,(pixels[i]&31)*8};fwrite(c,1,3,f);}fclose(f);
    free(allocation);free(base);puts("PASS: 32 palettes, 25 paired contours, 5 clipping offsets, guarded buffers");return 0;
}
