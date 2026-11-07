/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   macOS Connection GUI Implementation
   Native macOS port
*/

#import "MacRDPConnectionGUI.h"
#include "rdesktop.h"

// External variables from rdesktop
extern char g_hostname[16];
extern char *g_username;
extern char g_password[64];
extern int g_tcp_port_rdp;
extern uint32 g_requested_session_width;
extern uint32 g_requested_session_height;
extern char g_keymapname[PATH_MAX];

@implementation MacRDPConnectionGUI

- (void)windowDidLoad {
    [super windowDidLoad];
    
    // Set window title
    [[self window] setTitle:@"rDesktop GUI - Connect to RDP"];
    
    // Set default values
    [self.portField setStringValue:@"3389"];
    [self.widthField setStringValue:@"1366"];
    [self.heightField setStringValue:@"768"];
    
    // Populate keymap dropdown
    [self setupKeymapPopup];
    
    // Set default keymap to "en-us"
    [self.keymapPopup selectItemWithTitle:@"en-us"];
}

- (void)setupKeymapPopup {
    NSArray *keymaps = @[@"en-us", @"en-gb", @"de", @"fr", @"es", @"sv", @"da", @"no", @"fi", @"it", @"pt", @"nl", @"ru", @"ja", @"ko"];
    
    [self.keymapPopup removeAllItems];
    for (NSString *keymap in keymaps) {
        [self.keymapPopup addItemWithTitle:keymap];
    }
}

- (void)showConnectionWindow {
    if (!self.window) {
        // Create window programmatically - start with basic size (620 x 240)
        NSRect windowRect = NSMakeRect(0, 0, 620, 240);
        NSWindow *window = [[NSWindow alloc] initWithContentRect:windowRect
                                                       styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable)
                                                          backing:NSBackingStoreBuffered
                                                            defer:NO];
        [window setDelegate:self];
        [window setReleasedWhenClosed:NO];
        [self setWindow:window];
        
        // Create content view and disable subviews autoresizing
        NSView *contentView = [[NSView alloc] initWithFrame:windowRect];
        [contentView setAutoresizesSubviews:NO];
        [window setContentView:contentView];
        
        // Create basicView container
        self.basicView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 620, 240)];
        [self.basicView setAutoresizesSubviews:NO];
        [contentView addSubview:self.basicView];
        
        // Create advancedView container (starts hidden)
        self.advancedView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 620, 400)];
        [self.advancedView setAutoresizesSubviews:NO];
        [self.advancedView setHidden:YES];
        [contentView addSubview:self.advancedView];
        
        [self setupWindowControls:self.basicView];
        [self setupAdvancedOptions:self.advancedView];
    }
    
    [[self window] center];
    [[self window] makeKeyAndOrderFront:nil];
}

- (IBAction)advancedClicked:(id)sender {
    [self toggleAdvancedOptions];
}

- (void)toggleAdvancedOptions {
    self.advancedVisible = !self.advancedVisible;
    
    NSWindow *window = [self window];
    NSRect currentFrame = [window frame];
    NSRect newFrame;
    
    if (self.advancedVisible) {
        // Expand window to show advanced options (add 400 height)
        newFrame = NSMakeRect(currentFrame.origin.x, 
                             currentFrame.origin.y - 400, // Move up to keep top in place
                             currentFrame.size.width,
                             currentFrame.size.height + 400);
        [self.advancedButton setTitle:@"Basic"];
        // Position basicView at the top of the expanded window
        [self.basicView setFrame:NSMakeRect(0, 400, 620, 240)];
        // Position advancedView at the bottom of the expanded window
        [self.advancedView setFrame:NSMakeRect(0, 0, 620, 400)];
        [self.advancedView setHidden:NO];
    } else {
        // Contract window to hide advanced options (remove 400 height)
        newFrame = NSMakeRect(currentFrame.origin.x,
                             currentFrame.origin.y + 400, // Move down
                             currentFrame.size.width,
                             currentFrame.size.height - 400);
        [self.advancedButton setTitle:@"Advanced"];
        // Position basicView back at the bottom
        [self.basicView setFrame:NSMakeRect(0, 0, 620, 240)];
        [self.advancedView setHidden:YES];
    }
    
    // Animate the window resize
    [window setFrame:newFrame display:YES animate:YES];
}

