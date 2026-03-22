// usermods_list.cpp
// Replaces WLED's stock file to register the word clock usermod.
// Which usermod is compiled is controlled exclusively by the
// -D USERMOD_ID_* flag in platformio_override.ini — only one is
// active per build, preventing duplicate symbol errors.

#include "wled.h"

#ifdef USERMOD_ID_WORDCLOCK
  #include "../usermods/WordClock/wordclock_usermod.h"
#endif

#ifdef USERMOD_ID_WORDCLOCK_8X8
  #include "../usermods/WordClock8x8/wordclock_8x8_usermod.h"
#endif

void registerUsermods()
{
  #ifdef USERMOD_ID_WORDCLOCK
    usermods.add(new WordClockUsermod());
  #endif

  #ifdef USERMOD_ID_WORDCLOCK_8X8
    usermods.add(new WordClock8x8Usermod());
  #endif
}
