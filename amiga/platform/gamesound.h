#ifndef GAMESOUND_H
#define GAMESOUND_H
#include <stdint.h>
#define WPS24_PAL  3959136647u      /* stream words per frame, 8.24 fixed point: 25344/1536000 s * 3546895 Hz / 124 / 2 */
#define WPS24_NTSC 3995581428u      /* same with the NTSC Paula clock 3579545 Hz */
void gs_init(void);                 /* after board_init / platform init: installs the board hooks and starts the Paula stream */
void gs_frame_prepare(uint32_t fr); /* from the frame hook: synthesise the block of the frame `fr` that has just ended */
void gs_frame_commit(void);         /* queue it for Paula (call when the real-time tick of the frame has arrived) */
extern uint32_t gs_vlm_started, gs_vlm_cut;
#endif