- (void)setupWindowControls:(NSView *)contentView {
    CGFloat y = 195; // Start at 195 for a 240-tall window
    CGFloat fieldHeight = 22;
    CGFloat labelWidth = 120;
    CGFloat fieldWidth = 200;
    CGFloat margin = 150; // Center basic controls inside 620-wide window
    
    self.advancedVisible = NO;
    
    // Host:Port
    NSTextField *hostLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, labelWidth, fieldHeight)];
    [hostLabel setStringValue:@"Host:Port"];
    [hostLabel setBezeled:NO];
    [hostLabel setDrawsBackground:NO];
    [hostLabel setEditable:NO];
    [hostLabel setSelectable:NO];
    [contentView addSubview:hostLabel];
    
    self.hostField = [[NSComboBox alloc] initWithFrame:NSMakeRect(margin + labelWidth, y, fieldWidth - 80, fieldHeight)];
    [self.hostField setCompletes:YES];
    [self.hostField setHasVerticalScroller:YES];
    [self.hostField setDelegate:self];
    [contentView addSubview:self.hostField];
    [self loadSavedProfiles];
    
    self.portField = [[NSTextField alloc] initWithFrame:NSMakeRect(margin + labelWidth + fieldWidth - 75, y, 70, fieldHeight)];
    [self.portField setStringValue:@"3389"];
    [contentView addSubview:self.portField];
    
    y -= 30;
    
    // Username
    NSTextField *userLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, labelWidth, fieldHeight)];
    [userLabel setStringValue:@"Username"];
    [userLabel setBezeled:NO];
    [userLabel setDrawsBackground:NO];
    [userLabel setEditable:NO];
    [userLabel setSelectable:NO];
    [contentView addSubview:userLabel];
    
    self.usernameField = [[NSTextField alloc] initWithFrame:NSMakeRect(margin + labelWidth, y, fieldWidth, fieldHeight)];
    [contentView addSubview:self.usernameField];
    
    y -= 30;
    
    // Password
    NSTextField *passLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, labelWidth, fieldHeight)];
    [passLabel setStringValue:@"Password"];
    [passLabel setBezeled:NO];
    [passLabel setDrawsBackground:NO];
    [passLabel setEditable:NO];
    [passLabel setSelectable:NO];
    [contentView addSubview:passLabel];
    
    self.passwordField = [[NSSecureTextField alloc] initWithFrame:NSMakeRect(margin + labelWidth, y, fieldWidth, fieldHeight)];
    [contentView addSubview:self.passwordField];
    
    y -= 30;
    
    // Domain
    NSTextField *domainLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, labelWidth, fieldHeight)];
    [domainLabel setStringValue:@"Domain"];
    [domainLabel setBezeled:NO];
    [domainLabel setDrawsBackground:NO];
    [domainLabel setEditable:NO];
    [domainLabel setSelectable:NO];
    [contentView addSubview:domainLabel];
    
    self.domainField = [[NSTextField alloc] initWithFrame:NSMakeRect(margin + labelWidth, y, fieldWidth, fieldHeight)];
    [contentView addSubview:self.domainField];
    
    y -= 30;
    
    // Window geometry
    NSTextField *geomLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, labelWidth, fieldHeight)];
    [geomLabel setStringValue:@"Window geometry"];
    [geomLabel setBezeled:NO];
    [geomLabel setDrawsBackground:NO];
    [geomLabel setEditable:NO];
    [geomLabel setSelectable:NO];
    [contentView addSubview:geomLabel];
    
    self.widthField = [[NSTextField alloc] initWithFrame:NSMakeRect(margin + labelWidth, y, 80, fieldHeight)];
    [self.widthField setStringValue:@"1366"];
    [contentView addSubview:self.widthField];
    
    NSTextField *xLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin + labelWidth + 85, y, 15, fieldHeight)];
    [xLabel setStringValue:@"x"];
    [xLabel setBezeled:NO];
    [xLabel setDrawsBackground:NO];
    [xLabel setEditable:NO];
    [xLabel setSelectable:NO];
    [xLabel setAlignment:NSTextAlignmentCenter];
    [contentView addSubview:xLabel];
    
    self.heightField = [[NSTextField alloc] initWithFrame:NSMakeRect(margin + labelWidth + 105, y, 80, fieldHeight)];
    [self.heightField setStringValue:@"768"];
    [contentView addSubview:self.heightField];
    
    y -= 30;
    
    // Keymap
    NSTextField *keymapLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, labelWidth, fieldHeight)];
    [keymapLabel setStringValue:@"Keymap"];
    [keymapLabel setBezeled:NO];
    [keymapLabel setDrawsBackground:NO];
    [keymapLabel setEditable:NO];
    [keymapLabel setSelectable:NO];
    [contentView addSubview:keymapLabel];
    
    self.keymapPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(margin + labelWidth, y, 120, fieldHeight)];
    [self setupKeymapPopup];
    [contentView addSubview:self.keymapPopup];
    
    y -= 35;
    
    // Buttons
    self.addButton = [[NSButton alloc] initWithFrame:NSMakeRect(margin + labelWidth, y, 30, 30)];
    [self.addButton setTitle:@"+"];
    [self.addButton setTarget:self];
    [self.addButton setAction:@selector(addConnectionClicked:)];
    [contentView addSubview:self.addButton];

    self.logButton = [[NSButton alloc] initWithFrame:NSMakeRect(margin, y, 100, 30)];
    [self.logButton setTitle:@"Open Log"];
    [self.logButton setTarget:self];
    [self.logButton setAction:@selector(openLogFile:)];
    [self.logButton setBezelStyle:NSBezelStyleRounded];
    [contentView addSubview:self.logButton];
    
    self.advancedButton = [[NSButton alloc] initWithFrame:NSMakeRect(margin + labelWidth + 40, y, 80, 30)];
    [self.advancedButton setTitle:@"Advanced"];
    [self.advancedButton setTarget:self];
    [self.advancedButton setAction:@selector(advancedClicked:)];
    [self.advancedButton setBezelStyle:NSBezelStyleRounded];
    [contentView addSubview:self.advancedButton];
    
    self.connectButton = [[NSButton alloc] initWithFrame:NSMakeRect(margin + labelWidth + fieldWidth - 80, y, 80, 30)];
    [self.connectButton setTitle:@"Connect"];
    [self.connectButton setTarget:self];
    [self.connectButton setAction:@selector(connectClicked:)];
    [self.connectButton setBezelStyle:NSBezelStyleRounded];
    [self.connectButton setKeyEquivalent:@"\r"];
    [contentView addSubview:self.connectButton];
    
}

