import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import type { MouseEvent as ReactMouseEvent } from "react";

import * as api from "./api/client";
import type { DirEntry } from "./api/client";
import { EditorDialog } from "./components/EditorDialog";
import { FavoritesDialog } from "./components/FavoritesDialog";
import { FileTable } from "./components/FileTable";
import type { Sort, SortKey } from "./components/FileTable";
import { Icon } from "./components/Icon";
import type { IconName } from "./components/Icon";
import { ImageViewer } from "./components/ImageViewer";
import { Modal } from "./components/Modal";
import { PackageInfoDialog } from "./components/PackageInfoDialog";
import { PropertiesDialog } from "./components/PropertiesDialog";
import { ServersDialog } from "./components/ServersDialog";
import { SettingsDialog } from "./components/SettingsDialog";
import { Toasts } from "./components/Toasts";
import type { Toast, ToastTone } from "./components/Toasts";
import { breadcrumbs, formatSize, isArchive, isPackage, joinPath } from "./lib/format";

const DEFAULT_PATH = "/data/homebrew/ezremote-client";
const LAST_PATH_KEY = "ezrc:lastPath";
const TEXT_FILE = /\.(txt|log|ini|json|xml|html|conf|config|md|js|css|yml|yaml)$/i;
const IMAGE_FILE = /\.(bmp|jpg|jpeg|png|webp)$/i;

type Pane = "local" | "remote";

interface MenuItem {
  label?: string;
  icon?: IconName;
  onSelect?: () => void;
  disabled?: boolean;
  danger?: boolean;
  separator?: boolean;
}

interface MenuState {
  pane: Pane;
  entry: DirEntry | null;
  x: number;
  y: number;
}

interface PromptState {
  title: string;
  label: string;
  initial: string;
  confirmLabel: string;
  onSubmit: (value: string) => void;
}

interface ConfirmState {
  title: string;
  message: string;
  confirmLabel: string;
  onConfirm: () => void;
}

type UrlMode = "install" | "download";

function sortEntries(entries: DirEntry[], filter: string, sort: Sort): DirEntry[] {
  const needle = filter.trim().toLowerCase();
  const filtered = needle ? entries.filter((entry) => entry.name.toLowerCase().includes(needle)) : entries.slice();
  const direction = sort.dir === "asc" ? 1 : -1;
  filtered.sort((a, b) => {
    if (a.type !== b.type) return a.type === "dir" ? -1 : 1;
    if (sort.key === "size") return (Number(a.size || 0) - Number(b.size || 0)) * direction;
    if (sort.key === "date") return a.date.localeCompare(b.date) * direction;
    return a.name.localeCompare(b.name, undefined, { sensitivity: "base" }) * direction;
  });
  return filtered;
}

