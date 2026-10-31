/* VLM5030 BSY duration per phrase (cpu cycles at 1.536 MHz), measured in MAME 0.264 with trace/vlm_busy.lua (game start routine $C89A, 22-cycle poll loop).
 * Entries that were measured as 20 (BSY never seen) in the first, back-to-back run were an artefact of that harness (an NMI/IRQ hit the RAM program); they were
 * re-measured with one MAME run per phrase (see tools/vlmcap.py).  The other entries come from the first run (they agree with the per-phrase runs to within 176 cycles = less than one speech sample).
 * cycles = iterations*22 + 40 (fixed overhead of the routine tail); 0 iterations -> 20 cycles. Resolution +-22 cycles. */
static const uint32_t vlm_dur[128] = {
    303618, 534640, 563614, 245780, 505842, 216916, 884484, 1199414,
    939286, 736996, 43534, 43446, 43534, 43424, 43534, 43446,
    43534, 43468, 43534, 43512, 563680, 708154, 303618, 303442,
    274600, 910444, 245648, 43534, 43424, 72420, 43534, 72332,
    43468, 43534, 216806, 448092, 43468, 43534, 361302, 43534,
    43512, 740098, 43534, 43446, 43534, 43468, 43534, 101284,
    43424, 216872, 650228, 43446, 245824, 43468, 216916, 428226,
    43534, 43534, 43490, 43380, 419118, 43534, 43380, 43534,
    43402, 72376, 43512, 43402, 101284, 1343888, 43534, 43446,
    708154, 43468, 43534, 913480, 939176, 43556, 43446, 43534,
    43468, 43556, 43446, 43534, 43468, 43556, 43446, 43512,
    592566, 621474, 43534, 43424, 1176556, 43512, 43534, 43534,
    43446, 43534, 43468, 43534, 43490, 43380, 361324, 43402,
    43490, 43380, 43468, 274710, 1315002, 43424, 43490, 621474,
    43512, 43534, 1054918, 159034, 43534, 43402, 1228300, 43424,
    361368, 43446, 881536, 43468, 43556, 43446, 130214, 43534
};
