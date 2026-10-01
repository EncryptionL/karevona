import type { ClusterInfo } from "./types";

export class ApiError extends Error {
  constructor(
    message: string,
    readonly status: number,
  ) {
    super(message);
    this.name = "ApiError";
  }
}

export type Fetcher = (input: string, init?: RequestInit) => Promise<Response>;

/**
 * Typed client for the Karevona API. This is the *only* way the UI reaches
 * infrastructure state: it never talks to a hypervisor, storage system or
 * AI runtime directly.
 */
export class KarevonaClient {
  constructor(
    private readonly baseUrl: string = "/api/v1",
    private readonly fetcher: Fetcher = (input, init) => fetch(input, init),
  ) {}

  private async get<T>(path: string): Promise<T> {
    let response: Response;
    try {
      response = await this.fetcher(`${this.baseUrl}${path}`, { headers: { Accept: "application/json" } });
    } catch (cause) {
      throw new ApiError(`Control plane unreachable: ${(cause as Error).message}`, 0);
    }
    if (!response.ok) {
      throw new ApiError(`Request failed: ${response.status} ${response.statusText}`, response.status);
    }
    return (await response.json()) as T;
  }

  getClusterInfo(): Promise<ClusterInfo> {
    return this.get<ClusterInfo>("/cluster");
  }
}

export const defaultClient = new KarevonaClient();