export default function App() {
  const [path, setPath] = useState(() => localStorage.getItem(LAST_PATH_KEY) || DEFAULT_PATH);
  const [entries, setEntries] = useState<DirEntry[]>([]);
  const [loading, setLoading] = useState(true);
  const [selection, setSelection] = useState<Set<string>>(new Set());
  const [sort, setSort] = useState<Sort>({ key: "name", dir: "asc" });
  const [filter, setFilter] = useState("");
  const [reloadKey, setReloadKey] = useState(0);

  const [remotePath, setRemotePath] = useState("");
  const [remoteSite, setRemoteSite] = useState<string | null>(null);
  const [remoteEntries, setRemoteEntries] = useState<DirEntry[]>([]);
  const [remoteSelection, setRemoteSelection] = useState<Set<string>>(new Set());
  const [remoteLoading, setRemoteLoading] = useState(false);
  const [remoteError, setRemoteError] = useState<string | null>(null);
  const [remoteSort, setRemoteSort] = useState<Sort>({ key: "name", dir: "asc" });
  const [remoteFilter, setRemoteFilter] = useState("");

  const [activePane, setActivePane] = useState<Pane>("local");
  const [remoteEnabled, setRemoteEnabled] = useState(true);
  const [busy, setBusy] = useState<string | null>(null);
  const [menu, setMenu] = useState<MenuState | null>(null);
  const [clipboard, setClipboard] = useState<{ mode: "copy" | "cut"; items: string[] } | null>(null);
  const [toasts, setToasts] = useState<Toast[]>([]);
  const [promptState, setPromptState] = useState<PromptState | null>(null);
  const [confirmState, setConfirmState] = useState<ConfirmState | null>(null);
  const [urlMode, setUrlMode] = useState<UrlMode | null>(null);
  const [urlPrefill, setUrlPrefill] = useState("");
  const [serversOpen, setServersOpen] = useState(false);
  const [favoritesOpen, setFavoritesOpen] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [editor, setEditor] = useState<{ path: string; name: string } | null>(null);
  const [properties, setProperties] = useState<DirEntry | null>(null);
  const [pkgView, setPkgView] = useState<{ path: string; name: string } | null>(null);
  const [viewer, setViewer] = useState<{ src: string; name: string } | null>(null);
  const [serverInfo, setServerInfo] = useState<api.ServerList | null>(null);

  const uploadRef = useRef<HTMLInputElement>(null);
  const remoteUploadRef = useRef<HTMLInputElement>(null);
  const toastId = useRef(0);

  const notify = useCallback((message: string, tone: ToastTone = "info") => {
    const id = ++toastId.current;
    setToasts((current) => [...current, { id, tone, message }]);
    window.setTimeout(() => setToasts((current) => current.filter((toast) => toast.id !== id)), 4500);
  }, []);

  const dismissToast = useCallback((id: number) => setToasts((current) => current.filter((toast) => toast.id !== id)), []);
  const refresh = useCallback(() => setReloadKey((key) => key + 1), []);

  const loadServerInfo = useCallback(async (): Promise<api.ServerList | null> => {
    try {
      const data = await api.listServers();
      setServerInfo(data);
      return data;
    } catch {
      return null;
    }
  }, []);

  const loadRemote = useCallback(
    async (target: string, siteOverride?: string) => {
      const activeSite = siteOverride ?? serverInfo?.active;
      if (!activeSite) return;
      setRemoteLoading(true);
      setRemoteError(null);
      try {
        const data = await api.listRemote(activeSite, target);
        setRemoteEntries(data.entries);
        setRemotePath(data.path);
        setRemoteSite(activeSite);
        setRemoteSelection(new Set());
      } catch (error) {
        setRemoteEntries([]);
        setRemoteError(error instanceof Error ? error.message : "Cannot reach this server");
      } finally {
        setRemoteLoading(false);
      }
    },
    [serverInfo],
  );

  const resetRemotePane = useCallback(() => {
    setRemoteSite(null);
    setRemotePath("");
    setRemoteEntries([]);
    setRemoteSelection(new Set());
    setRemoteError(null);
  }, []);

  useEffect(() => {
    void loadServerInfo();
  }, [loadServerInfo]);

  // Connect the remote pane once a profile is known (and only once per failure).
  useEffect(() => {
    if (remoteEnabled && serverInfo && !remotePath && !remoteError && !remoteLoading) void loadRemote("");
  }, [remoteEnabled, serverInfo, remotePath, remoteError, remoteLoading, loadRemote]);

  // A different active site invalidates the remote pane: clear the old listing
  // and list the new site's default directory (empty path = server default).
  useEffect(() => {
    if (!serverInfo || remoteSite === null || remoteSite === serverInfo.active) return;
    setRemoteEntries([]);
    setRemoteSelection(new Set());
    void loadRemote("", serverInfo.active);
  }, [serverInfo, remoteSite, loadRemote]);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    api
      .list(path)
      .then((data) => {
        if (cancelled) return;
        setEntries(data);
        setSelection(new Set());
        localStorage.setItem(LAST_PATH_KEY, path);
      })
      .catch((error: unknown) => {
        if (cancelled) return;
        setEntries([]);
        notify(error instanceof Error ? error.message : "Cannot read this folder", "error");
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, [path, reloadKey, notify]);

  const activeProfile = useMemo(
    () => serverInfo?.sites.find((site) => site.name === serverInfo.active),
    [serverInfo],
  );

  const localVisible = useMemo(() => sortEntries(entries, filter, sort), [entries, filter, sort]);
  const remoteVisible = useMemo(() => sortEntries(remoteEntries, remoteFilter, remoteSort), [remoteEntries, remoteFilter, remoteSort]);
  const localSelected = useMemo(() => entries.filter((entry) => selection.has(entry.name)), [entries, selection]);
  const remoteSelected = useMemo(() => remoteEntries.filter((entry) => remoteSelection.has(entry.name)), [remoteEntries, remoteSelection]);
  const localBytes = useMemo(() => localSelected.reduce((total, entry) => total + Number(entry.size || 0), 0), [localSelected]);
  const remoteBytes = useMemo(() => remoteSelected.reduce((total, entry) => total + Number(entry.size || 0), 0), [remoteSelected]);

  const toggle = (setter: (fn: (prev: Set<string>) => Set<string>) => void) => (name: string, additive: boolean) => {
    setter((current) => {
      if (additive) {
        const next = new Set(current);
        if (next.has(name)) next.delete(name);
        else next.add(name);
        return next;
      }
      return current.size === 1 && current.has(name) ? new Set() : new Set([name]);
    });
  };

  const toggleAll = (setter: (fn: (prev: Set<string>) => Set<string>) => void, visible: DirEntry[]) => () => {
    setter((current) =>
      visible.length > 0 && visible.every((entry) => current.has(entry.name)) ? new Set() : new Set(visible.map((entry) => entry.name)),
    );
  };

  const changeSort = (setter: (fn: (prev: Sort) => Sort) => void) => (key: SortKey) => {
    setter((current) => ({ key, dir: current.key === key && current.dir === "asc" ? "desc" : "asc" }));
  };

  const run = useCallback(
    async (label: string, action: () => Promise<void>) => {
      setBusy(label);
      try {
        await action();
        refresh();
      } catch (error) {
        notify(error instanceof Error ? error.message : "Operation failed", "error");
      } finally {
        setBusy(null);
      }
    },
    [notify, refresh],
  );

  const runRemote = useCallback(
    async (label: string, action: () => Promise<void>) => {
      setBusy(label);
      try {
        await action();
        await loadRemote(remotePath);
      } catch (error) {
        notify(error instanceof Error ? error.message : "Operation failed", "error");
      } finally {
        setBusy(null);
      }
    },
    [loadRemote, notify, remotePath],
  );

  const crossRun = useCallback(
    async (label: string, action: () => Promise<void>) => {
      setBusy(label);
      try {
        await action();
        refresh();
        await loadRemote(remotePath);
      } catch (error) {
        notify(error instanceof Error ? error.message : "Transfer failed", "error");
      } finally {
        setBusy(null);
      }
    },
    [loadRemote, notify, refresh, remotePath],
  );

  /* ---------- local actions ---------- */

  const newFolder = () =>
    setPromptState({
      title: "New folder",
      label: "Folder name",
      initial: "",
      confirmLabel: "Create",
      onSubmit: (value) => {
        const name = value.trim();
        if (!name) return;
        void run("Creating folder…", async () => {
          await api.createFolder(joinPath(path, name));
          notify(`Created ${name}`, "success");
        });
      },
    });

  const renameSelection = () => {
    if (localSelected.length !== 1) {
      notify("Select exactly one item to rename", "error");
      return;
    }
    const entry = localSelected[0];
    setPromptState({
      title: "Rename",
      label: "New name",
      initial: entry.name,
      confirmLabel: "Rename",
      onSubmit: (value) => {
        const name = value.trim();
        if (!name || name === entry.name) return;
        void run("Renaming…", async () => {
          await api.rename(joinPath(path, entry.name), joinPath(path, name));
          notify(`Renamed to ${name}`, "success");
        });
      },
    });
  };

  const deleteSelection = () => {
    if (localSelected.length === 0) return;
    setConfirmState({
      title: "Delete",
      message: `Permanently delete ${localSelected.length} item${localSelected.length > 1 ? "s" : ""}? This cannot be undone.`,
      confirmLabel: "Delete",
      onConfirm: () =>
        run("Deleting…", async () => {
          await api.remove(localSelected.map((entry) => joinPath(path, entry.name)));
          notify("Deleted", "success");
        }),
    });
  };

  const downloadSelection = () => {
    if (localSelected.length === 0) {
      notify("Select something to download", "error");
      return;
    }
    if (localSelected.length === 1) {
      api.triggerDownload(api.downloadFileUrl(joinPath(path, localSelected[0].name)));
      return;
    }
    api.triggerDownload(api.downloadMultipleUrl(localSelected.map((entry) => joinPath(path, entry.name)), "ezremote-client.zip"));
  };

  const installSelection = () => {
    const packages = localSelected.filter((entry) => isPackage(entry.name)).map((entry) => joinPath(path, entry.name));
    if (packages.length === 0) {
      notify("Select a .pkg file to install", "error");
      return;
    }
    void run(`Installing ${packages.length} package${packages.length > 1 ? "s" : ""}…`, async () => {
      await api.install(packages);
      notify("Install finished", "success");
    });
  };

  const extractSelection = () => {
    if (localSelected.length !== 1 || !isArchive(localSelected[0].name)) {
      notify("Select one .zip, .rar or .7z archive to extract", "error");
      return;
    }
    const entry = localSelected[0];
    setPromptState({
      title: "Extract archive",
      label: "Destination folder",
      initial: entry.name.replace(/\.[^.]+$/, ""),
      confirmLabel: "Extract",
      onSubmit: (value) => {
        const folder = value.trim();
        if (!folder) return;
        void run("Extracting…", async () => {
          await api.extract(joinPath(path, entry.name), path, folder);
          notify(`Extracted to ${folder}`, "success");
        });
      },
    });
  };

  const compressSelection = () => {
    if (localSelected.length === 0) {
      notify("Select something to compress", "error");
      return;
    }
    setPromptState({
      title: "Create archive",
      label: "Archive name",
      initial: "archive.zip",
      confirmLabel: "Compress",
      onSubmit: (value) => {
        const name = value.trim();
        if (!name) return;
        void run("Compressing…", async () => {
          await api.compress(localSelected.map((entry) => joinPath(path, entry.name)), path, name);
          notify(`Created ${name}`, "success");
        });
      },
    });
  };

  const editSelection = () => {
    if (localSelected.length !== 1) {
      notify("Select one file to edit", "error");
      return;
    }
    const entry = localSelected[0];
    if (entry.type === "dir") {
      notify("A folder cannot be opened in the text editor", "error");
      return;
    }
    if (!TEXT_FILE.test(entry.name)) {
      notify("Only text files can be opened in the editor", "error");
      return;
    }
    setEditor({ path: joinPath(path, entry.name), name: entry.name });
  };

  const openPackageInfo = () => {
    if (localSelected.length !== 1 || !isPackage(localSelected[0].name)) {
      notify("Select a .pkg file", "error");
      return;
    }
    setPkgView({ path: joinPath(path, localSelected[0].name), name: localSelected[0].name });
  };

  const copySelection = () => {
    if (localSelected.length === 0) return;
    setClipboard({ mode: "copy", items: localSelected.map((entry) => joinPath(path, entry.name)) });
    notify(`Copied ${localSelected.length} item${localSelected.length > 1 ? "s" : ""}`, "info");
  };

  const cutSelection = () => {
    if (localSelected.length === 0) return;
    setClipboard({ mode: "cut", items: localSelected.map((entry) => joinPath(path, entry.name)) });
    notify(`Cut ${localSelected.length} item${localSelected.length > 1 ? "s" : ""}`, "info");
  };

  const pasteClipboard = () => {
    if (!clipboard) return;
    const { mode, items } = clipboard;
    void run(mode === "copy" ? "Copying…" : "Moving…", async () => {
      if (mode === "copy") await api.copy(items, path);
      else await api.move(items, path);
      if (mode === "cut") setClipboard(null);
      notify(`Pasted ${items.length} item${items.length > 1 ? "s" : ""}`, "success");
    });
  };

  const uploadFiles = async (files: FileList) => {
    const chunkSize = 4 * 1024 * 1024;
    let failed = false;
    for (const file of Array.from(files)) {
      setBusy(`Uploading ${file.name}…`);
      try {
        const chunks = Math.max(1, Math.ceil(file.size / chunkSize));
        for (let index = 0; index < chunks; index += 1) {
          const start = index * chunkSize;
          const form = new FormData();
          form.append("destination", path);
          form.append("_chunkSize", String(chunkSize));
          form.append("_chunkNumber", String(index));
          form.append("_totalSize", String(file.size));
          form.append("file", file.slice(start, Math.min(start + chunkSize, file.size)), file.name);
          const response = await fetch("/__local__/upload", { method: "POST", body: form });
          if (!response.ok) throw new Error(`Upload failed (HTTP ${response.status})`);
        }
        notify(`Uploaded ${file.name}`, "success");
      } catch (error) {
        failed = true;
        notify(error instanceof Error ? error.message : "Upload failed", "error");
      } finally {
        setBusy(null);
      }
    }
    if (!failed) refresh();
  };

  /* ---------- remote actions ---------- */

  const remoteDownload = () => {
    if (!serverInfo) {
      notify("Select a server profile first", "error");
      return;
    }
    if (remoteSelected.length !== 1 || remoteSelected[0].type !== "file") {
      notify("Select one file to download", "error");
      return;
    }
    api.triggerDownload(api.remoteDownloadUrl(serverInfo.active, joinPath(remotePath, remoteSelected[0].name)));
  };

  const remoteNewFolder = () => {
    if (!serverInfo) return;
    setPromptState({
      title: "New remote folder",
      label: "Folder name",
      initial: "",
      confirmLabel: "Create",
      onSubmit: (value) => {
        const name = value.trim();
        if (!name) return;
        void runRemote("Creating folder…", () => api.remoteMkdir(serverInfo.active, joinPath(remotePath, name)));
      },
    });
  };

  const remoteRename = () => {
    if (!serverInfo) return;
    if (remoteSelected.length !== 1) {
      notify("Select exactly one item to rename", "error");
      return;
    }
    const entry = remoteSelected[0];
    setPromptState({
      title: "Rename on server",
      label: "New name",
      initial: entry.name,
      confirmLabel: "Rename",
      onSubmit: (value) => {
        const name = value.trim();
        if (!name || name === entry.name) return;
        void runRemote("Renaming…", () =>
          api.remoteRename(serverInfo.active, joinPath(remotePath, entry.name), joinPath(remotePath, name)),
        );
      },
    });
  };

  const remoteDelete = () => {
    if (!serverInfo) return;
    if (remoteSelected.length === 0) {
      notify("Select something to delete", "error");
      return;
    }
    setConfirmState({
      title: "Delete on server",
      message: `Permanently delete ${remoteSelected.length} remote item${remoteSelected.length > 1 ? "s" : ""}? This cannot be undone.`,
      confirmLabel: "Delete",
      onConfirm: () => {
        const files = remoteSelected.filter((entry) => entry.type !== "dir").map((entry) => joinPath(remotePath, entry.name));
        const dirs = remoteSelected.filter((entry) => entry.type === "dir").map((entry) => joinPath(remotePath, entry.name));
        void runRemote("Deleting…", () => api.remoteRemove(serverInfo.active, files, dirs));
      },
    });
  };

  const uploadRemoteFiles = async (files: FileList) => {
    if (!serverInfo) {
      notify("Select a server profile first", "error");
      return;
    }
    const chunkSize = 4 * 1024 * 1024;
    for (const file of Array.from(files)) {
      setBusy(`Uploading ${file.name}…`);
      try {
        const uploadId = `${Date.now()}-${Math.random().toString(36).slice(2, 10)}`;
        const chunks = Math.max(1, Math.ceil(file.size / chunkSize));
        for (let index = 0; index < chunks; index += 1) {
          const start = index * chunkSize;
          const form = new FormData();
          form.append("site", serverInfo.active);
          form.append("path", remotePath);
          form.append("_uploadId", uploadId);
          form.append("_chunkSize", String(chunkSize));
          form.append("_chunkNumber", String(index));
          form.append("_totalSize", String(file.size));
          form.append("file", file.slice(start, Math.min(start + chunkSize, file.size)), file.name);
          await api.uploadRemoteChunk(form);
        }
        notify(`Uploaded ${file.name}`, "success");
      } catch (error) {
        notify(error instanceof Error ? error.message : "Upload failed", "error");
      } finally {
        setBusy(null);
      }
    }
    await loadRemote(remotePath);
  };

  const disconnectRemote = async () => {
    if (!serverInfo) return;
    try {
      await api.disconnectRemote(serverInfo.active);
      setRemoteEnabled(false);
      resetRemotePane();
      notify("Disconnected", "info");
    } catch (error) {
      notify(error instanceof Error ? error.message : "Disconnect failed", "error");
    }
  };

  /* ---------- cross-pane ---------- */

  const localToRemote = (move: boolean) => {
    if (!serverInfo) {
      notify("Select a server profile first", "error");
      return;
    }
    const items = localSelected.map((entry) => joinPath(path, entry.name));
    if (items.length === 0) return;
    void crossRun(move ? "Moving to remote…" : "Copying to remote…", async () => {
      await api.remoteCopyFromLocal(serverInfo.active, items, remotePath);
      if (move) await api.remove(items);
    });
  };

  const remoteToLocal = (move: boolean) => {
    if (!serverInfo) {
      notify("Select a server profile first", "error");
      return;
    }
    const items = remoteSelected.map((entry) => joinPath(remotePath, entry.name));
    if (items.length === 0) return;
    void crossRun(move ? "Moving to local…" : "Copying to local…", async () => {
      await api.copyFromRemote(serverInfo.active, items, path);
      if (move) await api.remoteRemove(serverInfo.active, items, []);
    });
  };

  /* ---------- menus ---------- */

  const openMenu = (pane: Pane, entry: DirEntry | null, event: ReactMouseEvent) => {
    event.preventDefault();
    setActivePane(pane);
    if (entry) {
      if (pane === "local" && !selection.has(entry.name)) setSelection(new Set([entry.name]));
      if (pane === "remote" && !remoteSelection.has(entry.name)) setRemoteSelection(new Set([entry.name]));
    }
    setMenu({ pane, entry, x: event.clientX, y: event.clientY });
  };

  const localMenu = (entry: DirEntry | null): MenuItem[] => {
    const items: MenuItem[] = [];
    if (entry) {
      if (entry.type === "dir") items.push({ label: "Open", icon: "folder", onSelect: () => setPath(joinPath(path, entry.name)) });
      else {
        items.push({ label: "Download", icon: "download", onSelect: downloadSelection });
        if (IMAGE_FILE.test(entry.name))
          items.push({ label: "Preview image", icon: "image", onSelect: () => setViewer({ src: api.downloadFileUrl(joinPath(path, entry.name)), name: entry.name }) });
        if (isPackage(entry.name)) {
          items.push({ label: "Install", icon: "install", onSelect: installSelection });
          items.push({ label: "Package info", icon: "package", onSelect: openPackageInfo });
        }
        if (isArchive(entry.name)) items.push({ label: "Extract", icon: "archive", onSelect: extractSelection });
        if (TEXT_FILE.test(entry.name)) items.push({ label: "Edit", icon: "text", onSelect: editSelection });
      }
      items.push({ label: "Compress to zip", icon: "package", onSelect: compressSelection });
      items.push({ separator: true });
      items.push({ label: "Copy to remote", icon: "copy", onSelect: () => localToRemote(false), disabled: !serverInfo || !remotePath });
      items.push({ label: "Move to remote", icon: "cut", onSelect: () => localToRemote(true), disabled: !serverInfo || !remotePath });
      items.push({ separator: true });
      items.push({ label: "Cut", icon: "cut", onSelect: cutSelection });
      items.push({ label: "Copy", icon: "copy", onSelect: copySelection });
      items.push({ separator: true });
      items.push({ label: "Rename", icon: "rename", onSelect: renameSelection });
      items.push({ label: "Delete", icon: "trash", danger: true, onSelect: deleteSelection });
      items.push({ label: "Properties", icon: "info", onSelect: () => setProperties(localSelected[0] ?? entry) });
      return items;
    }

    items.push({ label: "New folder", icon: "folderPlus", onSelect: newFolder });
    items.push({ label: "Upload", icon: "upload", onSelect: () => uploadRef.current?.click() });
    if (clipboard) items.push({ label: "Paste", icon: "paste", onSelect: pasteClipboard });
    items.push({ label: "Refresh", icon: "refresh", onSelect: refresh });
    items.push({ label: "Select all", icon: "selectAll", onSelect: toggleAll(setSelection, localVisible) });
    items.push({ separator: true });
    items.push({ label: "Install from URL", icon: "link", onSelect: () => { setUrlPrefill(""); setUrlMode("install"); } });
    items.push({ label: "Download from URL", icon: "link", onSelect: () => { setUrlPrefill(""); setUrlMode("download"); } });
    items.push({ label: "Favourites", icon: "star", onSelect: () => setFavoritesOpen(true) });
    return items;
  };

  const remoteMenu = (entry: DirEntry | null): MenuItem[] => {
    const items: MenuItem[] = [];
    if (entry) {
      if (entry.type === "dir") items.push({ label: "Open", icon: "folder", onSelect: () => void loadRemote(joinPath(remotePath, entry.name)) });
      else {
        items.push({ label: "Download", icon: "download", onSelect: remoteDownload });
        if (IMAGE_FILE.test(entry.name) && serverInfo)
          items.push({
            label: "Preview image",
            icon: "image",
            onSelect: () => setViewer({ src: api.remoteDownloadUrl(serverInfo.active, joinPath(remotePath, entry.name)), name: entry.name }),
          });
      }
      items.push({ separator: true });
      items.push({ label: "Copy to local", icon: "copy", onSelect: () => remoteToLocal(false), disabled: !serverInfo });
      items.push({ label: "Move to local", icon: "cut", onSelect: () => remoteToLocal(true), disabled: !serverInfo });
      items.push({ separator: true });
      items.push({ label: "Rename", icon: "rename", onSelect: remoteRename });
      items.push({ label: "Delete", icon: "trash", danger: true, onSelect: remoteDelete });
      items.push({ label: "Properties", icon: "info", onSelect: () => setProperties(remoteSelected[0] ?? entry) });
      return items;
    }

    items.push({ label: "New folder", icon: "folderPlus", onSelect: remoteNewFolder, disabled: !serverInfo });
    items.push({ label: "Upload", icon: "upload", onSelect: () => remoteUploadRef.current?.click(), disabled: !serverInfo });
    items.push({ label: "Refresh", icon: "refresh", onSelect: () => void loadRemote(remotePath), disabled: !serverInfo });
    items.push({ label: "Select all", icon: "selectAll", onSelect: toggleAll(setRemoteSelection, remoteVisible) });
    items.push({ separator: true });
    items.push({ label: "Disconnect", icon: "server", onSelect: () => void disconnectRemote(), disabled: !serverInfo });
    return items;
  };

  return (
    <div className="flex h-full flex-col">
      <header className="border-b border-abyss-600 bg-abyss-900">
        <div className="flex items-center gap-3 px-4 py-2.5">
          <div className="flex items-center gap-2">
            <span className="grid size-8 place-items-center rounded-xl bg-gradient-to-br from-accent-400 to-accent-600 text-abyss-950 shadow-lg shadow-accent-500/20">
              <Icon name="home" size={16} />
            </span>
            <div className="min-w-0 leading-tight">
              <p className="text-sm font-semibold tracking-tight">ezRemote Client</p>
              <p className="max-w-[60vw] truncate text-[13px] font-medium text-mist-300" title={activeProfile?.server ?? ""}>
                {serverInfo
                  ? `${serverInfo.active} · ${serverInfo.clientTypes[activeProfile?.type ?? -1] ?? "unset"}${
                      activeProfile?.server ? ` · ${activeProfile.server}` : ""
                    }`
                  : "no profile"}
              </p>
            </div>
          </div>
          <div className="ml-auto flex items-center gap-1.5">
            <HeaderButton icon="star" label="Favourites" onClick={() => setFavoritesOpen(true)} />
            <HeaderButton icon="server" label="Servers" onClick={() => setServersOpen(true)} />
            <HeaderButton icon="settings" label="Settings" onClick={() => setSettingsOpen(true)} />
          </div>
        </div>
      </header>

      <main className="grid min-h-0 flex-1 gap-2.5 p-2.5 lg:grid-cols-2">
        <Panel
          label="Local"
          path={path}
          entries={localVisible}
          selected={selection}
          loading={loading}
          sort={sort}
          filter={filter}
          active={activePane === "local"}
          onActivate={() => setActivePane("local")}
          onFilter={setFilter}
          onSort={changeSort(setSort)}
          onToggle={toggle(setSelection)}
          onToggleAll={toggleAll(setSelection, localVisible)}
          onOpen={(entry) => (entry.type === "dir" ? setPath(joinPath(path, entry.name)) : undefined)}
          onContext={(entry, event) => openMenu("local", entry, event)}
          onNavigate={setPath}
          onRefresh={refresh}
        />
        <Panel
          label="Remote"
          path={remotePath}
          entries={remoteVisible}
          selected={remoteSelection}
          loading={remoteLoading}
          error={remoteError}
          sort={remoteSort}
          filter={remoteFilter}
          active={activePane === "remote"}
          onActivate={() => setActivePane("remote")}
          onFilter={setRemoteFilter}
          onSort={changeSort(setRemoteSort)}
          onToggle={toggle(setRemoteSelection)}
          onToggleAll={toggleAll(setRemoteSelection, remoteVisible)}
          onOpen={(entry) => (entry.type === "dir" ? void loadRemote(joinPath(remotePath, entry.name)) : undefined)}
          onContext={(entry, event) => openMenu("remote", entry, event)}
          onNavigate={(target) => void loadRemote(target)}
          onRefresh={() => { setRemoteEnabled(true); void loadRemote(remotePath); }}
          onRetry={() => { setRemoteEnabled(true); void loadRemote(""); }}
        />
      </main>

      <footer className="flex flex-wrap items-center gap-x-4 gap-y-1 border-t border-abyss-600 px-4 py-1.5 text-[11px] text-mist-500">
        <span className="font-medium text-mist-300">{activePane === "local" ? "Local" : "Remote"}</span>
        <span>{activePane === "local" ? localSelected.length : remoteSelected.length} selected</span>
        <span>{formatSize(String(activePane === "local" ? localBytes : remoteBytes))}</span>
        <span className="truncate font-mono text-mist-500/70">{activePane === "local" ? path : remotePath || "not connected"}</span>
        {busy ? (
          <span className="ml-auto flex items-center gap-2 text-accent-300">
            <span className="size-3 animate-spin rounded-full border-2 border-accent-500/30 border-t-accent-400" />
            {busy}
          </span>
        ) : null}
      </footer>

      <input ref={uploadRef} type="file" multiple hidden onChange={(event) => { if (event.target.files?.length) void uploadFiles(event.target.files); event.target.value = ""; }} />
      <input ref={remoteUploadRef} type="file" multiple hidden onChange={(event) => { if (event.target.files?.length) void uploadRemoteFiles(event.target.files); event.target.value = ""; }} />

      {menu ? (
        <ContextMenu
          x={menu.x}
          y={menu.y}
          items={menu.pane === "local" ? localMenu(menu.entry) : remoteMenu(menu.entry)}
          onClose={() => setMenu(null)}
        />
      ) : null}

      {promptState ? <PromptDialog state={promptState} onClose={() => setPromptState(null)} /> : null}
      {confirmState ? <ConfirmDialog state={confirmState} onClose={() => setConfirmState(null)} /> : null}
      {urlMode ? (
        <UrlDialog
          mode={urlMode}
          initialUrl={urlPrefill}
          defaultDest={path}
          onClose={() => setUrlMode(null)}
          onSubmit={async (values) => {
            if (urlMode === "install") {
              await api.installFromUrl({
                url: values.url,
                use_alldebrid: values.alldebrid,
                use_realdebrid: values.realdebrid,
                use_disk_cache: values.diskCache,
                enable_rpi: values.rpi,
              });
              notify("Install queued", "success");
            } else {
              await api.downloadFromUrl({ url: values.url, dest: values.dest, use_alldebrid: values.alldebrid, use_realdebrid: values.realdebrid });
              notify("Download queued", "success");
            }
          }}
        />
      ) : null}
      {editor ? <EditorDialog item={editor.path} name={editor.name} onClose={() => setEditor(null)} onSaved={refresh} /> : null}
      {properties ? <PropertiesDialog entry={properties} path={activePane === "local" ? path : remotePath} onClose={() => setProperties(null)} /> : null}
      {pkgView ? (
        <PackageInfoDialog
          item={pkgView.path}
          name={pkgView.name}
          onClose={() => setPkgView(null)}
          onInstall={() => {
            const target = pkgView.path;
            setPkgView(null);
            void run("Installing package…", async () => {
              await api.install([target]);
              notify("Install finished", "success");
            });
          }}
        />
      ) : null}
      {viewer ? <ImageViewer src={viewer.src} name={viewer.name} onClose={() => setViewer(null)} /> : null}
      {favoritesOpen ? (
        <FavoritesDialog
          onClose={() => setFavoritesOpen(false)}
          onInstall={(url) => { setUrlPrefill(url); setUrlMode("install"); setFavoritesOpen(false); }}
          onDownload={(url) => { setUrlPrefill(url); setUrlMode("download"); setFavoritesOpen(false); }}
        />
      ) : null}
      {settingsOpen ? <SettingsDialog onClose={() => setSettingsOpen(false)} onSaved={() => void loadServerInfo()} /> : null}
      {serversOpen ? (
        <ServersDialog
          onClose={() => setServersOpen(false)}
          onChanged={() =>
            void (async () => {
              const updated = await loadServerInfo();
              if (!updated) return;
              setRemoteEnabled(true);
              // Drop the cached session so the new URL/credentials are used.
              try {
                await api.disconnectRemote(updated.active);
              } catch {
                // Not connected yet: nothing to drop.
              }
              setRemoteEntries([]);
              setRemoteSelection(new Set());
              // Empty path -> the site's defaultDirectory, then list it.
              await loadRemote("", updated.active);
            })()
          }
        />
      ) : null}
      <Toasts toasts={toasts} onDismiss={dismissToast} />
    </div>
  );
}

function HeaderButton({ icon, label, onClick }: { icon: IconName; label: string; onClick: () => void }) {
  return (
    <button
      type="button"
      onClick={onClick}
      className="flex items-center gap-1.5 rounded-lg border border-abyss-600 px-2.5 py-1.5 text-xs font-medium text-mist-300 transition hover:border-white/20 hover:bg-white/5 hover:text-mist-100"
    >
      <Icon name={icon} size={15} />
      <span className="hidden sm:inline">{label}</span>
    </button>
  );
}

function Panel({
  label,
  path,
  entries,
  selected,
  loading,
  error,
  sort,
  filter,
  active,
  onActivate,
  onFilter,
  onSort,
  onToggle,
  onToggleAll,
  onOpen,
  onContext,
  onNavigate,
  onRefresh,
  onRetry,
}: {
  label: string;
  path: string;
  entries: DirEntry[];
  selected: Set<string>;
  loading: boolean;
  error?: string | null;
  sort: Sort;
  filter: string;
  active: boolean;
  onActivate: () => void;
  onFilter: (value: string) => void;
  onSort: (key: SortKey) => void;
  onToggle: (name: string, additive: boolean) => void;
  onToggleAll: () => void;
  onOpen: (entry: DirEntry) => void;
  onContext: (entry: DirEntry | null, event: ReactMouseEvent) => void;
  onNavigate: (path: string) => void;
  onRefresh: () => void;
  onRetry?: () => void;
}) {
  const crumbs = breadcrumbs(path);

  return (
    <section
      onMouseDown={onActivate}
      className={`flex min-h-0 flex-col gap-2 rounded-2xl border p-2.5 transition ${
        active ? "border-accent-500 bg-abyss-900" : "border-abyss-600 bg-abyss-900"
      }`}
    >
      <div className="flex items-center gap-2">
        <span
          className={`rounded-md px-2 py-0.5 text-[10px] font-semibold uppercase tracking-wider ${
            active ? "bg-title text-white" : "bg-abyss-800 text-mist-500"
          }`}
        >
          {label}
        </span>
        <nav className="flex min-w-0 flex-1 flex-wrap items-center gap-0.5 text-xs">
          {crumbs.length === 0 ? <span className="truncate text-mist-500">not connected</span> : null}
          {crumbs.map((crumb, index) => (
            <span key={crumb.path} className="flex items-center gap-0.5">
              {index > 0 ? <Icon name="chevronRight" size={12} className="text-mist-500/60" /> : null}
              <button
                type="button"
                onClick={() => onNavigate(crumb.path)}
                className={`rounded px-1 py-0.5 transition hover:bg-white/5 hover:text-mist-100 ${
                  index === crumbs.length - 1 ? "font-medium text-mist-100" : "text-mist-500"
                }`}
              >
                {index === 0 ? <Icon name="home" size={13} /> : crumb.label}
              </button>
            </span>
          ))}
        </nav>
        <span className="text-[11px] text-mist-500">{entries.length}</span>
        <button
          type="button"
          onClick={onRefresh}
          title={`Refresh ${label} pane`}
          aria-label={`Refresh ${label} pane`}
          className="rounded-md p-1 text-mist-500 transition hover:bg-white/10 hover:text-mist-100"
        >
          <Icon name="refresh" size={14} />
        </button>
      </div>

      <input
        value={filter}
        onChange={(event) => onFilter(event.target.value)}
        placeholder="Filter…"
        className="w-full rounded-lg border border-abyss-600 bg-abyss-800 px-2.5 py-1 text-xs text-mist-100 placeholder:text-mist-500/70 focus:border-accent-500/60 focus:outline-none"
      />

      {error ? (
        <div className="flex items-center gap-2 rounded-lg border border-rose-400/30 bg-rose-400/10 px-2.5 py-1.5 text-[11px] text-rose-400">
          <Icon name="alert" size={14} className="shrink-0" />
          <span className="flex-1 truncate">{error}</span>
          {onRetry ? (
            <button type="button" onClick={onRetry} className="rounded border border-rose-400/40 px-2 py-0.5 font-medium hover:bg-rose-400/10">
              Connect
            </button>
          ) : null}
        </div>
      ) : null}

      <FileTable
        entries={entries}
        selected={selected}
        loading={loading}
        sort={sort}
        onSort={onSort}
        onToggle={onToggle}
        onToggleAll={onToggleAll}
        onOpen={onOpen}
        onContext={onContext}
      />
    </section>
  );
}

function ContextMenu({ x, y, items, onClose }: { x: number; y: number; items: MenuItem[]; onClose: () => void }) {
  useEffect(() => {
    const close = () => onClose();
    const onKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") onClose();
    };
    window.addEventListener("mousedown", close);
    window.addEventListener("keydown", onKey);
    return () => {
      window.removeEventListener("mousedown", close);
      window.removeEventListener("keydown", onKey);
    };
  }, [onClose]);

  const left = Math.min(x, window.innerWidth - 240);
  const top = Math.min(y, Math.max(8, window.innerHeight - 40 - items.length * 30));

  return (
    <div
      className="fixed z-50 w-56 overflow-y-auto rounded-xl border border-abyss-600 bg-abyss-900/95 p-1 shadow-2xl shadow-black/60 backdrop-blur"
      style={{ left, top, maxHeight: "70vh" }}
      onMouseDown={(event) => event.stopPropagation()}
      onContextMenu={(event) => event.preventDefault()}
    >
      {items.map((item, index) =>
        item.separator ? (
          <div key={index} className="my-1 h-px bg-white/5" />
        ) : (
          <button
            key={index}
            type="button"
            disabled={item.disabled}
            onClick={() => {
              onClose();
              item.onSelect?.();
            }}
            className={`flex w-full items-center gap-2 rounded-md px-2 py-1.5 text-left text-xs transition disabled:cursor-not-allowed disabled:opacity-40 ${
              item.danger ? "text-rose-400 hover:bg-rose-400/10" : "text-mist-300 hover:bg-white/5 hover:text-mist-100"
            }`}
          >
            {item.icon ? <Icon name={item.icon} size={14} /> : <span className="size-3.5" />}
            {item.label}
          </button>
        ),
      )}
    </div>
  );
}

