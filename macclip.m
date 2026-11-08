/* -*- c-basic-offset: 8 -*-
   rdesktop: A Remote Desktop Protocol client.
   Clipboard functionality - macOS native
   Native macOS port
*/

#import <Cocoa/Cocoa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rdesktop.h"

/* Stub implementations for clipboard functionality */

void
cliprdr_send_text_format_announce(void)
{
    // Announce text format capability
}

void
cliprdr_send_blah_format_announce(void)
{
    // Announce other format capability  
}

/* Removed conflicting functions - these are implemented in cliprdr.c */

void
cliprdr_register_server_formats(STREAM s)
{
    (void)s;
    // Register server formats
}

void
cliprdr_handle_SelectionNotify(void)
{
    // Handle selection notify
}

void
cliprdr_handle_SelectionRequest(void)
{
    // Handle selection request
}

void
cliprdr_handle_SelectionClear(void)
{
    // Handle selection clear
}

void
cliprdr_handle_PropertyNotify(void)
{
    // Handle property notify
}

void
ui_clip_format_announce(uint8 * data, uint32 length)
{
    (void)data;
    (void)length;
    // UI clip format announce
}

void
ui_clip_handle_data(uint8 * data, uint32 length)
{
    (void)data;
    (void)length;
    // Handle clipboard data
}

void
ui_clip_request_failed(void)
{
    // Handle clipboard request failure
}

void
ui_clip_request_data(uint32 format)
{
    (void)format;
    // Request clipboard data
}

void
ui_clip_sync(void)
{
    // Sync clipboard
}

void
ui_clip_set_mode(const char *optarg)
{
    (void)optarg;
    // Set UI clipboard mode
}
