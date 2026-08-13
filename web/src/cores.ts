// Console/computer core list, cross-checked against the official MiSTer
// FPGA docs (https://mister-devel.github.io/MkDocs_MiSTer/cores/console/
// and .../cores/computer/ - fetched 2026-08-12). `id` uses that
// documentation's exact folder-name casing, since that's the closest
// public source to the real CORENAME a MiSTer broadcasts. A few
// corrections this caught vs. an earlier hand-typed guess: the Genesis
// core is actually named "MegaDrive", "TGFX16" is actually
// "TurboGrafx16", "ZXSpectrum" is actually just "Spectrum", and cores for
// Saturn/N64/Gameboy2P/GBA2P/SGB/GameNWatch/C128/CoCo3/MSX1 etc. exist
// and were previously missing entirely.
//
// This is still not a live export from an actual MiSTer install, so
// treat it as a strong starting point rather than ground truth - the
// library UI lets you add/rename/remove entries.
//
// Arcade is deliberately NOT enumerated here: MiSTer arcade "cores" are
// one .mra per game (hundreds of them), not a fixed list. Those entries
// are generated at runtime in main.ts from whatever's left in the local
// pack index after console/computer matching - see buildArcadeCores().

export interface CoreDef {
  id: string; // CORENAME value as MiSTer would send it
  label: string;
  category: "Console" | "Computer" | "Arcade" | "Utility";
}