- (void)setupAdvancedOptions:(NSView *)advancedView {
    CGFloat startY = 370;
    CGFloat y = startY;
    CGFloat fieldHeight = 22;
    CGFloat labelWidth = 150;
    CGFloat fieldWidth = 160;
    CGFloat margin = 30;
    CGFloat rightColumn = 370;
    
    // self.advancedView is already initialized and added in showConnectionWindow
    
    // Authentication & Security Section (Left Column)
    NSTextField *authLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, 200, fieldHeight)];
    [authLabel setStringValue:@"Authentication & Security"];
    [authLabel setBezeled:NO];
    [authLabel setDrawsBackground:NO];
    [authLabel setEditable:NO];
    [authLabel setSelectable:NO];
    [authLabel setFont:[NSFont boldSystemFontOfSize:13]];
    [self.advancedView addSubview:authLabel];
    y -= 22;
    
    // Client hostname
    NSTextField *clientHostLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, labelWidth, fieldHeight)];
    [clientHostLabel setStringValue:@"Client Hostname:"];
    [clientHostLabel setBezeled:NO];
    [clientHostLabel setDrawsBackground:NO];
    [clientHostLabel setEditable:NO];
    [clientHostLabel setSelectable:NO];
    [self.advancedView addSubview:clientHostLabel];
    
    self.clientHostnameField = [[NSTextField alloc] initWithFrame:NSMakeRect(margin + labelWidth, y, fieldWidth, fieldHeight)];
    [self.advancedView addSubview:self.clientHostnameField];
    y -= 22;
    
    // TLS Version
    NSTextField *tlsLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, labelWidth, fieldHeight)];
    [tlsLabel setStringValue:@"TLS Version:"];
    [tlsLabel setBezeled:NO];
    [tlsLabel setDrawsBackground:NO];
    [tlsLabel setEditable:NO];
    [tlsLabel setSelectable:NO];
    [self.advancedView addSubview:tlsLabel];
    
    self.tlsVersionPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(margin + labelWidth, y, 100, fieldHeight)];
    [self.tlsVersionPopup addItemWithTitle:@"Auto"];
    [self.tlsVersionPopup addItemWithTitle:@"1.0"];
    [self.tlsVersionPopup addItemWithTitle:@"1.1"];
    [self.tlsVersionPopup addItemWithTitle:@"1.2"];
    [self.advancedView addSubview:self.tlsVersionPopup];
    y -= 22;
    
    // Security checkboxes
    self.smartcardButton = [[NSButton alloc] initWithFrame:NSMakeRect(margin, y, 220, fieldHeight)];
    [self.smartcardButton setTitle:@"Enable Smartcard Authentication"];
    [self.smartcardButton setButtonType:NSButtonTypeSwitch];
    [self.advancedView addSubview:self.smartcardButton];
    y -= 22;
    
    self.disableEncryptionButton = [[NSButton alloc] initWithFrame:NSMakeRect(margin, y, 220, fieldHeight)];
    [self.disableEncryptionButton setTitle:@"Disable Encryption (French TS)"];
    [self.disableEncryptionButton setButtonType:NSButtonTypeSwitch];
    [self.advancedView addSubview:self.disableEncryptionButton];
    y -= 22;
    
    self.disableClientEncryptionButton = [[NSButton alloc] initWithFrame:NSMakeRect(margin, y, 220, fieldHeight)];
    [self.disableClientEncryptionButton setTitle:@"Disable Client-to-Server Encryption"];
    [self.disableClientEncryptionButton setButtonType:NSButtonTypeSwitch];
    [self.advancedView addSubview:self.disableClientEncryptionButton];
    y -= 30;
    
    // Display Options Section
    NSTextField *displayLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, 200, fieldHeight)];
    [displayLabel setStringValue:@"Display Options"];
    [displayLabel setBezeled:NO];
    [displayLabel setDrawsBackground:NO];
    [displayLabel setEditable:NO];
    [displayLabel setSelectable:NO];
    [displayLabel setFont:[NSFont boldSystemFontOfSize:13]];
    [self.advancedView addSubview:displayLabel];
    y -= 22;
    
    // Color depth
    NSTextField *colorDepthLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, labelWidth, fieldHeight)];
    [colorDepthLabel setStringValue:@"Color Depth:"];
    [colorDepthLabel setBezeled:NO];
    [colorDepthLabel setDrawsBackground:NO];
    [colorDepthLabel setEditable:NO];
    [colorDepthLabel setSelectable:NO];
    [self.advancedView addSubview:colorDepthLabel];
    
    self.colorDepthPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(margin + labelWidth, y, 100, fieldHeight)];
    [self.colorDepthPopup addItemWithTitle:@"Auto"];
    [self.colorDepthPopup addItemWithTitle:@"8-bit"];
    [self.colorDepthPopup addItemWithTitle:@"15-bit"];
    [self.colorDepthPopup addItemWithTitle:@"16-bit"];
    [self.colorDepthPopup addItemWithTitle:@"24-bit"];
    [self.colorDepthPopup addItemWithTitle:@"32-bit"];
    [self.advancedView addSubview:self.colorDepthPopup];
    y -= 22;
    
    // DPI
    NSTextField *dpiLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, labelWidth, fieldHeight)];
    [dpiLabel setStringValue:@"DPI:"];
    [dpiLabel setBezeled:NO];
    [dpiLabel setDrawsBackground:NO];
    [dpiLabel setEditable:NO];
    [dpiLabel setSelectable:NO];
    [self.advancedView addSubview:dpiLabel];
    
    self.dpiField = [[NSTextField alloc] initWithFrame:NSMakeRect(margin + labelWidth, y, 60, fieldHeight)];
    [self.advancedView addSubview:self.dpiField];
    y -= 22;
    
    // Full-screen mode
    self.fullscreenButton = [[NSButton alloc] initWithFrame:NSMakeRect(margin, y, 150, fieldHeight)];
    [self.fullscreenButton setTitle:@"Full-screen Mode"];
    [self.fullscreenButton setButtonType:NSButtonTypeSwitch];
    [self.advancedView addSubview:self.fullscreenButton];
    y -= 30;
    
    // Input Options Section (Left Column continued)
    NSTextField *inputLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, 200, fieldHeight)];
    [inputLabel setStringValue:@"Input Options"];
    [inputLabel setBezeled:NO];
    [inputLabel setDrawsBackground:NO];
    [inputLabel setEditable:NO];
    [inputLabel setSelectable:NO];
    [inputLabel setFont:[NSFont boldSystemFontOfSize:13]];
    [self.advancedView addSubview:inputLabel];
    y -= 22;
    
    self.sendMotionEventsButton = [[NSButton alloc] initWithFrame:NSMakeRect(margin, y, 180, fieldHeight)];
    [self.sendMotionEventsButton setTitle:@"Send Motion Events"];
    [self.sendMotionEventsButton setButtonType:NSButtonTypeSwitch];
    [self.sendMotionEventsButton setState:NSControlStateValueOn]; // Default enabled
    [self.advancedView addSubview:self.sendMotionEventsButton];
    y -= 22;
    
    self.localMouseCursorButton = [[NSButton alloc] initWithFrame:NSMakeRect(margin, y, 180, fieldHeight)];
    [self.localMouseCursorButton setTitle:@"Use Local Mouse Cursor"];
    [self.localMouseCursorButton setButtonType:NSButtonTypeSwitch];
    [self.advancedView addSubview:self.localMouseCursorButton];
    y -= 22;
    
    self.numlockSyncButton = [[NSButton alloc] initWithFrame:NSMakeRect(margin, y, 180, fieldHeight)];
    [self.numlockSyncButton setTitle:@"Numlock Synchronization"];
    [self.numlockSyncButton setButtonType:NSButtonTypeSwitch];
    [self.advancedView addSubview:self.numlockSyncButton];
    
    // ----------------------------------------------------
    // Right Column Layout
    // ----------------------------------------------------
    y = startY;
    
    // Performance & Experience Section (Right Column)
    NSTextField *perfLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(rightColumn, y, 200, fieldHeight)];
    [perfLabel setStringValue:@"Performance & Experience"];
    [perfLabel setBezeled:NO];
    [perfLabel setDrawsBackground:NO];
    [perfLabel setEditable:NO];
    [perfLabel setSelectable:NO];
    [perfLabel setFont:[NSFont boldSystemFontOfSize:13]];
    [self.advancedView addSubview:perfLabel];
    y -= 22;
    
    // Experience level
    NSTextField *expLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(rightColumn, y, 100, fieldHeight)];
    [expLabel setStringValue:@"Experience:"];
    [expLabel setBezeled:NO];
    [expLabel setDrawsBackground:NO];
    [expLabel setEditable:NO];
    [expLabel setSelectable:NO];
    [self.advancedView addSubview:expLabel];
    
    self.experiencePopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(rightColumn + 100, y, 120, fieldHeight)];
    [self.experiencePopup addItemWithTitle:@"Auto"];
    [self.experiencePopup addItemWithTitle:@"Modem (28.8k)"];
    [self.experiencePopup addItemWithTitle:@"Broadband"];
    [self.experiencePopup addItemWithTitle:@"LAN"];
    [self.advancedView addSubview:self.experiencePopup];
    y -= 22;
    
    // Performance checkboxes (Compression & Cache checked by default)
    self.compressionButton = [[NSButton alloc] initWithFrame:NSMakeRect(rightColumn, y, 220, fieldHeight)];
    [self.compressionButton setTitle:@"Enable RDP Compression"];
    [self.compressionButton setButtonType:NSButtonTypeSwitch];
    [self.compressionButton setState:NSControlStateValueOn]; // Enabled by default
    [self.advancedView addSubview:self.compressionButton];
    y -= 22;
    
    self.persistentCachingButton = [[NSButton alloc] initWithFrame:NSMakeRect(rightColumn, y, 220, fieldHeight)];
    [self.persistentCachingButton setTitle:@"Persistent Bitmap Caching"];
    [self.persistentCachingButton setButtonType:NSButtonTypeSwitch];
    [self.persistentCachingButton setState:NSControlStateValueOff]; // Disabled by default
    [self.advancedView addSubview:self.persistentCachingButton];
    y -= 22;
    
    self.backingStoreButton = [[NSButton alloc] initWithFrame:NSMakeRect(rightColumn, y, 220, fieldHeight)];
    [self.backingStoreButton setTitle:@"Use BackingStore"];
    [self.backingStoreButton setButtonType:NSButtonTypeSwitch];
    [self.advancedView addSubview:self.backingStoreButton];
    y -= 22;
    
    self.forceBitmapUpdatesButton = [[NSButton alloc] initWithFrame:NSMakeRect(rightColumn, y, 220, fieldHeight)];
    [self.forceBitmapUpdatesButton setTitle:@"Force Bitmap Updates"];
    [self.forceBitmapUpdatesButton setButtonType:NSButtonTypeSwitch];
    [self.advancedView addSubview:self.forceBitmapUpdatesButton];
    y -= 30;
    
    // Protocol Options Section (Right Column)
    NSTextField *protocolLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(rightColumn, y, 200, fieldHeight)];
    [protocolLabel setStringValue:@"Protocol Options"];
    [protocolLabel setBezeled:NO];
    [protocolLabel setDrawsBackground:NO];
    [protocolLabel setEditable:NO];
    [protocolLabel setSelectable:NO];
    [protocolLabel setFont:[NSFont boldSystemFontOfSize:13]];
    [self.advancedView addSubview:protocolLabel];
    y -= 22;
    
    // RDP Version
    NSTextField *rdpVersionLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(rightColumn, y, 100, fieldHeight)];
    [rdpVersionLabel setStringValue:@"RDP Version:"];
    [rdpVersionLabel setBezeled:NO];
    [rdpVersionLabel setDrawsBackground:NO];
    [rdpVersionLabel setEditable:NO];
    [rdpVersionLabel setSelectable:NO];
    [self.advancedView addSubview:rdpVersionLabel];
    
    self.rdpVersionPopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(rightColumn + 100, y, 80, fieldHeight)];
    [self.rdpVersionPopup addItemWithTitle:@"RDP 5"];
    [self.rdpVersionPopup addItemWithTitle:@"RDP 4"];
    [self.advancedView addSubview:self.rdpVersionPopup];
    y -= 22;
    
    // Local codepage
    NSTextField *codepageLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(rightColumn, y, 100, fieldHeight)];
    [codepageLabel setStringValue:@"Codepage:"];
    [codepageLabel setBezeled:NO];
    [codepageLabel setDrawsBackground:NO];
    [codepageLabel setEditable:NO];
    [codepageLabel setSelectable:NO];
    [self.advancedView addSubview:codepageLabel];
    
    self.localCodepageField = [[NSTextField alloc] initWithFrame:NSMakeRect(rightColumn + 100, y, 100, fieldHeight)];
    [self.advancedView addSubview:self.localCodepageField];
    y -= 22;
    
    // Protocol Checkboxes (Right Column)
    self.consoleButton = [[NSButton alloc] initWithFrame:NSMakeRect(rightColumn, y, 220, fieldHeight)];
    [self.consoleButton setTitle:@"Attach to Console"];
    [self.consoleButton setButtonType:NSButtonTypeSwitch];
    [self.advancedView addSubview:self.consoleButton];
    y -= 22;
    
    self.verboseLoggingButton = [[NSButton alloc] initWithFrame:NSMakeRect(rightColumn, y, 220, fieldHeight)];
    [self.verboseLoggingButton setTitle:@"Verbose Logging"];
    [self.verboseLoggingButton setButtonType:NSButtonTypeSwitch];
    [self.verboseLoggingButton setState:NSControlStateValueOn];
    [self.advancedView addSubview:self.verboseLoggingButton];
    y -= 22;
    
    // Input/Key Bindings Checkboxes (moved to right column under protocol)
    self.disableRemoteCtrlButton = [[NSButton alloc] initWithFrame:NSMakeRect(rightColumn, y, 220, fieldHeight)];
    [self.disableRemoteCtrlButton setTitle:@"Disable Remote Ctrl"];
    [self.disableRemoteCtrlButton setButtonType:NSButtonTypeSwitch];
    [self.advancedView addSubview:self.disableRemoteCtrlButton];
    y -= 22;
    
    self.keepKeyBindingsButton = [[NSButton alloc] initWithFrame:NSMakeRect(rightColumn, y, 220, fieldHeight)];
    [self.keepKeyBindingsButton setTitle:@"Keep Key Bindings"];
    [self.keepKeyBindingsButton setButtonType:NSButtonTypeSwitch];
    [self.advancedView addSubview:self.keepKeyBindingsButton];
    
    // ----------------------------------------------------
    // Bottom Section (Full Width)
    // ----------------------------------------------------
    y = 45;
    
    // Device Redirection
    NSTextField *deviceLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, 200, fieldHeight)];
    [deviceLabel setStringValue:@"Device Redirection"];
    [deviceLabel setBezeled:NO];
    [deviceLabel setDrawsBackground:NO];
    [deviceLabel setEditable:NO];
    [deviceLabel setSelectable:NO];
    [deviceLabel setFont:[NSFont boldSystemFontOfSize:13]];
    [self.advancedView addSubview:deviceLabel];
    y -= 22;
    
    NSTextField *deviceDescLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, 560, fieldHeight)];
    [deviceDescLabel setStringValue:@"Examples: clipboard:CLIPBOARD, disk:share=/path, printer:name"];
    [deviceDescLabel setBezeled:NO];
    [deviceDescLabel setDrawsBackground:NO];
    [deviceDescLabel setEditable:NO];
    [deviceDescLabel setSelectable:NO];
    [deviceDescLabel setFont:[NSFont systemFontOfSize:10]];
    [deviceDescLabel setTextColor:[NSColor secondaryLabelColor]];
    [self.advancedView addSubview:deviceDescLabel];
    y -= 20;
    
    self.deviceRedirectionField = [[NSTextField alloc] initWithFrame:NSMakeRect(margin, y, 560, fieldHeight)];
    [self.advancedView addSubview:self.deviceRedirectionField];
}

