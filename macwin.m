/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   User interface services - macOS Cocoa
   Native macOS port
   
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "rdesktop.h"
#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <strings.h>

#ifdef __APPLE__
#include <sys/param.h>
#define HOST_NAME_MAX MAXHOSTNAMELEN
#endif

/* Forward declarations */
@interface RDPView : NSView
@property (nonatomic) CGContextRef backingStore;
@property (nonatomic) NSSize backingStoreSize;
@property (nonatomic, strong) NSTimer *displayTimer;
@property (nonatomic) BOOL needsRedraw;
@property (nonatomic, strong) NSRecursiveLock *backingStoreLock;  // Thread-safety for backingStore access (recursive to allow nested calls)
- (void)setupBackingStore:(NSSize)size;
- (void)startDisplayTimer;
- (void)stopDisplayTimer;
@end

typedef enum {
    EVENT_MOUSE,
    EVENT_KEY
} rdp_event_type_t;

typedef struct {
    rdp_event_type_t type;
    uint32 time;
    uint16 flags;
    int x_or_scancode;
    int y;
} rdp_queued_event_t;

#define EVENT_QUEUE_SIZE 256
static rdp_queued_event_t g_event_queue[EVENT_QUEUE_SIZE];
static int g_queue_head = 0;
static int g_queue_tail = 0;
static pthread_mutex_t g_queue_mutex = PTHREAD_MUTEX_INITIALIZER;

void queue_input_event(rdp_event_type_t type, uint32 time, uint16 flags, int param1, int param2) {
    extern RD_BOOL g_connection_established;
    if (!g_connection_established) {
        return;
    }
    pthread_mutex_lock(&g_queue_mutex);
    int next_tail = (g_queue_tail + 1) % EVENT_QUEUE_SIZE;
    if (next_tail != g_queue_head) { // not full
        g_event_queue[g_queue_tail].type = type;
        g_event_queue[g_queue_tail].time = time;
        g_event_queue[g_queue_tail].flags = flags;
        g_event_queue[g_queue_tail].x_or_scancode = param1;
        g_event_queue[g_queue_tail].y = param2;
        g_queue_tail = next_tail;
    }
    pthread_mutex_unlock(&g_queue_mutex);
}

void process_queued_input_events(void) {
    while (1) {
        rdp_queued_event_t ev;
        int has_event = 0;

        pthread_mutex_lock(&g_queue_mutex);
        if (g_queue_head != g_queue_tail) {
            ev = g_event_queue[g_queue_head];
            g_queue_head = (g_queue_head + 1) % EVENT_QUEUE_SIZE;
            has_event = 1;
        }
        pthread_mutex_unlock(&g_queue_mutex);

        if (!has_event) break;

        if (ev.type == EVENT_MOUSE) {
            rdp_send_input(ev.time, RDP_INPUT_MOUSE, ev.flags, ev.x_or_scancode, ev.y);
        } else if (ev.type == EVENT_KEY) {
            rdp_send_input(ev.time, RDP_INPUT_SCANCODE, ev.flags, ev.x_or_scancode, 0);
        }
    }
}


/* Data release callback for glyph data */
static void glyph_data_release_callback(void *info, const void *data, size_t size) {
    (void)info;
    (void)size;
    free((void *)data);
}

@interface RDPWindow : NSWindow <NSWindowDelegate>
@property (nonatomic, strong) RDPView *rdpView;
@end

/* Global variables */
static NSApplication *g_app = nil;
static RDPWindow *g_window = nil;
static RDPView *g_view = nil;
static CGColorSpaceRef g_colorspace = NULL;


// RDP state
extern RD_BOOL g_rdpsnd;
extern RD_BOOL g_sendmotion;
extern RD_BOOL g_fullscreen;
extern RD_BOOL g_grab_keyboard;
extern RD_BOOL g_hide_decorations;
extern char g_title[];
extern uint32 g_rdp_shareid;

uint16 g_width = 800;
uint16 g_height = 600;
extern int g_server_depth;  // Defined in rdesktop_main.c
extern uint16 mac_keycode_to_scancode(uint16 keycode);


/* Thread-safe UI dispatch macro */
#define DISPATCH_UI_SYNC(block) \
    do { \
        if ([NSThread isMainThread]) { \
            block(); \
        } else { \
            dispatch_sync(dispatch_get_main_queue(), ^{ block(); }); \
        } \
    } while (0)

#define DISPATCH_UI_ASYNC(block) \
    dispatch_async(dispatch_get_main_queue(), ^{ block(); })

/* Color conversion macros and functions */
typedef struct {
    uint8 red;
    uint8 green;
    uint8 blue;
} PixelColour;

#define SPLITCOLOUR15(colour, rv) \
{ \
    rv.red = ((colour >> 7) & 0xf8) | ((colour >> 12) & 0x7); \
    rv.green = ((colour >> 2) & 0xf8) | ((colour >> 8) & 0x7); \
    rv.blue = ((colour << 3) & 0xf8) | ((colour >> 2) & 0x7); \
}

#define SPLITCOLOUR16(colour, rv) \
{ \
    rv.red = ((colour >> 8) & 0xf8) | ((colour >> 13) & 0x7); \
    rv.green = ((colour >> 3) & 0xfc) | ((colour >> 9) & 0x3); \
    rv.blue = ((colour << 3) & 0xf8) | ((colour >> 2) & 0x7); \
}

#define SPLITCOLOUR24(colour, rv) \
{ \
    rv.blue = (colour & 0xff0000) >> 16; \
    rv.green = (colour & 0x00ff00) >> 8; \
    rv.red = (colour & 0x0000ff); \
}

static uint32
translate_colour(uint32 colour)
{
    PixelColour pc;
    switch (g_server_depth)
    {
        case 15:
            SPLITCOLOUR15(colour, pc);
            break;
        case 16:
            SPLITCOLOUR16(colour, pc);
            break;
        case 24:
        case 32:
            SPLITCOLOUR24(colour, pc);
            break;
        default:
            pc.red = 0;
            pc.green = 0;
            pc.blue = 0;
            break;
    }
    return (pc.red << 16) | (pc.green << 8) | pc.blue;
}

/* Utility functions */
static void
mac_fatal(char *format, ...)
{
    va_list ap;
    char message[1024];
    
    va_start(ap, format);
    vsnprintf(message, sizeof(message), format, ap);
    va_end(ap);
    
    NSLog(@"Fatal error: %s", message);
    exit(1);
}

