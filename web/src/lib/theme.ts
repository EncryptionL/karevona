"use client";

import { createTheme } from "@mui/material/styles";

export const theme = createTheme({
  cssVariables: { colorSchemeSelector: "class" },
  colorSchemes: { light: true, dark: true },
  shape: { borderRadius: 8 },
  typography: { fontFamily: 'system-ui, -apple-system, "Segoe UI", Roboto, sans-serif' },
});
