#include <cgreen/mocks.h>
#include "../rdesktop.h"

RD_BOOL bitmap_decompress(uint8 * output, int width, int height, uint8 * input, int size, int Bpp)
{
  return mock(output, width, height, input, size, Bpp);
};
