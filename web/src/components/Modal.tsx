import { useEffect, useRef } from "react";
import type { ReactNode } from "react";

import { Icon } from "./Icon";

interface ModalProps {
  title: string;
  subtitle?: string;
  onClose: () => void;
  children: ReactNode;
  footer?: ReactNode;
  width?: string;
}

export function Modal({ title, subtitle, onClose, children, footer, width = "max-w-lg" }: ModalProps) {
  const panelRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const previous = document.activeElement as HTMLElement | null;
    panelRef.current?.querySelector<HTMLElement>("input, button, select, textarea")?.focus();

    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        event.stopPropagation();
        onClose();
      }
    };
    document.addEventListener("keydown", onKeyDown);
    return () => {
      document.removeEventListener("keydown", onKeyDown);
      previous?.focus?.();
    };
  }, [onClose]);

  return (
    <div
      className="fixed inset-0 z-50 flex items-end justify-center bg-black/60 p-4 backdrop-blur-sm sm:items-center"
      onMouseDown={(event) => {
        if (event.target === event.currentTarget) onClose();
      }}
      role="dialog"
      aria-modal="true"
      aria-label={title}
    >
      <div
        ref={panelRef}
        className={`w-full ${width} overflow-hidden rounded-2xl border border-abyss-600 bg-abyss-850 shadow-2xl shadow-black/50`}
      >
        <header className="flex items-start justify-between gap-4 border-b border-abyss-600 px-5 py-4">
          <div>
            <h2 className="text-base font-semibold tracking-tight text-mist-100">{title}</h2>
            {subtitle ? <p className="mt-0.5 truncate text-xs text-mist-500">{subtitle}</p> : null}
          </div>
          <button
            type="button"
            onClick={onClose}
            className="rounded-lg p-1.5 text-mist-500 transition hover:bg-white/5 hover:text-mist-100"
            aria-label="Close"
          >
            <Icon name="close" size={18} />
          </button>
        </header>
        <div className="px-5 py-4">{children}</div>
        {footer ? <footer className="flex justify-end gap-2 border-t border-abyss-600 px-5 py-3.5">{footer}</footer> : null}
      </div>
    </div>
  );
}
