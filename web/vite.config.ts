import { defineConfig } from "vite";

// The engine is built by wasm/gui/build.sh and consumed straight from its build
// directory rather than copied into the tree, so there is one artifact and no
// chance of serving a stale copy. `fs.allow` is what lets Vite reach out of the
// web/ root to read it.
export default defineConfig({
  server: {
    fs: { allow: [".", "../wasm"] },
  },
  build: {
    target: "es2022",
  },
});
