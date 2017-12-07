#include <cgreen/cgreen.h>
#include <cgreen/mocks.h>
#include "../rdesktop.h"

/* Boilerplate */
Describe(Utils);
BeforeEach(Utils) {};
AfterEach(Utils) {};

/* globals */
char g_codepage[16];

#include "../utils.c"

/* malloc; exit if out of memory */
void *
xmalloc(int size)
{
	void *mem = malloc(size);
	if (mem == NULL)
	{
		logger(Core, Error, "xmalloc, failed to allocate %d bytes", size);
		exit(EX_UNAVAILABLE);
	}
	return mem;
}

/* Exit on NULL pointer. Use to verify result from XGetImage etc */
void
exit_if_null(void *ptr)
{
	if (ptr == NULL)
	{
		logger(Core, Error, "unexpected null pointer. Out of memory?");
		exit(EX_UNAVAILABLE);
	}
}

/* strdup */
char *
xstrdup(const char *s)
{
	char *mem = strdup(s);
	if (mem == NULL)
	{
		logger(Core, Error, "xstrdup(), strdup() failed: %s", strerror(errno));
		exit(EX_UNAVAILABLE);
	}
	return mem;
}

/* realloc; exit if out of memory */
void *
xrealloc(void *oldmem, size_t size)
{
	void *mem;

	if (size == 0)
		size = 1;
	mem = realloc(oldmem, size);
	if (mem == NULL)
	{
		logger(Core, Error, "xrealloc, failed to reallocate %ld bytes", size);
		exit(EX_UNAVAILABLE);
	}
	return mem;
}

/* free */
void
xfree(void *mem)
{
	free(mem);
}


/* Test function */
Ensure(Utils, CalculateDpiScaleFactorsWhenDpiIsZero) {
  uint32 physical_width, physical_height, desktop_scale, device_scale;

  utils_calculate_dpi_scale_factors(1024, 768, 0,
				    &physical_width, &physical_height,
				    &desktop_scale, &device_scale);

  assert_that(physical_width, is_equal_to(0));
  assert_that(physical_height, is_equal_to(0));
  assert_that(desktop_scale, is_equal_to(0));
  assert_that(device_scale, is_equal_to(0));
}

Ensure(Utils, CalculateDpiScaleFactorsWhenDpiLessThan96) {
  uint32 physical_width, physical_height, desktop_scale, device_scale;

  utils_calculate_dpi_scale_factors(1024, 768, 95,
				    &physical_width, &physical_height,
				    &desktop_scale, &device_scale);

  assert_that(physical_width, is_equal_to(273));
  assert_that(physical_height, is_equal_to(205));
  assert_that(desktop_scale, is_equal_to(100));
  assert_that(device_scale, is_equal_to(100));
}

Ensure(Utils, CalculateDpiScaleFactorsWhenDpiLessThan134) {
  uint32 physical_width, physical_height, desktop_scale, device_scale;

  utils_calculate_dpi_scale_factors(1024, 768, 133,
				    &physical_width, &physical_height,
				    &desktop_scale, &device_scale);

  assert_that(physical_width, is_equal_to(195));
  assert_that(physical_height, is_equal_to(146));
  assert_that(desktop_scale, is_equal_to(139));
  assert_that(device_scale, is_equal_to(100));
}

Ensure(Utils, CalculateDpiScaleFactorsWhenDpiLessThan173) {
  uint32 physical_width, physical_height, desktop_scale, device_scale;

  utils_calculate_dpi_scale_factors(1024, 768, 172,
				    &physical_width, &physical_height,
				    &desktop_scale, &device_scale);

  assert_that(physical_width, is_equal_to(151));
  assert_that(physical_height, is_equal_to(113));
  assert_that(desktop_scale, is_equal_to(179));
  assert_that(device_scale, is_equal_to(140));
}

Ensure(Utils, CalculateDpiScaleFactorsWhenDpiGreaterThanOrEqualTo173) {
  uint32 physical_width, physical_height, desktop_scale, device_scale;

  utils_calculate_dpi_scale_factors(1024, 768, 173,
				    &physical_width, &physical_height,
				    &desktop_scale, &device_scale);

  assert_that(physical_width, is_equal_to(150));
  assert_that(physical_height, is_equal_to(112));
  assert_that(desktop_scale, is_equal_to(180));
  assert_that(device_scale, is_equal_to(180));
}


Ensure(Utils, ApplySessionSizeLimitationLimitsWidthAndHeightToMax8192)
{
  uint32 width, height;

  width = height = 90000;

  utils_apply_session_size_limitations(&width, &height);

  assert_that(width, is_equal_to(8192));
  assert_that(height, is_equal_to(8192));
}


Ensure(Utils, ApplySessionSizeLimitationLimitsWidthAndHeightToMin200)
{
  uint32 width, height;

  width = height = 100;

  utils_apply_session_size_limitations(&width, &height);

  assert_that(width, is_equal_to(200));
  assert_that(height, is_equal_to(200));
}

Ensure(Utils, ApplySessionSizeLimitationRoundsWidthToClosestSmallerEvenNumber)
{
  uint32 width, height;

  width = height = 201;

  utils_apply_session_size_limitations(&width, &height);

  assert_that(width, is_equal_to(200));
  assert_that(height, is_equal_to(201));
}
