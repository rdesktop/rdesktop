/*
   rdesktop: A Remote Desktop Protocol client.
   User interface services - QT Window System
   Copyright (C) Matthew Chapman 1999-2002
   qtwin.cpp by Jay Sorg

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "../rdesktop.h"

#include <qapplication.h>
#include <qmainwindow.h>
#include <qwidget.h>
#include <qpainter.h>
#include <qpixmap.h>
#include <qbrush.h>
#include <qimage.h>
#include <qbitmap.h>
#include <qcursor.h>
#include <qsocketnotifier.h>
#include <qscrollview.h>
#include <qfile.h>

#include "qtwin.h"
#include <unistd.h> // gethostname
#include <pwd.h> // getpwuid
#include <stdarg.h> // va_list va_start va_end

/* rdesktop globals */
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
int g_bitmap_compression = 1;
int g_rdp5_performanceflags = 0;
int g_console_session = 0;
int g_keylayout = 0x409; /* Defaults to US keyboard layout */

/* hack globals */
int g_argc = 0;
char** g_argv = 0;
int UpAndRunning = 0;
int g_sock = 0;
int deactivated = 0;
uint32 ext_disc_reason = 0;
char g_servername[128];
char g_title[128] = "";

/* qt globals */
QSocketNotifier* SocketNotifier;
QApplication* App;
QMyMainWindow* MW;
QMyScrollView* SV;
QPixmap* BS;
QPixmap* DS;
QPainter* P1;
QPainter* P2;
QColor Color1;
QColor Color2;
struct QColorMap
{
  uint32 RGBColors[256];
  int NumColors;
};
QColorMap* CM = 0;
QRegion* ClipRect;

Qt::RasterOp OpCodes[16] = {
    Qt::ClearROP,        // BLACKNESS     0
    Qt::NorROP,          // NOTSRCERASE   DSon
    Qt::NotAndROP,       //               DSna
    Qt::NotCopyROP,      // NOTSRCCOPY    Sn
    Qt::AndNotROP,       // SRCERASE      SDna
    Qt::NotROP,          // DSTINVERT     Dn
    Qt::XorROP,          // SRCINVERT     DSx
    Qt::NandROP,         //               DSan
    Qt::AndROP,          // SRCAND        DSa
    Qt::NotXorROP,       //               DSxn
    Qt::NopROP,          //               D
    Qt::NotOrROP,        // MERGEPAINT    DSno
    Qt::CopyROP,         // SRCCOPY       S
    Qt::OrNotROP,        //               SDno
    Qt::OrROP,           // SRCPAINT      DSo
    Qt::SetROP};         // WHITENESS     1

//*****************************************************************************
uint32 Color15to32(uint32 InColor)
{
  uint32 r, g, b;
  r = (InColor & 0x7c00) >> 10;
  r = (r * 0xff) / 0x1f;
  g = (InColor & 0x03e0) >> 5;
  g = (g * 0xff) / 0x1f;
  b = (InColor & 0x001f);
  b = (b * 0xff) / 0x1f;
  return (r << 16) | (g << 8) | b;
}

//*****************************************************************************
uint32 Color16to32(uint32 InColor)
{
  uint32 r, g, b;
  r = (InColor & 0xf800) >> 11;
  r = (r * 0xff) / 0x1f;
  g = (InColor & 0x07e0) >> 5;
  g = (g * 0xff) / 0x3f;
  b = (InColor & 0x001f);
  b = (b * 0xff) / 0x1f;
  return (r << 16) | (g << 8) | b;
}

//*****************************************************************************
uint32 Color24to32(uint32 InColor)
{
  return ((InColor & 0x00ff0000) >> 16) | ((InColor & 0x000000ff) << 16) |
          (InColor & 0x0000ff00);
}

//*****************************************************************************
void SetColorx(QColor* Color, uint32 InColor)
{
  switch (g_server_bpp)
  {
    case 8:
      if (CM == NULL || InColor > 255)
      {
        Color->setRgb(0);
        return;
      }
      Color->setRgb(CM->RGBColors[InColor]);
      break;
    case 15:
      Color->setRgb(Color15to32(InColor));
      break;
    case 16:
      Color->setRgb(Color16to32(InColor));
      break;
    case 24:
      Color->setRgb(Color24to32(InColor));
      break;
    default:
      Color->setRgb(0);
  }
}

//*****************************************************************************
void SetOpCode(int opcode)
{
  if (opcode >= 0 && opcode < 16)
  {
    Qt::RasterOp op = OpCodes[opcode];
    if (op != Qt::CopyROP)
    {
      P1->setRasterOp(op);
      P2->setRasterOp(op);
    }
  }
}

//*****************************************************************************
void ResetOpCode(int opcode)
{
  if (opcode >= 0 && opcode < 16)
  {
    Qt::RasterOp op = OpCodes[opcode];
    if (op != Qt::CopyROP)
    {
      P1->setRasterOp(Qt::CopyROP);
      P2->setRasterOp(Qt::CopyROP);
    }
  }
}

/*****************************************************************************/
QMyMainWindow::QMyMainWindow() : QWidget()
{
}

/*****************************************************************************/
QMyMainWindow::~QMyMainWindow()
{
}

//*****************************************************************************
void QMyMainWindow::mouseMoveEvent(QMouseEvent* e)
{
  if (!UpAndRunning)
    return;
  rdp_send_input(0, RDP_INPUT_MOUSE, MOUSE_FLAG_MOVE, e->x(), e->y());
}