- (IBAction)connectClicked:(id)sender {
    // Validate inputs
    NSString *host = [self.hostField stringValue];
    NSString *port = [self.portField stringValue];
    NSString *username = [self.usernameField stringValue];
    NSString *domain = [self.domainField stringValue];
    NSString *width = [self.widthField stringValue];
    NSString *height = [self.heightField stringValue];
    NSString *keymap = [[self.keymapPopup selectedItem] title];
    
    if ([host length] == 0) {
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Connection Error"];
        [alert setInformativeText:@"Please enter a hostname or IP address."];
        [alert setAlertStyle:NSAlertStyleWarning];
        [alert runModal];
        return;
    }
    
    // Set global variables for rdesktop - basic options
    strncpy(g_hostname, [host UTF8String], sizeof(g_hostname) - 1);
    g_hostname[sizeof(g_hostname) - 1] = '\0';
    
    if ([username length] > 0) {
        if (g_username) {
            free(g_username);
        }
        g_username = strdup([username UTF8String]);
    }
    
    // Note: domain handling would need to be added to rdesktop.h if needed
    
    g_tcp_port_rdp = [port intValue];
    g_requested_session_width = [width intValue];
    g_requested_session_height = [height intValue];
    
    strncpy(g_keymapname, [keymap UTF8String], sizeof(g_keymapname) - 1);
    g_keymapname[sizeof(g_keymapname) - 1] = '\0';
    
    // Collect all advanced options for connection
    NSMutableDictionary *advancedOptions = [[NSMutableDictionary alloc] init];
    if ([domain length] > 0) {
        [advancedOptions setObject:domain forKey:@"domain"];
    }
    
    // Password
    NSString *password = [self.passwordField stringValue];
    if ([password length] > 0) {
        [advancedOptions setObject:password forKey:@"password"];
    }
    
    // Client hostname
    NSString *clientHostname = [self.clientHostnameField stringValue];
    if ([clientHostname length] > 0) {
        [advancedOptions setObject:clientHostname forKey:@"clientHostname"];
    }
    
    // TLS Version
    NSString *tlsVersion = [[self.tlsVersionPopup selectedItem] title];
    if (![tlsVersion isEqualToString:@"Auto"]) {
        [advancedOptions setObject:tlsVersion forKey:@"tlsVersion"];
    }
    
    // Color depth
    NSString *colorDepth = [[self.colorDepthPopup selectedItem] title];
    if (![colorDepth isEqualToString:@"Auto"]) {
        [advancedOptions setObject:colorDepth forKey:@"colorDepth"];
    }
    
    // Window title
    NSString *windowTitle = [self.windowTitleField stringValue];
    if ([windowTitle length] > 0) {
        [advancedOptions setObject:windowTitle forKey:@"windowTitle"];
    }
    
    // DPI
    NSString *dpi = [self.dpiField stringValue];
    if ([dpi length] > 0) {
        [advancedOptions setObject:dpi forKey:@"dpi"];
    }
    
    // Experience level
    NSString *experience = [[self.experiencePopup selectedItem] title];
    if (![experience isEqualToString:@"Auto"]) {
        [advancedOptions setObject:experience forKey:@"experience"];
    }
    
    // RDP Version
    NSString *rdpVersion = [[self.rdpVersionPopup selectedItem] title];
    [advancedOptions setObject:rdpVersion forKey:@"rdpVersion"];
    
    // Local codepage
    NSString *codepage = [self.localCodepageField stringValue];
    if ([codepage length] > 0) {
        [advancedOptions setObject:codepage forKey:@"codepage"];
    }
    
    // Shell/Application
    NSString *shell = [self.shellField stringValue];
    if ([shell length] > 0) {
        [advancedOptions setObject:shell forKey:@"shell"];
    }
    
    // Working directory
    NSString *workingDir = [self.workingDirField stringValue];
    if ([workingDir length] > 0) {
        [advancedOptions setObject:workingDir forKey:@"workingDir"];
    }
    
    // SeamlessRDP path
    NSString *seamlessPath = [self.seamlessPathField stringValue];
    if ([seamlessPath length] > 0) {
        [advancedOptions setObject:seamlessPath forKey:@"seamlessPath"];
    }
    
    // Device redirection
    NSString *deviceRedirection = [self.deviceRedirectionField stringValue];
    if ([deviceRedirection length] > 0) {
        [advancedOptions setObject:deviceRedirection forKey:@"deviceRedirection"];
    }
    
    // Boolean flags
    if ([self.smartcardButton state] == NSControlStateValueOn) {
        [advancedOptions setObject:@YES forKey:@"smartcard"];
    }
    if ([self.disableEncryptionButton state] == NSControlStateValueOn) {
        [advancedOptions setObject:@YES forKey:@"disableEncryption"];
    }
    if ([self.disableClientEncryptionButton state] == NSControlStateValueOn) {
        [advancedOptions setObject:@YES forKey:@"disableClientEncryption"];
    }
    if ([self.fullscreenButton state] == NSControlStateValueOn) {
        [advancedOptions setObject:@YES forKey:@"fullscreen"];
    }
    if ([self.forceBitmapUpdatesButton state] == NSControlStateValueOn) {
        [advancedOptions setObject:@YES forKey:@"forceBitmapUpdates"];
    }
    if ([self.hideDecorationsButton state] == NSControlStateValueOn) {
        [advancedOptions setObject:@YES forKey:@"hideDecorations"];
    }
    if ([self.compressionButton state] == NSControlStateValueOn) {
        [advancedOptions setObject:@YES forKey:@"compression"];
    }
    if ([self.persistentCachingButton state] == NSControlStateValueOn) {
        [advancedOptions setObject:@YES forKey:@"persistentCaching"];
    }
    if ([self.backingStoreButton state] == NSControlStateValueOn) {
        [advancedOptions setObject:@YES forKey:@"backingStore"];
    }
    if ([self.sendMotionEventsButton state] == NSControlStateValueOff) {
        [advancedOptions setObject:@YES forKey:@"disableMotionEvents"];
    }
    if ([self.localMouseCursorButton state] == NSControlStateValueOn) {
        [advancedOptions setObject:@YES forKey:@"localMouseCursor"];
    }
    if ([self.numlockSyncButton state] == NSControlStateValueOn) {
        [advancedOptions setObject:@YES forKey:@"numlockSync"];
    }
    if ([self.disableRemoteCtrlButton state] == NSControlStateValueOn) {
        [advancedOptions setObject:@YES forKey:@"disableRemoteCtrl"];
    }
    if ([self.keepKeyBindingsButton state] == NSControlStateValueOn) {
        [advancedOptions setObject:@YES forKey:@"keepKeyBindings"];
    }
    if ([self.consoleButton state] == NSControlStateValueOn) {
        [advancedOptions setObject:@YES forKey:@"console"];
    }
    if ([self.verboseLoggingButton state] == NSControlStateValueOn) {
        [advancedOptions setObject:@YES forKey:@"verboseLogging"];
    }
    
    // Store advanced options for connection
    extern void setAdvancedOptions(NSDictionary *options);
    setAdvancedOptions(advancedOptions);
    
    // Hide connection window
    [[self window] orderOut:nil];
    
    // Start the RDP connection
    [self startConnection];
}

