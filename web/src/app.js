/* Continuous camera/gesture workflow adapted from ABCurves, © 2026 Optima Manent (MIT). */
"use strict";
(() => {
  const $ = id => document.getElementById(id);
  const report = JSON.parse($("comparisonData").textContent), catalog = report.examples;
  const reduced = matchMedia("(prefers-reduced-motion:reduce)");
  const colors = ["#2563eb", "#059669"], cache = new Map();
  const st = {index: 0, draw: 0, t: 0, playing: !reduced.matches, speed: 1, trail: 1200,
    follow: true, guide: true, overlay: false, zoom: 1.10, pan: [0, 0]};
  const clamp = (n, lo, hi) => Math.min(hi, Math.max(lo, n));
  const current = () => catalog[st.index];
  const panes = ["original", "native"].map((kind, i) => ({canvas: $(kind), kind, ctx: $(kind).getContext("2d"), color: colors[i]}));
  let data = null, cameraFrames = [], viewport = null, requestId = 0;
  let raf = 0, last = null, ending = 0, dirty = true, sizesDirty = true;
  const number = n => n === 0 ? "0" : Math.abs(n) < .001 ? n.toExponential(5) : n.toPrecision(7);
  const precise = n => n === 0 ? "0" : n.toPrecision(16);

  async function load(meta) {
    if (cache.has(meta.id)) return cache.get(meta.id);
    if (!window.DecompressionStream) throw new Error("Please use a current browser with DecompressionStream support.");
    const response = await fetch("data/" + meta.file);
    if (!response.ok) throw new Error("Could not load this example (HTTP " + response.status + ").");
    const blob = await response.arrayBuffer();
    const stream = new Blob([blob]).stream().pipeThrough(new DecompressionStream("deflate"));
    const buffer = await new Response(stream).arrayBuffer();
    if (buffer.byteLength !== meta.bytes) throw new Error("The comparison data is incomplete.");
    if (window.crypto?.subtle) {
      const digest = await crypto.subtle.digest("SHA-256", buffer);
      const hash = [...new Uint8Array(digest)].map(v => v.toString(16).padStart(2, "0")).join("");
      if (hash !== meta.sha256) throw new Error("The comparison checksum did not match.");
    }
    const result = {};
    for (const [key, spec] of Object.entries(meta.layout)) result[key] = new Float64Array(buffer, spec.offset, spec.shape.reduce((a, b) => a * b, 1));
    cache.set(meta.id, result);
    while (cache.size > 2) cache.delete(cache.keys().next().value);
    return result;
  }

  function markDirty() {
    dirty = true;
    if (!raf && !document.hidden) raf = requestAnimationFrame(frame);
  }
  function play(on) {
    st.playing = on; last = null;
    $("play").textContent = on ? "Ⅱ pause" : "▶ play";
    $("play").setAttribute("aria-label", on ? "Pause" : "Play");
    $("play").setAttribute("aria-pressed", String(on)); $("play").classList.toggle("on", on);
    if (on && st.t >= current().duration) { st.t = 0; ending = 0; }
    markDirty();
  }
  function frame(now) {
    raf = 0;
    const dt = last === null ? 0 : Math.max(0, now - last); last = now;
    if (st.playing && data && !$("gallery").open) {
      if (st.t >= current().duration) {
        ending += dt;
        if (ending >= 650) { st.t = 0; ending = 0; }
      } else st.t = Math.min(current().duration, st.t + dt * st.speed);
      dirty = true;
    }
    if (dirty && data) { dirty = false; draw(); }
    if (st.playing && data && !document.hidden && !$("gallery").open) raf = requestAnimationFrame(frame);
    else last = null;
  }
  function fullCamera() {
    const [x0, y0, x1, y1] = current().bounds;
    return {cx: (x0 + x1) / 2, cy: (y0 + y1) / 2, sx: Math.max(1, x1 - x0) * 1.16, sy: Math.max(1, y1 - y0) * 1.16};
  }
  function buildCameras() {
    cameraFrames = [];
    const n = current().duration + 1, base = fullCamera();
    const tracks = [data.target, ...Array.from({length: 4}, (_, i) => [data["original" + i], data["native" + i]]).flat()];
    let previous = null;
    for (let t = 0; t < n; t += 40) {
      let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
      for (const path of tracks) for (let j = Math.max(0, t - 1000); j <= t; j += 12) {
        x0 = Math.min(x0, path[j * 2]); x1 = Math.max(x1, path[j * 2]);
        y0 = Math.min(y0, path[j * 2 + 1]); y1 = Math.max(y1, path[j * 2 + 1]);
      }
      const r = Math.max(data.radius[t * 2], data.radius[t * 2 + 1]);
      const c = {cx: (x0 + x1) / 2, cy: (y0 + y1) / 2,
        sx: Math.max((x1 - x0) * 1.25 + r * 2, r * 7, base.sx * .30),
        sy: Math.max((y1 - y0) * 1.25 + r * 2, r * 7, base.sy * .30)};
      if (previous) for (const k of ["cx", "cy", "sx", "sy"]) c[k] = previous[k] + (c[k] - previous[k]) * (k[0] === "s" && c[k] > previous[k] ? .48 : .2);
      for (let j = Math.max(0, t - 40); j <= Math.min(n - 1, t + 40); j++) for (const path of tracks) {
        const rx = path === data.target ? data.radius[j * 2] : 0, ry = path === data.target ? data.radius[j * 2 + 1] : 0;
        c.sx = Math.max(c.sx, 2.36 * (Math.abs(path[j * 2] - c.cx) + rx));
        c.sy = Math.max(c.sy, 2.36 * (Math.abs(path[j * 2 + 1] - c.cy) + ry));
      }
      cameraFrames.push(c); previous = c;
    }
    // Include the exact final endpoint when the duration is not divisible by 40.
    cameraFrames.push(previous);
  }
  function resize() {
    sizesDirty = false;
    const dpr = Math.min(window.devicePixelRatio || 1, 3);
    for (const p of panes) {
      const rect = p.canvas.getBoundingClientRect(); p.w = rect.width; p.h = rect.height; p.dpr = dpr;
      p.canvas.width = Math.max(1, Math.round(p.w * dpr)); p.canvas.height = Math.max(1, Math.round(p.h * dpr));
    }
    const rect = $("errorChart").getBoundingClientRect();
    $("errorChart").width = Math.max(1, Math.round(rect.width * dpr)); $("errorChart").height = Math.max(1, Math.round(rect.height * dpr));
  }
  function computeView() {
    if (sizesDirty) resize();
    let camera = fullCamera();
    if (st.follow && cameraFrames.length) {
      const at = st.t / 40, index = Math.min(cameraFrames.length - 1, Math.floor(at));
      const a = cameraFrames[index], b = cameraFrames[Math.min(index + 1, cameraFrames.length - 1)];
      camera = Object.fromEntries(["cx", "cy", "sx", "sy"].map(k => [k, a[k] + (b[k] - a[k]) * Math.min(1, at - index)]));
    }
    const scale = Math.min(...panes.map(p => Math.min(p.w / camera.sx, p.h / camera.sy))) * st.zoom;
    viewport = {cx: camera.cx + st.pan[0], cy: camera.cy + st.pan[1], scale};
    return viewport;
  }
  function point(p, x, y) { return [p.w / 2 + (x - viewport.cx) * viewport.scale, p.h / 2 - (y - viewport.cy) * viewport.scale]; }
  function world(p, x, y) { return [viewport.cx + (x - p.w / 2) / viewport.scale, viewport.cy - (y - p.h / 2) / viewport.scale]; }
  function fit() { st.follow = true; st.zoom = 1.10; st.pan = [0, 0]; $("follow").checked = true; markDirty(); }
  function freezeFollow() {
    if (!st.follow) return;
    const old = {...computeView()}, base = fullCamera();
    const scale = Math.min(...panes.map(p => Math.min(p.w / base.sx, p.h / base.sy)));
    st.follow = false; st.zoom = old.scale / scale; st.pan = [old.cx - base.cx, old.cy - base.cy]; $("follow").checked = false;
  }
  function zoom(factor, p, anchor) {
    if (!data) return;
    computeView(); const before = world(p, ...anchor);
    st.zoom = clamp(st.zoom * factor, .2, 80); computeView();
    const after = world(p, ...anchor); st.pan[0] += before[0] - after[0]; st.pan[1] += before[1] - after[1]; markDirty();
  }
  function stroke(p, path, lo, hi, color, width, alpha = 1, step = 1) {
    hi = Math.min(hi, path.length / 2 - 1); lo = Math.max(0, lo);
    if (hi <= lo) return;
    const g = p.ctx; g.strokeStyle = color; g.lineWidth = width; g.globalAlpha = alpha; g.beginPath();
    g.moveTo(...point(p, path[lo * 2], path[lo * 2 + 1]));
    for (let i = lo + step; i <= hi; i += step) g.lineTo(...point(p, path[i * 2], path[i * 2 + 1]));
    if ((hi - lo) % step) g.lineTo(...point(p, path[hi * 2], path[hi * 2 + 1]));
    g.stroke(); g.globalAlpha = 1;
  }
  function drawPath(p, tick) {
    const g = p.ctx, path = data[p.kind + st.draw];
    if (st.guide) stroke(p, data.target, 0, current().duration, "#dce8e3", 1, .8, Math.max(1, Math.floor(current().duration / 1000)));
    const [x, y] = point(p, data.target[tick * 2], data.target[tick * 2 + 1]);
    g.fillStyle = "#ecfdf5"; g.strokeStyle = "#84b49d"; g.lineWidth = 1.5; g.beginPath();
    g.ellipse(x, y, Math.max(1, data.radius[tick * 2] * viewport.scale), Math.max(1, data.radius[tick * 2 + 1] * viewport.scale), 0, 0, Math.PI * 2); g.fill(); g.stroke();
    if (st.trail && tick < st.trail) stroke(p, data.prefix, 0, data.prefix.length / 2 - 1, "#9aa5b1", 1.9, .65 * (1 - tick / st.trail));
    const start = st.trail ? Math.max(0, tick - st.trail) : 0;
    if (st.overlay) {
      g.setLineDash([5, 4]); stroke(p, data[(p.kind === "original" ? "native" : "original") + st.draw], start, tick, p.kind === "original" ? colors[1] : colors[0], 3, .45); g.setLineDash([]);
    }
    if (!st.trail) stroke(p, path, 0, tick, p.color, 2.15);
    else for (let part = 0; part < 18; part++) {
      const lo = Math.round(start + (tick - start) * part / 18), hi = Math.round(start + (tick - start) * (part + 1) / 18);
      stroke(p, path, lo, hi, p.color, 2.15, .08 + .92 * Math.pow(Math.max(0, 1 - (tick - (lo + hi) / 2) / st.trail), 1.2));
    }
    const xy = point(p, path[tick * 2], path[tick * 2 + 1]);
    g.fillStyle = p.color; g.strokeStyle = "white"; g.lineWidth = 1.3; g.beginPath(); g.arc(...xy, 3.7, 0, Math.PI * 2); g.fill(); g.stroke();
  }
  function drawError(tick) {
    const canvas = $("errorChart"), g = canvas.getContext("2d"), w = canvas.width, h = canvas.height;
    const dpr = w / canvas.getBoundingClientRect().width, left = 14 * dpr, right = w - left, mid = h / 2;
    const max = current().runs[st.draw].position.max_abs || 1e-12, scale = (h / 2 - 18 * dpr) / max;
    const a = data["original" + st.draw], b = data["native" + st.draw], n = current().duration;
    g.clearRect(0, 0, w, h); g.lineWidth = dpr; g.strokeStyle = "#e6ebf0";
    g.beginPath(); g.moveTo(left, mid); g.lineTo(right, mid); g.stroke();
    for (let axis = 0; axis < 2; axis++) {
      g.strokeStyle = colors[axis]; g.lineWidth = 1.1 * dpr; g.beginPath();
      // Draw every sample: narrow spikes must remain visible.
      for (let i = 0; i <= n; i++) {
        const x = left + (right - left) * i / n, y = mid - (b[i * 2 + axis] - a[i * 2 + axis]) * scale;
        if (!i) g.moveTo(x, y); else g.lineTo(x, y);
      }
      g.stroke();
    }
    const cursor = left + (right - left) * tick / n;
    g.strokeStyle = "#1f293380"; g.beginPath(); g.moveTo(cursor, 0); g.lineTo(cursor, h); g.stroke();
  }
  function inspect(tick) {
    const a = data["original" + st.draw], b = data["native" + st.draw];
    const values = [0, 1].map(axis => [axis ? "Position Y" : "Position X", a[tick * 2 + axis], b[tick * 2 + axis]]);
    for (let axis = 0; axis < 2; axis++) values.push([axis ? "Displacement Y" : "Displacement X",
      tick ? a[tick * 2 + axis] - a[(tick - 1) * 2 + axis] : 0,
      tick ? b[tick * 2 + axis] - b[(tick - 1) * 2 + axis] : 0]);
    $("sampleRows").replaceChildren(...values.map(([name, original, native]) => {
      const tr = document.createElement("tr");
      for (const text of [name, precise(original), precise(native), native - original === 0 ? "0" : (native - original).toExponential(9)]) { const td = document.createElement("td"); td.textContent = text; tr.append(td); }
      return tr;
    }));
    $("sampleTime").textContent = (tick * 1000).toLocaleString() + " µs";
  }
  function draw() {
    computeView(); const tick = clamp(Math.floor(st.t), 0, current().duration);
    for (const p of panes) { const g = p.ctx; g.setTransform(p.dpr, 0, 0, p.dpr, 0, 0); g.clearRect(0, 0, p.w, p.h); g.lineJoin = "round"; g.lineCap = "round"; drawPath(p, tick); }
    $("time").value = tick; $("clock").textContent = `${(tick / 1000).toFixed(3)} / ${(current().duration / 1000).toFixed(3)} s`;
    drawError(tick); inspect(tick);
  }
  function updateMetrics() {
    const meta = current(), run = meta.runs[st.draw], failed = Object.entries(run.checks).filter(([, ok]) => !ok);
    $("caseLabel").textContent = `${meta.sourceId} · ${meta.title} · seed ${run.seed}`;
    $("maxPosition").textContent = number(run.position.max_abs); $("maxDisplacement").textContent = number(run.displacement.max_abs); $("rmse").textContent = number(run.position.rmse);
    $("degrees").textContent = `≈ ${number(run.position.max_abs * .0003509487083280618 * 180 / Math.PI)}° of rotation`;
    $("diagnostics").textContent = failed.length ? `${8 - failed.length}/8 checks match` : "8/8 checks match";
    $("diagnostics").className = failed.length ? "mismatch" : "good";
    $("diagnosticDetail").textContent = failed.length ? failed.map(([key]) => `${key.replaceAll("_", " ")} differ from ${run.first_check_difference_ms[key]} ms`).join("; ") : "exact at every sample";
    $("exact").textContent = `${run.displacement.exact_pairs.toLocaleString()} / ${run.displacement.samples.toLocaleString()} displacement pairs are numerically identical (no tolerance). First position difference: ${run.position.first_difference_ms === null ? "none" : run.position.first_difference_ms + " ms"}. Initial anchors are excluded from statistics.`;
    $("chartScale").textContent = `Vertical scale: ±${number(run.position.max_abs || 0)} common angular counts · horizontal: 0–${meta.duration / 1000} s. Every sample is drawn.`;
    $("seed").value = st.draw;
  }
  function installNavigation(p) {
    const active = new Map();
    p.canvas.addEventListener("wheel", event => { event.preventDefault(); const rect = p.canvas.getBoundingClientRect(); zoom(Math.exp(-event.deltaY * .0015), p, [event.clientX - rect.left, event.clientY - rect.top]); }, {passive: false});
    p.canvas.addEventListener("pointerdown", event => {
      if (!data) return; event.preventDefault(); p.canvas.focus({preventScroll: true});
      freezeFollow(); p.canvas.setPointerCapture(event.pointerId); active.set(event.pointerId, [event.clientX, event.clientY]); p.canvas.classList.add("drag");
    });
    p.canvas.addEventListener("pointermove", event => {
      if (!data || !active.has(event.pointerId)) return;
      computeView(); const before = [...active.values()]; active.set(event.pointerId, [event.clientX, event.clientY]); const after = [...active.values()];
      if (before.length === 1) { st.pan[0] -= (after[0][0] - before[0][0]) / viewport.scale; st.pan[1] += (after[0][1] - before[0][1]) / viewport.scale; }
      else {
        const center = pts => [(pts[0][0] + pts[1][0]) / 2, (pts[0][1] + pts[1][1]) / 2];
        const distance = pts => Math.hypot(pts[0][0] - pts[1][0], pts[0][1] - pts[1][1]);
        const a = center(before), b = center(after), rect = p.canvas.getBoundingClientRect();
        zoom(distance(after) / Math.max(1, distance(before)), p, [a[0] - rect.left, a[1] - rect.top]);
        st.pan[0] -= (b[0] - a[0]) / viewport.scale; st.pan[1] += (b[1] - a[1]) / viewport.scale;
      }
      markDirty();
    });
    const release = event => { active.delete(event.pointerId); if (!active.size) p.canvas.classList.remove("drag"); };
    for (const name of ["pointerup", "pointercancel", "lostpointercapture"]) p.canvas.addEventListener(name, release);
    p.canvas.addEventListener("dblclick", fit);
  }
  async function select(index, updateHash = true) {
    const token = ++requestId; st.index = (index + catalog.length) % catalog.length; st.t = 0; ending = 0; data = null; last = null;
    const meta = current(); $("pick").value = meta.id; $("time").max = meta.duration;
    $("loadStatus").hidden = false; $("loadStatus").className = ""; $("loadStatus").textContent = "Loading " + meta.title + "…";
    for (const id of ["seed", "worst", "download"]) $(id).disabled = true;
    if (updateHash) history.replaceState(null, "", "#" + meta.id + "?seed=" + meta.runs[st.draw].seed);
    try {
      const result = await load(meta); if (token !== requestId) return;
      data = result; $("seed").replaceChildren(...meta.runs.map((run, i) => new Option(`${i + 1} · seed ${run.seed}`, i)));
      buildCameras(); fit(); updateMetrics(); $("loadStatus").hidden = true;
      for (const id of ["seed", "worst", "download"]) $(id).disabled = false;
      sizesDirty = true; markDirty();
    } catch (error) { if (token !== requestId) return; $("loadStatus").className = "error"; $("loadStatus").textContent = error.message + " Click to retry."; }
  }
  function gallery() {
    $("cards").replaceChildren(...catalog.map((meta, i) => {
      const button = document.createElement("button"); button.className = "card" + (i === st.index ? " selected" : "");
      const thumb = meta.thumbnail, xs = thumb.map(p => p[0]), ys = thumb.map(p => p[1]);
      const x0 = Math.min(...xs), y0 = Math.min(...ys), width = Math.max(1, Math.max(...xs) - x0), height = Math.max(1, Math.max(...ys) - y0);
      const scale = Math.min(180 / width, 70 / height);
      const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg"); svg.setAttribute("viewBox", "0 0 200 90"); svg.setAttribute("aria-hidden", "true");
      const path = document.createElementNS(svg.namespaceURI, "polyline"); path.setAttribute("points", thumb.map(p => `${100 + (p[0] - x0 - width / 2) * scale},${45 - (p[1] - y0 - height / 2) * scale}`).join(" ")); path.setAttribute("fill", "none"); path.setAttribute("stroke", colors[0]); path.setAttribute("stroke-width", "1.5"); svg.append(path);
      const title = document.createElement("strong"); title.textContent = `${meta.sourceId} · ${meta.title}`;
      const note = document.createElement("small"); note.textContent = meta.human ? "Recorded pursuit · 4 seeds" : "Designed target · 4 seeds";
      button.append(svg, title, note); button.onclick = () => { $("gallery").close(); select(i); }; return button;
    }));
    $("gallery").showModal(); last = null;
  }
  function seek(t) { play(false); st.t = clamp(t, 0, current().duration); markDirty(); }
  $("time").oninput = () => seek(Number($("time").value));
  $("play").onclick = () => play(!st.playing);
  $("replay").onclick = () => { st.t = 0; ending = 0; play(true); };
  $("prev").onclick = () => select(st.index - 1); $("next").onclick = () => select(st.index + 1);
  $("pick").onchange = () => select(catalog.findIndex(m => m.id === $("pick").value));
  $("seed").onchange = () => { st.draw = Number($("seed").value); history.replaceState(null, "", "#" + current().id + "?seed=" + current().runs[st.draw].seed); updateMetrics(); markDirty(); };
  $("speed").oninput = () => { st.speed = Number($("speed").value); $("speedLabel").textContent = st.speed.toFixed(2) + "×"; last = null; };
  $("trail").onchange = () => { st.trail = Number($("trail").value); markDirty(); };
  for (const id of ["guide", "overlay"]) $(id).onchange = () => { st[id] = $(id).checked; markDirty(); };
  $("follow").onchange = () => { if ($("follow").checked) fit(); else { freezeFollow(); markDirty(); } };
  $("fit").onclick = fit; $("browse").onclick = gallery; $("closeGallery").onclick = () => $("gallery").close();
  $("gallery").addEventListener("close", () => { last = null; markDirty(); });
  $("loadStatus").onclick = () => { if ($("loadStatus").classList.contains("error")) select(st.index); };
  $("worst").onclick = () => {
    if (!data) return; const a = data["original" + st.draw], b = data["native" + st.draw]; let maximum = -1, tick = 0;
    for (let i = 2; i < a.length; i++) if (Math.abs(b[i] - a[i]) > maximum) { maximum = Math.abs(b[i] - a[i]); tick = Math.floor(i / 2); }
    seek(tick);
  };
  $("download").onclick = () => {
    if (!data) return; const a = data["original" + st.draw], b = data["native" + st.draw];
    const rows = ["timestamp_us,original_x,original_y,native_x,native_y,error_x,error_y,original_dx,original_dy,native_dx,native_dy"];
    for (let t = 1; t <= current().duration; t++) { const i = t * 2; rows.push([t * 1000, a[i], a[i + 1], b[i], b[i + 1], b[i] - a[i], b[i + 1] - a[i + 1], a[i] - a[i - 2], a[i + 1] - a[i - 1], b[i] - b[i - 2], b[i + 1] - b[i - 1]].join(",")); }
    const url = URL.createObjectURL(new Blob([rows.join("\n") + "\n"], {type: "text/csv;charset=utf-8"}));
    const link = document.createElement("a"); link.href = url; link.download = `${current().sourceId}-seed${current().runs[st.draw].seed}.csv`; link.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
  };
  document.addEventListener("keydown", event => {
    if ($("gallery").open || /INPUT|SELECT|TEXTAREA|BUTTON|A|SUMMARY/.test(event.target.tagName)) return;
    if (event.code === "Space") { event.preventDefault(); play(!st.playing); }
    else if (event.key === "ArrowRight") { event.preventDefault(); select(st.index + 1); }
    else if (event.key === "ArrowLeft") { event.preventDefault(); select(st.index - 1); }
  });
  document.addEventListener("visibilitychange", () => { last = null; if (!document.hidden) markDirty(); });
  new ResizeObserver(() => { sizesDirty = true; markDirty(); }).observe($("panes"));
  window.addEventListener("resize", () => { sizesDirty = true; markDirty(); });
  function route() {
    if (location.hash === "#method" && data) return;
    let hash; try { hash = decodeURIComponent(location.hash.slice(1)); } catch { hash = ""; }
    const [id, query] = hash.split("?"), index = catalog.findIndex(m => m.id === id), meta = catalog[index < 0 ? 0 : index];
    const draw = meta.runs.findIndex(r => String(r.seed) === new URLSearchParams(query).get("seed"));
    st.draw = draw < 0 ? 0 : draw; select(index < 0 ? 0 : index, false);
  }
  window.addEventListener("hashchange", route);
  $("pick").replaceChildren(...catalog.map(meta => new Option(`${meta.sourceId} · ${meta.title}`, meta.id)));
  const summary = report.summary, matched = catalog.flatMap(m => m.runs).filter(r => Object.values(r.checks).every(Boolean)).length;
  $("aggregate").textContent = `${catalog.length} examples × 4 seeds = ${summary.runs} runs and ${summary.samples.toLocaleString()} paired samples. Largest position difference across all runs: ${number(summary.position.max_abs)} common angular counts (≈ ${number(summary.position.max_abs * .0003509487083280618 * 180 / Math.PI)}°). All eight timing and decision checks match in ${matched}/${summary.runs} runs.`;
  const versions = $("versions"), source = document.createElement("a"); source.href = `${report.upstream.repository}/tree/${report.upstream.commit}`; source.textContent = report.upstream.commit.slice(0, 12);
  versions.append("Original Python source: ", source, `. Native source commit: ${report.native.commit.slice(0, 12)}. Model SHA-256: ${report.native.model_sha256}. Individual source hashes are in the downloadable JSON.`);
  $("environment").textContent = `Recorded ${report.recorded_utc.slice(0, 10)} · ${report.environment.platform} · Python ${report.environment.python} · NumPy ${report.environment.packages.numpy}, Numba ${report.environment.packages.numba} · ${report.environment.build}.`;
  panes.forEach(installNavigation); play(st.playing); route();
})();
