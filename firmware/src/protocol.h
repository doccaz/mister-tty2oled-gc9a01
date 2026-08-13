#pragma once

// Serial command protocol: a superset of venice1200/MiSTer_tty2oled's
// wire format. See ../reference/MiSTer_SSD1322_USB/MiSTer_SSD1322_USB.ino
// and ../CLAUDE.md for the original grammar this stays compatible with.
//
// - protocol_init() sends "ttyrdy;" once, exactly like the original.
// - protocol_process() must be called every loop() iteration; it reads at
//   most one line per call and dispatches it, sending "ttyack;" afterward
//   (unless disabled via CMDSTTYACK,0), exactly like the original.

void protocol_init();
void protocol_process();