@implementation RDPView

- (void)setupBackingStore:(NSSize)size {
    NSLog(@"[macOS DEBUG] RDPView: setupBackingStore called with size %.0fx%.0f", size.width, size.height);

    if (self.backingStore) {
        NSLog(@"[macOS DEBUG] RDPView: Releasing existing backing store");
        CGContextRelease(self.backingStore);
    }

    // Validate dimensions
    if (size.width <= 0 || size.height <= 0) {
        NSLog(@"[macOS ERROR] RDPView: Invalid backing store size: %.0fx%.0f", size.width, size.height);
        mac_fatal("Invalid backing store dimensions (zero or negative)");
        return;
    }

    if (size.width > 8192 || size.height > 8192) {
        NSLog(@"[macOS WARNING] RDPView: Backing store size exceeds maximum (8192): %.0fx%.0f", size.width, size.height);
    }

    self.backingStoreSize = size;
    NSLog(@"[macOS DEBUG] RDPView: backingStoreSize set to %.0fx%.0f", self.backingStoreSize.width, self.backingStoreSize.height);

    size_t bytesPerRow = (size_t)size.width * 4;
    NSLog(@"[macOS DEBUG] RDPView: Creating CGBitmapContext with bytesPerRow=%zu", bytesPerRow);

    CGContextRef context = CGBitmapContextCreate(
        NULL,                               // data (allocated by system)
        (size_t)size.width,                 // width
        (size_t)size.height,                // height
        8,                                  // bitsPerComponent
        bytesPerRow,                        // bytesPerRow
        g_colorspace,                       // colorspace
        kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little      // bitmapInfo
    );

    if (!context) {
        NSLog(@"[macOS ERROR] RDPView: Failed to create backing store context");
        NSLog(@"[macOS ERROR] RDPView:   width=%.0f, height=%.0f, bytesPerRow=%zu", size.width, size.height, bytesPerRow);
        mac_fatal("Failed to create backing store context");
        return;
    }

    NSLog(@"[macOS DEBUG] RDPView: CGBitmapContext created successfully at %p", context);

    self.backingStore = context;

    // Clear to black for visual feedback
    NSLog(@"[macOS DEBUG] RDPView: Clearing backing store to black");
    CGContextSetRGBFillColor(context, 0, 0, 0, 1);
    CGContextFillRect(context, CGRectMake(0, 0, size.width, size.height));

    NSLog(@"[macOS DEBUG] RDPView: setupBackingStore completed successfully");
}

- (instancetype)initWithFrame:(NSRect)frameRect {
    NSLog(@"[macOS DEBUG] RDPView: initWithFrame called with frame origin=(%.0f,%.0f), size=(%.0fx%.0f)",
          frameRect.origin.x, frameRect.origin.y, frameRect.size.width, frameRect.size.height);

    self = [super initWithFrame:frameRect];
    if (self) {
        NSLog(@"[macOS DEBUG] RDPView: super initWithFrame successful, setting up backing store...");
        self.wantsLayer = YES;
        self.layer.contentsGravity = @"resize";
        self.backingStoreLock = [[NSRecursiveLock alloc] init];  // Recursive lock to allow nested drawing calls
        [self setupBackingStore:frameRect.size];
        self.needsRedraw = NO;
        [self startDisplayTimer];
        NSLog(@"[macOS DEBUG] RDPView: initWithFrame completed, display timer started");
    } else {
        NSLog(@"[macOS ERROR] RDPView: super initWithFrame returned nil!");
    }
    return self;
}

- (void)dealloc {
    [self stopDisplayTimer];
    if (self.backingStore) {
        CGContextRelease(self.backingStore);
    }
    [super dealloc];
}

- (void)startDisplayTimer {
    if (!self.displayTimer) {
        // 60 FPS refresh rate (every ~16ms)
        self.displayTimer = [NSTimer scheduledTimerWithTimeInterval:0.016
                                                             target:self
                                                           selector:@selector(timerFired:)
                                                           userInfo:nil
                                                            repeats:YES];
    }
}

- (void)stopDisplayTimer {
    if (self.displayTimer) {
        [self.displayTimer invalidate];
        self.displayTimer = nil;
    }
}

- (void)timerFired:(NSTimer *)timer {
    (void)timer;
    BOOL redraw = NO;
    [self.backingStoreLock lock];
    if (self.needsRedraw) {
        self.needsRedraw = NO;
        redraw = YES;
    }

    if (redraw && self.backingStore) {
        CGImageRef image = CGBitmapContextCreateImage(self.backingStore);
        [self.backingStoreLock unlock];

        if (image) {
            self.layer.contents = (id)image;
            CGImageRelease(image);
        }
    } else {
        [self.backingStoreLock unlock];
    }
}

- (void)drawRect:(NSRect)dirtyRect {
    // Drawing is handled by CALayer contents for hardware acceleration
    (void)dirtyRect;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)keyDown:(NSEvent *)event {
    // Convert NSEvent to RDP key event
    uint16 keycode = [event keyCode];
    uint16 scancode_with_flags = mac_keycode_to_scancode(keycode);
    uint8 scancode = scancode_with_flags & 0xFF;
    uint16 flags = RDP_KEYPRESS | (scancode_with_flags & 0xFF00);
    queue_input_event(EVENT_KEY, [event timestamp] * 1000, flags, scancode, 0);
}

- (void)keyUp:(NSEvent *)event {
    uint16 keycode = [event keyCode];
    uint16 scancode_with_flags = mac_keycode_to_scancode(keycode);
    uint8 scancode = scancode_with_flags & 0xFF;
    uint16 flags = RDP_KEYRELEASE | (scancode_with_flags & 0xFF00);
    queue_input_event(EVENT_KEY, [event timestamp] * 1000, flags, scancode, 0);
}

- (NSPoint)translateMouseLocation:(NSPoint)locationInWindow {
    NSPoint point = [self convertPoint:locationInWindow fromView:nil];
    NSRect bounds = [self bounds];
    if (bounds.size.width > 0 && bounds.size.height > 0) {
        double scaleX = (double)g_width / bounds.size.width;
        double scaleY = (double)g_height / bounds.size.height;
        point.x = point.x * scaleX;
        point.y = point.y * scaleY;
    }
    return point;
}

