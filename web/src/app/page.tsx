import { ClusterSummary } from "@/components/ClusterSummary";
import { ModulePage } from "@/components/ModulePage";

export default function DashboardPage() {
  return (
    <ModulePage id="dashboard">
      <ClusterSummary />
    </ModulePage>
  );
}