//*****************************************************************************
void QMyMainWindow::mousePressEvent(QMouseEvent* e)
{
  if (!UpAndRunning)
    return;
  if (e->button() == LeftButton)
    rdp_send_input(0, RDP_INPUT_MOUSE, MOUSE_FLAG_DOWN | MOUSE_FLAG_BUTTON1,
                   e->x(), e->y());
  else if (e->button() == RightButton)
    rdp_send_input(0, RDP_INPUT_MOUSE, MOUSE_FLAG_DOWN | MOUSE_FLAG_BUTTON2,
                   e->x(), e->y());
  else if (e->button() == MidButton)
    rdp_send_input(0, RDP_INPUT_MOUSE, MOUSE_FLAG_DOWN | MOUSE_FLAG_BUTTON3,
                   e->x(), e->y());
}

//*****************************************************************************
void QMyMainWindow::mouseReleaseEvent(QMouseEvent* e)
{
  if (!UpAndRunning)
    return;
  if (e->button() == LeftButton)
    rdp_send_input(0, RDP_INPUT_MOUSE, MOUSE_FLAG_BUTTON1, e->x(), e->y());
  else if (e->button() == RightButton)
    rdp_send_input(0, RDP_INPUT_MOUSE, MOUSE_FLAG_BUTTON2, e->x(), e->y());
  else if (e->button() == MidButton)
    rdp_send_input(0, RDP_INPUT_MOUSE, MOUSE_FLAG_BUTTON3, e->x(), e->y());
}

//*****************************************************************************
void QMyMainWindow::wheelEvent(QWheelEvent* e)
{
  if (!UpAndRunning)
    return;
  if (e->delta() > 0)
    rdp_send_input(0, RDP_INPUT_MOUSE, MOUSE_FLAG_BUTTON4, e->x(), e->y());
  else if (e->delta() < 0)
    rdp_send_input(0, RDP_INPUT_MOUSE, MOUSE_FLAG_BUTTON5, e->x(), e->y());
}

//*****************************************************************************
int GetScanCode(QKeyEvent* e)
{
  int Key = e->key();
  int ScanCode = 0;
  Qt::ButtonState bs = e->state();
  if (!(bs & Qt::ShiftButton)) // shift is not down
  {
    if (Key == 42) // *
      return 0x37;
    if (Key == 43) // +
      return 0x4e;
  }
  switch (Key)
  {
    case 4100: ScanCode = 0x1c; break; // enter
    case 4101: ScanCode = 0x1c; break;
    case 4117: ScanCode = 0xd0; break; // down arrow
    case 4115: ScanCode = 0xc8; break; // up arrow
    case 4114: ScanCode = 0xcb; break; // left arrow
    case 4116: ScanCode = 0xcd; break; // right arrow
    case 4112: ScanCode = 0xc7; break; // home
    case 4113: ScanCode = 0xcf; break; // end
    case 4102: ScanCode = 0xd2; break; // insert
    case 4103: ScanCode = 0xd3; break; // delete
    case 4118: ScanCode = 0xc9; break; // page up
    case 4119: ScanCode = 0xd1; break; // page down
    case 4143: ScanCode = 0x00; break; // num lock
    case 47:   ScanCode = 0x35; break; // /
    case 42:   ScanCode = 0x37; break; // *
    case 45:   ScanCode = 0x0c; break; // -
    case 95:   ScanCode = 0x0c; break; // _
    case 43:   ScanCode = 0x0d; break; // +
    case 46:   ScanCode = 0x34; break; // .
    case 48:   ScanCode = 0x0b; break; // 0
    case 41:   ScanCode = 0x0b; break; // )
    case 49:   ScanCode = 0x02; break; // 1
    case 33:   ScanCode = 0x02; break; // !
    case 50:   ScanCode = 0x03; break; // 2
    case 64:   ScanCode = 0x03; break; // @
    case 51:   ScanCode = 0x04; break; // 3
    case 35:   ScanCode = 0x04; break; // #
    case 52:   ScanCode = 0x05; break; // 4
    case 36:   ScanCode = 0x05; break; // $
    case 53:   ScanCode = 0x06; break; // 5
    case 37:   ScanCode = 0x06; break; // %
    case 54:   ScanCode = 0x07; break; // 6
    case 94:   ScanCode = 0x07; break; // ^
    case 55:   ScanCode = 0x08; break; // 7
    case 38:   ScanCode = 0x08; break; // &
    case 56:   ScanCode = 0x09; break; // 8
    case 57:   ScanCode = 0x0a; break; // 9
    case 40:   ScanCode = 0x0a; break; // (
    case 61:   ScanCode = 0x0d; break; // =
    case 65:   ScanCode = 0x1e; break; // a
    case 66:   ScanCode = 0x30; break; // b
    case 67:   ScanCode = 0x2e; break; // c
    case 68:   ScanCode = 0x20; break; // d
    case 69:   ScanCode = 0x12; break; // e
    case 70:   ScanCode = 0x21; break; // f
    case 71:   ScanCode = 0x22; break; // g
    case 72:   ScanCode = 0x23; break; // h
    case 73:   ScanCode = 0x17; break; // i
    case 74:   ScanCode = 0x24; break; // j
    case 75:   ScanCode = 0x25; break; // k
    case 76:   ScanCode = 0x26; break; // l
    case 77:   ScanCode = 0x32; break; // m
    case 78:   ScanCode = 0x31; break; // n
    case 79:   ScanCode = 0x18; break; // o
    case 80:   ScanCode = 0x19; break; // p
    case 81:   ScanCode = 0x10; break; // q
    case 82:   ScanCode = 0x13; break; // r
    case 83:   ScanCode = 0x1f; break; // s
    case 84:   ScanCode = 0x14; break; // t
    case 85:   ScanCode = 0x16; break; // u
    case 86:   ScanCode = 0x2f; break; // v
    case 87:   ScanCode = 0x11; break; // w
    case 88:   ScanCode = 0x2d; break; // x
    case 89:   ScanCode = 0x15; break; // y
    case 90:   ScanCode = 0x2c; break; // z
    case 32:   ScanCode = 0x39; break; // space
    case 44:   ScanCode = 0x33; break; // ,
    case 60:   ScanCode = 0x33; break; // <
    case 62:   ScanCode = 0x34; break; // >
    case 63:   ScanCode = 0x35; break; // ?
    case 92:   ScanCode = 0x2b; break; // backslash
    case 124:  ScanCode = 0x2b; break; // bar
    case 4097: ScanCode = 0x0f; break; // tab
    case 4132: ScanCode = 0x3a; break; // caps lock
    case 4096: ScanCode = 0x01; break; // esc
    case 59:   ScanCode = 0x27; break; // ;
    case 58:   ScanCode = 0x27; break; // :
    case 39:   ScanCode = 0x28; break; // '
    case 34:   ScanCode = 0x28; break; // "
    case 91:   ScanCode = 0x1a; break; // [
    case 123:  ScanCode = 0x1a; break; // {
    case 93:   ScanCode = 0x1b; break; // ]
    case 125:  ScanCode = 0x1b; break; // }
    case 4144: ScanCode = 0x3b; break; // f1
    case 4145: ScanCode = 0x3c; break; // f2
    case 4146: ScanCode = 0x3d; break; // f3
    case 4147: ScanCode = 0x3e; break; // f4
    case 4148: ScanCode = 0x3f; break; // f5
    case 4149: ScanCode = 0x40; break; // f6
    case 4150: ScanCode = 0x41; break; // f7
    case 4151: ScanCode = 0x42; break; // f8
    case 4152: ScanCode = 0x43; break; // f9
    case 4153: ScanCode = 0x44; break; // f10
    case 4154: ScanCode = 0x57; break; // f11
    case 4155: ScanCode = 0x58; break; // f12
    case 4128: ScanCode = 0x2a; break; // shift
    case 4131: ScanCode = 0x38; break; // alt
    case 4129: ScanCode = 0x1d; break; // ctrl
    case 96:   ScanCode = 0x29; break; // `
    case 126:  ScanCode = 0x29; break; // ~
    case 4099: ScanCode = 0x0e; break; // backspace
  }
//  if (ScanCode == 0)
//    printf("key %d scancode %d\n", Key, ScanCode);
  return ScanCode;
}

