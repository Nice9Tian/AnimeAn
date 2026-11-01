import fs from "node:fs";
import path from "node:path";

function formatValue(val, maxLen = 120) {
  if (val === undefined) {
    return "<MISSING>";
  }
  const raw = typeof val === "string" ? JSON.stringify(val) : JSON.stringify(val);
  if (raw.length <= maxLen) {
    return raw;
  }
  return raw.slice(0, maxLen - 3) + "...";
}

function isPlainObject(val) {
  return val !== null && typeof val === "object" && !Array.isArray(val);
}

// Painted images differ at the edges with the antialiasing. Sixteen by sixteen cells,
// so this catches a shifted, missing or recoloured picture and lets edge noise through.
const IMAGE_GRID_TOLERANCE = 24;

function gridDistance(a, b) {
  if (!Array.isArray(a) || !Array.isArray(b) || a.length !== b.length) {
    return Infinity;
  }
  let worst = 0;
  for (let i = 0; i < a.length; i++) {
    worst = Math.max(worst, Math.abs(a[i] - b[i]));
  }
  return worst;
}

export function compareRecords(oldRecords, newRecords, maxDiffs = 30) {
  const diffs = [];
  let fieldsCompared = 0;
  let softImages = 0;
  let maxGridDiff = 0;

  function pushDiff(scenario, stepLabel, fieldPath, oldVal, newVal) {
    diffs.push({
      scenario,
      step: stepLabel,
      path: fieldPath || "<root>",
      oldValue: formatValue(oldVal),
      newValue: formatValue(newVal),
    });
  }

  function walk(scenario, stepLabel, fieldPath, a, b) {
    if (/^images\[\d+\]$/.test(fieldPath) && isPlainObject(a) && isPlainObject(b)) {
      fieldsCompared++;
      if (a.w !== b.w || a.h !== b.h) {
        pushDiff(scenario, stepLabel, `${fieldPath}.size`, [a.w, a.h], [b.w, b.h]);
      } else if (a.sha256 !== b.sha256) {
        const worst = gridDistance(a.grid, b.grid);
        softImages++;
        maxGridDiff = Math.max(maxGridDiff, worst);
        if (worst > IMAGE_GRID_TOLERANCE) {
          pushDiff(scenario, stepLabel, `${fieldPath}.grid`, a.sha256, `${b.sha256} worst=${worst}`);
        }
      }
      return;
    }
    if (Array.isArray(a) || Array.isArray(b)) {
      if (!Array.isArray(a) || !Array.isArray(b)) {
        fieldsCompared++;
        pushDiff(scenario, stepLabel, fieldPath, a, b);
        return;
      }
      if (a.length !== b.length) {
        fieldsCompared++;
        pushDiff(scenario, stepLabel, `${fieldPath}.length`, a.length, b.length);
      }
      const minLen = Math.min(a.length, b.length);
      if (minLen === 0 && a.length === b.length) {
        fieldsCompared++;
        return;
      }
      for (let i = 0; i < minLen; i++) {
        walk(scenario, stepLabel, `${fieldPath}[${i}]`, a[i], b[i]);
      }
      return;
    }

    if (isPlainObject(a) || isPlainObject(b)) {
      if (!isPlainObject(a) || !isPlainObject(b)) {
        fieldsCompared++;
        pushDiff(scenario, stepLabel, fieldPath, a, b);
        return;
      }
      const keysA = Object.keys(a);
      const keysB = Object.keys(b);
      const allKeys = Array.from(new Set([...keysA, ...keysB])).sort();
      if (allKeys.length === 0) {
        fieldsCompared++;
        return;
      }
      for (const k of allKeys) {
        const subPath = fieldPath ? `${fieldPath}.${k}` : k;
        const hasA = Object.prototype.hasOwnProperty.call(a, k);
        const hasB = Object.prototype.hasOwnProperty.call(b, k);
        if (!hasA || !hasB) {
          fieldsCompared++;
          pushDiff(
            scenario,
            stepLabel,
            subPath,
            hasA ? a[k] : undefined,
            hasB ? b[k] : undefined
          );
          continue;
        }
        walk(scenario, stepLabel, subPath, a[k], b[k]);
      }
      return;
    }

    fieldsCompared++;
    if (!Object.is(a, b)) {
      pushDiff(scenario, stepLabel, fieldPath, a, b);
    }
  }

  if (oldRecords.length !== newRecords.length) {
    pushDiff(
      "<global>",
      "<step_count>",
      "records.length",
      oldRecords.length,
      newRecords.length
    );
  }

  const count = Math.min(oldRecords.length, newRecords.length);
  for (let i = 0; i < count; i++) {
    const recA = oldRecords[i];
    const recB = newRecords[i];
    const scenario = recA?.scenario ?? recB?.scenario ?? `<line_${i}>`;
    const stepNum = recA?.step ?? recB?.step ?? i;
    const stepName = recA?.name ?? recB?.name ?? "";
    const stepLabel = stepName ? `${stepNum}(${stepName})` : String(stepNum);
    walk(scenario, stepLabel, "", recA, recB);
  }

  const scenariosCount = new Set(oldRecords.map((r) => r?.scenario).filter(Boolean)).size;

  return {
    same: diffs.length === 0,
    scenarios: scenariosCount,
    steps: oldRecords.length,
    fields: fieldsCompared,
    totalDiffs: diffs.length,
    softImages,
    maxGridDiff,
    diffs: diffs.slice(0, maxDiffs),
  };
}