function PromptDialog({ state, onClose }: { state: PromptState; onClose: () => void }) {
  const [value, setValue] = useState(state.initial);
  const submit = () => {
    onClose();
    state.onSubmit(value);
  };

  return (
    <Modal
      title={state.title}
      onClose={onClose}
      footer={
        <>
          <button type="button" onClick={onClose} className="rounded-lg px-3 py-1.5 text-xs font-medium text-mist-300 hover:bg-white/5">
            Cancel
          </button>
          <button type="button" onClick={submit} className="rounded-lg bg-accent-500 px-3 py-1.5 text-xs font-semibold text-white transition hover:bg-accent-600">
            {state.confirmLabel}
          </button>
        </>
      }
    >
      <label className="block text-xs font-medium text-mist-500">{state.label}</label>
      <input
        autoFocus
        value={value}
        onChange={(event) => setValue(event.target.value)}
        onKeyDown={(event) => {
          if (event.key === "Enter") submit();
        }}
        className="input mt-1.5"
      />
    </Modal>
  );
}

function ConfirmDialog({ state, onClose }: { state: ConfirmState; onClose: () => void }) {
  return (
    <Modal
      title={state.title}
      onClose={onClose}
      footer={
        <>
          <button type="button" onClick={onClose} className="rounded-lg px-3 py-1.5 text-xs font-medium text-mist-300 hover:bg-white/5">
            Cancel
          </button>
          <button
            type="button"
            onClick={() => {
              onClose();
              state.onConfirm();
            }}
            className="rounded-lg bg-rose-500/90 px-3 py-1.5 text-xs font-semibold text-white transition hover:bg-rose-500"
          >
            {state.confirmLabel}
          </button>
        </>
      }
    >
      <p className="text-sm leading-relaxed text-mist-300">{state.message}</p>
    </Modal>
  );
}