- (IBAction)addConnectionClicked:(id)sender {
    NSString *host = [self.hostField stringValue];
    NSString *user = [self.usernameField stringValue];
    NSString *port = [self.portField stringValue];
    
    if ([host length] == 0) {
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Error"];
        [alert setInformativeText:@"Please enter a Host first."];
        [alert setAlertStyle:NSAlertStyleWarning];
        [alert runModal];
        return;
    }
    
    NSString *profile;
    if ([user length] > 0) {
        profile = [NSString stringWithFormat:@"%@@%@:%@", user, host, port];
    } else {
        profile = [NSString stringWithFormat:@"%@:%@", host, port];
    }
    
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSMutableArray *savedProfiles = [[defaults objectForKey:@"SavedProfiles"] mutableCopy];
    if (!savedProfiles) {
        savedProfiles = [[NSMutableArray alloc] init];
    }
    
    if (![savedProfiles containsObject:profile]) {
        [savedProfiles addObject:profile];
        [defaults setObject:savedProfiles forKey:@"SavedProfiles"];
        [defaults synchronize];
        
        [self loadSavedProfiles];
        [self.hostField selectItemWithObjectValue:profile];
        [self autofillFromProfile:profile];
        
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Success"];
        [alert setInformativeText:[NSString stringWithFormat:@"Saved connection profile: %@", profile]];
        [alert setAlertStyle:NSAlertStyleInformational];
        [alert runModal];
    } else {
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Info"];
        [alert setInformativeText:@"This connection profile is already saved."];
        [alert setAlertStyle:NSAlertStyleInformational];
        [alert runModal];
    }
}

