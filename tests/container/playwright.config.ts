import { defineConfig, devices } from "@playwright/test";

// Targets an already-running container (no webServer). The publish workflow
// starts the built image, then runs this against http://localhost:8080.
export default defineConfig({
  testDir: ".",
  timeout: 90_000,
  expect: { timeout: 15_000 },
  reporter: "list",
  use: {
    ...devices["Desktop Chrome"],
    baseURL: process.env.APP_URL || "http://localhost:8080",
    ignoreHTTPSErrors: true,
    viewport: { width: 1280, height: 800 },
    // Software WebGL (SwiftShader) so three.js / WebGL apps actually render in
    // headless CI instead of producing a blank canvas. Harmless for DOM apps.
    launchOptions: {
      args: [
        "--use-gl=angle",
        "--use-angle=swiftshader",
        "--enable-unsafe-swiftshader",
        "--ignore-gpu-blocklist",
        "--enable-webgl",
      ],
    },
  },
});