//*****************************************************************************
void QMyMainWindow::keyPressEvent(QKeyEvent* e)
{
  if (!UpAndRunning)
    return;
  int ScanCode = GetScanCode(e);
  if (ScanCode != 0)
  {
    rdp_send_input(0, RDP_INPUT_SCANCODE, RDP_KEYPRESS, ScanCode, 0);
    e->accept();
  }
}

//*****************************************************************************
void QMyMainWindow::keyReleaseEvent(QKeyEvent* e)
{
  if (!UpAndRunning)
    return;
  int ScanCode = GetScanCode(e);
  if (ScanCode != 0)
  {
    rdp_send_input(0, RDP_INPUT_SCANCODE, RDP_KEYRELEASE, ScanCode, 0);
    e->accept();
  }
}

//*****************************************************************************
void QMyMainWindow::paintEvent(QPaintEvent* pe)
{
  QRect Rect;
  Rect = pe->rect();
  bitBlt(this, Rect.left(), Rect.top(), BS, Rect.left(), Rect.top(),
         Rect.width(), Rect.height());
}

//*****************************************************************************
void QMyMainWindow::closeEvent(QCloseEvent* e)
{
  e->accept();
}

//*****************************************************************************
bool QMyMainWindow::event(QEvent* e)
{
  return QWidget::event(e);
}

//*****************************************************************************
void QMyMainWindow::dataReceived()
{
  if (!rdp_loop(&deactivated, &ext_disc_reason))
    SV->close();
}

//*****************************************************************************
void QMyScrollView::keyPressEvent(QKeyEvent* e)
{
  MW->keyPressEvent(e);
}

//*****************************************************************************
void QMyScrollView::keyReleaseEvent(QKeyEvent* e)
{
  MW->keyReleaseEvent(e);
}


//*****************************************************************************
void ui_begin_update(void)
{
  P1->begin(MW);
  P2->begin(BS);
}

//*****************************************************************************
void ui_end_update(void)
{
  P1->end();
  P2->end();
}

/*****************************************************************************/
int ui_init(void)
{
  App = new QApplication(g_argc, g_argv);
  return 1;
}

/*****************************************************************************/
void ui_deinit(void)
{
  delete App;
}

