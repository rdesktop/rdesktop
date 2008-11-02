
/* by Jay Sorg, public domain
   for testing brush cache */

#include <windows.h>

static HWND g_wnd;
static HWND g_button;
static HWND g_pulldown;

/*****************************************************************************/
static int WINAPI
brush_test(int selindex, int orgx, int orgy)
{
  HDC dc;
  RECT rect;
  HBRUSH br;
  BITMAPINFO* bi;
  RGBQUAD ce;
  int bi_size;
  int i;
  int j;
  UCHAR* d8;

  bi_size = sizeof(BITMAPINFO) + 15 * sizeof(RGBQUAD) + 4 * 8;
  bi = malloc(bi_size);
  memset(bi, 0, bi_size);
  bi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi->bmiHeader.biWidth = 8;
  bi->bmiHeader.biHeight = 8;
  bi->bmiHeader.biPlanes = 1;
  bi->bmiHeader.biBitCount = 4;
  bi->bmiHeader.biCompression = BI_RGB;
  bi->bmiHeader.biSizeImage = 32;
  ce.rgbBlue = 0xff;
  ce.rgbGreen = 0;
  ce.rgbRed = 0;
  ce.rgbReserved = 0;
  bi->bmiColors[0] = ce;
  ce.rgbBlue = 0;
  ce.rgbGreen = 0xff;
  ce.rgbRed = 0;
  ce.rgbReserved = 0;
  bi->bmiColors[1] = ce;
  ce.rgbBlue = 0;
  ce.rgbGreen = 0;
  ce.rgbRed = 0xff;
  ce.rgbReserved = 0;
  bi->bmiColors[2] = ce;
  ce.rgbBlue = 0x0f;
  ce.rgbGreen = 0;
  ce.rgbRed = 0;
  ce.rgbReserved = 0;
  bi->bmiColors[3] = ce;
  ce.rgbBlue = 0;
  ce.rgbGreen = 0x0f;
  ce.rgbRed = 0;
  ce.rgbReserved = 0;
  bi->bmiColors[4] = ce;
  d8 = (UCHAR*)bi;
  d8 = (d8 + bi_size) - 32;
  if (selindex == 1) /* 2 color */
  {
    d8[0] = 0x10;
  }
  else if (selindex == 2) /* 4 color */
  {
    d8[0] = 0x12;
    d8[1] = 0x33;
  }
  else if (selindex == 3) /* > 4 color */
  {
    d8[0] = 0x12;
    d8[1] = 0x34;
  }
  br = CreateDIBPatternBrushPt(bi, DIB_RGB_COLORS);
  if (br == 0)
  {
     MessageBox(g_wnd, "error in CreateDIBPatternBrushPt", "error", MB_OK);
  }
  dc = GetDC(g_wnd);
  rect.left = 100;
  rect.top = 60;
  rect.right = rect.left + 128;
  rect.bottom = rect.top + 128;
  for (j = 0; j < 128; j += 5)
  {
    for (i = 0; i < 128; i += 5)
    {
      SetBrushOrgEx(dc, orgx + i, orgy + j, 0);
      FillRect(dc, &rect, br);
    }
  }
  DeleteObject(br);
  free(bi);
  ReleaseDC(g_wnd, dc);
  return 0;
}

/*****************************************************************************/
static LRESULT CALLBACK
WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  HWND hwndCtl;
  int selindex;

  switch (message)
  {
    case WM_DESTROY:
      PostQuitMessage(0);
      break;
    case WM_COMMAND:
      hwndCtl = (HWND) lParam;
      if (hwndCtl == g_button)
      {
        selindex = SendMessage(g_pulldown, CB_GETCURSEL, 0, 0);
        if (selindex < 1)
        {
          MessageBox(g_wnd, "please select one", "info", MB_OK);
        }
        else
        {
          brush_test(selindex, 4, 4);
        }
      }
      break;
  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}

/*****************************************************************************/
static int WINAPI
create_window(void)
{
  WNDCLASS wc;
  DWORD style;
  int x;
  int y;
  int w;
  int h;

  ZeroMemory(&wc, sizeof(wc));
  wc.lpfnWndProc = WndProc; /* points to window procedure */
  wc.lpszClassName = "brushtest";
  wc.hbrBackground = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
  wc.hCursor = LoadCursor(0, IDC_ARROW);
  /* Register the window class. */
  if (!RegisterClass(&wc))
  {
    return 1; /* Failed to register window class */
  }
  style = WS_OVERLAPPED | WS_CAPTION | WS_POPUP | WS_MINIMIZEBOX |
          WS_SYSMENU | WS_SIZEBOX | WS_MAXIMIZEBOX;
  x = CW_USEDEFAULT;
  y = CW_USEDEFAULT;
  w = 640;
  h = 480;
  g_wnd = CreateWindow(wc.lpszClassName, "brushtest",
                       style, x, y, w, h, 0, 0, 0, 0);
  /* button */
  style = WS_CHILD | WS_VISIBLE;
  g_button = CreateWindow("BUTTON", "Go", style, 300, 10, 80, 24, g_wnd,
                          0, 0, 0);
  /* pull down */
  style = WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST;
  g_pulldown = CreateWindow("COMBOBOX", "", style, 100, 10, 160, 150, g_wnd,
                            0, 0, 0);
  ShowWindow(g_wnd, SW_SHOWNORMAL);
  SendMessage(g_pulldown, CB_ADDSTRING, 0, (LPARAM) (LPCTSTR) "");
  SendMessage(g_pulldown, CB_ADDSTRING, 0, (LPARAM) (LPCTSTR) "2 color test");
  SendMessage(g_pulldown, CB_ADDSTRING, 0, (LPARAM) (LPCTSTR) "4 color test");
  SendMessage(g_pulldown, CB_ADDSTRING, 0, (LPARAM) (LPCTSTR) "> 4 color test");
  return 0;
}

/*****************************************************************************/
static int WINAPI
main_loop(void)
{
  BOOL cont;
  BOOL gmcode;
  MSG msg1;

  create_window();
  cont = TRUE;
  while (cont)
  {
    MsgWaitForMultipleObjects(0, 0, 0, INFINITE, QS_ALLINPUT);
    while (cont && PeekMessage(&msg1, 0, 0, 0, PM_NOREMOVE))
    {
      gmcode = GetMessage(&msg1, 0, 0, 0);
      if (gmcode && (gmcode != -1))
      {
        TranslateMessage(&msg1);
        DispatchMessage(&msg1);
      }
      else
      {
        cont = FALSE;
      }
    }
  }
  return 0;
}

/*****************************************************************************/
int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
        LPSTR lpCmdLine, int nCmdShow)
{
  int rv;

  rv = main_loop();
  return rv;
}