- (void)mouseDown:(NSEvent *)event {
    NSPoint point = [self translateMouseLocation:[event locationInWindow]];
    int rdp_y = g_height - (int)point.y;
    queue_input_event(EVENT_MOUSE, [event timestamp] * 1000, MOUSE_FLAG_BUTTON1 | MOUSE_FLAG_DOWN, (int)point.x, rdp_y);
}

- (void)mouseUp:(NSEvent *)event {
    NSPoint point = [self translateMouseLocation:[event locationInWindow]];
    int rdp_y = g_height - (int)point.y;
    queue_input_event(EVENT_MOUSE, [event timestamp] * 1000, MOUSE_FLAG_BUTTON1, (int)point.x, rdp_y);
}

- (void)rightMouseDown:(NSEvent *)event {
    NSPoint point = [self translateMouseLocation:[event locationInWindow]];
    int rdp_y = g_height - (int)point.y;
    queue_input_event(EVENT_MOUSE, [event timestamp] * 1000, MOUSE_FLAG_BUTTON2 | MOUSE_FLAG_DOWN, (int)point.x, rdp_y);
}

- (void)rightMouseUp:(NSEvent *)event {
    NSPoint point = [self translateMouseLocation:[event locationInWindow]];
    int rdp_y = g_height - (int)point.y;
    queue_input_event(EVENT_MOUSE, [event timestamp] * 1000, MOUSE_FLAG_BUTTON2, (int)point.x, rdp_y);
}

- (void)mouseMoved:(NSEvent *)event {
    if (g_sendmotion) {
        NSPoint point = [self translateMouseLocation:[event locationInWindow]];
        int rdp_y = g_height - (int)point.y;
        queue_input_event(EVENT_MOUSE, [event timestamp] * 1000, MOUSE_FLAG_MOVE, (int)point.x, rdp_y);
    }
}

- (void)mouseDragged:(NSEvent *)event {
    NSPoint point = [self translateMouseLocation:[event locationInWindow]];
    int rdp_y = g_height - (int)point.y;
    queue_input_event(EVENT_MOUSE, [event timestamp] * 1000, MOUSE_FLAG_BUTTON1 | MOUSE_FLAG_MOVE, (int)point.x, rdp_y);
}

- (void)rightMouseDragged:(NSEvent *)event {
    NSPoint point = [self translateMouseLocation:[event locationInWindow]];
    int rdp_y = g_height - (int)point.y;
    queue_input_event(EVENT_MOUSE, [event timestamp] * 1000, MOUSE_FLAG_BUTTON2 | MOUSE_FLAG_MOVE, (int)point.x, rdp_y);
}

@end

@implementation RDPWindow

- (instancetype)initWithContentRect:(NSRect)contentRect {
    NSLog(@"[macOS DEBUG] RDPWindow: initWithContentRect called with origin=(%.0f,%.0f), size=(%.0fx%.0f)",
          contentRect.origin.x, contentRect.origin.y, contentRect.size.width, contentRect.size.height);

    NSUInteger styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

    NSLog(@"[macOS DEBUG] RDPWindow: Creating window with style mask=0x%lx, backing=NSBackingStoreBuffered, defer=NO", (unsigned long)styleMask);

    self = [super initWithContentRect:contentRect
                            styleMask:styleMask
                              backing:NSBackingStoreBuffered
                                defer:NO];

    if (self) {
        NSLog(@"[macOS DEBUG] RDPWindow: super initWithContentRect successful");

        [self setDelegate:self];
        [self setSharingType:NSWindowSharingReadOnly];
        NSLog(@"[macOS DEBUG] RDPWindow: Window delegate and sharingType set");

        NSLog(@"[macOS DEBUG] RDPWindow: Creating RDPView with frame origin=(%.0f,%.0f), size=(%.0fx%.0f)",
              contentRect.origin.x, contentRect.origin.y, contentRect.size.width, contentRect.size.height);

        self.rdpView = [[RDPView alloc] initWithFrame:contentRect];

        if (!self.rdpView) {
            NSLog(@"[macOS ERROR] RDPWindow: Failed to create RDPView!");
            return nil;
        }

        NSLog(@"[macOS DEBUG] RDPWindow: RDPView created successfully at %p", (void*)self.rdpView);

        [self setContentView:self.rdpView];
        NSLog(@"[macOS DEBUG] RDPWindow: Content view set");

        [self makeFirstResponder:self.rdpView];
        NSLog(@"[macOS DEBUG] RDPWindow: RDPView set as first responder");

        NSLog(@"[macOS DEBUG] RDPWindow: initWithContentRect completed successfully");
    } else {
        NSLog(@"[macOS ERROR] RDPWindow: super initWithContentRect returned nil!");
    }

    return self;
}

- (void)windowWillClose:(NSNotification *)notification {
    extern RD_BOOL g_exit_mainloop;
    g_exit_mainloop = True;
}

@end

/* UI Interface Implementation */

RD_BOOL
ui_init(void)
{
    if (![NSThread isMainThread]) {
        __block RD_BOOL result = False;
        dispatch_sync(dispatch_get_main_queue(), ^{
            result = ui_init();
        });
        return result;
    }

    NSLog(@"[macOS DEBUG] ui_init() ENTER");

    g_app = [NSApplication sharedApplication];
    [g_app setActivationPolicy:NSApplicationActivationPolicyRegular];
    [g_app finishLaunching];
    [g_app activateIgnoringOtherApps:YES];

    static id g_app_delegate = nil;
    if ([g_app delegate] == nil) {
        Class delegateClass = NSClassFromString(@"MacRDPAppDelegate");
        if (delegateClass) {
            g_app_delegate = [[delegateClass alloc] init];
            Class guiClass = NSClassFromString(@"MacRDPConnectionGUI");
            if (guiClass) {
                id gui = [[guiClass alloc] init];
                [g_app_delegate setValue:gui forKey:@"connectionGUI"];
            }
            [g_app setDelegate:g_app_delegate];
            [g_app_delegate performSelector:@selector(setupMenuBar)];
        }
    }

    NSLog(@"[macOS DEBUG] NSApplication initialized successfully");

    g_colorspace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (!g_colorspace) {
        NSLog(@"[macOS ERROR] Failed to create color space");
        mac_fatal("Failed to create color space");
        return False;
    }

    NSLog(@"[macOS DEBUG] Color space created successfully");
    NSLog(@"[macOS DEBUG] ui_init() EXIT - Success");

    return True;
}

