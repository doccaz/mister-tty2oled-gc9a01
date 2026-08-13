import { SEED_CORES, type CoreDef } from "./cores";
import { SerialLink, type ConnectionState } from "./serial";
import { MarqueeEditor } from "./editor";
import { loadAllCoreArt, saveCoreArt, type CoreArtRecord } from "./db";
import { loadImportIndex, entryUrl, findAutoMatch, type ImportEntry } from "./importLibrary";
import { DISPLAY_PROFILES, DEFAULT_DISPLAY_PROFILE, findDisplayProfile, type DisplayProfile } from "./displays";
import { DEFAULT_TRANSFORM_BY_SHAPE, type TransformMode } from "./transforms";
import { EFFECTS, SPEED_PRESETS, DEFAULT_SPEED } from "./effects";
import { COMMANDS, COMMAND_CATEGORIES, type CommandDef } from "./commands";

const REPO_URL = "https://github.com/doccaz/mister-tty2oled-gc9a01";

const ABOUT_LINKS = [
  { title: "This project's source", desc: "GitHub", url: REPO_URL },
  { title: "Original tty2oled", desc: "firmware/scripts this protocol is based on", url: "https://github.com/venice1200/MiSTer_tty2oled" },
  { title: "WLED OLED usermod", desc: "onboard status OLED support was ported from this", url: "https://github.com/wled/WLED/pull/5475" },
  { title: "tty2tft", desc: "sibling project for color TFT marquees, same community", url: "https://www.tty2tft.de" },
];

const app = document.getElementById("app")!;
app.innerHTML = `
  <a class="github-ribbon" href="${REPO_URL}" target="_blank" rel="noopener">
    <span>Fork me on GitHub</span>
  </a>
  <header>
    <h1>tty2oled marquee visualizer</h1>
    <label class="hint">display
      <select id="display-select">
        ${DISPLAY_PROFILES.map((p) => `<option value="${p.id}">${p.label}</option>`).join("")}
      </select>
    </label>
    <button id="review-matches-btn">Review core matches</button>
    <button id="command-console-btn">Command console</button>
    <button id="about-btn">About</button>
    <span class="status-dot" id="status-dot"></span>
    <span id="status-text" class="hint">disconnected</span>
    <button id="connect-btn">Connect device</button>
  </header>
  <main>
    <div id="library"></div>
    <div id="editor-panel"></div>
  </main>
`;

const link = new SerialLink();
const artCache = new Map<string, CoreArtRecord>();
let selectedCore: CoreDef | null = null;
let editor: MarqueeEditor | null = null;
let sendTimer: number | undefined;
// Local pack index (see importLibrary.ts) - loaded once at boot so the
// library grid can auto-fill thumbnails for cores with an exact filename
// match (e.g. "NES", "C64") without requiring a manual "Browse local
// packs" + save for every one of them.
let importIndex: ImportEntry[] = [];
// SEED_CORES (console/computer, cross-checked against the official MiSTer
// docs - see cores.ts) plus one CoreDef per local pack entry that isn't
// already claimed by a console/computer match - MiSTer arcade "cores" are
// one .mra per game, not a fixed list, so this is generated at runtime
// once importIndex loads (buildAllCores()) rather than hand-enumerated.
let allCores: CoreDef[] = SEED_CORES;
// Currently selected target display (round/rect, resolution) - a global
// preview setting, not stored per-core. Changing it re-fits whatever's
// currently loaded in the editor and reshapes the library thumbnails.
let currentProfile: DisplayProfile = DEFAULT_DISPLAY_PROFILE;

const displaySelect = document.getElementById("display-select") as HTMLSelectElement;
displaySelect.value = currentProfile.id;
displaySelect.addEventListener("change", () => {
  currentProfile = findDisplayProfile(displaySelect.value);
  editor?.setProfile(currentProfile);
  renderLibrary();
  if (selectedCore) syncFitControls();
  updateActionButtons();
});

// --- Connection UI -----------------------------------------------------