/*****************************************************************************/
int ui_create_window(void)
{
  int w, h;
  MW = new QMyMainWindow();
  SV = new QMyScrollView();
  SV->addChild(MW);
  BS = new QPixmap(g_width, g_height);
  QPainter* P = new QPainter(BS);
  P->fillRect(0, 0, g_width, g_height, QBrush(QColor("white")));
  P->fillRect(0, 0, g_width, g_height, QBrush(QBrush::CrossPattern));
  delete P;
  DS = new QPixmap(480, 480);
  P1 = new QPainter();
  P2 = new QPainter();
  ClipRect = new QRegion(0, 0, g_width, g_height);
  QWidget* d = QApplication::desktop();
  w = d->width();                   // returns screen width
  h = d->height();                  // returns screen height
  MW->resize(g_width, g_height);
  if (w < g_width || h < g_height)
    SV->resize(w, h);
  else
    SV->resize(g_width + 4, g_height + 4);
  SV->setMaximumWidth(g_width + 4);
  SV->setMaximumHeight(g_height + 4);
  App->setMainWidget(SV);
  SV->show();
  MW->setMouseTracking(true);
  if (g_title[0] != 0)
    SV->setCaption(g_title);

/*  XGrayKey(0, 64, AnyModifie, SV->winId(), 0, GrabModeAsync, GrabModeAsync);
  XGrayKey(0, 113, AnyModifie, SV->winId(), 0, GrabModeAsync, GrabModeAsync);
  XGrayKey(0, 37, AnyModifie, SV-winId(), 0, GrabModeAsync, GrabModeAsync);
  XGrayKey(0, 109, AnyModifie, SV->winId(), 0, GrabModeAsync, GrabModeAsync);
  XGrayKey(0, 115, AnyModifie, SV->winId(), 0, GrabModeAsync, GrabModeAsync);
  XGrayKey(0, 116, AnyModifie, SV->winId(), 0, GrabModeAsync, GrabModeAsync);
  XGrayKey(0, 117, AnyModifie, SV->winId(), 0, GrabModeAsync, GrabModeAsync);
  XGrayKey(0, 62, AnyModifie, SV->winId(), 0, GrabModeAsync, GrabModeAsync);
  XGrayKey(0, 50, AnyModifie, SV->winId(), 0, GrabModeAsync, GrabModeAsync);*/

  return 1;
}

//*****************************************************************************
void ui_main_loop(void)
{
  // connect
  if (!rdp_connect(g_servername, RDP_LOGON_NORMAL, "", "", "", ""))
    return;
  printf("connected\n");
  // start notifier
  SocketNotifier = new QSocketNotifier(g_sock, QSocketNotifier::Read, MW);
  MW->connect(SocketNotifier, SIGNAL(activated(int)), MW, SLOT(dataReceived()));
  UpAndRunning = 1;
  // app main loop
  App->exec();
}

//*****************************************************************************
void ui_destroy_window(void)
{
  delete MW;
  delete SV;
  delete BS;
  delete DS;
  delete P1;
  delete P2;
  delete ClipRect;
}

/*****************************************************************************/
void ui_bell(void)
{
}

/*****************************************************************************/
int ui_select(int in_val)
{
  g_sock = in_val;
  return 1;
}

/*****************************************************************************/
void ui_destroy_cursor(void* cursor)
{
  QCursor* Cursor;
  Cursor = (QCursor*)cursor;
  if (Cursor != NULL)
    delete Cursor;
}

/*****************************************************************************/
void* ui_create_glyph(int width, int height, uint8* data)
{
  QBitmap* Bitmap;
  Bitmap = new QBitmap(width, height, data);
  Bitmap->setMask(*Bitmap);
  return (HGLYPH)Bitmap;
}

/*****************************************************************************/
void ui_destroy_glyph(void* glyph)
{
  QBitmap* Bitmap;
  Bitmap = (QBitmap*)glyph;
  delete Bitmap;
}

/*****************************************************************************/
void ui_destroy_bitmap(void* bmp)
{
  QPixmap* Pixmap;
  Pixmap = (QPixmap*)bmp;
  delete Pixmap;
}

/*****************************************************************************/
void ui_reset_clip(void)
{
  P1->setClipRect(0, 0, g_width, g_height);
  P2->setClipRect(0, 0, g_width, g_height);
  delete ClipRect;
  ClipRect = new QRegion(0, 0, g_width, g_height);
}

/*****************************************************************************/
void ui_set_clip(int x, int y, int cx, int cy)
{
  P1->setClipRect(x, y, cx, cy);
  P2->setClipRect(x, y, cx, cy);
  delete ClipRect;
  ClipRect = new QRegion(x, y, cx, cy);
}

/*****************************************************************************/
void* ui_create_colourmap(COLOURMAP* colours)
{
  QColorMap* LCM;
  int i, r, g, b;
  LCM = (QColorMap*)malloc(sizeof(QColorMap));
  memset(LCM, 0, sizeof(QColorMap));
  i = 0;
  while (i < colours->ncolours && i < 256)
  {
    r = colours->colours[i].red;
    g = colours->colours[i].green;
    b = colours->colours[i].blue;
    LCM->RGBColors[i] = (r << 16) | (g << 8) | b;
    i++;
  }
  LCM->NumColors = colours->ncolours;
  return LCM;
}

