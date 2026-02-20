import { useThemeStore } from "../stores/theme-store";
import { getCanvasColors, type CanvasColors } from "@shared/theme-colors";

export function useCanvasColors(): CanvasColors {
  const theme = useThemeStore((s) => s.theme);
  return getCanvasColors(theme);
}
