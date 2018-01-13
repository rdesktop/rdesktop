#include <cgreen/cgreen.h>
#include <cgreen/mocks.h>
#include "../rdesktop.h"
#include "../proto.h"
#include <locale.h>
#include <langinfo.h>

#define always_expect_error_log() always_expect(logger, when(lvl, is_equal_to(Error)))

/* Boilerplate */
Describe(ParseGeometry);
BeforeEach(ParseGeometry) {};
AfterEach(ParseGeometry) {};

/* Global Variables.. :( */
int g_tcp_port_rdp;
RDPDR_DEVICE g_rdpdr_device[16];
uint32 g_num_devices;
char *g_rdpdr_clientname;
RD_BOOL g_using_full_workarea;

#define PACKAGE_VERSION "test"

#include "../rdesktop.c"


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

