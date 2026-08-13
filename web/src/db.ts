// Browser-local persistence for per-core art assignments. No backend -
// the web app is a standalone previewer/library editor (see CLAUDE.md
// "Web app design"). Each record stores the already-exported JPEG blob so
// re-sending to the device (or re-rendering a thumbnail) needs no re-crop.

const DB_NAME = "tty2oled-gc9a01";
const DB_VERSION = 1;
const STORE = "coreArt";

export interface CoreArtRecord {
  coreId: string;
  jpegBlob: Blob;
  updatedAt: number;
}

function openDb(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, DB_VERSION);
    req.onupgradeneeded = () => {
      const db = req.result;
      if (!db.objectStoreNames.contains(STORE)) {
        db.createObjectStore(STORE, { keyPath: "coreId" });
      }
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

export async function saveCoreArt(coreId: string, jpegBlob: Blob): Promise<void> {
  const db = await openDb();
  await new Promise<void>((resolve, reject) => {
    const tx = db.transaction(STORE, "readwrite");
    tx.objectStore(STORE).put({ coreId, jpegBlob, updatedAt: Date.now() } satisfies CoreArtRecord);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
}

export async function loadCoreArt(coreId: string): Promise<CoreArtRecord | undefined> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE, "readonly");
    const req = tx.objectStore(STORE).get(coreId);
    req.onsuccess = () => resolve(req.result as CoreArtRecord | undefined);
    req.onerror = () => reject(req.error);
  });
}

export async function loadAllCoreArt(): Promise<Map<string, CoreArtRecord>> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE, "readonly");
    const req = tx.objectStore(STORE).getAll();
    req.onsuccess = () => {
      const map = new Map<string, CoreArtRecord>();
      for (const rec of req.result as CoreArtRecord[]) map.set(rec.coreId, rec);
      resolve(map);
    };
    req.onerror = () => reject(req.error);
  });
}

export async function deleteCoreArt(coreId: string): Promise<void> {
  const db = await openDb();
  await new Promise<void>((resolve, reject) => {
    const tx = db.transaction(STORE, "readwrite");
    tx.objectStore(STORE).delete(coreId);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
}
