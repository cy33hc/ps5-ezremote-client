// Regenerates src/langs.json from the console's translation files, so the web
// UI ships every language and never needs a backend lookup.
//
//   npm run langs

import { existsSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const langsDir = join(here, "..", "..", "data", "assets", "langs");
const output = join(here, "..", "src", "langs.json");

const tables = {};

for (const file of readdirSync(langsDir).sort()) {
  if (!file.toLowerCase().endsWith(".ini")) continue;

  const table = {};
  for (const raw of readFileSync(join(langsDir, file), "utf8").split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith(";") || line.startsWith("#")) continue;

    const separator = line.indexOf("=");
    if (separator <= 0) continue;

    const key = line.slice(0, separator).trim();
    const value = line.slice(separator + 1).trim();
    if (key) table[key] = value;
  }

  tables[file.replace(/\.ini$/i, "")] = table;
}

// Web-only translations live in src/langs.overrides.json so the console's ini
// files stay untouched; they win over anything the ini files provide.
const overridesPath = join(here, "..", "src", "langs.overrides.json");
if (existsSync(overridesPath)) {
  const overrides = JSON.parse(readFileSync(overridesPath, "utf8"));
  for (const [language, entries] of Object.entries(overrides)) {
    tables[language] = { ...(tables[language] ?? {}), ...entries };
  }
}

writeFileSync(output, JSON.stringify(tables), "utf8");
console.log(`langs.json: ${Object.keys(tables).length} languages -> ${output}`);