void
ui_get_screen_size(uint32 * width, uint32 * height)
{
    NSScreen *mainScreen = [NSScreen mainScreen];
    NSRect screenRect = [mainScreen frame];
    *width = (uint32)screenRect.size.width;
    *height = (uint32)screenRect.size.height;
}

void
ui_get_screen_size_from_percentage(uint32 pw, uint32 ph, uint32 * width, uint32 * height)
{
    uint32 sw, sh;
    ui_get_screen_size(&sw, &sh);
    *width = sw * pw / 100;
    *height = sh * ph / 100;
}

void
ui_get_workarea_size(uint32 * width, uint32 * height)
{
    NSScreen *mainScreen = [NSScreen mainScreen];
    NSRect visibleFrame = [mainScreen visibleFrame];
    *width = (uint32)visibleFrame.size.width;
    *height = (uint32)visibleFrame.size.height;
}

void
ui_deinit(void)
{
    if (g_colorspace) {
        CGColorSpaceRelease(g_colorspace);
        g_colorspace = NULL;
    }
}

void
ui_update_window_sizehints(uint32 width, uint32 height)
{
    if (![NSThread isMainThread]) {
        dispatch_sync(dispatch_get_main_queue(), ^{
            ui_update_window_sizehints(width, height);
        });
        return;
    }

    if (g_window) {
        NSSize minSize = NSMakeSize(width, height);
        [g_window setContentMinSize:minSize];
    }
}

RD_BOOL
ui_create_window(uint32 width, uint32 height)
{
    if (![NSThread isMainThread]) {
        __block RD_BOOL result = False;
        dispatch_sync(dispatch_get_main_queue(), ^{
            result = ui_create_window(width, height);
        });
        return result;
    }

    NSLog(@"========================================");
    NSLog(@"[macOS DEBUG] ui_create_window() ENTER");
    NSLog(@"[macOS DEBUG] Requested dimensions: %u x %u", width, height);
    NSLog(@"========================================");

    // Validate dimensions
    if (width == 0 || height == 0) {
        NSLog(@"[macOS ERROR] Invalid dimensions: width=%u, height=%u", width, height);
        mac_fatal("Invalid window dimensions (zero width or height)");
        return False;
    }

    if (width > 8192 || height > 8192) {
        NSLog(@"[macOS WARNING] Dimensions exceed maximum (8192): width=%u, height=%u", width, height);
    }

    if (width < 200 || height < 200) {
        NSLog(@"[macOS WARNING] Dimensions below minimum (200): width=%u, height=%u", width, height);
    }

    g_width = width;
    g_height = height;

    NSLog(@"[macOS DEBUG] Global dimensions set: g_width=%u, g_height=%u", g_width, g_height);

    NSRect contentRect = NSMakeRect(0, 0, width, height);
    NSLog(@"[macOS DEBUG] Creating NSRect: origin=(%.0f,%.0f), size=(%.0fx%.0f)",
          contentRect.origin.x, contentRect.origin.y,
          contentRect.size.width, contentRect.size.height);

    g_window = [[RDPWindow alloc] initWithContentRect:contentRect];

    if (!g_window) {
        NSLog(@"[macOS ERROR] Failed to allocate RDPWindow");
        mac_fatal("Failed to create window");
        return False;
    }

    NSLog(@"[macOS DEBUG] RDPWindow allocated successfully at %p", (void*)g_window);

    g_view = g_window.rdpView;

    if (!g_view) {
        NSLog(@"[macOS ERROR] Failed to get RDPView from window");
        mac_fatal("Failed to get view from window");
        return False;
    }

    NSLog(@"[macOS DEBUG] RDPView obtained successfully at %p", (void*)g_view);
    NSLog(@"[macOS DEBUG] View backing store size: %.0fx%.0f",
          g_view.backingStoreSize.width, g_view.backingStoreSize.height);

    [g_window setTitle:[NSString stringWithUTF8String:g_title]];
    NSLog(@"[macOS DEBUG] Window title set to: %s", g_title);

    [g_window center];
    NSLog(@"[macOS DEBUG] Window centered on screen");

    [g_window makeKeyAndOrderFront:nil];
    NSLog(@"[macOS DEBUG] Window made key and ordered front");

    // Force window to foreground with multiple activation attempts
    [g_app activateIgnoringOtherApps:YES];
    NSLog(@"[macOS DEBUG] Application activated (ignoring other apps)");

    // Additional activation to ensure window is truly foregrounded
    [NSApp activateIgnoringOtherApps:YES];
    [g_window orderFrontRegardless];
    [g_window makeMainWindow];
    [g_window makeKeyWindow];
    NSLog(@"[macOS DEBUG] Window forced to foreground with orderFrontRegardless + makeMainWindow + makeKeyWindow");

    // Verify window is visible
    if ([g_window isVisible]) {
        NSLog(@"[macOS DEBUG] Window is now VISIBLE");
    } else {
        NSLog(@"[macOS WARNING] Window is NOT VISIBLE after makeKeyAndOrderFront");
    }

    // Log final window frame
    NSRect finalFrame = [g_window frame];
    NSLog(@"[macOS DEBUG] Final window frame: origin=(%.0f,%.0f), size=(%.0fx%.0f)",
          finalFrame.origin.x, finalFrame.origin.y,
          finalFrame.size.width, finalFrame.size.height);

    NSLog(@"========================================");
    NSLog(@"[macOS DEBUG] ui_create_window() EXIT - Success");
    NSLog(@"[macOS DEBUG] Window %p with dimensions %ux%u is ready", (void*)g_window, g_width, g_height);
    NSLog(@"========================================");

    return True;
}

void
ui_resize_window(uint32 width, uint32 height)
{
    if (![NSThread isMainThread]) {
        dispatch_sync(dispatch_get_main_queue(), ^{
            ui_resize_window(width, height);
        });
        return;
    }

    if (g_window && g_view) {
        g_width = width;
        g_height = height;
        
        NSSize newSize = NSMakeSize(width, height);
        [g_window setContentSize:newSize];
        [g_view setupBackingStore:newSize];
    }
}

RD_BOOL
ui_have_window(void)
{
    return g_window != nil;
}

void
ui_destroy_window(void)
{
    if (![NSThread isMainThread]) {
        dispatch_sync(dispatch_get_main_queue(), ^{
            ui_destroy_window();
        });
        return;
    }

    if (g_window) {
        [g_window setDelegate:nil];
        [g_window close];
        g_window = nil;
        g_view = nil;
    }
}



