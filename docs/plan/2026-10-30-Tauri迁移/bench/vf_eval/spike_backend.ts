// Spike: VibeFrontend's Runtime outside the tool, with a fake async backend in place of the simulate steps.
import path from "node:path";
import { loadProjectFiles } from "file:///C:/Users/admin/Documents/VibeFrontend/app/src/loader.ts";
import { Runtime } from "file:///C:/Users/admin/Documents/VibeFrontend/app/src/runtime.ts";
import { computeSceneLayout } from "file:///C:/Users/admin/Documents/VibeFrontend/app/src/model.ts";
import { nodeIO } from "file:///C:/Users/admin/Documents/VibeFrontend/app/scripts/node-io.ts";

const designs = process.argv.slice(2);
for (const design of designs) {
  const project = await loadProjectFiles(nodeIO(path.resolve(design)), null);
  const raised: string[] = [];
  let backendWrites = 0;
  let runtime: Runtime;
  runtime = new Runtime(project, {
    onChange() {},
    onLog() {},
    raiseBusiness(name) {
      raised.push(name);
      setTimeout(() => {
        const err = runtime.setBusinessState("layer.visible", false);
        if (err) console.log("backend write refused:", err);
        backendWrites++;
      }, 1);
      return true;
    },
  });
  runtime.start();
  await runtime.waitForIdle();

  const before = String(runtime.view("toggleVisibleBtn").props.text);
  runtime.dispatch("toggleVisibleBtn", "click");
  const rightAfter = String(runtime.view("toggleVisibleBtn").props.text);
  await new Promise((r) => setTimeout(r, 20));
  const afterBackend = String(runtime.view("toggleVisibleBtn").props.text);
  const refused = runtime.setBusinessState("layers.selectedIndex", 1);

  const ids = Object.keys(project.scene.nodes);
  const passes = 200;
  let a = performance.now();
  for (let i = 0; i < passes; i++) for (const id of ids) runtime.view(id);
  const viewMs = (performance.now() - a) / passes;
  a = performance.now();
  for (let i = 0; i < passes; i++) {
    computeSceneLayout(project.scene, {
      isVisible: (nid: string) => runtime.view(nid).visible,
      getProp: (nid: string, prop: string) => runtime.view(nid).props[prop] ?? null,
      getDockLayout: (wid: string) => runtime.getDockLayout(wid),
    });
  }
  const layoutMs = (performance.now() - a) / passes;
  console.log(JSON.stringify({ design: path.basename(path.dirname(design)) + "/" + path.basename(design), nodes: ids.length, raised, backendWrites, before, rightAfter, afterBackend, uiOwnedWriteRefused: refused, viewAllMs: +viewMs.toFixed(2), layoutMs: +layoutMs.toFixed(2) }));
}
process.exit(0);
