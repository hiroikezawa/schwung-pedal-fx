/* The generated manifests through Schwung's OWN page planner and contract
 * validator (the code the device runs), plus label rendering.
 *
 *   node tests/test_ui.mjs <schwung-root> <project-root>
 *
 * SPDX-License-Identifier: MIT
 */
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const [schwung, project] = process.argv.slice(2);
const load = (rel) => import(pathToFileURL(path.join(schwung, rel)).href);
const { planPages } = await load("src/shared/param_pages/page_plan.mjs");
const { validateContract } = await load("src/shared/param_pages/validate_contract.mjs");
const { labelForCell } = await load("src/shared/param_pages/render_page_movy.mjs");

let failures = 0;
const check = (cond, msg) => { if (!cond) { ++failures; console.log("FAIL " + msg); } };

for (const p of ["rat", "ts808", "ds1", "bd2", "mt2"]) {
    const m = JSON.parse(fs.readFileSync(path.join(project, "build", "gen", p, "module.json"), "utf8"));
    const caps = m.capabilities;
    check(m.component_type === "audio_fx" && caps.chainable && caps.audio_in && caps.audio_out, `${p}: chainable audio_fx`);
    check(m.dsp === `${m.id}.so`, `${p}: dsp must be <id>.so (the chain host loads audio_fx/<id>/<id>.so)`);
    const res = planPages({ hierarchy: caps.ui_hierarchy, chainParams: caps.chain_params });
    const pages = res.pages || res;
    const knobPages = pages.filter((pg) => (pg.keys || []).length);
    const switches = caps.chain_params.filter((c) => c.type === "enum").map((c) => c.key);
    check(knobPages[0] && knobPages[0].keys.length === 8 && !knobPages[0].keys.some((k) => switches.includes(k)),
          `${p}: the first page is the eight knobs`);
    check(knobPages.length === 1 + switches.length, `${p}: one extra page per switch (${switches.join(",") || "none"})`);
    for (const k of switches) {
        const c = caps.chain_params.find((x) => x.key === k);
        check(c.options.every((o) => o.length <= 4), `${p}: switch ${k} options fit the enum square`);
    }
    check(pages.some((pg) => /preset/i.test(pg.kind)), `${p}: a Presets browser page`);
    const { findings } = validateContract({ id: m.id, hierarchy: caps.ui_hierarchy, chainParams: caps.chain_params });
    for (const f of findings) check(f.level === "info", `${p}: [${f.level}] ${f.message}`);
    const labels = caps.chain_params.map((c) => labelForCell(c.name));
    if (switches.length) console.log(`  ${m.name.padEnd(18)} pages: ${knobPages.map((pg) => pg.title || pg.name).join(" | ")}`);
    for (const l of labels) check(l.length > 0 && l.length <= 6, `${p}: label ${l}`);
    console.log(`  ${m.name.padEnd(18)} ${labels.join(" ")}`);
}
console.log(failures ? `FAILED: ${failures}` : "PASS");
process.exit(failures ? 1 : 0);