- (void)startConnection {
    NSLog(@"Starting RDP connection to %s:%d", g_hostname, g_tcp_port_rdp);
    NSLog(@"Username: %s", g_username ? g_username : "(none)");
    NSLog(@"Resolution: %dx%d, Keymap: %s", g_requested_session_width, g_requested_session_height, g_keymapname);
    
    self.isConnecting = YES;
    
    // Launch the RDP connection in a separate thread to avoid blocking the GUI
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        extern int launch_rdp_connection_from_gui(void);
        int result = launch_rdp_connection_from_gui();
        
        dispatch_async(dispatch_get_main_queue(), ^{
            self.isConnecting = NO;
            if (result != 0) {
                // Force foreground focus
                [NSApp activateIgnoringOtherApps:YES];
                [[self window] makeKeyAndOrderFront:nil];
                
                extern RD_BOOL g_nla_failure;
                extern RD_BOOL g_connection_established;
                if (g_nla_failure && !g_connection_established) {
                    extern void show_nla_instructions_alert(void);
                    show_nla_instructions_alert();
                } else {
                    NSAlert *alert = [[NSAlert alloc] init];
                    [alert setMessageText:@"Connection Failed"];
                    [alert setInformativeText:[NSString stringWithFormat:@"Could not connect to %s:%d", g_hostname, g_tcp_port_rdp]];
                    [alert setAlertStyle:NSAlertStyleCritical];
                    [alert runModal];
                }
            }
        });
    });
}