//*****************************************************************************
// todo, does this leak at end of program
void ui_destroy_colourmap(HCOLOURMAP map)
{
  QColorMap* LCM;
  LCM = (QColorMap*)map;
  if (LCM == NULL)
    return;
  free(LCM);
}

/*****************************************************************************/
void ui_set_colourmap(void* map)
{
  // destoy old colormap
  ui_destroy_colourmap(CM);
  CM = (QColorMap*)map;
}

/*****************************************************************************/
HBITMAP ui_create_bitmap(int width, int height, uint8* data)
{
  QImage* Image = NULL;
  QPixmap* Pixmap;
  uint32* d = NULL;
  uint16* s;
  switch (g_server_bpp)
  {
    case 8:
      Image = new QImage(data, width, height, 8, (QRgb*)&CM->RGBColors,
                         CM->NumColors, QImage::IgnoreEndian);
      break;
    case 15:
      d = (uint32*)malloc(width * height * 4);
      s = (uint16*)data;
      for (int i = 0; i < width * height; i++)
        d[i] = Color15to32(s[i]);
      Image = new QImage((uint8*)d, width, height, 32, NULL,
                         0, QImage::IgnoreEndian);
      break;
    case 16:
      d = (uint32*)malloc(width * height * 4);
      s = (uint16*)data;
      for (int i = 0; i < width * height; i++)
        d[i] = Color16to32(s[i]);
      Image = new QImage((uint8*)d, width, height, 32, NULL,
                         0, QImage::IgnoreEndian);
      break;
    case 24:
      d = (uint32*)malloc(width * height * 4);
      memset(d, 0, width * height * 4);
      for (int i = 0; i < width * height; i++)
        memcpy(d + i, data + i * 3, 3);
      Image = new QImage((uint8*)d, width, height, 32, NULL,
                         0, QImage::IgnoreEndian);
      //Image = new QImage(data, width, height, 24, NULL,
      //                   0, QImage::IgnoreEndian);
      break;
  }
  if (Image == NULL)
    return NULL;
  Pixmap = new QPixmap();
  Pixmap->convertFromImage(*Image);
  delete Image;
  if (d != NULL)
    free(d);
  return (HBITMAP)Pixmap;
}

//******************************************************************************
// adjust coordinates for cliping rect
BOOL WarpCoords(int* x, int* y, int* cx, int* cy, int* srcx, int* srcy)
{
  int dx, dy;
  QRect InRect(*x, *y, *cx, *cy);
  QRect OutRect;
  QRect CRect = ClipRect->boundingRect();
  OutRect = InRect.intersect(CRect);
  if (OutRect.isEmpty())
    return False;
  dx = OutRect.x() - InRect.x();
  dy = OutRect.y() - InRect.y();
  *x = OutRect.x();
  *y = OutRect.y();
  *cx = OutRect.width();
  *cy = OutRect.height();
  *srcx = *srcx + dx;
  *srcy = *srcy + dy;
  return True;
}

//******************************************************************************
// needed because bitBlt don't seem to care about clipping rects
// also has 2 dsts and src can be nil
void bitBltClip(QPaintDevice* dst1, QPaintDevice* dst2, int dx, int dy,
                QPaintDevice* src, int sx, int sy, int sw, int sh,
                Qt::RasterOp rop, bool im)
{
  if (WarpCoords(&dx, &dy, &sw, &sh, &sx, &sy))
  {
    if (dst1 != NULL)
      if (src == NULL)
        bitBlt(dst1, dx, dy, dst1, sx, sy, sw, sh, rop, im);
      else
        bitBlt(dst1, dx, dy, src, sx, sy, sw, sh, rop, im);
    if (dst2 != NULL)
      if (src == NULL)
        bitBlt(dst2, dx, dy, dst2, sx, sy, sw, sh, rop, im);
      else
        bitBlt(dst2, dx, dy, src, sx, sy, sw, sh, rop, im);
  }
}

#define DO_GLYPH(ttext,idx) \
{\
  glyph = cache_get_font (font, ttext[idx]);\
  if (!(flags & TEXT2_IMPLICIT_X))\
    {\
      xyoffset = ttext[++idx];\
      if ((xyoffset & 0x80))\
	{\
	  if (flags & TEXT2_VERTICAL) \
	    y += ttext[idx+1] | (ttext[idx+2] << 8);\
	  else\
	    x += ttext[idx+1] | (ttext[idx+2] << 8);\
	  idx += 2;\
	}\
      else\
	{\
	  if (flags & TEXT2_VERTICAL) \
	    y += xyoffset;\
	  else\
	    x += xyoffset;\
	}\
    }\
  if (glyph != NULL)\
    {\
      P2->drawPixmap(x + glyph->offset, y + glyph->baseline, *((QBitmap*)glyph->pixmap)); \
      if (flags & TEXT2_IMPLICIT_X)\
	x += glyph->width;\
    }\
}

