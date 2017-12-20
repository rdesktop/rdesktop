#include <cgreen/cgreen.h>
#include <cgreen/mocks.h>

#include "../rdesktop.h"

uint32 utils_djb2_hash(const char *str) { return mock(str); }
char *utils_string_escape(const char *str) { return (char *)mock(str); }
char *utils_string_unescape(const char *str) { return (char *)mock(str); }
int utils_locale_to_utf8(const char *src, size_t is, char *dest, size_t os) { return mock(src, is, dest, os); }
int utils_mkdir_safe(const char *path, int mask) { return mock(path, mask); }
int utils_mkdir_p(const char *path, int mask) { return mock(path, mask); }
void utils_calculate_dpi_scale_factors(uint32 width, uint32 height, uint32 dpi,
				       uint32 *physwidth, uint32 *physheight,
				       uint32 *desktopscale, uint32 *devicescale) { mock(width, height, dpi, physwidth, physheight, desktopscale, devicescale); }
void utils_apply_session_size_limitations(uint32 *width, uint32 *height) { mock(width, height); }

void logger(log_subject_t c, log_level_t lvl, char *format, ...) { mock(c, lvl, format); }
void logger_set_verbose(int verbose) { mock(verbose); }
void logger_set_subjects(char *subjects) { mock(subjects); }