- (void)windowWillClose:(NSNotification *)notification {
    extern RD_BOOL g_connection_established;
    if (!g_connection_established && !self.isConnecting) {
        [NSApp terminate:self];
    }
}

#pragma mark - Menu Actions

- (IBAction)showConnectionWindow:(id)sender {
    [self showConnectionWindow];
}

- (IBAction)showSettings:(id)sender {
    [self showConnectionWindow];
    if (!self.advancedVisible) {
        [self toggleAdvancedOptions];
    }
    [[self window] makeKeyAndOrderFront:nil];
}

- (IBAction)openLogFile:(id)sender {
    extern const char *mac_gui_log_path(void);
    NSString *path = [NSString stringWithUTF8String:mac_gui_log_path()];

    if (!path || [path length] == 0) {
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Log unavailable"];
        [alert setInformativeText:@"The rdesktop log file could not be located."];
        [alert runModal];
        return;
    }

    if (![[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:path]]) {
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Unable to open log file"];
        [alert setInformativeText:[NSString stringWithFormat:@"Log file: %@", path]];
        [alert runModal];
    }
}

- (IBAction)disconnect:(id)sender {
    extern int g_exit_mainloop;
    g_exit_mainloop = 1;
}

- (void)toggleFullscreenSetting:(id)sender {
    NSControlStateValue state = [self.fullscreenButton state];
    [self.fullscreenButton setState:(state == NSControlStateValueOn ? NSControlStateValueOff : NSControlStateValueOn)];
}