void
ui_select(int rdp_socket)
{
    process_queued_input_events();
    fd_set rfds;
    struct timeval tv;
    int retval;

    FD_ZERO(&rfds);
    FD_SET(rdp_socket, &rfds);

    tv.tv_sec = 0;
    tv.tv_usec = 5000;  // 5ms timeout - responsive and CPU efficient

    retval = select(rdp_socket + 1, &rfds, NULL, NULL, &tv);
    (void)retval; // Suppress unused variable warning

    // Process Cocoa events synchronously (only if we're on the main thread)
    if ([NSThread isMainThread]) {
        NSEvent *event;
        while ((event = [g_app nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate distantPast]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES]))
        {
            [g_app sendEvent:event];
            [g_app updateWindows];
        }
    }

    // Don't force display here - let the 60 FPS timer handle all redraws to avoid race conditions
}

void
ui_move_pointer(int x, int y)
{
    (void)x; (void)y;
    // Move cursor - not implemented for now
}

RD_HBITMAP
ui_create_bitmap(int width, int height, uint8 * data)
{
    CGContextRef context = CGBitmapContextCreate(
        NULL,                               // data (let CG allocate it)
        width,                              // width  
        height,                             // height
        8,                                  // bitsPerComponent
        width * 4,                          // bytesPerRow
        g_colorspace,                       // colorspace
        kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little // bitmapInfo
    );
    
    if (context && data) {
        void *dstData = CGBitmapContextGetData(context);
        if (dstData) {
            memcpy(dstData, data, width * height * 4);
        }
    }
    
    return (RD_HBITMAP)context;
}

void
ui_paint_bitmap(int x, int y, int cx, int cy, int width, int height, uint8 * data)
{
    NSLog(@"[macOS DEBUG] ui_paint_bitmap: x=%d y=%d cx=%d cy=%d width=%d height=%d", x, y, cx, cy, width, height);
    if (!g_view || !g_view.backingStore) {
        NSLog(@"[macOS DEBUG] ui_paint_bitmap: g_view or backingStore is NULL, returning");
        return;
    }

    [g_view.backingStoreLock lock];

    CGContextRef context = g_view.backingStore;

    CGDataProviderRef dataProvider = CGDataProviderCreateWithData(NULL, data, width * height * 4, NULL);
    CGImageRef image = CGImageCreate(
        width,                              // width
        height,                             // height
        8,                                  // bitsPerComponent
        32,                                 // bitsPerPixel
        width * 4,                          // bytesPerRow
        g_colorspace,                       // colorspace
        (CGBitmapInfo)(kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little), // bitmapInfo
        dataProvider,                       // dataProvider
        NULL,                               // decode
        false,                              // shouldInterpolate
        kCGRenderingIntentDefault           // intent
    );

    CGRect destRect = CGRectMake(x, g_view.backingStoreSize.height - y - cy, cx, cy);
    CGRect sourceRect = CGRectMake(0, 0, width, height);

    CGImageRef croppedImage = CGImageCreateWithImageInRect(image, sourceRect);
    CGContextDrawImage(context, destRect, croppedImage);

    CGImageRelease(croppedImage);
    CGImageRelease(image);
    CGDataProviderRelease(dataProvider);

    [g_view.backingStoreLock unlock];

    g_view.needsRedraw = YES;
    dispatch_async(dispatch_get_main_queue(), ^{
        [g_view setNeedsDisplayInRect:NSMakeRect(x, y, cx, cy)];
    });
}

void
ui_destroy_bitmap(RD_HBITMAP bmp)
{
    if (bmp) {
        CGContextRelease((CGContextRef)bmp);
    }
}

/* Simplified stubs for functions we don't immediately need */
RD_HGLYPH ui_create_glyph(int width, int height, uint8 * data) {
    NSLog(@"[macOS DEBUG] ui_create_glyph: width=%d height=%d", width, height);

    // Glyph data is 1-bit per pixel, MSB first
    int scanline = (width + 7) / 8;

    // Allocate memory for the glyph data (copy it since it may be freed by caller)
    uint8 *glyph_data = (uint8 *)malloc(scanline * height);
    if (!glyph_data) {
        NSLog(@"[macOS DEBUG] ui_create_glyph: failed to allocate memory");
        return NULL;
    }

    // Just copy the data directly - let's test without bit reversal
    memcpy(glyph_data, data, scanline * height);

    // Create a CGDataProvider for the 1-bit data
    CGDataProviderRef dataProvider = CGDataProviderCreateWithData(
        NULL,           // info
        glyph_data,     // data
        scanline * height,  // size
        glyph_data_release_callback  // releaseData callback
    );

    if (!dataProvider) {
        NSLog(@"[macOS DEBUG] ui_create_glyph: failed to create data provider");
        free(glyph_data);
        return NULL;
    }

    // Create a 1-bit mask image
    // decode array to invert: {1, 0} means map input 0->1 (opaque) and input 1->0 (transparent)
    // This inverts the mask since RDP uses 1=pixel set, 0=pixel clear
    // but CGImageMask uses 0=clear/transparent, 1=opaque/paint
    CGFloat decode[2] = {1.0, 0.0};  // Invert the mask
    CGImageRef maskImage = CGImageMaskCreate(
        width,              // width
        height,             // height
        1,                  // bitsPerComponent (1-bit)
        1,                  // bitsPerPixel
        scanline,           // bytesPerRow
        dataProvider,       // provider
        decode,             // decode array to invert mask
        false               // shouldInterpolate
    );

    CGDataProviderRelease(dataProvider);

    if (!maskImage) {
        NSLog(@"[macOS DEBUG] ui_create_glyph: failed to create mask image");
        free(glyph_data);
        return NULL;
    }

    NSLog(@"[macOS DEBUG] ui_create_glyph: successfully created glyph");
    return (RD_HGLYPH)maskImage;
}

void ui_destroy_glyph(RD_HGLYPH glyph) {
    if (glyph) {
        CGImageRelease((CGImageRef)glyph);
    }
}
RD_HCURSOR ui_create_cursor(unsigned int x, unsigned int y, uint32 width, uint32 height, uint8 * andmask, uint8 * xormask, int bpp) { 
    (void)x; (void)y; (void)width; (void)height; (void)andmask; (void)xormask; (void)bpp;
    return NULL; 
}
void ui_set_cursor(RD_HCURSOR cursor) { (void)cursor; }
void ui_destroy_cursor(RD_HCURSOR cursor) { (void)cursor; }
void ui_set_null_cursor(void) {}
void ui_set_standard_cursor(void) {}
RD_HCOLOURMAP ui_create_colourmap(COLOURMAP * colours) { (void)colours; return NULL; }
void ui_destroy_colourmap(RD_HCOLOURMAP map) { (void)map; }
void ui_set_colourmap(RD_HCOLOURMAP map) { (void)map; }
void ui_set_clip(int x, int y, int cx, int cy) { (void)x; (void)y; (void)cx; (void)cy; }
void ui_reset_clip(void) {}
void ui_bell(void) { NSBeep(); }