const statusDot = document.getElementById("status-dot")!;
const statusText = document.getElementById("status-text")!;
const connectBtn = document.getElementById("connect-btn") as HTMLButtonElement;

function renderConnectionState(state: ConnectionState) {
  statusDot.className = "status-dot " + state;
  statusText.textContent = state;
  connectBtn.textContent = state === "connected" ? "Disconnect" : "Connect device";
  connectBtn.disabled = state === "connecting";
}

link.addEventListener("statechange", (e) => renderConnectionState((e as CustomEvent).detail));
renderConnectionState(link.state);

if (!SerialLink.isSupported()) {
  connectBtn.disabled = true;
  statusText.textContent = "Web Serial not supported - use Chrome or Edge";
}

connectBtn.addEventListener("click", async () => {
  try {
    if (link.state === "connected") {
      await link.disconnect();
    } else {
      await link.connect();
    }
  } catch (err) {
    statusText.textContent = err instanceof Error ? err.message : String(err);
  }
});

(document.getElementById("review-matches-btn") as HTMLButtonElement).addEventListener("click", () => {
  openMatchReviewModal();
});

(document.getElementById("command-console-btn") as HTMLButtonElement).addEventListener("click", () => {
  openCommandConsole();
});

(document.getElementById("about-btn") as HTMLButtonElement).addEventListener("click", () => {
  openAboutModal();
});

// --- Library grid --------------------------------------------------------

const libraryEl = document.getElementById("library")!;

function thumbUrl(coreId: string): string | null {
  const rec = artCache.get(coreId);
  if (rec) return URL.createObjectURL(rec.jpegBlob);
  const match = findAutoMatch(importIndex, coreId);
  return match ? entryUrl(match) : null;
}

function renderLibrary() {
  const categories = Array.from(new Set(allCores.map((c) => c.category)));
  libraryEl.innerHTML = "";
  for (const category of categories) {
    const label = document.createElement("div");
    label.className = "category-label";
    label.textContent = `${category} (${allCores.filter((c) => c.category === category).length})`;
    libraryEl.appendChild(label);

    const grid = document.createElement("div");
    grid.className = "core-grid";
    for (const core of allCores.filter((c) => c.category === category)) {
      const card = document.createElement("div");
      card.className = "core-card" + (core.id === selectedCore?.id ? " selected" : "");
      const thumb = document.createElement("div");
      thumb.className = "thumb";
      thumb.style.aspectRatio = `${currentProfile.width} / ${currentProfile.height}`;
      thumb.style.borderRadius = currentProfile.shape === "round" ? "50%" : "6px";
      const url = thumbUrl(core.id);
      if (url) {
        thumb.style.backgroundImage = `url(${url})`;
      } else {
        const missing = document.createElement("span");
        missing.className = "thumb-missing";
        missing.textContent = "(not found)";
        thumb.appendChild(missing);
      }
      const name = document.createElement("div");
      name.textContent = core.label;
      card.appendChild(thumb);
      card.appendChild(name);
      card.addEventListener("click", () => selectCore(core));
      grid.appendChild(card);
    }
    libraryEl.appendChild(grid);
  }
}

// --- Editor panel ----------------------------------------------------------

const editorPanel = document.getElementById("editor-panel")!;

