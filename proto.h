/* bitmap.c */
BOOL bitmap_decompress(unsigned char *output, int width, int height, unsigned char *input, int size,
		       int Bpp);
/* cache.c */
HBITMAP cache_get_bitmap(uint8 cache_id, uint16 cache_idx);
void cache_put_bitmap(uint8 cache_id, uint16 cache_idx, HBITMAP bitmap);
FONTGLYPH *cache_get_font(uint8 font, uint16 character);
void cache_put_font(uint8 font, uint16 character, uint16 offset, uint16 baseline, uint16 width,
		    uint16 height, HGLYPH pixmap);
DATABLOB *cache_get_text(uint8 cache_id);
void cache_put_text(uint8 cache_id, void *data, int length);
uint8 *cache_get_desktop(uint32 offset, int cx, int cy, int bytes_per_pixel);
void cache_put_desktop(uint32 offset, int cx, int cy, int scanline, int bytes_per_pixel,
		       uint8 * data);
HCURSOR cache_get_cursor(uint16 cache_idx);
void cache_put_cursor(uint16 cache_idx, HCURSOR cursor);
/* channels.c */
uint16 get_num_channels(void);
void register_channel(char *name, uint32 flags, void (*callback) (STREAM, uint16));
rdp5_channel *find_channel_by_channelno(uint16 channelno);
rdp5_channel *find_channel_by_num(uint16 num);
void dummy_callback(STREAM s, uint16 channelno);
void channels_init(void);
/* cliprdr.c */
void cliprdr_ipc_format_announce(unsigned char *data, uint16 length);
void cliprdr_handle_SelectionClear(void);
void cliprdr_handle_server_data(uint32 length, uint32 flags, STREAM s);
void cliprdr_handle_server_data_request(STREAM s);
void cliprdr_callback(STREAM s, uint16 channelno);
void cliprdr_ipc_primary_lost(unsigned char *data, uint16 length);
void cliprdr_init(void);
/* ewmhints.c */
int get_current_workarea(uint32 * x, uint32 * y, uint32 * width, uint32 * height);
/* ipc.c */
void ipc_register_ipcnotify(uint16 messagetype, void (*notifycallback) (unsigned char *, uint16));
void ipc_deregister_ipcnotify(uint16 messagetype);
void ipc_init(void);
void ipc_send_message(uint16 messagetype, unsigned char *data, uint16 length);
/* iso.c */
STREAM iso_init(int length);
void iso_send(STREAM s);
STREAM iso_recv(void);
BOOL iso_connect(char *server, char *username);
void iso_disconnect(void);
/* licence.c */
void licence_process(STREAM s);
/* mcs.c */
STREAM mcs_init(int length);
void mcs_send_to_channel(STREAM s, uint16 channel);
void mcs_send(STREAM s);
STREAM mcs_recv(uint16 * channel);
BOOL mcs_connect(char *server, STREAM mcs_data, char *username);
void mcs_disconnect(void);
/* orders.c */
void process_orders(STREAM s, uint16 num_orders);
void reset_order_state(void);
/* rdesktop.c */
int main(int argc, char *argv[]);
void generate_random(uint8 * random);
void *xmalloc(int size);
void *xrealloc(void *oldmem, int size);
void xfree(void *mem);
void error(char *format, ...);
void warning(char *format, ...);
void unimpl(char *format, ...);
void hexdump(unsigned char *p, int len);
int load_licence(unsigned char **data);
void save_licence(unsigned char *data, int length);
/* rdp.c */
void rdp_out_unistr(STREAM s, char *string, int len);
void rdp_send_input(uint32 time, uint16 message_type, uint16 device_flags, uint16 param1,
		    uint16 param2);
void process_null_system_pointer_pdu(STREAM s);
void process_colour_pointer_pdu(STREAM s);
void process_cached_pointer_pdu(STREAM s);
void process_bitmap_updates(STREAM s);
void process_palette(STREAM s);
void rdp_main_loop(void);
BOOL rdp_connect(char *server, uint32 flags, char *domain, char *password, char *command,
		 char *directory);
