
#include "../rdesktop.h"

extern int g_tcp_port_rdp;
int g_use_rdp5 = 0;
char g_hostname[16];
char g_username[64];
int g_height = 600;
int g_width = 800;
int g_server_bpp = 8;
int g_encryption = 1;
int g_desktop_save =1;
int g_bitmap_cache = 1;
int g_bitmap_cache_persist_enable = False;
int g_bitmap_cache_precache = True;
int g_bitmap_compression = 1;
int g_rdp5_performanceflags = 0;
int g_console_session = 0;
int g_keylayout = 0x409; /* Defaults to US keyboard layout */

/*****************************************************************************/
void ui_bell(void)
{
}

/*****************************************************************************/
int ui_select(int in)
{
  return 1;
}

/*****************************************************************************/
void ui_destroy_cursor(void* cursor)
{
}

/*****************************************************************************/
void* ui_create_glyph(int width, int height, uint8* data)
{
  return 0;
}

/*****************************************************************************/
void ui_destroy_glyph(void* glyph)
{
}

/*****************************************************************************/
void ui_destroy_bitmap(void* bmp)
{
}

/*****************************************************************************/
void ui_reset_clip(void)
{
}

/*****************************************************************************/
void ui_set_clip(int x, int y, int cx, int cy)
{
}

/*****************************************************************************/
void* ui_create_colourmap(COLOURMAP * colours)
{
  return 0;
}

/*****************************************************************************/
void ui_set_colourmap(void* map)
{
}

/*****************************************************************************/
HBITMAP ui_create_bitmap(int width, int height, uint8* data)
{
  return 0;
}

/*****************************************************************************/
void ui_draw_text(uint8 font, uint8 flags, int mixmode,
                  int x, int y,
                  int clipx, int clipy, int clipcx, int clipcy,
                  int boxx, int boxy, int boxcx, int boxcy,
                  int bgcolour, int fgcolour, uint8* text, uint8 length)
{
}

/*****************************************************************************/
void ui_line(uint8 opcode, int startx, int starty, int endx, int endy,
             PEN * pen)
{
}

/*****************************************************************************/
void ui_triblt(uint8 opcode, int x, int y, int cx, int cy,
               HBITMAP src, int srcx, int srcy,
               BRUSH* brush, int bgcolour, int fgcolour)
{
}

/*****************************************************************************/
void ui_memblt(uint8 opcode, int x, int y, int cx, int cy,
               HBITMAP src, int srcx, int srcy)
{
}

/*****************************************************************************/
void ui_desktop_restore(uint32 offset, int x, int y, int cx, int cy)
{
}

/*****************************************************************************/
void ui_desktop_save(uint32 offset, int x, int y, int cx, int cy)
{
}

/*****************************************************************************/
void ui_rect(int x, int y, int cx, int cy, int colour)
{
}

/*****************************************************************************/
void ui_screenblt(uint8 opcode, int x, int y, int cx, int cy,
                  int srcx, int srcy)
{
}

/*****************************************************************************/
void ui_patblt(uint8 opcode, int x, int y, int cx, int cy,
               BRUSH * brush, int bgcolour, int fgcolour)
{
}

/*****************************************************************************/
void ui_destblt(uint8 opcode, int x, int y, int cx, int cy)
{
}

/*****************************************************************************/
void ui_move_pointer(int x, int y)
{
}

/*****************************************************************************/
void ui_set_null_cursor(void)
{
}

/*****************************************************************************/
void ui_paint_bitmap(int x, int y, int cx, int cy,
                     int width, int height, uint8* data)
{
}

/*****************************************************************************/
void ui_set_cursor(HCURSOR cursor)
{
}

/*****************************************************************************/
HCURSOR ui_create_cursor(unsigned int x, unsigned int y,
                         int width, int height,
                         uint8* andmask, uint8* xormask)
{
  return 0;
}

/*****************************************************************************/
uint16 ui_get_numlock_state(unsigned int state)
{
  return 0;
}

/*****************************************************************************/
unsigned int read_keyboard_state(void)
{
  return 0;
}

/*****************************************************************************/
void ui_resize_window(void)
{
}

/*****************************************************************************/
void ui_begin_update(void)
{
}

/*****************************************************************************/
void ui_end_update(void)
{
}

/*****************************************************************************/
void generate_random(uint8* random)
{
}

/*****************************************************************************/
void save_licence(uint8* data, int length)
{
}

/*****************************************************************************/
int load_licence(uint8** data)
{
  return 0;
}

/*****************************************************************************/
void* xrealloc(void* in, int size)
{
  return 0;
}

/*****************************************************************************/
void* xmalloc(int size)
{
  return 0;
}

/*****************************************************************************/
void xfree(void* in)
{
}

/*****************************************************************************/
void warning(char* format, ...)
{
}

/*****************************************************************************/
void unimpl(char* format, ...)
{
}

/*****************************************************************************/
void error(char* format, ...)
{
}

/*****************************************************************************/
BOOL rd_pstcache_mkdir(void)
{
  return 0;
}

/*****************************************************************************/
int rd_open_file(char *filename)
{
  return 0;
}

/*****************************************************************************/
void rd_close_file(int fd)
{
  return;
}

/*****************************************************************************/
int rd_read_file(int fd, void *ptr, int len)
{
  return 0;
}

/*****************************************************************************/
int rd_write_file(int fd, void* ptr, int len)
{
  return 0;
}

/*****************************************************************************/
int rd_lseek_file(int fd, int offset)
{
  return 0;
}

/*****************************************************************************/
BOOL rd_lock_file(int fd, int start, int len)
{
  return False;
}

/*****************************************************************************/
int main(int c, char** p)
{
  return 0;
}
