// The console's top-level information architecture. Each entry is a module;
// plugin-contributed pages will register additional entries (src/plugins).
export interface NavItem {
  id: string;
  label: string;
  href: string;
  description: string;
}

export const navigation: readonly NavItem[] = [
  { id: "dashboard", label: "Dashboard", href: "/", description: "Health, capacity and activity across all providers." },
  { id: "infrastructure", label: "Infrastructure", href: "/infrastructure", description: "Nodes, VMs, networks and providers in one inventory." },
  { id: "storage", label: "Storage", href: "/storage", description: "Pools, volumes, snapshots and storage intelligence." },
  { id: "security", label: "Security", href: "/security", description: "Scan findings, incidents and protection policies." },
  { id: "ai", label: "AI", href: "/ai", description: "Investigations, recommendations and approvals." },
  { id: "operations", label: "Operations", href: "/operations", description: "Tasks, events and audit trail." },
  { id: "administration", label: "Administration", href: "/administration", description: "Providers, plugins, users, roles and policies." },
];
