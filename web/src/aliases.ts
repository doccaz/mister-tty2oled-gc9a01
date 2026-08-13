// Best-effort alternate spellings mapping a core id to filenames actually
// present in the imported local packs (see importLibrary.ts / library/).
// Needed because the community pack files predate (and don't always match)
// the official MiSTer core folder names cores.ts now uses - e.g. the pack
// calls Sega's console "Genesis" where the real core is "MegaDrive", "PCE"
// where the real core is "TurboGrafx16", etc. This is a best-effort guess,
// not verified against a live MiSTer install; the "Review core matches"
// table in the UI lets you confirm or override every row before saving.

export const CORE_ALIASES: Record<string, string[]> = {
  MegaDrive: ["Genesis"],
  TurboGrafx16: ["PCE", "PCE_arcade", "PCE_CD", "TGFX16"],
  NeoGeo: ["NEOGEO"],
  WonderSwan: ["WONDERSWAN"],
  Saturn: ["Sega Saturn"],
  Amiga: ["Minimig"],
  MacPlus: ["MACPLUS"],
  PC8801: ["PC88"],
  SMS: ["GameGear"],
  Astrocade: ["A.ASTROCADE"],
  X68000: ["X68K"],
};
