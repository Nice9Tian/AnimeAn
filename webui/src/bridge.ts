import type { Value } from "../vf/src/expr.ts";
import type { Runtime, RuntimeHooks } from "../vf/src/runtime.ts";
import type { BackendClient } from "./net.ts";

/**
 * 后端 state 消息到 Runtime 业务状态的映射条目：
 * - 字符串：直接将消息的 value 写入对应的 Runtime 业务状态路径（调用 setBusinessState）
 * - 函数：将后端下发的复杂结构拆解或转换为一个或多个 { [statePath]: Value }，再逐项调用 setBusinessState
 */
export type StateMapEntry =
  | string
  | ((value: unknown, runtime: Runtime) => Record<string, Value> | void);

export type StateMapping = Record<string, StateMapEntry>;

/**
 * 示例映射表（后续由派活方按实际设计项目状态路径补充）。
 */
export const DEFAULT_STATE_MAPPING: StateMapping = {
  "layer/visible": "layer.visible",
  "layers/main": (value) => {
    if (value && typeof value === "object" && !Array.isArray(value)) {
      const v = value as Record<string, unknown>;
      if (typeof v.visible === "boolean") {
        return { "layer.visible": v.visible };
      }
    }
    return undefined;
  },
};

export interface BridgeHandle {
  hooks: Pick<RuntimeHooks, "raiseBusiness" | "raiseAction">;
  dispose(): void;
}

/**
 * 生成可传入 Runtime / createPlayer 的 raiseBusiness 与 raiseAction 钩子。
 */
export function createBridgeHooks(
  client: BackendClient,
  getRuntime: () => Runtime | null,
): Pick<RuntimeHooks, "raiseBusiness" | "raiseAction"> {
  return {
    raiseBusiness(name: string, args: Record<string, Value>): boolean {
      void client.call(name, args as Record<string, unknown>).catch(() => {
        // 断线或后端报错时不抛未捕获异常
      });
      return true;
    },
    raiseAction(name: string, input: Record<string, Value>): boolean {
      void client
        .call(name, input as Record<string, unknown>)
        .then(
          (result) => {
            getRuntime()?.finishActionSuccess(name, (result as Value) ?? null);
          },
          (err) => {
            const msg = err instanceof Error ? err.message : String(err);
            getRuntime()?.finishActionError(name, msg);
          },
        );
      return true;
    },
  };
}

/**
 * 将 Runtime 与 BackendClient 双向桥接：
 * 1. Runtime 的 raiseBusiness 转为 client.call(name, args) 并返回 true；
 * 2. Runtime 的 raiseAction 转为 client.call(name, input) 并返回 true，完成后回调 finishActionSuccess / finishActionError；
 * 3. BackendClient 收到的 state 消息按 mapping 映射调用 runtime.setBusinessState。
 */
export function attachBridge(
  runtime: Runtime,
  client: BackendClient,
  mapping: StateMapping = DEFAULT_STATE_MAPPING,
): BridgeHandle {
  const hooks = createBridgeHooks(client, () => runtime);
  const runtimeHooks = (runtime as unknown as { hooks?: RuntimeHooks }).hooks;
  const prevRaiseBusiness = runtimeHooks?.raiseBusiness;
  const prevRaiseAction = runtimeHooks?.raiseAction;

  if (runtimeHooks) {
    runtimeHooks.raiseBusiness = hooks.raiseBusiness;
    runtimeHooks.raiseAction = hooks.raiseAction;
  }

  const offState = client.onState("*", (value, key) => {
    const entry = mapping[key];
    if (!entry) return;
    if (typeof entry === "string") {
      runtime.setBusinessState(entry, value as Value);
      return;
    }
    const mapped = entry(value, runtime);
    if (mapped && typeof mapped === "object") {
      for (const [path, val] of Object.entries(mapped)) {
        runtime.setBusinessState(path, val);
      }
    }
  });

  return {
    hooks,
    dispose() {
      offState();
      if (runtimeHooks) {
        runtimeHooks.raiseBusiness = prevRaiseBusiness;
        runtimeHooks.raiseAction = prevRaiseAction;
      }
    },
  };
}