function renderEditorPanel(core: CoreDef) {
  editorPanel.innerHTML = `
    <div class="core-title">${core.label} <span class="hint">(${core.id})</span></div>
    <canvas id="editor-canvas"></canvas>
    <div class="controls-row">
      <input type="file" id="file-input" accept="image/*" />
      <button id="browse-local-btn">Browse local packs</button>
      <label class="hint">effect
        <select id="effect-select">
          ${EFFECTS.map((e) => `<option value="${e.id}"${e.id === 1 ? " selected" : ""}>${e.label}</option>`).join("")}
        </select>
      </label>
      <label class="hint">speed
        <select id="speed-select">
          ${SPEED_PRESETS.map((s) => `<option value="${s.id}"${s.id === DEFAULT_SPEED.id ? " selected" : ""}>${s.label}</option>`).join("")}
        </select>
      </label>
      <button id="preview-effect-btn">▶ Preview effect</button>
    </div>
    <div class="controls-row">
      <label class="hint">fit
        <select id="fit-select">
          <option value="cover">cover (fill, crop)</option>
          <option value="contain">contain (fit, letterbox)</option>
          <option value="center-scale">centered @ scale</option>
        </select>
      </label>
      <input type="number" id="fit-scale" min="10" max="200" step="1" style="width:4.5em" />
      <span class="hint">%</span>
    </div>
    <div class="controls-row">
      <button id="save-btn">Save to library</button>
      <button id="send-btn">Send to device</button>
      <label class="hint"><input type="checkbox" id="blank-first-checkbox" checked /> blank first</label>
    </div>
    <p class="hint">Drag to pan, scroll to zoom. The dimmed ring shows what falls outside the round glass (round displays only). Changes preview live on the device while connected.</p>
  `;

  const canvas = editorPanel.querySelector("#editor-canvas") as HTMLCanvasElement;
  editor = new MarqueeEditor(canvas, currentProfile);
  updateActionButtons(); // starts disabled: nothing loaded yet

  const existing = artCache.get(core.id);
  const autoMatch = existing ? undefined : findAutoMatch(importIndex, core.id);
  if (existing) {
    editor.loadFile(existing.jpegBlob).then(updateActionButtons);
  } else if (autoMatch) {
    fetch(entryUrl(autoMatch))
      .then((r) => r.blob())
      .then((b) => editor?.loadFile(b))
      .then(updateActionButtons);
    const note = document.createElement("p");
    note.className = "hint";
    note.textContent = `Auto-matched from local pack "${autoMatch.pack}" - click "Save to library" to keep it.`;
    editorPanel.insertBefore(note, editorPanel.querySelector(".controls-row"));
  }

  const fileInput = editorPanel.querySelector("#file-input") as HTMLInputElement;
  fileInput.addEventListener("change", async () => {
    const file = fileInput.files?.[0];
    if (file) {
      await editor!.loadFile(file);
      syncFitControls();
      updateActionButtons();
    }
  });

  syncFitControls();
  const fitSelect = editorPanel.querySelector("#fit-select") as HTMLSelectElement;
  const fitScale = editorPanel.querySelector("#fit-scale") as HTMLInputElement;
  const applyFit = () => {
    editor?.applyPreset({ mode: fitSelect.value as TransformMode, scalePercent: Number(fitScale.value) || 100 });
  };
  fitSelect.addEventListener("change", applyFit);
  fitScale.addEventListener("change", applyFit);

  editor.setOnChange(() => scheduleLivePreview(core));

  const saveBtn = editorPanel.querySelector("#save-btn") as HTMLButtonElement;
  saveBtn.addEventListener("click", async () => {
    if (!editor?.hasImage()) return;
    const blob = await editor.exportJpegBlob();
    await saveCoreArt(core.id, blob);
    artCache.set(core.id, { coreId: core.id, jpegBlob: blob, updatedAt: Date.now() });
    renderLibrary();
  });

  const sendBtn = editorPanel.querySelector("#send-btn") as HTMLButtonElement;
  sendBtn.addEventListener("click", () => sendPreview(core, true));

  const browseBtn = editorPanel.querySelector("#browse-local-btn") as HTMLButtonElement;
  browseBtn.addEventListener("click", () => openImportModal(core));

  const previewBtn = editorPanel.querySelector("#preview-effect-btn") as HTMLButtonElement;
  previewBtn.addEventListener("click", async () => {
    if (!editor?.hasImage()) return;
    previewBtn.disabled = true;
    try {
      await editor.playEffectPreview(currentEffect(), currentSpeedMs());
    } finally {
      previewBtn.disabled = false;
    }
  });
}

