/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   macOS GUI Application Main Entry Point
   Native macOS port
*/

#import <Cocoa/Cocoa.h>
#import "MacRDPConnectionGUI.h"
#include "rdesktop.h"
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

// External functions from rdesktop
extern int rdesktop_main(int argc, char *argv[]);

static char g_gui_log_path[PATH_MAX];

static void
setup_gui_logging(void)
{
    NSString *logDirectory;
    NSString *logPath;
    const char *path;
    int fd;

    logDirectory = [NSHomeDirectory() stringByAppendingPathComponent:
                    @"Library/Logs/rdesktop-mac"];
    [[NSFileManager defaultManager] createDirectoryAtPath:logDirectory
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
    logPath = [logDirectory stringByAppendingPathComponent:@"rdesktop-mac.log"];
    path = [logPath fileSystemRepresentation];
    strncpy(g_gui_log_path, path, sizeof(g_gui_log_path) - 1);
    g_gui_log_path[sizeof(g_gui_log_path) - 1] = '\0';

    fd = open(g_gui_log_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0)
        return;

    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
    setenv("RDESKTOP_DEBUG", "All", 1);
    NSLog(@"[macOS GUI] Logging to %s", g_gui_log_path);
}

const char *mac_gui_log_path(void)
{
    return g_gui_log_path;
}

/*
 * Xcode can append launch arguments to an application during development.
 * Those arguments are useful for a command-line executable, but an app
 * bundle must still start its GUI even when Xcode has stale arguments saved
 * in a per-user scheme.  Keep direct ./rdesktop-mac invocations available
 * for the traditional command-line client.
 */
static BOOL
launched_from_application_bundle(void)
{
    NSString *bundlePath = [[NSBundle mainBundle] bundlePath];
    return [bundlePath hasSuffix:@".app"];
}

@interface MacRDPAppDelegate : NSObject <NSApplicationDelegate>
@property (strong) MacRDPConnectionGUI *connectionGUI;
@property (assign) BOOL shouldShowGUI;
- (void)setupMenuBar;
@end

@implementation MacRDPAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    // Initialize connection GUI if not already set
    if (!self.connectionGUI) {
        self.connectionGUI = [[MacRDPConnectionGUI alloc] init];
    }
    [self setupMenuBar];
    if (self.shouldShowGUI) {
        [self.connectionGUI showConnectionWindow];
    }
}