//*****************************************************************************
void ui_draw_text(uint8 font, uint8 flags, int mixmode,
                  int x, int y, int clipx, int clipy,
                  int clipcx, int clipcy, int boxx,
                  int boxy, int boxcx, int boxcy, int bgcolour,
                  int fgcolour, uint8 * text, uint8 length)
{
  FONTGLYPH *glyph;
  int i, j, xyoffset;
  DATABLOB *entry;

  SetColorx(&Color1, fgcolour);
  SetColorx(&Color2, bgcolour);
  P2->setBackgroundColor(Color2);
  P2->setPen(Color1);
  if (boxcx > 1)
    P2->fillRect(boxx, boxy, boxcx, boxcy, QBrush(Color2));
  else if (mixmode == MIX_OPAQUE)
    P2->fillRect(clipx, clipy, clipcx, clipcy, QBrush(Color2));

  /* Paint text, character by character */
  for (i = 0; i < length;)
  {
    switch (text[i])
    {
      case 0xff:
        if (i + 2 < length)
          cache_put_text(text[i + 1], text, text[i + 2]);
        else
        {
          error("this shouldn't be happening\n");
          exit(1);
        }
        /* this will move pointer from start to first character after FF command */
        length -= i + 3;
        text = &(text[i + 3]);
        i = 0;
        break;

      case 0xfe:
        entry = cache_get_text(text[i + 1]);
        if (entry != NULL)
        {
          if ((((uint8 *) (entry->data))[1] == 0) && (!(flags & TEXT2_IMPLICIT_X)))
          {
            if (flags & TEXT2_VERTICAL)
              y += text[i + 2];
            else
              x += text[i + 2];
          }
          for (j = 0; j < entry->size; j++)
            DO_GLYPH(((uint8 *) (entry->data)), j);
        }
        if (i + 2 < length)
          i += 3;
        else
          i += 2;
        length -= i;
        /* this will move pointer from start to first character after FE command */
        text = &(text[i]);
        i = 0;
        break;

      default:
        DO_GLYPH(text, i);
        i++;
        break;
    }
  }
  if (boxcx > 1)
    bitBltClip(MW, NULL, boxx, boxy, BS, boxx, boxy, boxcx, boxcy, Qt::CopyROP, true);
  else
    bitBltClip(MW, NULL, clipx, clipy, BS, clipx, clipy, clipcx, clipcy, Qt::CopyROP, true);
}

/*****************************************************************************/
void ui_line(uint8 opcode, int startx, int starty, int endx, int endy,
             PEN* pen)
{
  SetColorx(&Color1, pen->colour);
  SetOpCode(opcode);
  P1->setPen(Color1);
  P1->moveTo(startx, starty);
  P1->lineTo(endx, endy);
  P2->setPen(Color1);
  P2->moveTo(startx, starty);
  P2->lineTo(endx, endy);
  ResetOpCode(opcode);
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
  QPixmap* Pixmap;
  Pixmap = (QPixmap*)src;
  if (Pixmap != NULL)
  {
    SetOpCode(opcode);
    P1->drawPixmap(x, y, *Pixmap, srcx, srcy, cx, cy);
    P2->drawPixmap(x, y, *Pixmap, srcx, srcy, cx, cy);
    ResetOpCode(opcode);
  }
}

//******************************************************************************
void CommonDeskSave(QPixmap* Pixmap1, QPixmap* Pixmap2, int Offset, int x,
                    int y, int cx, int cy, int dir)
{
  int lx;
  int ly;
  int x1;
  int y1;
  int width;
  int lcx;
  int right;
  int bottom;
  lx = Offset % 480;
  ly = Offset / 480;
  y1 = y;
  right = x + cx;
  bottom = y + cy;
  while (y1 < bottom)
  {
    x1 = x;
    lcx = cx;
    while (x1 < right)
    {
      width = 480 - lx;
      if (width > lcx)
        width = lcx;
      if (dir == 0)
        bitBlt(Pixmap1, lx, ly, Pixmap2, x1, y1, width, 1, Qt::CopyROP, true);
      else
        bitBlt(Pixmap2, x1, y1, Pixmap1, lx, ly, width, 1, Qt::CopyROP, true);
      lx = lx + width;
      if (lx >= 480)
      {
        lx = 0;
        ly++;
        if (ly >= 480)
          ly = 0;
      }
      lcx = lcx - width;
      x1 = x1 + width;
    }
    y1++;
  }
}

/*****************************************************************************/
void ui_desktop_restore(uint32 offset, int x, int y, int cx, int cy)
{
  QPixmap* Pixmap;
  Pixmap = new QPixmap(cx, cy);
  CommonDeskSave(DS, Pixmap, offset, 0, 0, cx, cy, 1);
  bitBltClip(MW, BS, x, y, Pixmap, 0, 0, cx, cy, Qt::CopyROP, true);
  delete Pixmap;
}

/*****************************************************************************/
void ui_desktop_save(uint32 offset, int x, int y, int cx, int cy)
{
  CommonDeskSave(DS, BS, offset, x, y, cx, cy, 0);
}

/*****************************************************************************/
void ui_rect(int x, int y, int cx, int cy, int colour)
{
  SetColorx(&Color1, colour);
  P1->fillRect(x, y, cx, cy, QBrush(Color1));
  P2->fillRect(x, y, cx, cy, QBrush(Color1));
}

/*****************************************************************************/
void ui_screenblt(uint8 opcode, int x, int y, int cx, int cy,
                  int srcx, int srcy)
{
  SetOpCode(opcode);
  bitBltClip(MW, BS, x, y, NULL, srcx, srcy, cx, cy, Qt::CopyROP, true);
  ResetOpCode(opcode);
}