// Preview/save/send are no-ops with nothing loaded - disable them instead
// of silently doing nothing, which otherwise reads as "the effect/save/send
// is broken" (this is what "iris effect showing blank" turned out to be:
// clicking Preview on a core with no auto-matched or loaded art).
function updateActionButtons() {
  const has = editor?.hasImage() ?? false;
  for (const id of ["preview-effect-btn", "save-btn"]) {
    const btn = editorPanel.querySelector(`#${id}`) as HTMLButtonElement | null;
    if (btn) btn.disabled = !has;
  }
  // "Send to device" additionally needs the selected display profile's real
  // hardware to understand CMDCORC - see displays.ts's colorArt field.
  // Sending it to e.g. the legacy SSD1322 profile would desync that
  // firmware's command parser instead of doing nothing harmlessly, so this
  // is a hard gate, not just a UX nicety.
  const sendBtn = editorPanel.querySelector("#send-btn") as HTMLButtonElement | null;
  if (sendBtn) {
    const sendable = currentProfile.colorArt || currentProfile.legacyMono;
    sendBtn.disabled = !has || !sendable;
    sendBtn.title = sendable ? "" : `"${currentProfile.label}" has no matching real hardware - preview/export only.`;
  }
}

// Reflects the current display profile's default fit preset into the
// fit-select/fit-scale controls, without re-applying it to the editor
// (used when a fresh core loads and already got the default via
// loadFile()/setProfile(), and when just switching profile with an image
// already loaded, where setProfile() itself re-applies the new default).
function syncFitControls() {
  const fitSelect = editorPanel.querySelector("#fit-select") as HTMLSelectElement | null;
  const fitScale = editorPanel.querySelector("#fit-scale") as HTMLInputElement | null;
  if (!fitSelect || !fitScale) return;
  const preset = DEFAULT_TRANSFORM_BY_SHAPE[currentProfile.shape];
  fitSelect.value = preset.mode;
  fitScale.value = String(preset.scalePercent);
}

// --- Local pack browser (imported from reference/Pictures/ZIPs) -----------
//
// See /home/erico/Projetos/mister-tty2oled-gc9a01/library/ for the
// conversion pipeline: original .gsc (4bpp)/.xbm (1bpp) 256x64 files from
// the reference project's community marquee packs, decoded to PNG.

async function openImportModal(core: CoreDef) {
  const overlay = document.createElement("div");
  overlay.className = "modal-overlay";
  overlay.innerHTML = `
    <div class="modal">
      <div class="modal-header">
        <input type="text" id="import-search" placeholder="Search packs/games..." />
        <button id="import-close">Close</button>
      </div>
      <div class="modal-hint hint">Imported from the reference project's community marquee packs (256x64 grayscale, originals live in ../library/converted). Selecting one loads it into the editor for ${core.label}.</div>
      <div class="modal-grid" id="import-grid">Loading...</div>
    </div>
  `;
  document.body.appendChild(overlay);

  const close = () => overlay.remove();
  overlay.addEventListener("click", (e) => {
    if (e.target === overlay) close();
  });
  (overlay.querySelector("#import-close") as HTMLButtonElement).addEventListener("click", close);

  const grid = overlay.querySelector("#import-grid") as HTMLDivElement;
  const searchInput = overlay.querySelector("#import-search") as HTMLInputElement;

  if (importIndex.length === 0) {
    try {
      importIndex = await loadImportIndex();
    } catch (err) {
      grid.textContent = err instanceof Error ? err.message : String(err);
      return;
    }
  }

  function renderGrid(filter: string) {
    const term = filter.trim().toLowerCase();
    const items = importIndex.filter(
      (e) => !term || e.name.toLowerCase().includes(term) || e.pack.toLowerCase().includes(term),
    );
    grid.innerHTML = "";
    if (items.length === 0) {
      grid.innerHTML = `<div class="hint">No matches.</div>`;
      return;
    }
    for (const entry of items.slice(0, 400)) {
      const card = document.createElement("div");
      card.className = "import-card";
      card.innerHTML = `<img src="${entryUrl(entry)}" loading="lazy" /><div class="import-name">${entry.name}</div>`;
      card.addEventListener("click", async () => {
        const res = await fetch(entryUrl(entry));
        const blob = await res.blob();
        await editor!.loadFile(blob);
        close();
      });
      grid.appendChild(card);
    }
  }

  renderGrid("");
  searchInput.addEventListener("input", () => renderGrid(searchInput.value));
  searchInput.focus();
}

