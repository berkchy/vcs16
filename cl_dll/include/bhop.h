#pragma once

// Standalone movement helpers: bhop assist and ground strafe.
// No gameplay automation beyond movement keys; the names of the two cvars
// match the QSI client so existing configs keep working.
struct usercmd_s;

void BHOP_Init( void );
void BHOP_CreateMove( struct usercmd_s *cmd );
