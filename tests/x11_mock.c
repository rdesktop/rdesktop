#include <cgreen/mocks.h>
#include "../rdesktop.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>


XClassHint *XAllocClassHint()
{
  return (XClassHint *)mock();
}
