#include <cgreen/mocks.h>
#include "../rdesktop.h"

void ui_get_screen_size(uint32 *width, uint32 *height)
{
  mock(width, height);
}

void ui_get_screen_size_from_percentage(uint32 pw, uint32 ph, uint32 *width, uint32 *height)
{
  mock(pw, ph, width, height);
}

void ui_get_workarea_size(uint32 *width, uint32 *height)
{
  mock(width, height);
}
