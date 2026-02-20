import { createRoot } from "react-dom/client";
import { App } from "./App";
import { useThemeStore } from "./stores/theme-store";
import "./styles/global.css";

// Sync the dark class on <html> before first render
useThemeStore.getState().setTheme(useThemeStore.getState().theme);

createRoot(document.getElementById("root")!).render(<App />);
