import { describe, expect, it } from "vitest";
import { navigation } from "./navigation";

describe("navigation", () => {
  it("covers the seven console areas with unique ids and routes", () => {
    expect(navigation.map((n) => n.id)).toEqual([
      "dashboard",
      "infrastructure",
      "storage",
      "security",
      "ai",
      "operations",
      "administration",
    ]);
    expect(new Set(navigation.map((n) => n.href)).size).toBe(navigation.length);
  });
});
