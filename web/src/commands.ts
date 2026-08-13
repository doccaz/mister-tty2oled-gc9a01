// Catalog of every tty2oled wire-protocol command this firmware
// understands - both the ones compatible with the original
// venice1200/MiSTer_tty2oled grammar and the new commands added for this
// round-display project. Drives main.ts's "Command console" modal, which
// documents and lets you send each one directly over WebSerial. Mirrors
// firmware/src/protocol.cpp's dispatch() and ../../CLAUDE.md's "Wire
// protocol" section - keep in sync when either changes.

export type ParamType = "number" | "text" | "select";

export interface CommandParam {
  key: string;
  label: string;
  type: ParamType;
  default: string;
  min?: number;
  max?: number;
  options?: { value: string; label: string }[];
}

export interface CommandDef {
  id: string; // unique key, usually the command name
  category: string;
  syntax: string; // display-only grammar, e.g. "CMDCLST,<transition>,<color 0-15>"
  summary: string;
  params?: CommandParam[];
  /** Builds the exact line to send. Defaults to "<id>[,<param values...>]" if omitted. */
  build?: (values: Record<string, string>) => string;
  sendable: boolean; // false for commands that need out-of-band bytes (pictures) or aren't meant to be hand-triggered
  note?: string;
  confirmMessage?: string; // if set, ask for confirmation before sending (e.g. CMDRESET)
}

const EFFECT_OPTIONS = [
  { value: "-1", label: "-1 (random)" },
  { value: "0", label: "0 (instant)" },
  { value: "1", label: "1 (wipe left→right)" },
  { value: "2", label: "2 (wipe right→left)" },
  { value: "3", label: "3 (wipe top→bottom)" },
  { value: "4", label: "4 (iris)" },
  { value: "5", label: "5 (fade)" },
];

function defaultBuild(id: string, params?: CommandParam[]) {
  return (values: Record<string, string>) => {
    if (!params || params.length === 0) return id;
    return [id, ...params.map((p) => values[p.key] ?? p.default)].join(",");
  };
}

function cmd(def: Omit<CommandDef, "sendable"> & { sendable?: boolean }): CommandDef {
  return {
    ...def,
    sendable: def.sendable ?? true,
    build: def.build ?? defaultBuild(def.id, def.params),
  };
}