void rdp_disconnect(void);
/* rdp5.c */
void rdp5_process(STREAM s, BOOL encryption, BOOL shortform);
void rdp5_process_channel(STREAM s, uint16 channelno);
/* secure.c */
void sec_hash_48(uint8 * out, uint8 * in, uint8 * salt1, uint8 * salt2, uint8 salt);
void sec_hash_16(uint8 * out, uint8 * in, uint8 * salt1, uint8 * salt2);
void buf_out_uint32(uint8 * buffer, uint32 value);
void sec_sign(uint8 * signature, int siglen, uint8 * session_key, int keylen, uint8 * data,
	      int datalen);
void sec_decrypt(uint8 * data, int length);
STREAM sec_init(uint32 flags, int maxlen);
void sec_send_to_channel(STREAM s, uint32 flags, uint16 channel);
void sec_send(STREAM s, uint32 flags);
void sec_process_mcs_data(STREAM s);
STREAM sec_recv(void);
BOOL sec_connect(char *server, char *username);
void sec_disconnect(void);
/* tcp.c */
STREAM tcp_init(uint32 maxlen);
void tcp_send(STREAM s);
STREAM tcp_recv(uint32 length);
BOOL tcp_connect(char *server);
void tcp_disconnect(void);
/* xkeymap.c */
void xkeymap_init(void);
BOOL handle_special_keys(uint32 keysym, unsigned int state, uint32 ev_time, BOOL pressed);
key_translation xkeymap_translate_key(uint32 keysym, unsigned int keycode, unsigned int state);
uint16 xkeymap_translate_button(unsigned int button);
char *get_ksname(uint32 keysym);
void ensure_remote_modifiers(uint32 ev_time, key_translation tr);
void reset_modifier_keys(unsigned int state);
void rdp_send_scancode(uint32 time, uint16 flags, uint8 scancode);
/* xwin.c */
BOOL get_key_state(unsigned int state, uint32 keysym);
BOOL ui_init(void);
void ui_deinit(void);
BOOL ui_create_window(void);
void ui_destroy_window(void);
void xwin_toggle_fullscreen(void);
int ui_select(int rdp_socket);
void ui_move_pointer(int x, int y);
HBITMAP ui_create_bitmap(int width, int height, uint8 * data);
void ui_paint_bitmap(int x, int y, int cx, int cy, int width, int height, uint8 * data);
void ui_destroy_bitmap(HBITMAP bmp);
HGLYPH ui_create_glyph(int width, int height, uint8 * data);
void ui_destroy_glyph(HGLYPH glyph);
HCURSOR ui_create_cursor(unsigned int x, unsigned int y, int width, int height, uint8 * andmask,
			 uint8 * xormask);
void ui_set_cursor(HCURSOR cursor);
void ui_destroy_cursor(HCURSOR cursor);
HCOLOURMAP ui_create_colourmap(COLOURMAP * colours);
void ui_destroy_colourmap(HCOLOURMAP map);
void ui_set_colourmap(HCOLOURMAP map);
void ui_set_clip(int x, int y, int cx, int cy);
void ui_reset_clip(void);
void ui_bell(void);
void ui_destblt(uint8 opcode, int x, int y, int cx, int cy);
void ui_patblt(uint8 opcode, int x, int y, int cx, int cy, BRUSH * brush, int bgcolour,
	       int fgcolour);
void ui_screenblt(uint8 opcode, int x, int y, int cx, int cy, int srcx, int srcy);
void ui_memblt(uint8 opcode, int x, int y, int cx, int cy, HBITMAP src, int srcx, int srcy);
void ui_triblt(uint8 opcode, int x, int y, int cx, int cy, HBITMAP src, int srcx, int srcy,
	       BRUSH * brush, int bgcolour, int fgcolour);
void ui_line(uint8 opcode, int startx, int starty, int endx, int endy, PEN * pen);
void ui_rect(int x, int y, int cx, int cy, int colour);
void ui_draw_glyph(int mixmode, int x, int y, int cx, int cy, HGLYPH glyph, int srcx, int srcy,
		   int bgcolour, int fgcolour);
void ui_draw_text(uint8 font, uint8 flags, int mixmode, int x, int y, int clipx, int clipy,
		  int clipcx, int clipcy, int boxx, int boxy, int boxcx, int boxcy, int bgcolour,
		  int fgcolour, uint8 * text, uint8 length);
void ui_desktop_save(uint32 offset, int x, int y, int cx, int cy);
void ui_desktop_restore(uint32 offset, int x, int y, int cx, int cy);
