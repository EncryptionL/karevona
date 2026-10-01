"use client";

import Alert from "@mui/material/Alert";
import Button from "@mui/material/Button";
import Card from "@mui/material/Card";
import CardContent from "@mui/material/CardContent";
import Chip from "@mui/material/Chip";
import CircularProgress from "@mui/material/CircularProgress";
import Stack from "@mui/material/Stack";
import Typography from "@mui/material/Typography";
import { useApi } from "@/hooks/useApi";
import { defaultClient } from "@/sdk";

/** First live view: asks the control plane who it is and which architecture pools it manages. */
export function ClusterSummary() {
  const { data, error, loading, reload } = useApi(() => defaultClient.getClusterInfo());

  if (loading) return <CircularProgress aria-label="Loading cluster" />;
  if (error || !data) {
    return (
      <Alert severity="warning" action={<Button onClick={reload}>Retry</Button>}>
        {error ?? "No data"}
      </Alert>
    );
  }
  return (
    <Card variant="outlined">
      <CardContent>
        <Typography variant="overline" color="text.secondary">
          Cluster
        </Typography>
        <Typography variant="h5">{data.name}</Typography>
        <Typography color="text.secondary" sx={{ mb: 2 }}>
          {data.nodeCount} nodes · server {data.serverVersion} · API {data.apiVersion}
        </Typography>
        <Stack direction="row" spacing={1} aria-label="Architecture pools">
          {data.architectures.map((a) => (
            <Chip key={a} label={a} size="small" />
          ))}
        </Stack>
      </CardContent>
    </Card>
  );
}
