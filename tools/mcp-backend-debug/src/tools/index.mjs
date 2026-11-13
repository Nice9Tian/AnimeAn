import { ToolRegistry } from "../registry.mjs";
import { actionTools } from "./action.mjs";
import { connectionTools } from "./connection.mjs";
import { sceneTools } from "./scene.mjs";
import { snapshotTools } from "./snapshot.mjs";
import { stateTools } from "./state.mjs";
import { traceTools } from "./trace.mjs";

/**
 * Create and populate the default AnimeAn backend debug ToolRegistry.
 */
export function createDefaultRegistry() {
  const registry = new ToolRegistry();
  registry
    .registerAll(connectionTools)
    .registerAll(stateTools)
    .registerAll(sceneTools)
    .registerAll(traceTools)
    .registerAll(snapshotTools)
    .registerAll(actionTools);
  return registry;
}