/* Drawing operations - basic implementations */
void ui_destblt(uint8 opcode, int x, int y, int cx, int cy) {
    (void)opcode; (void)x; (void)y; (void)cx; (void)cy;
}
void ui_patblt(uint8 opcode, int x, int y, int cx, int cy, BRUSH * brush, uint32 bgcolor, uint32 fgcolor) {
    (void)brush; (void)fgcolor;
    NSLog(@"[macOS DEBUG] ui_patblt: opcode=%d x=%d y=%d cx=%d cy=%d", opcode, x, y, cx, cy);
    if (!g_view || !g_view.backingStore) {
        NSLog(@"[macOS DEBUG] ui_patblt: g_view or backingStore is NULL, returning");
        return;
    }

    [g_view.backingStoreLock lock];

    CGContextRef context = g_view.backingStore;
    CGRect rect = CGRectMake(x, g_view.backingStoreSize.height - y - cy, cx, cy);

    // Convert bgcolor from 32-bit RGBA to CGColor components
    float r = ((bgcolor >> 16) & 0xFF) / 255.0f;
    float g = ((bgcolor >> 8) & 0xFF) / 255.0f;
    float b = (bgcolor & 0xFF) / 255.0f;
    float a = 1.0f;

    CGContextSetRGBFillColor(context, r, g, b, a);

    switch(opcode) {
        case ROP2_COPY:
            CGContextFillRect(context, rect);
            break;
        case ROP2_XOR:
            CGContextSetBlendMode(context, kCGBlendModeExclusion);
            CGContextFillRect(context, rect);
            CGContextSetBlendMode(context, kCGBlendModeNormal);
            break;
        default:
            CGContextFillRect(context, rect);
            break;
    }

    g_view.needsRedraw = YES;
    [g_view.backingStoreLock unlock];

    dispatch_async(dispatch_get_main_queue(), ^{
        [g_view setNeedsDisplayInRect:NSMakeRect(x, y, cx, cy)];
    });
}
void ui_screenblt(uint8 opcode, int x, int y, int cx, int cy, int srcx, int srcy) {
    (void)opcode;
    if (!g_view || !g_view.backingStore) {
        return;
    }

    [g_view.backingStoreLock lock];

    CGContextRef context = g_view.backingStore;

    // Create a snapshot of the entire backing store
    CGImageRef screenImage = CGBitmapContextCreateImage(context);
    if (screenImage) {
        // Crop the source rect (translating RDP top-left to CG bottom-left)
        CGRect cropRect = CGRectMake(srcx, g_view.backingStoreSize.height - srcy - cy, cx, cy);
        CGImageRef croppedImage = CGImageCreateWithImageInRect(screenImage, cropRect);

        if (croppedImage) {
            // Destination rect
            CGRect destRect = CGRectMake(x, g_view.backingStoreSize.height - y - cy, cx, cy);

            // Draw back into the context
            CGContextDrawImage(context, destRect, croppedImage);

            CGImageRelease(croppedImage);
        }
        CGImageRelease(screenImage);
    }

    g_view.needsRedraw = YES;
    [g_view.backingStoreLock unlock];
}

void ui_memblt(uint8 opcode, int x, int y, int cx, int cy, RD_HBITMAP src, int srcx, int srcy) {
    (void)opcode;
    if (!g_view || !g_view.backingStore || !src) {
        return;
    }

    [g_view.backingStoreLock lock];

    CGContextRef dstContext = g_view.backingStore;
    CGContextRef srcContext = (CGContextRef)src;

    // Create an image from the source context
    CGImageRef srcImage = CGBitmapContextCreateImage(srcContext);
    if (srcImage) {
        size_t srcHeight = CGImageGetHeight(srcImage);

        // Crop the source rect (translating RDP top-left to CG bottom-left)
        CGRect cropRect = CGRectMake(srcx, srcHeight - srcy - cy, cx, cy);
        CGImageRef croppedImage = CGImageCreateWithImageInRect(srcImage, cropRect);

        if (croppedImage) {
            // Destination rect in backingStore (bottom-left coordinate system)
            CGRect destRect = CGRectMake(x, g_view.backingStoreSize.height - y - cy, cx, cy);

            // Draw it
            CGContextDrawImage(dstContext, destRect, croppedImage);

            CGImageRelease(croppedImage);
        }
        CGImageRelease(srcImage);
    }

    g_view.needsRedraw = YES;
    [g_view.backingStoreLock unlock];
}

void ui_triblt(uint8 opcode, int x, int y, int cx, int cy, RD_HBITMAP src, int srcx, int srcy, BRUSH * brush, uint32 bgcolor, uint32 fgcolor) {
    (void)brush; (void)bgcolor; (void)fgcolor;
    ui_memblt(opcode, x, y, cx, cy, src, srcx, srcy);
}