export const COMMANDS: CommandDef[] = [
  // --- Display basics -----------------------------------------------------
  cmd({ id: "CMDCLS", category: "Display basics", syntax: "CMDCLS", summary: "Clear the screen and update the display." }),
  cmd({ id: "CMDCLSWU", category: "Display basics", syntax: "CMDCLSWU", summary: "Clear the screen without a display update." }),
  cmd({ id: "CMDSORG", category: "Display basics", syntax: "CMDSORG", summary: "Show the boot splash screen (name/version/repo URL)." }),
  cmd({ id: "CMDDOFF", category: "Display basics", syntax: "CMDDOFF", summary: "Blank the GC9A01 (also used by the screensaver)." }),
  cmd({ id: "CMDDON", category: "Display basics", syntax: "CMDDON", summary: "Restore the GC9A01 and redraw the last picture." }),
  cmd({ id: "CMDDUPD", category: "Display basics", syntax: "CMDDUPD", summary: "Update/flush the display (no-op here - draws happen immediately)." }),
  cmd({
    id: "CMDCON",
    category: "Display basics",
    syntax: "CMDCON,<0-255>",
    summary: "Set backlight/contrast level.",
    params: [{ key: "level", label: "level", type: "number", default: "128", min: 0, max: 255 }],
  }),
  cmd({
    id: "CMDROT",
    category: "Display basics",
    syntax: "CMDROT,<0|1>",
    summary: "Set rotation (0 = normal, 1 = 180°).",
    params: [{ key: "rot", label: "rotation", type: "select", default: "0", options: [{ value: "0", label: "0 (normal)" }, { value: "1", label: "1 (180°)" }] }],
  }),

  // --- Drawing -------------------------------------------------------------
  cmd({
    id: "CMDTXT",
    category: "Drawing",
    syntax: "CMDTXT,x,y,size,text",
    summary: "Draw a text string at x,y.",
    params: [
      { key: "x", label: "x", type: "number", default: "20", min: 0, max: 240 },
      { key: "y", label: "y", type: "number", default: "120", min: 0, max: 240 },
      { key: "size", label: "size", type: "number", default: "2", min: 1, max: 6 },
      { key: "text", label: "text", type: "text", default: "hello" },
    ],
  }),
  cmd({
    id: "CMDGEO",
    category: "Drawing",
    syntax: "CMDGEO,type,x,y,w_or_r,h,fill",
    summary: "Draw a rect (0), circle (1, w=radius), or line (2, w/h = x2/y2).",
    params: [
      { key: "type", label: "type", type: "select", default: "0", options: [{ value: "0", label: "0 (rect)" }, { value: "1", label: "1 (circle)" }, { value: "2", label: "2 (line)" }] },
      { key: "x", label: "x", type: "number", default: "60", min: 0, max: 240 },
      { key: "y", label: "y", type: "number", default: "60", min: 0, max: 240 },
      { key: "w", label: "w / r / x2", type: "number", default: "80", min: 0, max: 240 },
      { key: "h", label: "h / y2", type: "number", default: "80", min: 0, max: 240 },
      { key: "fill", label: "fill", type: "select", default: "1", options: [{ value: "0", label: "0 (outline)" }, { value: "1", label: "1 (filled)" }] },
    ],
  }),

  // --- Corename --------------------------------------------------------------
  cmd({ id: "CMDSNAM", category: "Corename", syntax: "CMDSNAM", summary: "Show the currently-tracked core name as text." }),
  cmd({
    id: "bare-corename",
    category: "Corename",
    syntax: "<corename>  (no CMD prefix)",
    summary: "Legacy fallback: a bare line with no CMD prefix is treated as a plain-text core name. This is what makes an unmodified tty2oled.sh work with zero MiSTer-side changes.",
    params: [{ key: "corename", label: "core name", type: "text", default: "SNES" }],
    build: (values) => values.corename ?? "",
  }),

  // --- Pictures (sent via the main gallery/editor, not this console) ------
  cmd({
    id: "CMDCOR",
    category: "Pictures",
    syntax: "CMDCOR,<name>,<effect> + 2048 or 8192 raw bytes",
    summary: "Legacy 1bpp XBM / 4bpp GSC marquee art, classified purely by byte count.",
    sendable: false,
    note: "Needs a raw picture payload - use the main gallery/editor to send legacy art, not this console.",
  }),
  cmd({
    id: "CMDAPD",
    category: "Pictures",
    syntax: "CMDAPD,<name>,<effect> + 2048 or 8192 raw bytes",
    summary: "Same wire format as CMDCOR (kept for original-script compatibility).",
    sendable: false,
    note: "Needs a raw picture payload - use the main gallery/editor to send legacy art, not this console.",
  }),
  cmd({
    id: "CMDCORC",
    category: "Pictures",
    syntax: "CMDCORC,<name>,<effect>,<durationMs>,<length> + <length> raw JPEG bytes",
    summary: "New full-color, round-native JPEG art with a transition effect and duration.",
    sendable: false,
    note: "Needs a raw JPEG payload - use the main gallery/editor's \"Send to device\" button, not this console.",
  }),

  // --- Redisplay -------------------------------------------------------------
  cmd({
    id: "CMDCLST",
    category: "Redisplay",
    syntax: "CMDCLST,<transition>,<color 0-15>",
    summary: "Fill the screen with a solid grayscale color and reveal it with a transition.",
    params: [
      { key: "effect", label: "transition", type: "select", default: "1", options: EFFECT_OPTIONS },
      { key: "color", label: "color (0-15)", type: "number", default: "8", min: 0, max: 15 },
    ],
  }),
  cmd({
    id: "CMDSPIC",
    category: "Redisplay",
    syntax: "CMDSPIC[,<effect>]",
    summary: "Redisplay the last-drawn picture with a (possibly random) transition - no resend needed.",
    params: [{ key: "effect", label: "transition", type: "select", default: "-1", options: EFFECT_OPTIONS }],
  }),
  cmd({ id: "CMDSSCP", category: "Redisplay", syntax: "CMDSSCP", summary: "Redisplay the last-drawn picture at reduced (\"1/4 area\") size." }),

  // --- Diagnostics -------------------------------------------------------------
  cmd({ id: "CMDNULL", category: "Diagnostics", syntax: "CMDNULL", summary: "No-op test command - just exercises the parser/ack round-trip." }),
  cmd({
    id: "CMDSECD",
    category: "Diagnostics",
    syntax: "CMDSECD,<ms>",
    summary: "Set the delay before ttyack; is sent after each command.",
    params: [{ key: "ms", label: "ms", type: "number", default: "15", min: 0, max: 5000 }],
  }),
  cmd({ id: "CMDSHCD", category: "Diagnostics", syntax: "CMDSHCD", summary: "Show the current command delay on screen." }),
  cmd({
    id: "CMDHWINF",
    category: "Diagnostics",
    syntax: "CMDHWINF",
    summary: "Ask the device for its hardware id + firmware version (reply appears in the log below, not on screen).",
  }),
  cmd({ id: "CMDSHSYSHW", category: "Diagnostics", syntax: "CMDSHSYSHW", summary: "Show a system-info screen: firmware version, chip model, free heap." }),
  cmd({ id: "CMDTEST", category: "Diagnostics", syntax: "CMDTEST", summary: "Show a built-in concentric-ring test pattern." }),
  cmd({ id: "CMDBYE", category: "Diagnostics", syntax: "CMDBYE", summary: "Show a built-in farewell screen." }),

  // --- Screensaver -------------------------------------------------------------
  cmd({
    id: "CMDSAVER",
    category: "Screensaver",
    syntax: "CMDSAVER,<mode>,<interval-sec>,<logotime-sec>",
    summary: "Configure and enable the idle screensaver (mode>0 = enabled). Blanks both the GC9A01 and the onboard OLED after <interval> seconds idle; logotime is accepted for grammar compatibility but unused.",
    params: [
      { key: "mode", label: "mode (0-255)", type: "number", default: "1", min: 0, max: 255 },
      { key: "interval", label: "interval (5-600s)", type: "number", default: "60", min: 5, max: 600 },
      { key: "logotime", label: "logotime (20-600s)", type: "number", default: "60", min: 20, max: 600 },
    ],
  }),
  cmd({
    id: "CMDSWSAVER",
    category: "Screensaver",
    syntax: "CMDSWSAVER,<0|1>",
    summary: "Toggle the screensaver on/off without changing its configured interval.",
    params: [{ key: "on", label: "enabled", type: "select", default: "0", options: [{ value: "0", label: "0 (off)" }, { value: "1", label: "1 (on)" }] }],
  }),

  // --- System -------------------------------------------------------------
  cmd({
    id: "CMDSTTYACK",
    category: "System",
    syntax: "CMDSTTYACK,<0|1>",
    summary: "Enable/disable the ttyack; reply sent after each command. Leave at 1 here, or the console's own ack-wait will time out.",
    params: [{ key: "on", label: "enabled", type: "select", default: "1", options: [{ value: "0", label: "0 (off)" }, { value: "1", label: "1 (on)" }] }],
  }),
  cmd({
    id: "CMDRESET",
    category: "System",
    syntax: "CMDRESET",
    summary: "Reboot the device.",
    confirmMessage: "Reboot the device now? This will drop the serial connection.",
  }),

  // --- Protocol internals -------------------------------------------------------------
  cmd({
    id: "QWERTZ",
    category: "Protocol internals",
    syntax: "QWERTZ",
    summary: "First transmission a real MiSTer sends after boot, to clear the buffer. Harmless no-op if sent manually.",
  }),
];

export const COMMAND_CATEGORIES: string[] = Array.from(new Set(COMMANDS.map((c) => c.category)));
