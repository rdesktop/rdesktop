/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   macOS Connection GUI Interface
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

#import <Cocoa/Cocoa.h>

@interface MacRDPConnectionGUI : NSWindowController <NSWindowDelegate, NSComboBoxDelegate>

// Basic connection fields
@property (retain) IBOutlet NSComboBox *hostField;
@property (retain) IBOutlet NSTextField *portField;
@property (retain) IBOutlet NSTextField *usernameField;
@property (retain) IBOutlet NSTextField *domainField;
@property (retain) IBOutlet NSTextField *widthField;
@property (retain) IBOutlet NSTextField *heightField;
@property (retain) IBOutlet NSPopUpButton *keymapPopup;
@property (retain) IBOutlet NSButton *connectButton;
@property (retain) IBOutlet NSButton *addButton;
@property (retain) IBOutlet NSButton *logButton;
@property (retain) IBOutlet NSButton *advancedButton;

// Advanced options panel
@property (retain) IBOutlet NSView *basicView;
@property (retain) IBOutlet NSView *advancedView;
@property (assign) BOOL advancedVisible;

// Authentication & Security
@property (retain) IBOutlet NSTextField *passwordField;
@property (retain) IBOutlet NSTextField *clientHostnameField;
@property (retain) IBOutlet NSButton *smartcardButton;
@property (retain) IBOutlet NSPopUpButton *tlsVersionPopup;
@property (retain) IBOutlet NSButton *disableEncryptionButton;
@property (retain) IBOutlet NSButton *disableClientEncryptionButton;

// Display options
@property (retain) IBOutlet NSButton *fullscreenButton;
@property (retain) IBOutlet NSButton *forceBitmapUpdatesButton;
@property (retain) IBOutlet NSPopUpButton *colorDepthPopup;
@property (retain) IBOutlet NSTextField *windowTitleField;
@property (retain) IBOutlet NSButton *hideDecorationsButton;
@property (retain) IBOutlet NSTextField *dpiField;

// Performance & Experience  
@property (retain) IBOutlet NSButton *compressionButton;
@property (retain) IBOutlet NSPopUpButton *experiencePopup;
@property (retain) IBOutlet NSButton *persistentCachingButton;
@property (retain) IBOutlet NSButton *backingStoreButton;

// Input options
@property (retain) IBOutlet NSButton *sendMotionEventsButton;
@property (retain) IBOutlet NSButton *localMouseCursorButton;
@property (retain) IBOutlet NSButton *disableRemoteCtrlButton;
@property (retain) IBOutlet NSButton *numlockSyncButton;
@property (retain) IBOutlet NSButton *keepKeyBindingsButton;

// Application options
@property (retain) IBOutlet NSTextField *shellField;
@property (retain) IBOutlet NSTextField *workingDirField;
@property (retain) IBOutlet NSTextField *seamlessPathField;
@property (retain) IBOutlet NSButton *consoleButton;

// Protocol options
@property (retain) IBOutlet NSPopUpButton *rdpVersionPopup;
@property (retain) IBOutlet NSButton *verboseLoggingButton;
@property (retain) IBOutlet NSTextField *localCodepageField;

// Device redirection
@property (retain) IBOutlet NSTextField *deviceRedirectionField;

- (IBAction)connectClicked:(id)sender;
- (IBAction)addConnectionClicked:(id)sender;
- (IBAction)advancedClicked:(id)sender;
- (void)showConnectionWindow;
- (void)startConnection;
- (void)toggleAdvancedOptions;

// Menu actions
@property (assign) BOOL isConnecting;
- (IBAction)showConnectionWindow:(id)sender;
- (IBAction)showSettings:(id)sender;
- (IBAction)openLogFile:(id)sender;
- (IBAction)disconnect:(id)sender;
- (void)toggleFullscreenSetting:(id)sender;
- (void)toggleCompressionSetting:(id)sender;
- (void)togglePersistentCachingSetting:(id)sender;
- (void)toggleLocalMouseCursorSetting:(id)sender;
- (void)toggleConsoleSetting:(id)sender;
- (void)toggleVerboseLoggingSetting:(id)sender;

@end