// --- Core-name match review table -----------------------------------------
//
// Pre-fills a guess for every core using exact + alias filename matching
// (importLibrary.ts's findAutoMatch(), backed by aliases.ts) and lets the
// user confirm/uncheck rows before bulk-saving them to the library, rather
// than requiring a manual "Browse local packs" + save per core.

interface MatchRow {
  core: CoreDef;
  match: ImportEntry | undefined;
  alreadySaved: boolean;
}

async function openMatchReviewModal() {
  const overlay = document.createElement("div");
  overlay.className = "modal-overlay";
  overlay.innerHTML = `
    <div class="modal">
      <div class="modal-header">
        <div class="hint">Guessed matches (exact + alias filename matching against the local packs). Uncheck any you don't want, then save.</div>
        <button id="review-close">Close</button>
      </div>
      <div class="review-list" id="review-list">Loading...</div>
      <div class="controls-row">
        <button id="review-save-all">Save checked matches to library</button>
      </div>
    </div>
  `;
  document.body.appendChild(overlay);

  const close = () => overlay.remove();
  overlay.addEventListener("click", (e) => {
    if (e.target === overlay) close();
  });
  (overlay.querySelector("#review-close") as HTMLButtonElement).addEventListener("click", close);

  const list = overlay.querySelector("#review-list") as HTMLDivElement;

  if (importIndex.length === 0) {
    try {
      importIndex = await loadImportIndex();
    } catch (err) {
      list.textContent = err instanceof Error ? err.message : String(err);
      return;
    }
  }

  const rows: MatchRow[] = allCores.map((core) => ({
    core,
    match: findAutoMatch(importIndex, core.id),
    alreadySaved: artCache.has(core.id),
  }));

  const checkboxes: { row: MatchRow; checkbox: HTMLInputElement }[] = [];
  list.innerHTML = "";
  for (const row of rows) {
    if (!row.match) continue; // nothing to guess for this core
    const item = document.createElement("label");
    item.className = "review-row";
    item.innerHTML = `
      <input type="checkbox" ${row.alreadySaved ? "" : "checked"} />
      <img src="${entryUrl(row.match)}" loading="lazy" />
      <span class="review-core">${row.core.label} <span class="hint">(${row.core.id})</span></span>
      <span class="hint">←</span>
      <span class="review-match hint">${row.match.name}${row.alreadySaved ? " · already saved" : ""}</span>
    `;
    checkboxes.push({ row, checkbox: item.querySelector("input") as HTMLInputElement });
    list.appendChild(item);
  }
  if (checkboxes.length === 0) {
    list.innerHTML = `<div class="hint">No guessable matches beyond what's already assigned.</div>`;
  }

  const saveAllBtn = overlay.querySelector("#review-save-all") as HTMLButtonElement;
  saveAllBtn.addEventListener("click", async () => {
    saveAllBtn.disabled = true;
    saveAllBtn.textContent = "Saving...";
    let count = 0;
    for (const { row, checkbox } of checkboxes) {
      if (!checkbox.checked || !row.match) continue;
      const res = await fetch(entryUrl(row.match));
      const blob = await res.blob();
      await saveCoreArt(row.core.id, blob);
      artCache.set(row.core.id, { coreId: row.core.id, jpegBlob: blob, updatedAt: Date.now() });
      count++;
    }
    renderLibrary();
    close();
    statusText.textContent = `Saved ${count} matched core${count === 1 ? "" : "s"}.`;
  });
}

// --- Command console -------------------------------------------------------
//
// Documents and exercises every protocol command the firmware understands
// (see commands.ts, kept in sync with firmware/src/protocol.cpp and
// CLAUDE.md's "Wire protocol" section) - both the original tty2oled
// grammar and the new commands added for this round display. Picture
// commands (CMDCOR/CMDAPD/CMDCORC) are listed but not sendable here since
// they need raw binary payloads; use the main gallery/editor for those.

