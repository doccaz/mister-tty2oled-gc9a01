#pragma once

// Shared by protocol.cpp (CMDHWINF reply, CMDSHSYSHW screen), display.cpp
// (boot splash), and oled_status.cpp (status dashboard) - single place to
// bump on release.
#define FW_VERSION "0.4.0"
// No "github.com/" prefix - the full URL (~240px at the boot splash's
// 6px/char font) is wider than the display itself; this shorter form
// still reads as a findable repo path.
#define REPO_URL "doccaz/mister-tty2oled-gc9a01"
