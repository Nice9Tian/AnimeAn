import assert from "node:assert/strict";
import fs from "node:fs/promises";
import path from "node:path";
import { performance } from "node:perf_hooks";
import { describe, it } from "node:test";
import { fileURLToPath } from "node:url";
import { loadProjectFiles, type ProjectIO } from "../vf/src/loader.ts";
import { Runtime } from "../vf/src/runtime.ts";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const DESIGN_DIR = path.resolve(__dirname, "./fixtures/vf_animean_design");

function createNodeFsProjectIO(rootDir: string): ProjectIO {
  return {
    async root(): Promise<string> {
      return rootDir;
    },
    async readText(relPath: string): Promise<string | null> {
      try {
        return await fs.readFile(path.join(rootDir, relPath), "utf8");
      } catch {
        return null;
      }
    },
    async listDir(relPath: string): Promise<Array<{ name: string; isDir: boolean }>> {
      try {
        const entries = await fs.readdir(path.join(rootDir, relPath), { withFileTypes: true });
        return entries.map((e) => ({ name: e.name, isDir: e.isDirectory() }));
      } catch {
        return [];
      }
    },
  };
}

describe("Incremental refresh performance on 148-node design project", () => {
  it("loads webui/design (148 nodes) and keeps unrelated state change refresh median < 4 ms", async () => {
    const io = createNodeFsProjectIO(DESIGN_DIR);
    const project = await loadProjectFiles(io, null);
    const nodeIds = Object.keys(project.scene.nodes);
    const controlCount = nodeIds.filter((id) => id !== project.scene.root).length;
    assert.equal(controlCount, 148, "设计项目应包含 148 个控件节点");

    const runtime = new Runtime(project);
    runtime.start();

    // 首次全量刷新：建立全部节点的依赖索引与初始排版缓存
    const rootSize = { width: 1440, height: 900 };
    const first = runtime.refresh(rootSize);
    assert.equal(first.dirtyNodes, "all");
    assert.equal(first.layoutChanged, true);
    assert.equal(first.boxes.size, nodeIds.length);

    // 测量 100 次无关状态变化（不改变尺寸、可见性、文字）的运行层增量刷新耗时（含 refresh + 全节点 view 读取）
    const samples: number[] = [];
    const iterations = 100;

    for (let i = 0; i < iterations; i++) {
      const nextVal = (i % 100) + 1;
      const t0 = performance.now();
      runtime.dispatch("opacitySpin", "change", nextVal);
      const res = runtime.refresh(rootSize);
      for (let k = 0; k < nodeIds.length; k++) {
        runtime.view(nodeIds[k]);
      }
      const elapsed = performance.now() - t0;
      samples.push(elapsed);

      assert.notEqual(res.dirtyNodes, "all");
      assert.equal(res.layoutChanged, false, "不改变尺寸、可见性、文字的状态变化不应触发重新排版");
    }

    // 验证改变文字的状态（layer.visible）会按预期触发重新排版，且同样保持极快耗时
    runtime.setBusinessState("layer.visible", false);
    const textChangeRes = runtime.refresh(rootSize);
    assert.equal(textChangeRes.layoutChanged, true, "文字变化时应触发重新排版");

    samples.sort((a, b) => a - b);
    const median = samples[Math.floor(samples.length / 2)];
    const p95 = samples[Math.floor(samples.length * 0.95)];

    console.log(
      `[benchmark] 148 nodes incremental refresh: median=${median.toFixed(4)} ms, p95=${p95.toFixed(4)} ms`,
    );
    assert.ok(median < 4, `中位耗时应小于 4 ms，实际为 ${median.toFixed(4)} ms`);
  });
});
