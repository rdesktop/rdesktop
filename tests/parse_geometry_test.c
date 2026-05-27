#include "../rdesktop.h"
#include "../proto.h"
#include <locale.h>
#include <langinfo.h>

/* Global Variables.. :( */
int g_tcp_port_rdp;
RDPDR_DEVICE g_rdpdr_device[16];
uint32 g_num_devices;
char *g_rdpdr_clientname;
RD_BOOL g_using_full_workarea;

#define PACKAGE_VERSION "test"

#include "../rdesktop.c"

#include <cgreen/cgreen.h>
#include <cgreen/mocks.h>

#define always_expect_error_log() always_expect(logger, when(lvl, is_equal_to(Error)))

int
xgui_startup(int *argc, char ***argv)
{
	(void) argc;
	(void) argv;
	return 0;
}

void
xgui_message_dialog(const char *title, const char *message, const char *button_label)
{
	(void) title;
	(void) message;
	(void) button_label;
}

/* Boilerplate */
Describe(ParseGeometry);
BeforeEach(ParseGeometry) {};
AfterEach(ParseGeometry) {};


Ensure(ParseGeometry, HandlesWxH)
{
  g_requested_session_width = g_requested_session_height = 0;

  assert_that(parse_geometry_string("1234x2345"), is_equal_to(0));

  assert_that(g_requested_session_width, is_equal_to(1234));
  assert_that(g_requested_session_height, is_equal_to(2345));
  assert_that(g_window_size_type, is_equal_to(Fixed));
}

Ensure(ParseGeometry, FailsOnMissingHeight)
{

  always_expect_error_log();

  g_requested_session_width = g_requested_session_height = 0;
  assert_that(parse_geometry_string("1234"), is_equal_to(-1));

  assert_that(g_requested_session_width, is_equal_to(1234));
  assert_that(g_requested_session_height, is_equal_to(0));
  assert_that(g_window_size_type, is_equal_to(Fixed));
}

Ensure(ParseGeometry, FailsOnMissingHeightVariant2)
{
  always_expect_error_log();

  g_requested_session_width = g_requested_session_height = 0;
  assert_that(parse_geometry_string("1234x"), is_equal_to(-1));

  assert_that(g_requested_session_width, is_equal_to(1234));
  assert_that(g_requested_session_height, is_equal_to(0));
  assert_that(g_window_size_type, is_equal_to(Fixed));
}

Ensure(ParseGeometry, HandlesPercentageOfScreen)
{
  g_requested_session_width = g_requested_session_height = 0;

  assert_that(parse_geometry_string("80%"), is_equal_to(0));

  assert_that(g_requested_session_width, is_equal_to(80));
  assert_that(g_requested_session_height, is_equal_to(80));
  assert_that(g_window_size_type, is_equal_to(PercentageOfScreen));
}

Ensure(ParseGeometry, HandlesSpecificWidthAndHeightPercentageOfScreen)
{
  g_requested_session_width = g_requested_session_height = 0;

  assert_that(parse_geometry_string("100%x60%"), is_equal_to(0));

  assert_that(g_requested_session_width, is_equal_to(100));
  assert_that(g_requested_session_height, is_equal_to(60));
  assert_that(g_window_size_type, is_equal_to(PercentageOfScreen));
}

Ensure(ParseGeometry, HandlesSpecifiedDPI)
{
  g_dpi = g_requested_session_width = g_requested_session_height = 0;

  assert_that(parse_geometry_string("1234x2345@234"), is_equal_to(0));

  assert_that(g_dpi, is_equal_to(234));
  assert_that(g_requested_session_width, is_equal_to(1234));
  assert_that(g_requested_session_height, is_equal_to(2345));
  assert_that(g_window_size_type, is_equal_to(Fixed));
}


Ensure(ParseGeometry, HandlesSpecifiedXPosition)
{
  g_xpos = g_ypos = g_requested_session_width = g_requested_session_height = 0;

  assert_that(parse_geometry_string("1234x2345+123"), is_equal_to(0));

  assert_that(g_xpos, is_equal_to(123));
  assert_that(g_ypos, is_equal_to(0));
  assert_that(g_pos, is_equal_to(1));
  assert_that(g_requested_session_width, is_equal_to(1234));
  assert_that(g_requested_session_height, is_equal_to(2345));
  assert_that(g_window_size_type, is_equal_to(Fixed));
}