void ui_line(uint8 opcode, int startx, int starty, int endx, int endy, PEN * pen) {
    (void)opcode;
    if (!g_view || !g_view.backingStore) {
        return;
    }

    [g_view.backingStoreLock lock];

    CGContextRef context = g_view.backingStore;

    // Set line width
    CGContextSetLineWidth(context, pen->width);

    // Translate color
    uint32 rgb_colour = translate_colour(pen->colour);
    float r = ((rgb_colour >> 16) & 0xFF) / 255.0f;
    float g = ((rgb_colour >> 8) & 0xFF) / 255.0f;
    float b = (rgb_colour & 0xFF) / 255.0f;
    float a = 1.0f;
    CGContextSetRGBStrokeColor(context, r, g, b, a);

    // Draw line
    CGContextBeginPath(context);
    CGContextMoveToPoint(context, startx, g_view.backingStoreSize.height - starty);
    CGContextAddLineToPoint(context, endx, g_view.backingStoreSize.height - endy);
    CGContextStrokePath(context);

    g_view.needsRedraw = YES;
    [g_view.backingStoreLock unlock];
}
void ui_rect(int x, int y, int cx, int cy, uint32 colour) {
    if (!g_view || !g_view.backingStore) {
        return;
    }

    [g_view.backingStoreLock lock];

    CGContextRef context = g_view.backingStore;
    CGRect rect = CGRectMake(x, g_view.backingStoreSize.height - y - cy, cx, cy);

    // Translate RDP color format to RGB
    uint32 rgb_colour = translate_colour(colour);

    // Convert color to CGColor components
    float r = ((rgb_colour >> 16) & 0xFF) / 255.0f;
    float g = ((rgb_colour >> 8) & 0xFF) / 255.0f;
    float b = (rgb_colour & 0xFF) / 255.0f;
    float a = 1.0f;

    CGContextSetRGBFillColor(context, r, g, b, a);
    CGContextFillRect(context, rect);

    g_view.needsRedraw = YES;
    [g_view.backingStoreLock unlock];
}
void ui_polygon(uint8 opcode, uint8 fillmode, RD_POINT * point, int npoints, BRUSH * brush, uint32 bgcolor, uint32 fgcolor) {
    (void)opcode; (void)fillmode; (void)point; (void)npoints;
    (void)brush; (void)bgcolor; (void)fgcolor;
}
void ui_polyline(uint8 opcode, RD_POINT * points, int npoints, PEN * pen) {
    (void)opcode; (void)points; (void)npoints; (void)pen;
}
void ui_ellipse(uint8 opcode, uint8 fillmode, int x, int y, int cx, int cy, BRUSH * brush, uint32 bgcolor, uint32 fgcolor) {
    (void)opcode; (void)fillmode; (void)x; (void)y; (void)cx; (void)cy;
    (void)brush; (void)bgcolor; (void)fgcolor;
}
void ui_draw_glyph(int mixmode, int x, int y, int cx, int cy, RD_HGLYPH glyph, int srcx, int srcy, uint32 bgcolour, uint32 fgcolour) {
    (void)srcx; (void)srcy;  // These are always 0

    if (!g_view || !g_view.backingStore || !glyph) {
        return;
    }

    [g_view.backingStoreLock lock];

    CGContextRef context = g_view.backingStore;
    CGImageRef maskImage = (CGImageRef)glyph;

    // Convert Y coordinate (RDP uses top-left origin, CG uses bottom-left)
    CGFloat cgY = g_view.backingStoreSize.height - y - cy;

    CGContextSaveGState(context);

    // Translate RDP color format to RGB
    uint32 fgRGB = translate_colour(fgcolour);
    uint32 bgRGB = translate_colour(bgcolour);

    // Set foreground and background colors
    float fgR = ((fgRGB >> 16) & 0xFF) / 255.0f;
    float fgG = ((fgRGB >> 8) & 0xFF) / 255.0f;
    float fgB = (fgRGB & 0xFF) / 255.0f;

    float bgR = ((bgRGB >> 16) & 0xFF) / 255.0f;
    float bgG = ((bgRGB >> 8) & 0xFF) / 255.0f;
    float bgB = (bgRGB & 0xFF) / 255.0f;

    CGRect rect = CGRectMake(x, cgY, cx, cy);

    // Draw background if not transparent mode
    if (mixmode != 0) {  // MIX_OPAQUE
        CGContextSetRGBFillColor(context, bgR, bgG, bgB, 1.0f);
        CGContextFillRect(context, rect);
    }

    // Draw the glyph using the mask
    CGContextSetRGBFillColor(context, fgR, fgG, fgB, 1.0f);
    CGContextClipToMask(context, rect, maskImage);
    CGContextFillRect(context, rect);

    CGContextRestoreGState(context);

    g_view.needsRedraw = YES;
    [g_view.backingStoreLock unlock];
}
void ui_draw_text(uint8 font, uint8 flags, uint8 opcode, int mixmode, int x, int y, int clipx, int clipy, int clipcx, int clipcy, int boxx, int boxy, int boxcx, int boxcy, BRUSH * brush, uint32 bgcolor, uint32 fgcolor, uint8 * text, uint8 length) {
    (void)opcode; (void)brush;

    if (!g_view || !g_view.backingStore) {
        return;
    }

    [g_view.backingStoreLock lock];

    CGContextRef context = g_view.backingStore;
    FONTGLYPH *glyph;
    int i, j, xyoffset, x1, y1;
    DATABLOB *entry;

    // Clamp boxcx to window width to avoid overflow issues
    if (boxx + boxcx > (int)g_view.backingStoreSize.width)
        boxcx = g_view.backingStoreSize.width - boxx;

    // Draw text box background
    if (boxcx > 1) {
        // Draw background box
        float bgR = ((bgcolor >> 16) & 0xFF) / 255.0f;
        float bgG = ((bgcolor >> 8) & 0xFF) / 255.0f;
        float bgB = (bgcolor & 0xFF) / 255.0f;
        CGFloat cgBoxY = g_view.backingStoreSize.height - boxy - boxcy;
        CGContextSetRGBFillColor(context, bgR, bgG, bgB, 1.0f);
        CGContextFillRect(context, CGRectMake(boxx, cgBoxY, boxcx, boxcy));
    } else if (mixmode == 1) {  // MIX_OPAQUE
        // Draw background in clip region
        float bgR = ((bgcolor >> 16) & 0xFF) / 255.0f;
        float bgG = ((bgcolor >> 8) & 0xFF) / 255.0f;
        float bgB = (bgcolor & 0xFF) / 255.0f;
        CGFloat cgClipY = g_view.backingStoreSize.height - clipy - clipcy;
        CGContextSetRGBFillColor(context, bgR, bgG, bgB, 1.0f);
        CGContextFillRect(context, CGRectMake(clipx, cgClipY, clipcx, clipcy));
    }

    // Process text, character by character
    for (i = 0; i < length;) {
        switch (text[i]) {
            case 0xff:
                // Cache text command
                if (i + 3 > length) {
                    NSLog(@"[macOS DEBUG] ui_draw_text: skipping short 0xff command");
                    i = length = 0;
                    break;
                }
                cache_put_text(text[i + 1], text, text[i + 2]);
                i += 3;
                length -= i;
                text = &(text[i]);
                i = 0;
                break;

            case 0xfe:
                // Retrieve cached text
                if (i + 2 > length) {
                    NSLog(@"[macOS DEBUG] ui_draw_text: skipping short 0xfe command");
                    i = length = 0;
                    break;
                }
                entry = cache_get_text(text[i + 1]);
                if (entry && entry->data != NULL) {
                    if ((((uint8 *)(entry->data))[1] == 0) &&
                        (!(flags & 0x20)) && (i + 2 < length)) {  // TEXT2_IMPLICIT_X
                        if (flags & 0x04)  // TEXT2_VERTICAL
                            y += text[i + 2];
                        else
                            x += text[i + 2];
                    }
                    for (j = 0; j < entry->size; j++) {
                        // DO_GLYPH macro inlined
                        glyph = cache_get_font(font, ((uint8 *)(entry->data))[j]);
                        if (!(flags & 0x20)) {  // TEXT2_IMPLICIT_X
                            xyoffset = ((uint8 *)(entry->data))[++j];
                            if ((xyoffset & 0x80)) {
                                if (flags & 0x04)  // TEXT2_VERTICAL
                                    y += ((uint8 *)(entry->data))[j+1] | (((uint8 *)(entry->data))[j+2] << 8);
                                else
                                    x += ((uint8 *)(entry->data))[j+1] | (((uint8 *)(entry->data))[j+2] << 8);
                                j += 2;
                            } else {
                                if (flags & 0x04)  // TEXT2_VERTICAL
                                    y += xyoffset;
                                else
                                    x += xyoffset;
                            }
                        }
                        if (glyph != NULL) {
                            x1 = x + glyph->offset;
                            y1 = y + glyph->baseline;
                            ui_draw_glyph(mixmode, x1, y1, glyph->width, glyph->height,
                                        glyph->pixmap, 0, 0, bgcolor, fgcolor);
                            if (flags & 0x20)  // TEXT2_IMPLICIT_X
                                x += glyph->width;
                        }
                    }
                }
                if (i + 2 < length)
                    i += 3;
                else
                    i += 2;
                length -= i;
                text = &(text[i]);
                i = 0;
                break;

            default:
                // Regular character
                glyph = cache_get_font(font, text[i]);
                if (!(flags & 0x20)) {  // TEXT2_IMPLICIT_X
                    xyoffset = text[++i];
                    if ((xyoffset & 0x80)) {
                        if (flags & 0x04)  // TEXT2_VERTICAL
                            y += text[i+1] | (text[i+2] << 8);
                        else
                            x += text[i+1] | (text[i+2] << 8);
                        i += 2;
                    } else {
                        if (flags & 0x04)  // TEXT2_VERTICAL
                            y += xyoffset;
                        else
                            x += xyoffset;
                    }
                }
                if (glyph != NULL) {
                    x1 = x + glyph->offset;
                    y1 = y + glyph->baseline;
                    ui_draw_glyph(mixmode, x1, y1, glyph->width, glyph->height,
                                glyph->pixmap, 0, 0, bgcolor, fgcolor);
                    if (flags & 0x20)  // TEXT2_IMPLICIT_X
                        x += glyph->width;
                }
                i++;
                break;
        }
    }

    [g_view.backingStoreLock unlock];
}
void ui_desktop_save(uint32 offset, int x, int y, int cx, int cy) {
    (void)offset; (void)x; (void)y; (void)cx; (void)cy;
}
void ui_desktop_restore(uint32 offset, int x, int y, int cx, int cy) {
    (void)offset; (void)x; (void)y; (void)cx; (void)cy;
}