/*****************************************************************************/
void ui_patblt(uint8 opcode, int x, int y, int cx, int cy,
               BRUSH* brush, int bgcolour, int fgcolour)
{
  QBitmap* Bitmap;
  QBrush* Brush;
  uint8 ipattern[8], i;
  SetOpCode(opcode);
  switch (brush->style)
  {
    case 0:
      SetColorx(&Color1, fgcolour);
      P2->fillRect(x, y, cx, cy, QBrush(Color1));
      break;
    case 3:
      SetColorx(&Color1, fgcolour);
      SetColorx(&Color2, bgcolour);
      for (i = 0; i != 8; i++)
        ipattern[7 - i] = ~brush->pattern[i];
      Bitmap = new QBitmap(8, 8, ipattern);
      Brush = new QBrush(Color1, *Bitmap);
      P2->setBackgroundMode(Qt::OpaqueMode);
      P2->setBrushOrigin(brush->xorigin, brush->yorigin);
      P2->setBackgroundColor(Color2);
      P2->fillRect(x, y, cx, cy, *Brush);
      delete Brush;
      delete Bitmap;
      P2->setBackgroundMode(Qt::TransparentMode);
      P2->setBrushOrigin(0, 0);
      break;
  }
  ResetOpCode(opcode);
  bitBltClip(MW, NULL, x, y, BS, x, y, cx, cy, Qt::CopyROP, true);
}

/*****************************************************************************/
void ui_destblt(uint8 opcode, int x, int y, int cx, int cy)
{
  SetOpCode(opcode);
  P1->fillRect(x, y, cx, cy, QBrush(QColor("black")));
  P2->fillRect(x, y, cx, cy, QBrush(QColor("black")));
  ResetOpCode(opcode);
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
  QImage* Image = NULL;
  QPixmap* Pixmap;
  uint32* d = NULL;
  uint16* s;
  switch (g_server_bpp)
  {
    case 8:
      Image = new QImage(data, width, height, 8, (QRgb*)&CM->RGBColors,
                         CM->NumColors, QImage::IgnoreEndian);
      break;
    case 15:
      d = (uint32*)malloc(width * height * 4);
      s = (uint16*)data;
      for (int i = 0; i < width * height; i++)
        d[i] = Color15to32(s[i]);
      Image = new QImage((uint8*)d, width, height, 32, NULL,
                         0, QImage::IgnoreEndian);
      break;
    case 16:
      d = (uint32*)malloc(width * height * 4);
      s = (uint16*)data;
      for (int i = 0; i < width * height; i++)
        d[i] = Color16to32(s[i]);
      Image = new QImage((uint8*)d, width, height, 32, NULL,
                         0, QImage::IgnoreEndian);
      break;
    case 24:
      d = (uint32*)malloc(width * height * 4);
      memset(d, 0, width * height * 4);
      for (int i = 0; i < width * height; i++)
        memcpy(d + i, data + i * 3, 3);
      Image = new QImage((uint8*)d, width, height, 32, NULL,
                         0, QImage::IgnoreEndian);
      break;
  }
  if (Image == NULL)
    return;
  Pixmap = new QPixmap();
  Pixmap->convertFromImage(*Image);
  P1->drawPixmap(x, y, *Pixmap, 0, 0, cx, cy);
  P2->drawPixmap(x, y, *Pixmap, 0, 0, cx, cy);
  delete Image;
  delete Pixmap;
  if (d != NULL)
    free(d);
}

//******************************************************************************
BOOL Is24On(uint8* Data, int X, int Y)
{
  uint8 R, G, B;
  int Start;
  Start = Y * 32 * 3 + X * 3;
  R = Data[Start];
  G = Data[Start + 1];
  B = Data[Start + 2];
  return !((R == 0) && (G == 0) && (B == 0));
}

//******************************************************************************
BOOL Is1On(uint8* Data, int X, int Y)
{
  int Start;
  int Shift;
  Start = (Y * 32) / 8 + X / 8;
  Shift = X % 8;
  return (Data[Start] & (0x80 >> Shift)) == 0;
}

//******************************************************************************
void Set1(uint8* Data, int X, int Y)
{
  int Start;
  int Shift;
  Start = (Y * 32) / 8 + X / 8;
  Shift = X % 8;
  Data[Start] = Data[Start] | (0x80 >> Shift);
}

//******************************************************************************
void FlipOver(uint8* Data)
{
  uint8 AData[128];
  int Index;
  memcpy(AData, Data, 128);
  for (Index = 0; Index <= 31; Index++)
  {
    Data[127 - (Index * 4 + 3)] = AData[Index * 4];
    Data[127 - (Index * 4 + 2)] = AData[Index * 4 + 1];
    Data[127 - (Index * 4 + 1)] = AData[Index * 4 + 2];
    Data[127 - Index * 4] = AData[Index * 4 + 3];
  }
}

/*****************************************************************************/
void ui_set_cursor(HCURSOR cursor)
{
  QCursor* Cursor;
  Cursor = (QCursor*)cursor;
  if (Cursor != NULL)
    MW->setCursor(*Cursor);
}