Ensure(ParseGeometry, HandlesSpecifiedNegativeXPosition)
{
  g_ypos = g_xpos = g_requested_session_width = g_requested_session_height = 0;

  assert_that(parse_geometry_string("1234x2345-500"), is_equal_to(0));

  assert_that(g_xpos, is_equal_to(-500));
  assert_that(g_ypos, is_equal_to(0));
  assert_that(g_pos, is_equal_to(2));
  assert_that(g_requested_session_width, is_equal_to(1234));
  assert_that(g_requested_session_height, is_equal_to(2345));
  assert_that(g_window_size_type, is_equal_to(Fixed));
}

Ensure(ParseGeometry, HandlesSpecifiedNegativeXAndYPosition)
{
  g_ypos = g_xpos = g_requested_session_width = g_requested_session_height = 0;

  assert_that(parse_geometry_string("1234x2345-500-501"), is_equal_to(0));

  assert_that(g_xpos, is_equal_to(-500));
  assert_that(g_ypos, is_equal_to(-501));
  assert_that(g_pos, is_equal_to(2 | 4));
  assert_that(g_requested_session_width, is_equal_to(1234));
  assert_that(g_requested_session_height, is_equal_to(2345));
  assert_that(g_window_size_type, is_equal_to(Fixed));
}

Ensure(ParseGeometry, HandlesSpecifiedXandYPosition)
{
  g_xpos = g_ypos = g_requested_session_width = g_requested_session_height = 0;

  assert_that(parse_geometry_string("1234x2345+123+234"), is_equal_to(0));

  assert_that(g_xpos, is_equal_to(123));
  assert_that(g_ypos, is_equal_to(234));
  assert_that(g_pos, is_equal_to(1));
  assert_that(g_requested_session_width, is_equal_to(1234));
  assert_that(g_requested_session_height, is_equal_to(2345));
  assert_that(g_window_size_type, is_equal_to(Fixed));
}

Ensure(ParseGeometry, HandlesSpecifiedXandYPositionWithDPI)
{
  g_dpi = g_xpos = g_ypos = g_requested_session_width = g_requested_session_height = 0;

  assert_that(parse_geometry_string("1234x2345@678+123+234"), is_equal_to(0));

  assert_that(g_dpi, is_equal_to(678));
  assert_that(g_xpos, is_equal_to(123));
  assert_that(g_ypos, is_equal_to(234));
  assert_that(g_requested_session_width, is_equal_to(1234));
  assert_that(g_requested_session_height, is_equal_to(2345));
  assert_that(g_window_size_type, is_equal_to(Fixed));
}

Ensure(ParseGeometry, HandlesSpecialNameWorkarea)
{
  assert_that(parse_geometry_string("workarea"), is_equal_to(0));

  assert_that(g_window_size_type, is_equal_to(Workarea));
}


Ensure(ParseGeometry, FailsOnNegativeDPI)
{
  always_expect_error_log();

  assert_that(parse_geometry_string("1234x2345@-105"), is_equal_to(-1));
}


Ensure(ParseGeometry, FailsOnNegativeWidth)
{
  always_expect_error_log();

  assert_that(parse_geometry_string("-1234x2345"), is_equal_to(-1));
}


Ensure(ParseGeometry, FailsOnNegativeHeight)
{
  always_expect_error_log();

  assert_that(parse_geometry_string("1234x-2345"), is_equal_to(-1));
}

Ensure(ParseGeometry, FailsOnMixingPixelsAndPercents)
{
  always_expect_error_log();

  g_window_size_type = Fixed;
  assert_that(parse_geometry_string("1234%x2345"), is_equal_to(-1));

  g_window_size_type = Fixed;
  assert_that(parse_geometry_string("1234x2345%"), is_equal_to(-1));
}

Ensure(ParseGeometry, FailsOnGarbageAtEndOfString)
{
  always_expect_error_log();

  g_window_size_type = Fixed;
  assert_that(parse_geometry_string("1234%1239123081232345abcdefgadkfjafa4af048"), is_equal_to(-1));

  g_window_size_type = Fixed;
  assert_that(parse_geometry_string("1235abcer9823461"), is_equal_to(-1));

  g_window_size_type = Fixed;
  assert_that(parse_geometry_string("1235%x123%+123123+123123asdkjfasdf"), is_equal_to(-1));

  g_window_size_type = Fixed;
  assert_that(parse_geometry_string("1235%x123%@123asdkjfasdf"), is_equal_to(-1));

  g_window_size_type = Fixed;
  assert_that(parse_geometry_string("1235%x123%@123+1-2asdkjfasdf"), is_equal_to(-1));
}