- (void)toggleCompressionSetting:(id)sender {
    NSControlStateValue state = [self.compressionButton state];
    [self.compressionButton setState:(state == NSControlStateValueOn ? NSControlStateValueOff : NSControlStateValueOn)];
}

- (void)togglePersistentCachingSetting:(id)sender {
    NSControlStateValue state = [self.persistentCachingButton state];
    [self.persistentCachingButton setState:(state == NSControlStateValueOn ? NSControlStateValueOff : NSControlStateValueOn)];
}

- (void)toggleLocalMouseCursorSetting:(id)sender {
    NSControlStateValue state = [self.localMouseCursorButton state];
    [self.localMouseCursorButton setState:(state == NSControlStateValueOn ? NSControlStateValueOff : NSControlStateValueOn)];
}

- (void)toggleConsoleSetting:(id)sender {
    NSControlStateValue state = [self.consoleButton state];
    [self.consoleButton setState:(state == NSControlStateValueOn ? NSControlStateValueOff : NSControlStateValueOn)];
}

- (void)toggleVerboseLoggingSetting:(id)sender {
    NSControlStateValue state = [self.verboseLoggingButton state];
    [self.verboseLoggingButton setState:(state == NSControlStateValueOn ? NSControlStateValueOff : NSControlStateValueOn)];
}

#pragma mark - Menu Item Validation

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem {
    SEL action = [menuItem action];
    if (action == @selector(disconnect:)) {
        return self.isConnecting;
    }
    if (action == @selector(toggleFullscreenSetting:)) {
        [menuItem setState:[self.fullscreenButton state]];
        return YES;
    }
    if (action == @selector(toggleCompressionSetting:)) {
        [menuItem setState:[self.compressionButton state]];
        return YES;
    }
    if (action == @selector(togglePersistentCachingSetting:)) {
        [menuItem setState:[self.persistentCachingButton state]];
        return YES;
    }
    if (action == @selector(toggleLocalMouseCursorSetting:)) {
        [menuItem setState:[self.localMouseCursorButton state]];
        return YES;
    }
    if (action == @selector(toggleConsoleSetting:)) {
        [menuItem setState:[self.consoleButton state]];
        return YES;
    }
    if (action == @selector(toggleVerboseLoggingSetting:)) {
        [menuItem setState:[self.verboseLoggingButton state]];
        return YES;
    }
    return YES;
}

- (void)loadSavedProfiles {
    NSArray *savedProfiles = [[NSUserDefaults standardUserDefaults] objectForKey:@"SavedProfiles"];
    [self.hostField removeAllItems];
    if (savedProfiles) {
        [self.hostField addItemsWithObjectValues:savedProfiles];
    }
}

- (void)autofillFromProfile:(NSString *)profile {
    if (!profile || [profile length] == 0) return;
    
    NSString *user = @"";
    NSString *host = profile;
    NSString *port = @"3389";
    
    NSRange atRange = [profile rangeOfString:@"@"];
    if (atRange.location != NSNotFound) {
        user = [profile substringToIndex:atRange.location];
        host = [profile substringFromIndex:atRange.location + 1];
    }
    
    NSRange colonRange = [host rangeOfString:@":"];
    if (colonRange.location != NSNotFound) {
        port = [host substringFromIndex:colonRange.location + 1];
        host = [host substringToIndex:colonRange.location];
    }
    
    [self.hostField setStringValue:host];
    [self.usernameField setStringValue:user];
    [self.portField setStringValue:port];
}

- (void)comboBoxSelectionDidChange:(NSNotification *)notification {
    if (notification.object == self.hostField) {
        NSInteger index = [self.hostField indexOfSelectedItem];
        if (index >= 0) {
            NSString *profile = [self.hostField itemObjectValueAtIndex:index];
            [self autofillFromProfile:profile];
        }
    }
}

@end
