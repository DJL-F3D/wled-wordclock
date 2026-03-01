/*
 * This file registers all enabled usermods.
 * Copy this file into wled00/ in the WLED source tree,
 * or add the relevant lines to your existing usermods_list.cpp.
 *
 * IMPORTANT: USERMOD_ID_WORDCLOCK must be defined in const.h (or add it yourself):
 *   #define USERMOD_ID_WORDCLOCK  99   // pick an unused ID
 */

// ── WordClock usermod ────────────────────────────────────────────────────────
#include "../usermods/WordClock/wordclock_usermod.h"

void registerUsermods()
{
  usermods.add(new WordClockUsermod());
}
