import type { DirEntry } from "../api/client";
import { formatSize, joinPath } from "../lib/format";
import { Modal } from "./Modal";

interface PropertiesDialogProps {
  entry: DirEntry;
  path: string;
  onClose: () => void;
}

function Row({ label, value, mono }: { label: string; value: string; mono?: boolean }) {
  return (
    <div className="grid grid-cols-[7.5rem_1fr] gap-3 border-b border-abyss-600 py-2 text-sm last:border-0">
      <span className="text-xs text-mist-500">{label}</span>
      <span className={`min-w-0 break-all text-mist-100 ${mono ? "font-mono text-xs" : ""}`}>{value || "—"}</span>
    </div>
  );
}

export function PropertiesDialog({ entry, path, onClose }: PropertiesDialogProps) {
  return (
    <Modal
      title="Properties"
      subtitle={entry.name}
      onClose={onClose}
      footer={
        <button type="button" onClick={onClose} className="rounded-lg bg-white/5 px-3 py-1.5 text-xs font-medium text-mist-200 hover:bg-white/10">
          Close
        </button>
      }
    >
      <div className="-my-1">
        <Row label="Name" value={entry.name} />
        <Row label="Location" value={joinPath(path, entry.name)} mono />
        <Row label="Type" value={entry.type === "dir" ? "Folder" : "File"} />
        <Row label="Size" value={entry.type === "dir" ? "" : `${formatSize(entry.size)} (${Number(entry.size || 0).toLocaleString()} bytes)`} />
        <Row label="Modified" value={entry.date} />
        <Row label="Rights" value={entry.rights} mono />
      </div>
    </Modal>
  );
}