static void
write_test_rdp_file(char *path, size_t path_size, const char *contents)
{
  int fd;
  FILE *fp;

  STRNCPY(path, "/tmp/rdesktop-rdp-file-test-XXXXXX", path_size);
  fd = mkstemp(path);
  assert_that(fd, is_greater_than(-1));

  fp = fdopen(fd, "w");
  assert_that(fp, is_not_null);
  fputs(contents, fp);
  fclose(fp);
}

Ensure(ParseGeometry, ParsesRdpFileSettings)
{
  char path[64];
  char server[256] = "";
  char domain[256] = "";
  char shell[256] = "";
  char directory[256] = "";

  g_username = NULL;
  g_password[0] = 0;
  g_keymapname[0] = 0;
  g_tcp_port_rdp = 3389;
  g_server_depth = -1;
  g_requested_session_width = 0;
  g_requested_session_height = 0;
  g_window_size_type = Fixed;
  g_fullscreen = False;
  g_rdpclip = True;

  write_test_rdp_file(path, sizeof(path),
                      "full address:s:rdp.example.com\n"
                      "server port:i:3390\n"
                      "username:s:alice\n"
                      "domain:s:EXAMPLE\n"
                      "desktopwidth:i:1280\n"
                      "desktopheight:i:720\n"
                      "session bpp:i:24\n"
                      "alternate shell:s:notepad.exe\n"
                      "shell working directory:s:C:\\Temp\n"
                      "keyboard layout:s:en-us\n"
                      "redirectclipboard:i:0\n");

  assert_that(parse_rdp_file(path, server, sizeof(server), domain, sizeof(domain), shell,
                             sizeof(shell), directory, sizeof(directory), False, False,
                             False, False, False, False, False, False, False),
              is_equal_to(True));

  assert_that(server, is_equal_to_string("rdp.example.com"));
  assert_that(g_tcp_port_rdp, is_equal_to(3390));
  assert_that(g_username, is_equal_to_string("alice"));
  assert_that(domain, is_equal_to_string("EXAMPLE"));
  assert_that(shell, is_equal_to_string("notepad.exe"));
  assert_that(directory, is_equal_to_string("C:\\Temp"));
  assert_that(g_keymapname, is_equal_to_string("en-us"));
  assert_that(g_requested_session_width, is_equal_to(1280));
  assert_that(g_requested_session_height, is_equal_to(720));
  assert_that(g_server_depth, is_equal_to(24));
  assert_that(g_rdpclip, is_equal_to(False));

  unlink(path);
  xfree(g_username);
  g_username = NULL;
}

Ensure(ParseGeometry, RdpFileDoesNotOverrideExplicitOptions)
{
  char path[64];
  char server[256] = "";
  char domain[256] = "cli-domain";
  char shell[256] = "cli-shell";
  char directory[256] = "cli-dir";

  g_username = xstrdup("cli-user");
  g_password[0] = 0;
  STRNCPY(g_keymapname, "cli-keymap", sizeof(g_keymapname));
  g_server_depth = 16;
  g_requested_session_width = 1024;
  g_requested_session_height = 768;
  g_window_size_type = Fixed;
  g_fullscreen = False;

  write_test_rdp_file(path, sizeof(path),
                      "full address:s:rdp.example.com\n"
                      "username:s:file-user\n"
                      "domain:s:FILE\n"
                      "desktopwidth:i:1280\n"
                      "desktopheight:i:720\n"
                      "session bpp:i:24\n"
                      "alternate shell:s:file-shell\n"
                      "shell working directory:s:file-dir\n"
                      "keyboard layout:s:file-keymap\n");

  assert_that(parse_rdp_file(path, server, sizeof(server), domain, sizeof(domain), shell,
                             sizeof(shell), directory, sizeof(directory), True, False,
                             True, True, True, True, True, False, True),
              is_equal_to(True));

  assert_that(server, is_equal_to_string("rdp.example.com"));
  assert_that(g_username, is_equal_to_string("cli-user"));
  assert_that(domain, is_equal_to_string("cli-domain"));
  assert_that(shell, is_equal_to_string("cli-shell"));
  assert_that(directory, is_equal_to_string("cli-dir"));
  assert_that(g_keymapname, is_equal_to_string("cli-keymap"));
  assert_that(g_requested_session_width, is_equal_to(1024));
  assert_that(g_requested_session_height, is_equal_to(768));
  assert_that(g_server_depth, is_equal_to(16));

  unlink(path);
  xfree(g_username);
  g_username = NULL;
}