void ui_begin_update(void) {
}

void ui_end_update(void) {
    if (g_view) {
        [g_view.backingStoreLock lock];
        g_view.needsRedraw = YES;
        [g_view.backingStoreLock unlock];
    }
}

/* Seamless mode stubs */
void ui_seamless_begin(RD_BOOL hidden) { (void)hidden; }
void ui_seamless_end(void) {}
void ui_seamless_hide_desktop(void) {}
void ui_seamless_unhide_desktop(void) {}
void ui_seamless_toggle(void) {}
void ui_seamless_create_window(unsigned long id, unsigned long group, unsigned long parent, unsigned long flags) {
    (void)id; (void)group; (void)parent; (void)flags;
}
void ui_seamless_destroy_window(unsigned long id, unsigned long flags) { (void)id; (void)flags; }
void ui_seamless_destroy_group(unsigned long id, unsigned long flags) { (void)id; (void)flags; }
void ui_seamless_seticon(unsigned long id, const char *format, int width, int height, int chunk, const char *data, size_t chunk_len) {
    (void)id; (void)format; (void)width; (void)height; (void)chunk; (void)data; (void)chunk_len;
}
void ui_seamless_delicon(unsigned long id, const char *format, int width, int height) {
    (void)id; (void)format; (void)width; (void)height;
}
void ui_seamless_move_window(unsigned long id, int x, int y, int width, int height, unsigned long flags) {
    (void)id; (void)x; (void)y; (void)width; (void)height; (void)flags;
}
void ui_seamless_restack_window(unsigned long id, unsigned long behind, unsigned long flags) {
    (void)id; (void)behind; (void)flags;
}
void ui_seamless_settitle(unsigned long id, const char *title, unsigned long flags) {
    (void)id; (void)title; (void)flags;
}
void ui_seamless_setstate(unsigned long id, unsigned int state, unsigned long flags) {
    (void)id; (void)state; (void)flags;
}
void ui_seamless_syncbegin(unsigned long flags) { (void)flags; }
void ui_seamless_ack(unsigned int serial) { (void)serial; }

/* Global variables for dynamic session management */
RD_BOOL g_dynamic_session_resize = False;
uint32 g_wait_for_deactivate_ts = 0;

void show_nla_instructions_alert(void) {
    if (![NSThread isMainThread]) {
        dispatch_sync(dispatch_get_main_queue(), ^{
            show_nla_instructions_alert();
        });
        return;
    }
    
    [NSApp activateIgnoringOtherApps:YES];
    
    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Connection Failed / RDP Security Error"];
    
    NSString *infoText = 
        @"The RDP connection was closed or rejected by the server.\n\n"
        @"This typically happens due to one of the following reasons:\n\n"
        @"1. Another user is logged in at the Windows physical console, which prevents new incoming RDP sessions on non-Server Windows editions. (Please ensure you have logged out the console user on the host machine before connecting).\n\n"
        @"2. A network drop or credential mismatch occurred during secure negotiation.";
        
    [alert setInformativeText:infoText];
    [alert setAlertStyle:NSAlertStyleWarning];
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
}
