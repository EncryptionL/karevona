import { describe, expect, it } from "vitest";
import { ApiError, KarevonaClient } from "./client";

const json = (body: unknown, status = 200) =>
  new Response(JSON.stringify(body), { status, headers: { "Content-Type": "application/json" } });

describe("KarevonaClient", () => {
  it("fetches cluster info from the API base path", async () => {
    const seen: string[] = [];
    const client = new KarevonaClient("/api/v1", async (url) => {
      seen.push(url);
      return json({ clusterId: "c1", name: "dev", serverVersion: "0.1.0", apiVersion: "v1", architectures: ["x86_64"], nodeCount: 3 });
    });
    const info = await client.getClusterInfo();
    expect(seen).toEqual(["/api/v1/cluster"]);
    expect(info.nodeCount).toBe(3);
  });

  it("turns HTTP failures into ApiError with the status", async () => {
    const client = new KarevonaClient("/api/v1", async () => json({}, 503));
    await expect(client.getClusterInfo()).rejects.toMatchObject({ name: "ApiError", status: 503 });
  });

  it("reports an unreachable control plane distinctly", async () => {
    const client = new KarevonaClient("/api/v1", async () => {
      throw new Error("ECONNREFUSED");
    });
    const error = await client.getClusterInfo().catch((e: unknown) => e);
    expect(error).toBeInstanceOf(ApiError);
    expect((error as ApiError).status).toBe(0);
    expect((error as ApiError).message).toContain("unreachable");
  });
});