- (void)setupMenuBar {
    NSMenu *mainMenu = [[NSMenu alloc] init];
    
    // Application (Apple) Menu
    NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:appMenuItem];
    NSMenu *appMenu = [[NSMenu alloc] init];
    NSString *appName = @"rdesktop-mac";
    
    // About
    NSMenuItem *aboutMenuItem = [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"About %@", appName]
                                                           action:@selector(orderFrontStandardAboutPanel:)
                                                    keyEquivalent:@""];
    [appMenu addItem:aboutMenuItem];
    [appMenu addItem:[NSMenuItem separatorItem]];
    
    // Settings...
    NSMenuItem *settingsMenuItem = [[NSMenuItem alloc] initWithTitle:@"Settings..."
                                                             action:@selector(showSettings:)
                                                      keyEquivalent:@","];
    [settingsMenuItem setTarget:self.connectionGUI];
    [appMenu addItem:settingsMenuItem];
    NSMenuItem *logMenuItem = [[NSMenuItem alloc] initWithTitle:@"Open Log File"
                                                         action:@selector(openLogFile:)
                                                  keyEquivalent:@""];
    [logMenuItem setTarget:self.connectionGUI];
    [appMenu addItem:logMenuItem];
    [appMenu addItem:[NSMenuItem separatorItem]];
    
    // Hide/Hide Others/Show All/Quit
    [appMenu addItem:[[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"Hide %@", appName] action:@selector(hide:) keyEquivalent:@"h"]];
    
    NSMenuItem *hideOthersItem = [[NSMenuItem alloc] initWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    [hideOthersItem setKeyEquivalentModifierMask:(NSEventModifierFlagOption | NSEventModifierFlagCommand)];
    [appMenu addItem:hideOthersItem];
    
    [appMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""]];
    [appMenu addItem:[NSMenuItem separatorItem]];
    
    NSMenuItem *quitMenuItem = [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"Quit %@", appName] action:@selector(terminate:) keyEquivalent:@"q"];
    [appMenu addItem:quitMenuItem];
    [appMenuItem setSubmenu:appMenu];
    
    // Connection Menu
    NSMenuItem *connMenuItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:connMenuItem];
    NSMenu *connMenu = [[NSMenu alloc] initWithTitle:@"Connection"];
    
    NSMenuItem *connectItem = [[NSMenuItem alloc] initWithTitle:@"Connect..." action:@selector(showConnectionWindow:) keyEquivalent:@"n"];
    [connectItem setTarget:self.connectionGUI];
    [connMenu addItem:connectItem];
    
    NSMenuItem *disconnectItem = [[NSMenuItem alloc] initWithTitle:@"Disconnect" action:@selector(disconnect:) keyEquivalent:@"d"];
    [disconnectItem setTarget:self.connectionGUI];
    [connMenu addItem:disconnectItem];

    NSMenuItem *connectionLogItem = [[NSMenuItem alloc] initWithTitle:@"Open Log File"
                                                                action:@selector(openLogFile:)
                                                         keyEquivalent:@""];
    [connectionLogItem setTarget:self.connectionGUI];
    [connMenu addItem:connectionLogItem];
    
    [connMenuItem setSubmenu:connMenu];
    
    // Settings Menu
    NSMenuItem *settingsMenuParentItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:settingsMenuParentItem];
    NSMenu *settingsMenu = [[NSMenu alloc] initWithTitle:@"Settings"];
    
    NSMenuItem *showSettingsItem = [[NSMenuItem alloc] initWithTitle:@"Show All Settings" action:@selector(showSettings:) keyEquivalent:@""];
    [showSettingsItem setTarget:self.connectionGUI];
    [settingsMenu addItem:showSettingsItem];
    [settingsMenu addItem:[NSMenuItem separatorItem]];
    
    // Quick toggles
    NSArray *toggles = @[
        @{@"title": @"Full-screen Mode", @"action": @"toggleFullscreenSetting:"},
        @{@"title": @"Enable RDP Compression", @"action": @"toggleCompressionSetting:"},
        @{@"title": @"Persistent Bitmap Caching", @"action": @"togglePersistentCachingSetting:"},
        @{@"title": @"Use Local Mouse Cursor", @"action": @"toggleLocalMouseCursorSetting:"},
        @{@"title": @"Attach to Console", @"action": @"toggleConsoleSetting:"},
        @{@"title": @"Verbose Logging", @"action": @"toggleVerboseLoggingSetting:"}
    ];
    
    for (NSDictionary *toggle in toggles) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:toggle[@"title"]
                                                      action:NSSelectorFromString(toggle[@"action"])
                                               keyEquivalent:@""];
        [item setTarget:self.connectionGUI];
        [settingsMenu addItem:item];
    }
    
    [settingsMenuParentItem setSubmenu:settingsMenu];
    
    [NSApp setMainMenu:mainMenu];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return NO;
}

@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        // An app bundle is always the GUI entry point.  Direct invocations of
        // the executable retain the traditional command-line behavior.
        BOOL useGUI = launched_from_application_bundle();
        
        // Also use the GUI when launched without arguments or explicitly
        // requested with --gui.
        if (argc == 1) {
            useGUI = YES;
        } else if (!useGUI) {
            for (int i = 1; i < argc; i++) {
                if (strcmp(argv[i], "--gui") == 0) {
                    useGUI = YES;
                    break;
                }
            }
        }
        
        if (useGUI) {
            setup_gui_logging();
            // Run macOS GUI application
            NSApplication *app = [NSApplication sharedApplication];
            [app setActivationPolicy:NSApplicationActivationPolicyRegular];
            [app activateIgnoringOtherApps:YES];
            
            MacRDPAppDelegate *delegate = [[MacRDPAppDelegate alloc] init];
            delegate.shouldShowGUI = YES;
            [app setDelegate:delegate];
            
[app run];
            return 0;
        } else {
            // Run traditional command-line rdesktop
            int result = rdesktop_main(argc, argv);
            extern RD_BOOL g_nla_failure;
            extern RD_BOOL g_connection_established;
            if (result != 0 && g_nla_failure && !g_connection_established) {
                extern void show_nla_instructions_alert(void);
                show_nla_instructions_alert();
            }
            return result;
        }
    }
}

// Global storage for advanced options
static NSDictionary *g_advanced_options = nil;

void setAdvancedOptions(NSDictionary *options) {
    if (g_advanced_options) {
        [g_advanced_options release];
    }
    g_advanced_options = [options retain];
}

