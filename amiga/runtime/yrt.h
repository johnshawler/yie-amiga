#ifndef YRT_H
#define YRT_H
#include <stdint.h>
struct rtregs { uint32_t d[5]; uint32_t ccr, a3, pc, s, force, dp, cch; };
void rt_init(uint8_t *image);
void rt_run(void);                       /* never returns */
void rt_fatal(const char *msg, uint32_t a, uint32_t b);
extern uint64_t rt_ev_t;
extern uint32_t rt_stat_slow, rt_stat_interp1;
#endif