function openCommandConsole() {
  const overlay = document.createElement("div");
  overlay.className = "modal-overlay";
  overlay.innerHTML = `
    <div class="modal">
      <div class="modal-header">
        <div class="hint">Every command the firmware understands - see commands.ts / CLAUDE.md. Requires a connected device to send.</div>
        <button id="console-close">Close</button>
      </div>
      <div class="command-list" id="command-list"></div>
      <div class="command-log" id="command-log"></div>
    </div>
  `;
  document.body.appendChild(overlay);

  const close = () => {
    link.removeEventListener("message", onMessage);
    link.removeEventListener("statechange", onStateChange);
    overlay.remove();
  };
  overlay.addEventListener("click", (e) => {
    if (e.target === overlay) close();
  });
  (overlay.querySelector("#console-close") as HTMLButtonElement).addEventListener("click", close);

  const log = overlay.querySelector("#command-log") as HTMLDivElement;
  function appendLog(direction: "tx" | "rx", text: string) {
    const line = document.createElement("div");
    line.className = direction === "tx" ? "log-tx" : "log-rx";
    line.textContent = (direction === "tx" ? "> " : "< ") + text;
    log.appendChild(line);
    log.scrollTop = log.scrollHeight;
  }

  const onMessage = (e: Event) => appendLog("rx", (e as CustomEvent<string>).detail);
  link.addEventListener("message", onMessage);

  const list = overlay.querySelector("#command-list") as HTMLDivElement;
  const sendButtons: HTMLButtonElement[] = [];

  function onStateChange() {
    const connected = link.state === "connected";
    for (const btn of sendButtons) btn.disabled = !connected;
  }
  link.addEventListener("statechange", onStateChange);

  list.innerHTML = "";
  for (const category of COMMAND_CATEGORIES) {
    const label = document.createElement("div");
    label.className = "command-category-label";
    label.textContent = category;
    list.appendChild(label);

    for (const def of COMMANDS.filter((c) => c.category === category)) {
      list.appendChild(buildCommandRow(def, appendLog, sendButtons));
    }
  }
  onStateChange();
}

function buildCommandRow(
  def: CommandDef,
  appendLog: (direction: "tx" | "rx", text: string) => void,
  sendButtons: HTMLButtonElement[],
): HTMLDivElement {
  const row = document.createElement("div");
  row.className = "command-row" + (def.sendable ? "" : " disabled");

  const main = document.createElement("div");
  main.className = "command-main";
  main.innerHTML = `
    <div class="command-syntax">${def.syntax}</div>
    <div class="command-summary">${def.summary}${def.note ? " " + def.note : ""}</div>
  `;
  row.appendChild(main);

  if (!def.sendable) return row;

  const paramsEl = document.createElement("div");
  paramsEl.className = "command-params";
  const inputs: Record<string, HTMLInputElement | HTMLSelectElement> = {};
  for (const p of def.params ?? []) {
    const wrap = document.createElement("label");
    let field: HTMLInputElement | HTMLSelectElement;
    if (p.type === "select") {
      const select = document.createElement("select");
      for (const opt of p.options ?? []) {
        const o = document.createElement("option");
        o.value = opt.value;
        o.textContent = opt.label;
        if (opt.value === p.default) o.selected = true;
        select.appendChild(o);
      }
      field = select;
    } else {
      const input = document.createElement("input");
      input.type = p.type === "number" ? "number" : "text";
      if (p.min !== undefined) input.min = String(p.min);
      if (p.max !== undefined) input.max = String(p.max);
      input.value = p.default;
      field = input;
    }
    wrap.appendChild(document.createTextNode(p.label));
    wrap.appendChild(field);
    inputs[p.key] = field;
    paramsEl.appendChild(wrap);
  }
  row.appendChild(paramsEl);

  const sendBtn = document.createElement("button");
  sendBtn.textContent = "Send";
  sendBtn.addEventListener("click", async () => {
    if (def.confirmMessage && !window.confirm(def.confirmMessage)) return;
    const values: Record<string, string> = {};
    for (const key of Object.keys(inputs)) values[key] = inputs[key].value;
    const line = (def.build ?? (() => def.id))(values);
    appendLog("tx", line);
    sendBtn.disabled = true;
    try {
      await link.sendCommand(line);
    } finally {
      sendBtn.disabled = link.state !== "connected";
    }
  });
  sendButtons.push(sendBtn);
  row.appendChild(sendBtn);

  return row;
}

