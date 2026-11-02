/* Test cheat for the long play-throughs (env INFLIVES=1): while a game is running, the lives counter ($5430) is put back to 3 whenever it drops (when it falls to 1 or 2).
 * Applied by yref and yrun at the same point (end of the frame, after the dump), so both runs see identical memory.  Also: env DUMPEVERY=N writes only every Nth frame dump. */
#include <stdlib.h>
static int cheat_on = -1, cheat_seen3, dump_every = 1; static unsigned cheat_deaths;
static void cheat_frame(uint8_t *img) {
    if (cheat_on < 0) { cheat_on = getenv("INFLIVES") != 0; if (getenv("DUMPEVERY")) dump_every = atoi(getenv("DUMPEVERY")); }
    if (!cheat_on) return;
    /* real lives counter is $5430 (game over test at $8F1B); keep it at 3 whenever it is 1 or 2 during a game */
    if (img[0x51C3] != 2) return;
    if (img[0x5430] == 1 || img[0x5430] == 2) { cheat_deaths++; img[0x5430] = 3; }
}