interface UrlValues {
  url: string;
  dest: string;
  alldebrid: boolean;
  realdebrid: boolean;
  diskCache: boolean;
  rpi: boolean;
}

function UrlDialog({
  mode,
  initialUrl,
  defaultDest,
  onClose,
  onSubmit,
}: {
  mode: UrlMode;
  initialUrl: string;
  defaultDest: string;
  onClose: () => void;
  onSubmit: (values: UrlValues) => Promise<void>;
}) {
  const [url, setUrl] = useState(initialUrl);
  const [dest, setDest] = useState(defaultDest);
  const [alldebrid, setAlldebrid] = useState(false);
  const [realdebrid, setRealdebrid] = useState(false);
  const [diskCache, setDiskCache] = useState(false);
  const [rpi, setRpi] = useState(false);
  const [working, setWorking] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const isInstall = mode === "install";

  const submit = async () => {
    if (!url.trim() || working) return;
    setWorking(true);
    setError(null);
    try {
      await onSubmit({ url: url.trim(), dest: dest.trim(), alldebrid, realdebrid, diskCache, rpi });
      onClose();
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : "Request failed");
    } finally {
      setWorking(false);
    }
  };

  return (
    <Modal
      title={isInstall ? "Install from URL" : "Download from URL"}
      subtitle={isInstall ? "Queue a package hosted on a direct link or file host" : "Download a file to the current folder"}
      onClose={onClose}
      footer={
        <>
          <button type="button" onClick={onClose} className="rounded-lg px-3 py-1.5 text-xs font-medium text-mist-300 hover:bg-white/5">
            Cancel
          </button>
          <button
            type="button"
            onClick={() => void submit()}
            disabled={!url.trim() || working}
            className="rounded-lg bg-accent-500 px-3 py-1.5 text-xs font-semibold text-white transition hover:bg-accent-600 disabled:opacity-40"
          >
            {working ? "Working…" : isInstall ? "Install" : "Download"}
          </button>
        </>
      }
    >
      <div className="space-y-3">
        <div>
          <label className="block text-xs font-medium text-mist-500">URL</label>
          <input autoFocus value={url} onChange={(event) => setUrl(event.target.value)} placeholder="https://…" className="input mt-1.5" />
        </div>
        {!isInstall ? (
          <div>
            <label className="block text-xs font-medium text-mist-500">Destination folder</label>
            <input value={dest} onChange={(event) => setDest(event.target.value)} className="input mt-1.5 font-mono text-xs" />
          </div>
        ) : null}
        <div className="flex flex-wrap gap-x-4 gap-y-2 pt-1">
          <Checkbox label="AllDebrid" checked={alldebrid} onChange={setAlldebrid} />
          <Checkbox label="RealDebrid" checked={realdebrid} onChange={setRealdebrid} />
          {isInstall ? <Checkbox label="Disk cache" checked={diskCache} onChange={setDiskCache} /> : null}
          {isInstall ? <Checkbox label="RPI" checked={rpi} onChange={setRpi} /> : null}
        </div>
        {error ? (
          <p className="flex items-start gap-2 rounded-lg border border-rose-400/30 bg-rose-400/10 px-3 py-2 text-xs text-rose-400">
            <Icon name="alert" size={14} className="mt-px shrink-0" />
            {error}
          </p>
        ) : null}
      </div>
    </Modal>
  );
}

function Checkbox({ label, checked, onChange }: { label: string; checked: boolean; onChange: (value: boolean) => void }) {
  return (
    <label className="flex cursor-pointer items-center gap-2 text-xs text-mist-300">
      <input type="checkbox" checked={checked} onChange={(event) => onChange(event.target.checked)} className="size-3.5 accent-accent-500" />
      {label}
    </label>
  );
}

