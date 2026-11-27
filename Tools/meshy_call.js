// Calls Meshy's API from the command line (2026-11-21, agent wizard: the connector's tools were not loaded in an agent session).
// The key comes from the MESHY_API_KEY environment variable and is never printed.
//   node Tools/meshy_call.js balance
//   node Tools/meshy_call.js run <endpoint> <body.json> <out prefix>   posts, waits for the task, downloads what it made
//       endpoints: v1/text-to-image, v1/image-to-image, v1/image-to-3d, v1/rigging ...
//       in the body, a string "@file:<path>" becomes that picture as a data URI
const fs = require("fs"), path = require("path");
const KEY = process.env.MESHY_API_KEY, BASE = "https://api.meshy.ai/openapi/";
if (!KEY) { console.error("no MESHY_API_KEY"); process.exit(1); }
const H = { Authorization: "Bearer " + KEY, "Content-Type": "application/json" };
const sleep = ms => new Promise(r => setTimeout(r, ms));
function fill(v) {
  if (typeof v === "string" && v.startsWith("@file:")) {
    const p = v.slice(6), ext = path.extname(p).slice(1).toLowerCase();
    return "data:image/" + (ext === "jpg" ? "jpeg" : ext) + ";base64," + fs.readFileSync(p).toString("base64");
  }
  if (Array.isArray(v)) return v.map(fill);
  if (v && typeof v === "object") { const o = {}; for (const k in v) o[k] = fill(v[k]); return o; }
  return v;
}
async function save(url, file) {
  const r = await fetch(url);
  if (!r.ok) throw new Error("download " + r.status);
  fs.writeFileSync(file, Buffer.from(await r.arrayBuffer()));
  console.log("saved", file, Math.round(fs.statSync(file).size / 1024), "KB");
}
(async () => {
  const [cmd, ep, bodyFile, out] = process.argv.slice(2);
  if (cmd === "balance") { console.log(await (await fetch(BASE + "v1/balance", { headers: H })).text()); return; }
  const body = fill(JSON.parse(fs.readFileSync(bodyFile, "utf8")));
  const r = await fetch(BASE + ep, { method: "POST", headers: H, body: JSON.stringify(body) });
  const j = await r.json();
  if (!r.ok) { console.error("POST failed", r.status, JSON.stringify(j)); process.exit(1); }
  const id = j.result;
  console.log("task", id);
  for (;;) {
    await sleep(6000);
    const t = await (await fetch(BASE + ep + "/" + id, { headers: H })).json();
    if (t.status === "SUCCEEDED") {
      fs.writeFileSync(out + "_task.json", JSON.stringify(t, null, 1));
      if (t.image_urls) for (let i = 0; i < t.image_urls.length; ++i) await save(t.image_urls[i], out + (i ? "_" + i : "") + ".png");
      if (t.model_urls && t.model_urls.glb) await save(t.model_urls.glb, out + ".glb");
      if (t.result && t.result.rigged_character_glb_url) await save(t.result.rigged_character_glb_url, out + "_rigged.glb");
      if (t.result && t.result.animation_glb_url) await save(t.result.animation_glb_url, out + ".glb");   // (v1/animations, 2026-11-26)
      if (t.texture_urls) for (const tu of t.texture_urls) for (const k in tu) await save(tu[k], out + "_" + k + ".png");
      return;
    }
    if (t.status === "FAILED" || t.status === "CANCELED" || t.status === "EXPIRED") { console.error("task", t.status, JSON.stringify(t.task_error || t)); process.exit(1); }
  }
})().catch(e => { console.error(String(e)); process.exit(1); });