// --- About -------------------------------------------------------------

function openAboutModal() {
  const overlay = document.createElement("div");
  overlay.className = "modal-overlay";
  overlay.innerHTML = `
    <div class="modal about-modal">
      <div class="modal-header">
        <div class="about-title">
          <span class="about-title-main">tty2oled-gc9a01</span>
          <span class="about-title-sub">marquee visualizer</span>
        </div>
        <button id="about-close">Close</button>
      </div>
      <div class="about-body">
        <p class="about-lead">
          A protocol-compatible re-implementation of the MiSTer FPGA community's
          <strong>tty2oled</strong> marquee display, targeting an ESP32-C3-mini
          + GC9A01 240×240 round SPI display (plus this board's onboard 0.42"
          status OLED). This web app is its companion: browse and edit the
          per-core marquee art library, then preview it live on the physical
          display over WebSerial - no MiSTer required for editing.
        </p>

        <section class="about-section">
          <h3>Architecture</h3>
          <ul class="about-list">
            <li><strong>Firmware</strong> — PlatformIO/Arduino on the ESP32-C3, speaking a superset of the original tty2oled serial protocol (see the Command console for the full grammar).</li>
            <li><strong>This app</strong> — a Vite + TypeScript SPA: Canvas2D crop/pan/zoom editor, IndexedDB for per-core art, and a WebSerial link that mirrors the firmware's wire format exactly.</li>
            <li><strong>Local marquee library</strong> — community packs converted from the original project's grayscale formats, browsable from the editor's "Browse local packs" button.</li>
          </ul>
        </section>

        <section class="about-section">
          <h3>Links</h3>
          <ul class="about-links">
            ${ABOUT_LINKS.map(
              (l) => `
              <li>
                <a href="${l.url}" target="_blank" rel="noopener">
                  <span class="about-link-title">${l.title}</span>
                  <span class="about-link-desc">${l.desc}</span>
                  <span class="about-link-url">${l.url}</span>
                </a>
              </li>`,
            ).join("")}
          </ul>
        </section>
      </div>
    </div>
  `;
  document.body.appendChild(overlay);

  const close = () => overlay.remove();
  overlay.addEventListener("click", (e) => {
    if (e.target === overlay) close();
  });
  (overlay.querySelector("#about-close") as HTMLButtonElement).addEventListener("click", close);
}

function selectCore(core: CoreDef) {
  selectedCore = core;
  renderLibrary();
  renderEditorPanel(core);
}

// Debounced live preview: every pan/zoom edit re-sends to the device so the
// round display updates in real time, without flooding the serial link on
// every pointermove event.
function scheduleLivePreview(core: CoreDef) {
  if (link.state !== "connected" || !(currentProfile.colorArt || currentProfile.legacyMono)) return;
  window.clearTimeout(sendTimer);
  sendTimer = window.setTimeout(() => sendPreview(core, false), 150);
}

function currentEffect(): number {
  const sel = editorPanel.querySelector("#effect-select") as HTMLSelectElement | null;
  return sel ? Number(sel.value) : 1;
}

function currentSpeedMs(): number {
  const sel = editorPanel.querySelector("#speed-select") as HTMLSelectElement | null;
  const preset = SPEED_PRESETS.find((s) => s.id === sel?.value);
  return preset?.durationMs ?? DEFAULT_SPEED.durationMs;
}

function blankFirstEnabled(): boolean {
  const cb = editorPanel.querySelector("#blank-first-checkbox") as HTMLInputElement | null;
  return cb?.checked ?? false;
}

