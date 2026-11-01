import fs from "node:fs";
const p = process.argv[2];
let t = fs.readFileSync(p, "utf8");
const marker = /^(\s*)- \{ id: "lyr_bg".*$/m;
const m = t.match(marker);
let rows = m[0];
for (let i = 0; i < 497; i++) rows += `\n${m[1]}- { id: "gen_${i}", name: "生成图层 ${i}", kind: "Vector", isGroup: false, depth: 0, collapsed: false, checked: true, visible: true, locked: false }`;
fs.writeFileSync(p, t.replace(marker, rows));
