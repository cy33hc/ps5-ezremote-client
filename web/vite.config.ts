import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";

// The PS5 serves the package directory over HTTP with the whole filesystem
// mounted at "/", so the built app must reference its assets by the absolute
// path it will be deployed to.
export default defineConfig({
  base: "/data/homebrew/ezremote-client/assets/",
  plugins: [react(), tailwindcss()],
  build: {
    outDir: "../data/assets",
    // data/assets also holds fonts, images, langs, certs and
    // download_status.html, which must survive the build.
    emptyOutDir: false,
    sourcemap: false,
  },
});
