#include <cgreen/mocks.h>
#include "../rdesktop.h"

RD_HCURSOR
cache_get_cursor(uint16 cache_idx)
{
  return (RD_HCURSOR)mock(cache_idx);
}

void
cache_put_cursor(uint16 cache_idx, RD_HCURSOR cursor)
{
  mock(cache_idx, cursor);
}


FONTGLYPH *
cache_get_font(uint8 font, uint16 character)
{
  return (FONTGLYPH *) mock(font, character);
}

DATABLOB *
cache_get_text(uint8 cache_id)
{
  return (DATABLOB *)mock(cache_id);
}

void
cache_put_text(uint8 cache_id, void *data, int length)
{
  mock(cache_id, data, length);
}

uint8 *
cache_get_desktop(uint32 offset, int cx, int cy, int bytes_per_pixel)
{
  return (uint8 *) mock(offset, cx, cy, bytes_per_pixel);
}

void
cache_put_desktop(uint32 offset, int cx, int cy, int scanline,
		  int bytes_per_pixel, uint8 * data)
{
  mock(offset, cx, cy, scanline, bytes_per_pixel, data);
}

void
cache_save_state()
{
  mock();
}