async function sendPreview(core: CoreDef, force: boolean) {
  if (!editor?.hasImage()) return;
  if (!force && link.state !== "connected") return;
  if (link.state !== "connected") {
    statusText.textContent = "Connect the device to preview";
    return;
  }
  try {
    // transitionReveal() on the firmware only ever writes new pixels over
    // whatever's already on the physical screen (it never clears first,
    // matching the original tty2oled's own behavior - see CLAUDE.md) - so
    // re-sending pixel-identical content with a different effect shows no
    // visible transition at all. Clearing first (explicit "Send to
    // device" clicks only, not the debounced live pan/zoom preview, which
    // would otherwise flash on every drag) makes effect changes always
    // visible when evaluating them here.
    if (force && blankFirstEnabled()) {
      await link.sendCommand("CMDCLS");
    }
    if (currentProfile.colorArt) {
      const bytes = await editor.exportJpeg();
      await link.sendColorArt(core.id, currentEffect(), currentSpeedMs(), bytes);
    } else if (currentProfile.legacyMono) {
      const bytes = editor.exportGsc();
      await link.sendLegacyPicture(core.id, currentEffect(), bytes);
    } else {
      statusText.textContent = `"${currentProfile.label}" has no matching real hardware to send to`;
    }
  } catch (err) {
    statusText.textContent = err instanceof Error ? err.message : String(err);
  }
}

// Builds allCores = SEED_CORES + one CoreDef per local pack entry not
// already claimed by a console/computer match (see the `allCores` comment
// above). Returns the per-core guessed match too, so the caller doesn't
// have to re-run findAutoMatch a second time for auto-seeding.
function buildAllCores(entries: ImportEntry[]): { cores: CoreDef[]; guesses: Map<string, ImportEntry> } {
  const guesses = new Map<string, ImportEntry>();
  const claimed = new Set<string>();
  for (const core of SEED_CORES) {
    const m = findAutoMatch(entries, core.id);
    if (m) {
      guesses.set(core.id, m);
      claimed.add(m.name);
    }
  }
  const arcadeCores: CoreDef[] = entries
    .filter((e) => !claimed.has(e.name))
    .map((e) => ({ id: e.name, label: e.name, category: "Arcade" as const }));
  for (const core of arcadeCores) {
    const m = entries.find((e) => e.name === core.id)!;
    guesses.set(core.id, m);
  }
  return { cores: [...SEED_CORES, ...arcadeCores], guesses };
}

// Assigns the complete local library by default: every core (console,
// computer, and the generated arcade entries) that doesn't already have
// saved art gets its guessed match saved automatically. Idempotent -
// only touches cores with nothing saved yet, so repeat boots are cheap
// once everything's been seeded once.
async function autoSeedLibrary(guesses: Map<string, ImportEntry>) {
  const toSeed = allCores.filter((core) => !artCache.has(core.id) && guesses.has(core.id));
  if (toSeed.length === 0) return;

  statusText.textContent = `Assigning ${toSeed.length} matched marquees…`;
  await Promise.all(
    toSeed.map(async (core) => {
      const match = guesses.get(core.id)!;
      const res = await fetch(entryUrl(match));
      const blob = await res.blob();
      await saveCoreArt(core.id, blob);
      artCache.set(core.id, { coreId: core.id, jpegBlob: blob, updatedAt: Date.now() });
    }),
  );
  renderLibrary();
  statusText.textContent = `Assigned ${toSeed.length} marquees from the local library.`;
}

// --- Boot ----------------------------------------------------------------

(async () => {
  const all = await loadAllCoreArt();
  for (const [id, rec] of all) artCache.set(id, rec);
  renderLibrary(); // first paint with saved art only, so the UI isn't empty while the pack index loads

  try {
    importIndex = await loadImportIndex();
    const built = buildAllCores(importIndex);
    allCores = built.cores;
    renderLibrary(); // re-paint with the full catalog (console/computer + arcade) and auto-matched thumbnails
    await autoSeedLibrary(built.guesses);
  } catch (err) {
    console.error("Failed to load local pack index:", err);
  }

  editorPanel.innerHTML = `<div class="empty-state">Select a core on the left to assign or preview its marquee.</div>`;
})();