export const SEED_CORES: CoreDef[] = [
  // --- Consoles ---
  { id: "NES", label: "Nintendo Entertainment System", category: "Console" },
  { id: "SNES", label: "Super Nintendo Entertainment System", category: "Console" },
  { id: "Gameboy", label: "Nintendo Game Boy", category: "Console" },
  { id: "Gameboy2P", label: "2x Game Boy (2-Player)", category: "Console" },
  { id: "GBA", label: "Nintendo Game Boy Advance", category: "Console" },
  { id: "GBA2P", label: "2x Game Boy Advance (2-Player)", category: "Console" },
  { id: "SGB", label: "Nintendo Super Game Boy", category: "Console" },
  { id: "GameNWatch", label: "Nintendo Game & Watch", category: "Console" },
  { id: "N64", label: "Nintendo 64", category: "Console" },
  { id: "MegaDrive", label: "Sega Mega Drive / Genesis", category: "Console" },
  { id: "MegaCD", label: "Sega CD / Mega CD", category: "Console" },
  { id: "S32X", label: "Sega 32X", category: "Console" },
  { id: "SMS", label: "Sega Master System / Game Gear", category: "Console" },
  { id: "Saturn", label: "Sega Saturn", category: "Console" },
  { id: "TurboGrafx16", label: "NEC TurboGrafx-16 / PC Engine", category: "Console" },
  { id: "NeoGeo", label: "SNK Neo Geo", category: "Console" },
  { id: "PSX", label: "Sony PlayStation", category: "Console" },
  { id: "Coleco", label: "ColecoVision", category: "Console" },
  { id: "Intv", label: "Mattel Intellivision", category: "Console" },
  { id: "Atari5200", label: "Atari 5200 SuperSystem", category: "Console" },
  { id: "Atari7800", label: "Atari 7800 ProSystem", category: "Console" },
  { id: "AtariLynx", label: "Atari Lynx", category: "Console" },
  { id: "WonderSwan", label: "Bandai WonderSwan", category: "Console" },
  { id: "Odyssey2", label: "Magnavox Odyssey 2", category: "Console" },
  { id: "Vectrex", label: "Vectrex", category: "Console" },
  { id: "Astrocade", label: "Bally Astrocade", category: "Console" },
  { id: "SuperVision", label: "Watara SuperVision", category: "Console" },
  { id: "Super_Vision_8000", label: "Bandai Super Vision 8000", category: "Console" },
  { id: "PokemonMini", label: "Pokémon Mini", category: "Console" },
  { id: "ChannelF", label: "Fairchild Channel F", category: "Console" },
  { id: "VC4000", label: "Interton VC4000", category: "Console" },
  { id: "Arcadia", label: "Emerson Arcadia 2001", category: "Console" },
  { id: "AVision", label: "Entex Adventure Vision", category: "Console" },
  { id: "AY-3-8500", label: "Pong-on-a-chip", category: "Console" },
  { id: "Gamate", label: "Bit Corp Gamate", category: "Console" },
  { id: "CreatiVision", label: "VTech CreatiVision", category: "Console" },
  { id: "Casio_PV-1000", label: "Casio PV-1000", category: "Console" },
  { id: "MyVision", label: "Nichibutsu My Vision", category: "Console" },
  { id: "BBCBridgeCompanion", label: "BBC Bridge Companion", category: "Console" },
  { id: "RX78", label: "Bandai RX-78 Gundam", category: "Console" },

  // --- Computers ---
  { id: "Amiga", label: "Commodore Amiga", category: "Computer" },
  { id: "C64", label: "Commodore 64 / 128", category: "Computer" },
  { id: "C128", label: "Commodore 128", category: "Computer" },
  { id: "C16", label: "Commodore C16 / Plus4", category: "Computer" },
  { id: "VIC20", label: "Commodore VIC-20", category: "Computer" },
  { id: "PET2001", label: "Commodore PET 2001", category: "Computer" },
  { id: "AtariST", label: "Atari ST / STe", category: "Computer" },
  { id: "Atari800", label: "Atari 800 / XL / XE", category: "Computer" },
  { id: "AO486", label: "PC (486DX33-compatible)", category: "Computer" },
  { id: "PCXT", label: "IBM PC/XT", category: "Computer" },
  { id: "Amstrad", label: "Amstrad CPC 6128", category: "Computer" },
  { id: "AmstradPCW", label: "Amstrad PCW", category: "Computer" },
  { id: "MSX", label: "MSX / MSX2 / Plus / MSX3 / TurboR", category: "Computer" },
  { id: "MSX1", label: "MSX1", category: "Computer" },
  { id: "Spectrum", label: "Sinclair ZX Spectrum", category: "Computer" },
  { id: "ZX81", label: "Sinclair ZX80 / ZX81", category: "Computer" },
  { id: "ZXNext", label: "ZX Spectrum Next", category: "Computer" },
  { id: "Apple-I", label: "Apple I", category: "Computer" },
  { id: "Apple-II", label: "Apple IIe", category: "Computer" },
  { id: "MacPlus", label: "Apple Macintosh Plus", category: "Computer" },
  { id: "ARCHIE", label: "Acorn Archimedes", category: "Computer" },
  { id: "BBCMicro", label: "BBC Micro B / Master 128K", category: "Computer" },
  { id: "AcornAtom", label: "Acorn Atom", category: "Computer" },
  { id: "AcornElectron", label: "Acorn Electron", category: "Computer" },
  { id: "SAMCOUPE", label: "Miles Gordon Technology SAM Coupé", category: "Computer" },
  { id: "X68000", label: "Sharp X68000", category: "Computer" },
  { id: "SharpMZ", label: "Sharp MZ", category: "Computer" },
  { id: "PC8801", label: "NEC PC8801 MKII SR", category: "Computer" },
  { id: "TRS-80", label: "Radio Shack / Tandy TRS-80", category: "Computer" },
  { id: "CoCo2", label: "Tandy Color Computer 2 / Dragon 32", category: "Computer" },
  { id: "CoCo3", label: "Tandy Color Computer 3", category: "Computer" },
  { id: "TI-99_4A", label: "Texas Instruments TI-99/4A", category: "Computer" },
  { id: "Sord M5", label: "Sord M5", category: "Computer" },
  { id: "SVI328", label: "Spectravideo SV-328", category: "Computer" },
  { id: "Oric", label: "Tangerine Oric / Oric-1", category: "Computer" },
  { id: "ORAO", label: "PEL Varaždin Orao / Eagle", category: "Computer" },
  { id: "Galaksija", label: "Galaksija", category: "Computer" },
  { id: "Jupiter", label: "Jupiter Ace", category: "Computer" },
  { id: "AQUARIUS", label: "Mattel Aquarius", category: "Computer" },
  { id: "QL", label: "Sinclair QL", category: "Computer" },
  { id: "EDSAC", label: "EDSAC", category: "Computer" },
  { id: "PDP1", label: "DEC PDP-1", category: "Computer" },
  { id: "UK101", label: "Compukit UK101", category: "Computer" },
  { id: "VECTOR06", label: "Vector-06C", category: "Computer" },
  { id: "TSConf", label: "TSConf (ZX-Evolution)", category: "Computer" },
  { id: "Chip8", label: "CHIP-8", category: "Computer" },
  { id: "Adam", label: "Coleco Adam", category: "Computer" },
  { id: "Altair8800", label: "MITS Altair 8800", category: "Computer" },
  { id: "AliceMC10", label: "Matra & Hachette Alice", category: "Computer" },
  { id: "APOGEE", label: "Apogee BK-01 / Radio-86RK", category: "Computer" },
  { id: "Casio_PV-2000", label: "Casio PV-2000", category: "Computer" },
  { id: "eg2000", label: "EACA EG2000 Colour Genie", category: "Computer" },
  { id: "Homelab", label: "Compukit Homelab", category: "Computer" },
  { id: "Interact", label: "Interact Home Computer", category: "Computer" },
  { id: "Laser", label: "Vtech Laser 310", category: "Computer" },
  { id: "Lynx48", label: "Camputers Lynx 48k/96k", category: "Computer" },
  { id: "MultiComp", label: "Grant Searle's MultiComp", category: "Computer" },
  { id: "Ondra_SPO186", label: "Tesla Ondra SPO-186", category: "Computer" },
  { id: "PMD85", label: "Tesla PMD 85", category: "Computer" },
  { id: "SPMX", label: "Specialist", category: "Computer" },
  { id: "TatungEinstein", label: "Tatung Einstein TC01 & 256", category: "Computer" },
  { id: "TomyTutor", label: "Tomy Tutor / Pyuta", category: "Computer" },

  // --- Utility / system screens ---
  { id: "MENU", label: "MiSTer Menu", category: "Utility" },
];
