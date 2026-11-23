#ifndef HOTD4_HIGH_SCORES_H
#define HOTD4_HIGH_SCORES_H
#include <stdint.h>
#include <string.h>
enum { HOTD4_SCORE_ROWS=10, HOTD4_SCORE_WORDS=6 };
typedef struct { uint32_t score; char initials[4]; } HighScoreEntry;
typedef struct { HighScoreEntry rows[HOTD4_SCORE_ROWS]; int count,playing,practice; } HighScoreState;
typedef int (*HighScoreReader)(HighScoreState *);
void host_high_scores_register(HighScoreReader reader);
/* Decode the original game's rank records. Never import defaults or game data. */
static inline int high_scores_decode(HighScoreState *out,const uint32_t words[HOTD4_SCORE_ROWS*HOTD4_SCORE_WORDS])
{
    HighScoreState result={0};
    for(int i=0;i<HOTD4_SCORE_ROWS;i++){
        const uint32_t *row=words+i*HOTD4_SCORE_WORDS;
        if(row[0]>999999999u || (i && row[0]>result.rows[i-1].score))return 0;
        result.rows[i].score=row[0];
        for(int j=0;j<3;j++){
            if(row[j+1]>26)return 0;
            result.rows[i].initials[j]=row[j+1]?(char)('A'+row[j+1]-1):' ';
        }
    }
    result.count=HOTD4_SCORE_ROWS;*out=result;return 1;
}
#endif
