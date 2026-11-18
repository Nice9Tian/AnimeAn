import { spawnSync } from "node:child_process";
import net from "node:net";

/**
 * Ports and listen addresses for the dev launcher.
 *
 * On a machine shared by several accounts a port gate (`port-gate`, or the
 * program named by ANIMEAN_PORT_GATE) hands each account its own port range:
 * `port-gate alloc -n N` prints N free ports of the caller's range, one per
 * line, and `port-gate check <port>...` fails for a port outside it. Without a
 * gate, free ports come from the system.
 */

function gateCommand() {
  return process.env.ANIMEAN_PORT_GATE || "port-gate";
}

/** @returns {{ status: number | null, stdout: string, stderr: string } | null} null when there is no gate */
function runGate(args) {
  const res = spawnSync(gateCommand(), args, { encoding: "utf-8", windowsHide: true });
  if (res.error && /** @type {NodeJS.ErrnoException} */ (res.error).code === "ENOENT") {
    return null;
  }
  if (res.error) {
    throw new Error(`${gateCommand()} ${args.join(" ")} failed: ${res.error.message}`);
  }
  return { status: res.status, stdout: res.stdout || "", stderr: (res.stderr || "").trim() };
}

function freeSystemPort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.unref();
    server.on("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const { port } = /** @type {net.AddressInfo} */ (server.address());
      server.close(() => resolve(port));
    });
  });
}

/**
 * N free ports: from the gate when there is one, otherwise from the system.
 * @param {number} count
 * @returns {Promise<{ ports: number[], source: "port-gate" | "system" }>}
 */
export async function allocatePorts(count) {
  const res = runGate(["alloc", "-n", String(count)]);
  if (res === null) {
    const ports = [];
    for (let i = 0; i < count; i += 1) {
      ports.push(await freeSystemPort());
    }
    return { ports, source: "system" };
  }
  const ports = res.stdout.split(/\s+/).filter(Boolean).map(Number);
  if (res.status !== 0 || ports.length !== count || ports.some((p) => !Number.isInteger(p) || p <= 0)) {
    throw new Error(`${gateCommand()} could not allocate ${count} ports: ${res.stderr || res.stdout.trim()}`);
  }
  return { ports, source: "port-gate" };
}

/**
 * Throws when a gate exists and refuses one of the ports (outside this
 * account's range, or held by another account). Port 0 (system picks) passes.
 * @param {number[]} ports
 */
export function assertPortsAllowed(ports) {
  const fixed = ports.filter((p) => p > 0);
  if (fixed.length === 0) return;
  const res = runGate(["check", ...fixed.map(String)]);
  if (res !== null && res.status !== 0) {
    throw new Error(`${res.stderr || res.stdout.trim()}\nPass --auto to get ports from ${gateCommand()}.`);
  }
}

/**
 * The address to serve the page on. "tailscale" means this machine's
 * Tailscale IPv4 address.
 * @param {string | null | undefined} host
 * @returns {string | null}
 */
export function resolveListenHost(host) {
  if (!host) return null;
  if (host !== "tailscale") return String(host);
  const res = spawnSync("tailscale", ["ip", "-4"], { encoding: "utf-8", windowsHide: true });
  const ip = (res.stdout || "").split(/\s+/).find((s) => net.isIPv4(s));
  if (res.error || !ip) {
    throw new Error(`--host tailscale: could not read this machine's Tailscale address (${res.error?.message || res.stderr || "no IPv4"}).`);
  }
  return ip;
}

/** Host as written inside a URL. */
export function urlHost(host) {
  return net.isIPv6(host) ? `[${host}]` : host;
}
