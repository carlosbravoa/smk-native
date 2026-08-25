/* Can the kart drive through the gap in Rainbow Road's Thwomp row?
 * Entities sit at (604,28) (604,36) (604,52) (604,60): the 16 px gap
 * between 36 and 52 is the one you are meant to thread. */
#include "smk.h"
#include <stdio.h>
#include <string.h>
int main(void){
    smk_rom rom; char err[128];
    if(!smk_rom_load(&rom,"rom/smk_usa.sfc",err,sizeof err)){puts(err);return 77;}
    static smk_track trk; static smk_course crs;
    if(!smk_track_load(&rom,5,-1,&trk,err,sizeof err))return 1;
    if(!smk_course_load(&rom,5,&crs))return 1;
    smk_track_place_objects(&rom,&trk);
    printf("track 5 entities:");
    for(int i=0;i<crs.nent&&i<4;i++)printf(" (%d,%d)",crs.ent[i].x,crs.ent[i].y);
    puts("");
    for(int lane=28;lane<=64;lane+=4){
        smk_kart k; memset(&k,0,sizeof k);
        k.x=(int32_t)(560*SMK_POS_ONE); k.y=(int32_t)(lane*SMK_POS_ONE);
        k.angle=SMK_ANGLE_TURN/4; k.speed=600;
        int passed=0;
        for(int f=0;f<120;f++){
            smk_kart_face(&k); smk_kart_gravity(&k);
            smk_kart_move(&k,&trk);
            smk_collide_objects(&k,&crs);
            if(smk_kart_px(k.x)>640){passed=1;break;}
        }
        printf("  lane y=%2d: %s\n",lane,passed?"PASSES":"blocked");
    }
    return 0;
}