/*****************************************************************************/
HCURSOR ui_create_cursor(unsigned int x, unsigned int y,
                         int width, int height,
                         uint8* andmask, uint8* xormask)
{
  uint8 AData[128];
  uint8 AMask[128];
  QBitmap* DataBitmap;
  QBitmap* MaskBitmap;
  QCursor* Cursor;
  int I1;
  int I2;
  BOOL BOn;
  BOOL MOn;
  if (width != 32 || height != 32)
    return 0;
  memset(AData, 0, 128);
  memset(AMask, 0, 128);
  for (I1 = 0; I1 <= 31; I1++)
    for (I2 = 0; I2 <= 31; I2++)
    {
      MOn = Is24On(xormask, I1, I2);
      BOn = Is1On(andmask, I1, I2);
      if (BOn ^ MOn) // xor
      {
        Set1(AData, I1, I2);
        if (!MOn)
          Set1(AMask, I1, I2);
      }
      if (MOn)
        Set1(AMask, I1, I2);
    }
  FlipOver(AData);
  FlipOver(AMask);
  DataBitmap = new QBitmap(32, 32, AData);
  MaskBitmap = new QBitmap(32, 32, AMask);
  Cursor = new QCursor(*DataBitmap, *MaskBitmap, x, y);
  delete DataBitmap;
  delete MaskBitmap;
  return Cursor;
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
void generate_random(uint8* random)
{
  QFile File("/dev/random");
  File.open(IO_ReadOnly);
  if (File.readBlock((char*)random, 32) == 32)
    return;
  warning("no /dev/random\n");
  memcpy(random, "12345678901234567890123456789012", 32);
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
void* xrealloc(void* in_val, int size)
{
  return realloc(in_val, size);
}

/*****************************************************************************/
void* xmalloc(int size)
{
  return malloc(size);
}

/*****************************************************************************/
void xfree(void* in_val)
{
  free(in_val);
}

/*****************************************************************************/
void warning(char* format, ...)
{
  va_list ap;

  fprintf(stderr, "WARNING: ");
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
}

/*****************************************************************************/
void unimpl(char* format, ...)
{
  va_list ap;

  fprintf(stderr, "NOT IMPLEMENTED: ");
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
}

/*****************************************************************************/
void error(char* format, ...)
{
  va_list ap;

  fprintf(stderr, "ERROR: ");
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
}

/*****************************************************************************/
/* produce a hex dump */
void hexdump(unsigned char *p, unsigned int len)
{
  unsigned char *line = p;
  int i, thisline;
  unsigned int offset = 0;

  while (offset < len)
  {
    printf("%04x ", offset);
    thisline = len - offset;
    if (thisline > 16)
      thisline = 16;
    for (i = 0; i < thisline; i++)
      printf("%02x ", line[i]);
    for (; i < 16; i++)
      printf("   ");
    for (i = 0; i < thisline; i++)
      printf("%c", (line[i] >= 0x20 && line[i] < 0x7f) ? line[i] : '.');
    printf("\n");
    offset += thisline;
    line += thisline;
  }
}

/*****************************************************************************/
void get_username_and_hostname(void)
{
  char fullhostname[64];
  char* p;
  struct passwd* pw;

  STRNCPY(g_username, "unknown", sizeof(g_username));
  STRNCPY(g_hostname, "unknown", sizeof(g_hostname));
  pw = getpwuid(getuid());
  if (pw != NULL && pw->pw_name != NULL)
  {
    STRNCPY(g_username, pw->pw_name, sizeof(g_username));
  }
  if (gethostname(fullhostname, sizeof(fullhostname)) != -1)
  {
    p = strchr(fullhostname, '.');
    if (p != NULL)
      *p = 0;
    STRNCPY(g_hostname, fullhostname, sizeof(g_hostname));
  }
}

/*****************************************************************************/
int parse_parameters(int in_argc, char** in_argv)
{
  int i;
  char* p;

  if (in_argc <= 1)
    return 0;
  g_argc = in_argc;
  g_argv = in_argv;
  for (i = 1; i < in_argc; i++)
  {
    strcpy(g_servername, in_argv[i]);
    if (strcmp(in_argv[i], "-g") == 0)
    {
      g_width = strtol(in_argv[i + 1], &p, 10);
      if (g_width <= 0)
      {
        error("invalid geometry\n");
        return 0;
      }
      if (*p == 'x')
        g_height = strtol(p + 1, NULL, 10);
      if (g_height <= 0)
      {
        error("invalid geometry\n");
        return 0;
      }
      g_width = (g_width + 3) & ~3;
    }
    else if (strcmp(in_argv[i], "-T") == 0)
      strcpy(g_title, in_argv[i + 1]);
    else if (strcmp(in_argv[i], "-4") == 0)
      g_use_rdp5 = 0;
    else if (strcmp(in_argv[i], "-5") == 0)
      g_use_rdp5 = 1;
    else if (strcmp(in_argv[i], "-a") == 0)
    {
      g_server_bpp = strtol(in_argv[i + 1], &p, 10);
      if (g_server_bpp != 8 && g_server_bpp != 15 &&
          g_server_bpp != 16 && g_server_bpp != 24)
      {
        error("invalid bpp\n");
        return 0;
      }
    }
    else if (strcmp(in_argv[i], "-t") == 0)
      g_tcp_port_rdp = strtol(in_argv[i + 1], &p, 10);
  }
  return 1;
}

/*****************************************************************************/
int main(int in_argc, char** in_argv)
{
  get_username_and_hostname();
  if (!parse_parameters(in_argc, in_argv))
    return 0;
  if (!ui_init())
    return 1;
  if (!ui_create_window())
    return 1;
  ui_main_loop();
  ui_destroy_window();
  ui_deinit();
  return 0;
}
