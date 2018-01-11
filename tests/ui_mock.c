#include <cgreen/mocks.h>
#include "../rdesktop.h"

time_t g_wait_for_deactivate_ts;

/* Mock implementation of F1 */
void ui_resize_window(uint32 width, uint32 height)
{
  mock(width, height);
}

void ui_set_cursor(RD_HCURSOR cursor)
{
  mock(cursor);
}

void ui_set_standard_cursor()
{
  mock();
}

void ui_bell()
{
  mock();
}

void ui_paint_bitmap(int x, int y, int cx, int cy, int width, int height, uint8 * data)
{
  mock(x,y,cx,cy,width,height,data);
}

void ui_begin_update()
{
  mock();
}

RD_HCOLOURMAP ui_create_colourmap(COLOURMAP * colours)
{
  return (RD_HCOLOURMAP) mock(colours);
}

RD_HCURSOR ui_create_cursor(unsigned int x, unsigned int y, uint32 width, uint32 height, uint8 * andmask,
                            uint8 * xormask, int bpp)
{
  return (RD_HCURSOR) mock(x, y, width, height, andmask, xormask, bpp);
}

void ui_end_update()
{
  mock();
}

void ui_move_pointer(int x, int y)
{
  mock(x, y);
}

void ui_set_colourmap(RD_HCOLOURMAP map)
{
  mock(map);
}

void ui_set_null_cursor()
{
  mock();
}


void ui_set_clip(int x,int y, int cx, int cy)
{
  mock(x,y,cx,cy);
}

RD_BOOL
ui_create_window(uint32 width, uint32 height)
{
  return mock(width, height);
}

void
ui_deinit()
{
  mock();
}

void
ui_destroy_window()
{
  mock();
}

void
ui_init_connection()
{
  mock();
}

RD_BOOL
ui_have_window()
{
  return mock();
}

RD_BOOL
ui_init()
{
  return mock();
}

void
ui_reset_clip()
{
  mock();
}

void
ui_seamless_end()
{
  mock();
}