// Helper function to launch RDP connection from GUI
int launch_rdp_connection_from_gui(void) {
    // This function would be called by the GUI to start the actual RDP connection
    // It would use the global variables set by the GUI
    
    extern char g_hostname[16];
    extern char *g_username;
    extern int g_tcp_port_rdp;
    extern uint32 g_requested_session_width;
    extern uint32 g_requested_session_height;
    extern char g_keymapname[PATH_MAX];
    
    // Construct argc/argv equivalent for the connection - increased size for all options
    char *fake_argv[100];
    int fake_argc = 1;
    
    // Static buffers for arguments that need to persist
    static char geom_buf[32];
    static char port_buf[16];
    
    fake_argv[0] = "rdesktop-mac";
    
    // Basic options
    if (g_username && strlen(g_username) > 0) {
        fake_argv[fake_argc++] = "-u";
        fake_argv[fake_argc++] = g_username;
    }
    
    // Add geometry
    snprintf(geom_buf, sizeof(geom_buf), "%dx%d", g_requested_session_width, g_requested_session_height);
    fake_argv[fake_argc++] = "-g";
    fake_argv[fake_argc++] = geom_buf;
    
    // Add keymap
    if (strlen(g_keymapname) > 0) {
        fake_argv[fake_argc++] = "-k";
        fake_argv[fake_argc++] = g_keymapname;
    }
    
    // Process advanced options if available
    if (g_advanced_options) {
        // Domain
        NSString *domain = [g_advanced_options objectForKey:@"domain"];
        if (domain && [domain length] > 0) {
            fake_argv[fake_argc++] = "-d";
            fake_argv[fake_argc++] = (char *)[domain UTF8String];
        }
        
        // Password
        NSString *password = [g_advanced_options objectForKey:@"password"];
        if (password && [password length] > 0) {
            fake_argv[fake_argc++] = "-p";
            fake_argv[fake_argc++] = (char *)[password UTF8String];
        }
        
        // Client hostname
        NSString *clientHostname = [g_advanced_options objectForKey:@"clientHostname"];
        if (clientHostname && [clientHostname length] > 0) {
            fake_argv[fake_argc++] = "-n";
            fake_argv[fake_argc++] = (char *)[clientHostname UTF8String];
        }
        
        // TLS Version
        NSString *tlsVersion = [g_advanced_options objectForKey:@"tlsVersion"];
        if (tlsVersion && [tlsVersion length] > 0) {
            fake_argv[fake_argc++] = "-V";
            fake_argv[fake_argc++] = (char *)[tlsVersion UTF8String];
        }
        
        // Color depth
        NSString *colorDepth = [g_advanced_options objectForKey:@"colorDepth"];
        if (colorDepth && [colorDepth length] > 0) {
            fake_argv[fake_argc++] = "-a";
            if ([colorDepth isEqualToString:@"8-bit"]) {
                fake_argv[fake_argc++] = "8";
            } else if ([colorDepth isEqualToString:@"15-bit"]) {
                fake_argv[fake_argc++] = "15";
            } else if ([colorDepth isEqualToString:@"16-bit"]) {
                fake_argv[fake_argc++] = "16";
            } else if ([colorDepth isEqualToString:@"24-bit"]) {
                fake_argv[fake_argc++] = "24";
            } else if ([colorDepth isEqualToString:@"32-bit"]) {
                fake_argv[fake_argc++] = "32";
            }
        }
        
        // Window title
        NSString *windowTitle = [g_advanced_options objectForKey:@"windowTitle"];
        if (windowTitle && [windowTitle length] > 0) {
            fake_argv[fake_argc++] = "-T";
            fake_argv[fake_argc++] = (char *)[windowTitle UTF8String];
        }
        
        // Experience level
        NSString *experience = [g_advanced_options objectForKey:@"experience"];
        if (experience && [experience length] > 0) {
            fake_argv[fake_argc++] = "-x";
            if ([experience isEqualToString:@"Modem (28.8k)"]) {
                fake_argv[fake_argc++] = "m";
            } else if ([experience isEqualToString:@"Broadband"]) {
                fake_argv[fake_argc++] = "b";
            } else if ([experience isEqualToString:@"LAN"]) {
                fake_argv[fake_argc++] = "l";
            }
        }
        
        // Shell/Application
        NSString *shell = [g_advanced_options objectForKey:@"shell"];
        if (shell && [shell length] > 0) {
            fake_argv[fake_argc++] = "-s";
            fake_argv[fake_argc++] = (char *)[shell UTF8String];
        }
        
        // Working directory
        NSString *workingDir = [g_advanced_options objectForKey:@"workingDir"];
        if (workingDir && [workingDir length] > 0) {
            fake_argv[fake_argc++] = "-c";
            fake_argv[fake_argc++] = (char *)[workingDir UTF8String];
        }
        
        // SeamlessRDP path
        NSString *seamlessPath = [g_advanced_options objectForKey:@"seamlessPath"];
        if (seamlessPath && [seamlessPath length] > 0) {
            fake_argv[fake_argc++] = "-A";
            fake_argv[fake_argc++] = (char *)[seamlessPath UTF8String];
        }
        
        // Local codepage
        NSString *codepage = [g_advanced_options objectForKey:@"codepage"];
        if (codepage && [codepage length] > 0) {
            fake_argv[fake_argc++] = "-L";
            fake_argv[fake_argc++] = (char *)[codepage UTF8String];
        }
        
        // Device redirection
        NSString *deviceRedirection = [g_advanced_options objectForKey:@"deviceRedirection"];
        if (deviceRedirection && [deviceRedirection length] > 0) {
            fake_argv[fake_argc++] = "-r";
            fake_argv[fake_argc++] = (char *)[deviceRedirection UTF8String];
        }
        
        // Boolean flags
        if ([g_advanced_options objectForKey:@"smartcard"]) {
            fake_argv[fake_argc++] = "-i";
        }
        if ([g_advanced_options objectForKey:@"disableEncryption"]) {
            fake_argv[fake_argc++] = "-e";
        }
        if ([g_advanced_options objectForKey:@"disableClientEncryption"]) {
            fake_argv[fake_argc++] = "-E";
        }
        if ([g_advanced_options objectForKey:@"fullscreen"]) {
            fake_argv[fake_argc++] = "-f";
        }
        if ([g_advanced_options objectForKey:@"forceBitmapUpdates"]) {
            fake_argv[fake_argc++] = "-b";
        }
        if ([g_advanced_options objectForKey:@"hideDecorations"]) {
            fake_argv[fake_argc++] = "-D";
        }
        if ([g_advanced_options objectForKey:@"compression"]) {
            fake_argv[fake_argc++] = "-z";
        }
        if ([g_advanced_options objectForKey:@"persistentCaching"]) {
            fake_argv[fake_argc++] = "-P";
        }
        if ([g_advanced_options objectForKey:@"backingStore"]) {
            fake_argv[fake_argc++] = "-B";
        }
        if ([g_advanced_options objectForKey:@"disableMotionEvents"]) {
            fake_argv[fake_argc++] = "-m";
        }
        if ([g_advanced_options objectForKey:@"localMouseCursor"]) {
            fake_argv[fake_argc++] = "-M";
        }
        if ([g_advanced_options objectForKey:@"numlockSync"]) {
            fake_argv[fake_argc++] = "-N";
        }
        if ([g_advanced_options objectForKey:@"disableRemoteCtrl"]) {
            fake_argv[fake_argc++] = "-t";
        }
        if ([g_advanced_options objectForKey:@"keepKeyBindings"]) {
            fake_argv[fake_argc++] = "-K";
        }
        if ([g_advanced_options objectForKey:@"console"]) {
            fake_argv[fake_argc++] = "-0";
        }
        if ([g_advanced_options objectForKey:@"verboseLogging"]) {
            fake_argv[fake_argc++] = "-v";
        }
        
        // RDP Version
        NSString *rdpVersion = [g_advanced_options objectForKey:@"rdpVersion"];
        if (rdpVersion && [rdpVersion isEqualToString:@"RDP 4"]) {
            fake_argv[fake_argc++] = "-4";
        } else {
            fake_argv[fake_argc++] = "-5"; // Default to RDP 5
        }
    }
    
    // Add hostname, including a non-default port in the positional server
    // argument.  -P is persistent bitmap caching and does not take a port.
    if (g_tcp_port_rdp != 3389) {
        snprintf(port_buf, sizeof(port_buf), "%s:%d", g_hostname, g_tcp_port_rdp);
        fake_argv[fake_argc++] = port_buf;
    } else {
        fake_argv[fake_argc++] = g_hostname;
    }
    fake_argv[fake_argc] = NULL;

    // Log the constructed arguments for debugging
    NSLog(@"[macOS GUI] Constructed fake_argv (argc=%d):", fake_argc);
    for (int i = 0; i < fake_argc; i++) {
        if (i > 0 && strcmp(fake_argv[i - 1], "-p") == 0)
            NSLog(@"  fake_argv[%d] = '<redacted>'", i);
        else
            NSLog(@"  fake_argv[%d] = '%s'", i, fake_argv[i]);
    }
    
    // Call the main RDP connection function
    return rdesktop_main(fake_argc, fake_argv);
}
