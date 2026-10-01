// The browser only ever calls same-origin /api/v1/*. This rewrite forwards
// those calls to the Karevona API gateway; the UI has no other backend.
const apiUrl = process.env.KAREVONA_API_URL ?? "http://localhost:8080";

/** @type {import('next').NextConfig} */
const nextConfig = {
  output: "standalone",
  reactStrictMode: true,
  async rewrites() {
    return [{ source: "/api/v1/:path*", destination: `${apiUrl}/api/v1/:path*` }];
  },
};

export default nextConfig;
