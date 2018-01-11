#include <cgreen/mocks.h>
#include "../rdesktop.h"

#include <X11/Xlib.h>

unsigned int read_keyboard_state()
{
  return mock();
}

void
xkeymap_init()
{
  mock();
}

void
xkeymap_send_keys(uint32 keysym, unsigned int keycode, unsigned int state, uint32 ev_time,
		  RD_BOOL pressed, uint8 nesting)
{
  mock(keysym, keycode, state, ev_time, pressed, nesting);
}

uint16
xkeymap_translate_button(unsigned int button, uint16 * input_type)
{
  return mock(button, input_type);
}

RD_BOOL
handle_special_keys(uint32 keysym, unsigned int state, uint32 ev_time, RD_BOOL pressed)
{
  return mock(keysym, state, ev_time, pressed);
}

void
set_keypress_keysym(unsigned int keycode, KeySym keysym)
{
  mock(keycode, keysym);
}

KeySym
reset_keypress_keysym(unsigned int keycode, KeySym keysym)
{
  return mock(keycode, keysym);
}

void
reset_modifier_keys()
{
  mock();
}

char *
get_ksname(uint32 keysym)
{
  return (char *) mock(keysym);
}

uint16 ui_get_numlock_state(unsigned int state)
{
  return mock(state);
}

RD_BOOL
xkeymap_from_locale(const char *locale)
{
  return mock(locale);
}
