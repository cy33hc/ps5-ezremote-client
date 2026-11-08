// Translations are generated from data/assets/langs/*.ini into langs.json, so
// the web UI never needs a backend call. Entries are looked up by their English
// text, which lets existing literals be wrapped with t("…") as they are touched.

import tables from "./langs.json";

type Table = Record<string, string>;

const languages = tables as unknown as Record<string, Table>;

let dictionary: Record<string, string> = {};
let currentLanguage = "English";

function normalise(text: string): string {
  return text.trim().replace(/\s+/g, " ").toLowerCase();
}

const CACHE_KEY = "ezremote.language";

/** Last language seen, so the sign-in screen can translate before any request. */
export function cachedLanguage(): string | null {
  try {
    return localStorage.getItem(CACHE_KEY);
  } catch {
    return null;
  }
}

function rememberLanguage(language: string): void {
  try {
    localStorage.setItem(CACHE_KEY, language);
  } catch {
    // Storage may be unavailable; translations still work for this session.
  }
}

/** Builds the English → localized lookup for one language. */
export function loadLanguage(language: string): void {
  currentLanguage = language && language !== "Default" ? language : "English";

  const english = languages.English ?? {};
  const localized = languages[currentLanguage] ?? null;

  if (!localized || currentLanguage === "English") {
    dictionary = {};
    return;
  }

  // Keyed by the normalised English text so casing/spacing differences in the
  // UI literals still resolve.
  const next: Record<string, string> = {};
  for (const key of Object.keys(english)) {
    const value = localized[key];
    const from = english[key];
    if (!value || !from || value === from) continue;

    // Two keys can share an English value (e.g. STR_INSTALL_FAILED and
    // STR_STATE_FAILED). The console's INI order comes first, so it wins;
    // this keeps the lookup deterministic instead of last-writer-wins.
    const id = normalise(from);
    if (!(id in next)) next[id] = value;
  }
  dictionary = next;
  rememberLanguage(currentLanguage);
}

// Prime from the last known language before the first paint: the sign-in
// screen otherwise waits for /__local__/auth to come back.
const cached = cachedLanguage();
if (cached && languages[cached]) loadLanguage(cached);

/** Small handle for diagnosing translations from the browser console. */
if (typeof window !== "undefined") {
  (window as unknown as Record<string, unknown>).__i18n = () => ({
    language: currentLanguage,
    entries: Object.keys(dictionary).length,
  });
}

export function getLanguage(): string {
  return currentLanguage;
}

/** Languages present in the generated table, for pickers. */
export function languageNames(): string[] {
  return Object.keys(languages).sort();
}

/** Translates by English text; returns the input unchanged when unknown. */
export function t(text: string | undefined): string {
  if (!text) return "";
  return dictionary[normalise(text)] ?? text;
}

/** Text nodes and placeholder/title attributes that should never be touched. */
const SKIP_TAGS = new Set(["INPUT", "TEXTAREA", "CODE", "PRE", "SCRIPT", "STYLE"]);

function translateAttributes(element: Element): void {
  for (const name of ["title", "placeholder", "aria-label"]) {
    const value = element.getAttribute(name);
    if (!value) continue;
    const translated = t(value);
    if (translated !== value) element.setAttribute(name, translated);
  }
}

function translateNode(node: Node): void {
  if (node.nodeType === Node.TEXT_NODE) {
    const raw = node.nodeValue;
    if (!raw) return;
    const trimmed = raw.trim();
    if (!trimmed) return;

    const translated = t(trimmed);
    if (translated === trimmed) return;
    node.nodeValue = raw.replace(trimmed, translated);
    return;
  }

  if (node.nodeType !== Node.ELEMENT_NODE) return;

  const element = node as Element;
  if (SKIP_TAGS.has(element.tagName)) return;
  if (element instanceof HTMLElement && element.dataset.noI18n !== undefined) return;

  translateAttributes(element);
  for (const child of Array.from(element.childNodes)) translateNode(child);
}

/**
 * Translates the rendered DOM in place. React keeps its own English strings in
 * the virtual tree, so this runs after every commit and on language change,
 * which gives full coverage without touching every call site.
 */
export function translateDom(root: Node = document.body): void {
  translateNode(root);
}

/** Watches for re-renders so newly shown dialogs are translated too. */
export function observeTranslations(): () => void {
  if (typeof MutationObserver === "undefined") return () => {};

  const observer = new MutationObserver((mutations) => {
    for (const mutation of mutations) {
      if (mutation.type === "characterData") translateNode(mutation.target);
      for (const added of Array.from(mutation.addedNodes)) translateNode(added);
    }
  });

  observer.observe(document.body, { childList: true, subtree: true, characterData: true });
  translateDom();
  return () => observer.disconnect();
}

/**
 * Translates a template and fills {name} placeholders. The template is looked up
 * first, so translations may reorder or drop placeholders.
 */
export function tp(template: string, values: Record<string, string | number>): string {
  const text = t(template);
  return text.replace(/\{(\w+)\}/g, (_match, key: string) => {
    const value = values[key];
    return value === undefined ? `{${key}}` : String(value);
  });
}