function loadJsonl(filePath) {
  const resolved = path.resolve(filePath);
  const content = fs.readFileSync(resolved, "utf8");
  const lines = content
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line.length > 0);
  return lines.map((line, idx) => {
    try {
      return JSON.parse(line);
    } catch (err) {
      throw new Error(`Invalid JSON at ${resolved}:${idx + 1}: ${err.message}`);
    }
  });
}

function runSelfTest(quiet = false) {
  const baseRecords = [
    {
      scenario: "pen",
      step: 0,
      name: "draw_curve",
      action: { kind: "inputs", count: 3 },
      reply: { ok: true, result: { width: 800, height: 600 }, error: null },
      state: {
        drawColor: { r: 229, g: 57, b: 53, a: 255, hex: "#e53935" },
        history: { canUndo: true, canRedo: false, currentIndex: 1 },
      },
      boards: {
        main: { w: 800, h: 600, count: 14, sha256: "aaaa1111bbbb2222" },
        child: { w: 400, h: 400, count: 5, sha256: "cccc3333dddd4444" },
      },
      images: [{ w: 32, h: 32, sha256: "eeee5555ffff6666", grid: [0, 0, 0, 0] }],
      hooks: [
        {
          event: "linefinish",
          message: {
            event: "linefinish",
            view: "main",
            tool: "Pen",
            cell: { row: 0, layer: 0, asset: 0, frame_id: 1 },
          },
        },
      ],
    },
  ];

  const identicalCopy = JSON.parse(JSON.stringify(baseRecords));
  const sameRes = compareRecords(baseRecords, identicalCopy);
  if (!sameRes.same || sameRes.totalDiffs !== 0) {
    console.error("SELF_TEST_FAILED: identical records reported diff", sameRes);
    process.exit(1);
  }

  const mutatedCopy = JSON.parse(JSON.stringify(baseRecords));
  mutatedCopy[0].hooks[0].message.cell.layer = 2;

  const diffRes = compareRecords(baseRecords, mutatedCopy);
  if (
    diffRes.same ||
    diffRes.totalDiffs !== 1 ||
    diffRes.diffs[0].scenario !== "pen" ||
    diffRes.diffs[0].step !== "0(draw_curve)" ||
    diffRes.diffs[0].path !== "hooks[0].message.cell.layer" ||
    diffRes.diffs[0].oldValue !== "0" ||
    diffRes.diffs[0].newValue !== "2"
  ) {
    console.error("SELF_TEST_FAILED: single field mutation not caught as expected", diffRes);
    process.exit(1);
  }

  if (!quiet) {
    console.log(
      `SELF_TEST_OK caught=${diffRes.diffs[0].path} (${diffRes.diffs[0].oldValue} -> ${diffRes.diffs[0].newValue})`
    );
    process.exit(0);
  }
}

function main() {
  const args = process.argv.slice(2);
  if (args.includes("--self-test")) {
    runSelfTest(false);
    return;
  }

  runSelfTest(true);

  if (args.length < 2) {
    console.error("Usage: node compare.mjs <old.jsonl> <new.jsonl> | node compare.mjs --self-test");
    process.exit(1);
  }

  const oldRecords = loadJsonl(args[0]);
  const newRecords = loadJsonl(args[1]);
  const res = compareRecords(oldRecords, newRecords, 30);

  if (res.same) {
    console.log(
      `GOLDEN_SAME scenarios=${res.scenarios} steps=${res.steps} fields=${res.fields} soft_images=${res.softImages} max_grid_diff=${res.maxGridDiff}`
    );
    process.exit(0);
  }
  console.log(
    `GOLDEN_DIFF scenarios=${res.scenarios} steps=${res.steps} fields=${res.fields} total_diffs=${res.totalDiffs}`
  );
  for (let i = 0; i < res.diffs.length; i++) {
    const d = res.diffs[i];
    console.log(
      `  [${i + 1}] scenario=${d.scenario} step=${d.step} path=${d.path} old=${d.oldValue} new=${d.newValue}`
    );
  }
  process.exit(1);
}

main();
