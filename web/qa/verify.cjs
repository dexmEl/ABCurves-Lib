/* Browser checks for actual frozen comparisons and user controls. */
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const path = require("node:path");
const zlib = require("node:zlib");

async function verify(page, baseURL = "http://127.0.0.1:8765/") {
  const errors = [];
  page.on("pageerror", error => errors.push(String(error)));
  await page.emulateMedia({reducedMotion: "reduce"});
  await page.setViewportSize({width: 1360, height: 960});
  await page.goto(baseURL);
  await page.waitForFunction(() => document.getElementById("loadStatus").hidden);
  const report = await page.locator("#comparisonData").evaluate(el => JSON.parse(el.textContent));
  assert.equal(report.examples.length, 18);
  let inspected = 0;
  const format = n => n === 0 ? "0" : Math.abs(n) < .001 ? n.toExponential(5) : n.toPrecision(7);
  for (const meta of report.examples) {
    await page.selectOption("#pick", meta.id);
    await page.waitForFunction(id => document.getElementById("pick").value === id && document.getElementById("loadStatus").hidden, meta.id);
    const response = await page.request.get(new URL("data/" + meta.file, baseURL).href);
    assert.equal(response.status(), 200);
    const raw = zlib.inflateSync(await response.body());
    for (let draw = 0; draw < 4; draw++) {
      const run = meta.runs[draw];
      await page.selectOption("#seed", String(draw));
      assert.equal(await page.locator("#maxPosition").innerText(), format(run.position.max_abs));
      assert.equal(await page.locator("#maxDisplacement").innerText(), format(run.displacement.max_abs));
      assert.equal(await page.locator("#rmse").innerText(), format(run.position.rmse));
      const checks = Object.values(run.checks).filter(Boolean).length;
      assert.equal(await page.locator("#diagnostics").innerText(), checks + "/8 checks match");
      const offsetA = meta.layout["original" + draw].offset, offsetB = meta.layout["native" + draw].offset;
      let max = -1, expectedTick = 0;
      for (let i = 2; i < (meta.duration + 1) * 2; i++) {
        const difference = Math.abs(raw.readDoubleLE(offsetB + i * 8) - raw.readDoubleLE(offsetA + i * 8));
        if (difference > max) { max = difference; expectedTick = Math.floor(i / 2); }
      }
      await page.click("#worst");
      await page.waitForFunction(tick => Number(document.getElementById("time").value) === tick, expectedTick);
      const tick = Number(await page.locator("#time").inputValue());
      const a = [0, 1].map(axis => raw.readDoubleLE(offsetA + (tick * 2 + axis) * 8));
      const b = [0, 1].map(axis => raw.readDoubleLE(offsetB + (tick * 2 + axis) * 8));
      assert.equal(Math.max(Math.abs(b[0] - a[0]), Math.abs(b[1] - a[1])), run.position.max_abs);
      const cells = await page.locator("#sampleRows tr:first-child td").allTextContents();
      assert.equal(cells[1], a[0] === 0 ? "0" : a[0].toPrecision(16));
      assert.equal(cells[2], b[0] === 0 ? "0" : b[0].toPrecision(16));
      assert.equal(cells[3], b[0] - a[0] === 0 ? "0" : (b[0] - a[0]).toExponential(9));
      inspected++;
    }
  }
  // The known head divergence remains available through the regular controls.
  await page.selectOption("#pick", "continuous/H04");
  await page.waitForFunction(() => document.getElementById("pick").value === "continuous/H04" && document.getElementById("loadStatus").hidden);
  await page.selectOption("#seed", "2");
  assert.equal(await page.locator("#seed").inputValue(), "2");
  assert.equal(await page.locator("#diagnostics").innerText(), "7/8 checks match");
  assert.match(await page.locator("#diagnosticDetail").innerText(), /3425 ms/);
  assert.match(page.url(), /H04\?seed=101$/);
  const downloadEvent = page.waitForEvent("download");
  await page.click("#download");
  const download = await downloadEvent;
  assert.equal(download.suggestedFilename(), "H04-seed101.csv");
  const csv = await fs.readFile(await download.path(), "utf8");
  assert.equal(csv.trim().split("\n").length, 10001);
  assert.match(csv, /^timestamp_us,original_x,original_y,native_x,native_y/);
  await page.click("#browse");
  assert.equal(await page.locator("#cards button").count(), 18);
  await page.locator("#cards button").last().click();
  await page.waitForFunction(() => document.getElementById("loadStatus").hidden);
  assert.equal(await page.locator("#pick").inputValue(), "continuous/S10");
  await page.click("#replay");
  await page.waitForFunction(() => Number(document.getElementById("time").value) > 100);
  await page.click("#play");
  const paused = await page.locator("#time").inputValue();
  await page.waitForTimeout(150);
  assert.equal(await page.locator("#time").inputValue(), paused);
  // Scrubbing, overlay, camera controls and reset work without desynchronizing.
  await page.locator("#time").evaluate(el => { el.value = 3210; el.dispatchEvent(new Event("input")); });
  await page.waitForFunction(() => document.getElementById("clock").textContent.startsWith("3.210"));
  await page.check("#overlay"); await page.uncheck("#follow"); await page.click("#fit");
  assert.equal(await page.locator("#follow").isChecked(), true);
  const canvas = await page.locator("#original").boundingBox();
  await page.mouse.move(canvas.x + 100, canvas.y + 100); await page.mouse.down(); await page.mouse.move(canvas.x + 130, canvas.y + 120); await page.mouse.up();
  assert.equal(await page.locator("#follow").isChecked(), false);
  await page.click("#fit");
  for (const width of [390, 320]) {
    await page.setViewportSize({width, height: 844});
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
    assert.equal(await page.locator(".pane:visible").count(), 2);
    assert.ok((await page.locator("#pick").boundingBox()).width >= 150);
  }
  await page.goto(baseURL + "#continuous/H04?seed=101");
  await page.waitForFunction(() => document.getElementById("loadStatus").hidden);
  assert.equal(await page.locator("#seed").inputValue(), "2");
  await page.goto(baseURL + "#method");
  await page.waitForFunction(() => document.getElementById("loadStatus").hidden);
  assert.deepEqual(errors, []);
  return {examples: 18, draws: inspected, desktop: true, mobileWidths: [390, 320], downloads: true,
    exceptionVisible: true, playback: true, navigation: true, gestures: true, pageErrors: errors};
}

module.exports = verify;
if (require.main === module) {
  (async () => {
    const {chromium} = require(path.resolve(__dirname, "../../build/web-qa/node_modules/playwright"));
    const browser = await chromium.launch({headless: true});
    try {
      const page = await browser.newPage();
      const result = await verify(page, process.argv[2] || undefined);
      const output = path.resolve(__dirname, "../../build/web-verification.json");
      await fs.writeFile(output, JSON.stringify(result, null, 2) + "\n");
      console.log(JSON.stringify(result, null, 2));
    } finally { await browser.close(); }
  })().catch(error => { console.error(error); process.exitCode = 1; });
}
