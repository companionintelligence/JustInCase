import { test, expect } from "@playwright/test";

// Proves the published container actually serves a non-blank web UI, and saves a
// screenshot artifact. Kept deliberately app-agnostic so the same spec works for
// every CI Hub app: it passes if the page renders visible text, a canvas, or a
// mounted SPA root.
const url = process.env.APP_URL || "http://localhost:8080";

test("container serves a non-blank web UI", async ({ page }) => {
  const resp = await page.goto(url, { waitUntil: "domcontentloaded", timeout: 60_000 });
  expect(resp, "navigation should return a response").toBeTruthy();
  expect(resp!.status(), "HTTP status should be < 400").toBeLessThan(400);

  // Let an SPA mount/paint; ignore networkidle timeout for apps with long polling.
  await page.waitForLoadState("networkidle", { timeout: 20_000 }).catch(() => {});

  const canvasCount = await page.locator("canvas").count();
  // WebGL/three.js scenes need a few frames after assets load before the first
  // meaningful paint — give them a settle window so the screenshot isn't blank.
  if (canvasCount > 0) {
    await page.waitForTimeout(4_000);
  }

  const bodyText = (await page.locator("body").innerText().catch(() => "")).trim();
  const rootCount = await page.locator("#root, #app, main, [data-reactroot]").count();
  const hasContent = bodyText.length > 0 || canvasCount > 0 || rootCount > 0;

  expect(hasContent, "page should render text, a canvas, or a mounted app root").toBeTruthy();

  await page.screenshot({
    path: process.env.SCREENSHOT_PATH || "screenshot.png",
    fullPage: true,
  });
});
