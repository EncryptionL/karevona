import Alert from "@mui/material/Alert";
import Typography from "@mui/material/Typography";
import type { ReactNode } from "react";
import { navigation } from "@/modules/navigation";

/** Standard page frame for a console area. Real content lands per milestone. */
export function ModulePage({ id, children }: { id: string; children?: ReactNode }) {
  const item = navigation.find((n) => n.id === id);
  return (
    <>
      <Typography variant="h4" component="h1" gutterBottom>
        {item?.label ?? id}
      </Typography>
      <Typography color="text.secondary" sx={{ mb: 3 }}>
        {item?.description}
      </Typography>
      {children ?? (
        <Alert severity="info">
          This area is part of the foundation only. It will be built on the Karevona API in a later milestone.
        </Alert>
      )}
    </>
  );
}
