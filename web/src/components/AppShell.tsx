"use client";

import MenuIcon from "@mui/icons-material/Menu";
import AppBar from "@mui/material/AppBar";
import Box from "@mui/material/Box";
import Drawer from "@mui/material/Drawer";
import IconButton from "@mui/material/IconButton";
import List from "@mui/material/List";
import ListItemButton from "@mui/material/ListItemButton";
import ListItemText from "@mui/material/ListItemText";
import Toolbar from "@mui/material/Toolbar";
import Typography from "@mui/material/Typography";
import Link from "next/link";
import { usePathname } from "next/navigation";
import { useState, type ReactNode } from "react";
import { navigation } from "@/modules/navigation";

const drawerWidth = 232;

export function AppShell({ children }: { children: ReactNode }) {
  const pathname = usePathname();
  const [open, setOpen] = useState(false);

  const nav = (
    <Box role="navigation" aria-label="Main">
      <Toolbar>
        <Typography variant="h6" component="span" sx={{ fontWeight: 700, letterSpacing: 1 }}>
          KAREVONA
        </Typography>
      </Toolbar>
      <List>
        {navigation.map((item) => (
          <ListItemButton
            key={item.id}
            component={Link}
            href={item.href}
            selected={item.href === "/" ? pathname === "/" : pathname.startsWith(item.href)}
            onClick={() => setOpen(false)}
          >
            <ListItemText primary={item.label} />
          </ListItemButton>
        ))}
      </List>
    </Box>
  );

  return (
    <Box sx={{ display: "flex" }}>
      <AppBar position="fixed" sx={{ display: { sm: "none" } }}>
        <Toolbar>
          <IconButton color="inherit" edge="start" aria-label="Open navigation" onClick={() => setOpen(true)}>
            <MenuIcon />
          </IconButton>
          <Typography variant="h6" component="span" sx={{ ml: 1 }}>
            KAREVONA
          </Typography>
        </Toolbar>
      </AppBar>
      <Drawer
        variant="temporary"
        open={open}
        onClose={() => setOpen(false)}
        ModalProps={{ keepMounted: true }}
        sx={{ display: { xs: "block", sm: "none" }, "& .MuiDrawer-paper": { width: drawerWidth } }}
      >
        {nav}
      </Drawer>
      <Drawer
        variant="permanent"
        open
        sx={{ display: { xs: "none", sm: "block" }, "& .MuiDrawer-paper": { width: drawerWidth } }}
      >
        {nav}
      </Drawer>
      <Box component="main" sx={{ flexGrow: 1, p: 3, ml: { sm: `${drawerWidth}px` } }}>
        <Toolbar sx={{ display: { sm: "none" } }} />
        {children}
      </Box>
    </Box>
  );
}
