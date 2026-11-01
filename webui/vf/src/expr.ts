// 表达式：状态路径、条件代号、数组下标访问、字符串、数字、true/false，
// 以及 == != < <= > >= + - and or not 和括号。
// 由这里的解析器处理，不使用 eval。

export type Value = string | number | boolean | null | Value[] | { [key: string]: Value };

export type BinaryOp = "==" | "!=" | "<" | "<=" | ">" | ">=" | "+" | "-" | "and" | "or";

export type Expr =
  | { kind: "literal"; value: Value }
  | { kind: "path"; path: string }
  | { kind: "index"; object: Expr; index: Expr; tail: string[] }
  | { kind: "not"; operand: Expr }
  | { kind: "binary"; op: BinaryOp; left: Expr; right: Expr }
  | { kind: "ternary"; cond: Expr; thenExpr: Expr; elseExpr: Expr };

type Token =
  | { type: "literal"; value: Value }
  | { type: "path"; path: string }
  | { type: "op"; op: BinaryOp | "not" | "(" | ")" | "[" | "]" | "?" | ":" };

// 名字可以用英文字母、下划线和非 ASCII 字符（例如中文）开头。
// 不用 \p{L} 这样的写法，因为 Qt 自带的 JS 引擎不一定支持，而这份代码也要在那里运行。
const NAME_START = /[^\x00-\x40\x5B-\x5E\x60\x7B-\x7F]/;
const NAME_PATH = /^[^\x00-\x40\x5B-\x5E\x60\x7B-\x7F][^\x00-\x2F\x3A-\x40\x5B-\x5E\x60\x7B-\x7F]*(\.[^\x00-\x40\x5B-\x5E\x60\x7B-\x7F][^\x00-\x2F\x3A-\x40\x5B-\x5E\x60\x7B-\x7F]*)*/;
const SIMPLE_ASSIGNMENT = /^\s*([^\x00-\x40\x5B-\x5E\x60\x7B-\x7F][^\x00-\x2D\x2F\x3A-\x40\x5B-\x5E\x60\x7B-\x7F]*)\s*=(?!=)([\s\S]+)$/;
const INDEXED_ASSIGNMENT =
  /^\s*([^\x00-\x40\x5B-\x5E\x60\x7B-\x7F][^\x00-\x2D\x2F\x3A-\x40\x5B-\x5E\x60\x7B-\x7F]*)\s*\[\s*([^\]]+?)\s*\](?:\.([^\x00-\x40\x5B-\x5E\x60\x7B-\x7F][^\x00-\x2D\x2F\x3A-\x40\x5B-\x5E\x60\x7B-\x7F]*))?\s*=(?!=)([\s\S]+)$/;

function canPrecedeUnaryMinus(prev: Token | undefined): boolean {
  if (!prev) return true;
  if (prev.type !== "op") return false;
  return prev.op !== ")" && prev.op !== "]";
}

function tokenize(source: string): Token[] {
  const tokens: Token[] = [];
  let i = 0;
  while (i < source.length) {
    const ch = source[i];
    if (/\s/.test(ch)) {
      i++;
    } else if (ch === "(" || ch === ")" || ch === "[" || ch === "]") {
      tokens.push({ type: "op", op: ch });
      i++;
    } else if (
      source.startsWith("==", i) ||
      source.startsWith("!=", i) ||
      source.startsWith("<=", i) ||
      source.startsWith(">=", i)
    ) {
      tokens.push({ type: "op", op: source.slice(i, i + 2) as BinaryOp });
      i += 2;
    } else if (ch === "<" || ch === ">" || ch === "+" || ch === "?" || ch === ":") {
      tokens.push({ type: "op", op: ch });
      i++;
    } else if (ch === "'" || ch === '"') {
      const end = source.indexOf(ch, i + 1);
      if (end < 0) throw new Error(`字符串缺少结束的引号：${source.slice(i)}`);
      tokens.push({ type: "literal", value: source.slice(i + 1, end) });
      i = end + 1;
    } else if (/[0-9]/.test(ch) || (ch === "-" && /[0-9]/.test(source[i + 1] ?? "") && canPrecedeUnaryMinus(tokens[tokens.length - 1]))) {
      const match = /^-?[0-9]+(\.[0-9]+)?/.exec(source.slice(i))!;
      tokens.push({ type: "literal", value: Number(match[0]) });
      i += match[0].length;
    } else if (ch === "-") {
      tokens.push({ type: "op", op: "-" });
      i++;
    } else if (ch === "." && tokens[tokens.length - 1]?.type === "op" && (tokens[tokens.length - 1] as { op: string }).op === "]") {
      const match = NAME_PATH.exec(source.slice(i + 1));
      if (!match) throw new Error(`下标后面的属性名不对：${source.slice(i)}`);
      tokens.push({ type: "path", path: "." + match[0] });
      i += 1 + match[0].length;
    } else if (NAME_START.test(ch)) {
      const match = NAME_PATH.exec(source.slice(i))!;
      const word = match[0];
      if (word === "and" || word === "or" || word === "not") tokens.push({ type: "op", op: word });
      else if (word === "true" || word === "false") tokens.push({ type: "literal", value: word === "true" });
      else tokens.push({ type: "path", path: word });
      i += word.length;
    } else {
      throw new Error(`表达式中有无法识别的字符“${ch}”`);
    }
  }
  return tokens;
}

export function parseExpr(source: string): Expr {
  const tokens = tokenize(String(source));
  let pos = 0;
  const peekOp = (op: string) => {
    const t = tokens[pos];
    return t?.type === "op" && t.op === op;
  };

  function parseTernary(): Expr {
    const cond = parseOr();
    if (peekOp("?")) {
      pos++;
      const thenExpr = parseTernary();
      if (!peekOp(":")) throw new Error("三目表达式缺少冒号“:”");
      pos++;
      const elseExpr = parseTernary();
      return { kind: "ternary", cond, thenExpr, elseExpr };
    }
    return cond;
  }
  function parseOr(): Expr {
    let left = parseAnd();
    while (peekOp("or")) {
      pos++;
      left = { kind: "binary", op: "or", left, right: parseAnd() };
    }
    return left;
  }
  function parseAnd(): Expr {
    let left = parseNot();
    while (peekOp("and")) {
      pos++;
      left = { kind: "binary", op: "and", left, right: parseNot() };
    }
    return left;
  }
  function parseNot(): Expr {
    if (peekOp("not")) {
      pos++;
      return { kind: "not", operand: parseNot() };
    }
    return parseCompare();
  }
  function parseCompare(): Expr {
    const left = parseAdd();
    if (peekOp("==") || peekOp("!=") || peekOp("<") || peekOp("<=") || peekOp(">") || peekOp(">=")) {
      const op = (tokens[pos++] as { op: BinaryOp }).op;
      return { kind: "binary", op, left, right: parseAdd() };
    }
    return left;
  }
  function parseAdd(): Expr {
    let left = parsePostfix();
    while (peekOp("+") || peekOp("-")) {
      const op = (tokens[pos++] as { op: "+" | "-" }).op;
      left = { kind: "binary", op, left, right: parsePostfix() };
    }
    return left;
  }
  function parsePostfix(): Expr {
    let expr = parsePrimary();
    while (peekOp("[")) {
      pos++;
      const index = parseTernary();
      if (!peekOp("]")) throw new Error("缺少右方括号");
      pos++;
      let tail: string[] = [];
      const next = tokens[pos];
      if (next?.type === "path" && next.path.startsWith(".")) {
        pos++;
        tail = next.path.slice(1).split(".");
      }
      expr = { kind: "index", object: expr, index, tail };
    }
    return expr;
  }
  function parsePrimary(): Expr {
    const t = tokens[pos++];
    if (!t) throw new Error("表达式不完整");
    if (t.type === "literal") return { kind: "literal", value: t.value };
    if (t.type === "path") {
      if (t.path.startsWith(".")) throw new Error(`表达式中“${t.path}”的位置不对`);
      return { kind: "path", path: t.path };
    }
    if (t.op === "(") {
      const inner = parseTernary();
      if (!peekOp(")")) throw new Error("缺少右括号");
      pos++;
      return inner;
    }
    throw new Error(`表达式中“${t.op}”的位置不对`);
  }

  const expr = parseTernary();
  if (pos < tokens.length) throw new Error("表达式后面有多余的内容");
  return expr;
}

function readTail(base: Value, tail: string[]): Value {
  let cursor: Value = base;
  for (const part of tail) {
    if (cursor === null || cursor === undefined) return null;
    if (Array.isArray(cursor)) {
      if (part === "length") cursor = cursor.length;
      else return null;
    } else if (typeof cursor === "object") {
      const next = (cursor as Record<string, Value>)[part];
      cursor = next === undefined ? null : next;
    } else if (typeof cursor === "string" && part === "length") {
      cursor = cursor.length;
    } else {
      return null;
    }
  }
  return cursor;
}

function sameValue(a: Value, b: Value): boolean {
  if (a === b) return true;
  if (typeof a === "object" && a !== null && typeof b === "object" && b !== null) {
    return JSON.stringify(a) === JSON.stringify(b);
  }
  return false;
}

/** resolve 负责把路径（状态路径或条件代号）变成值；找不到时应抛出错误。 */
export function evalExpr(expr: Expr, resolve: (path: string) => Value): Value {
  switch (expr.kind) {
    case "literal":
      return expr.value;
    case "path":
      return resolve(expr.path);
    case "index": {
      const container = evalExpr(expr.object, resolve);
      const idx = evalExpr(expr.index, resolve);
      if (Array.isArray(container)) {
        const n = Number(idx);
        const item = Number.isInteger(n) && n >= 0 && n < container.length ? container[n] : null;
        return readTail(item, expr.tail);
      }
      if (typeof container === "object" && container !== null) {
        const item = (container as Record<string, Value>)[String(idx)] ?? null;
        return readTail(item, expr.tail);
      }
      return null;
    }
    case "not":
      return !evalExpr(expr.operand, resolve);
    case "binary": {
      if (expr.op === "and") return Boolean(evalExpr(expr.left, resolve)) && Boolean(evalExpr(expr.right, resolve));
      if (expr.op === "or") return Boolean(evalExpr(expr.left, resolve)) || Boolean(evalExpr(expr.right, resolve));
      const left = evalExpr(expr.left, resolve);
      const right = evalExpr(expr.right, resolve);
      if (expr.op === "==") return sameValue(left, right);
      if (expr.op === "!=") return !sameValue(left, right);
      if (expr.op === "+") {
        if (typeof left === "string" || typeof right === "string") return String(left ?? "") + String(right ?? "");
        return Number(left ?? 0) + Number(right ?? 0);
      }
      if (expr.op === "-") return Number(left ?? 0) - Number(right ?? 0);
      if (expr.op === "<") return Number(left ?? 0) < Number(right ?? 0);
      if (expr.op === "<=") return Number(left ?? 0) <= Number(right ?? 0);
      if (expr.op === ">") return Number(left ?? 0) > Number(right ?? 0);
      return Number(left ?? 0) >= Number(right ?? 0);
    }
    case "ternary":
      return evalExpr(expr.cond, resolve) ? evalExpr(expr.thenExpr, resolve) : evalExpr(expr.elseExpr, resolve);
  }
}

/** 表达式中出现的全部路径，用于检查名字是否存在。 */
export function pathsIn(expr: Expr): string[] {
  switch (expr.kind) {
    case "literal":
      return [];
    case "path":
      return [expr.path];
    case "index":
      return [...pathsIn(expr.object), ...pathsIn(expr.index)];
    case "not":
      return pathsIn(expr.operand);
    case "binary":
      return [...pathsIn(expr.left), ...pathsIn(expr.right)];
    case "ternary":
      return [...pathsIn(expr.cond), ...pathsIn(expr.thenExpr), ...pathsIn(expr.elseExpr)];
  }
}

export interface ParsedAssignment {
  target: string;
  index?: Expr;
  field?: string;
  value: Expr;
}

/** 解析 `状态路径 = 表达式` 或 `数组路径[下标].字段 = 表达式` 形式的赋值语句。 */
export function parseAssignment(source: string): ParsedAssignment {
  const indexed = INDEXED_ASSIGNMENT.exec(String(source));
  if (indexed) {
    return {
      target: indexed[1],
      index: parseExpr(indexed[2]),
      field: indexed[3] || undefined,
      value: parseExpr(indexed[4]),
    };
  }
  const match = SIMPLE_ASSIGNMENT.exec(String(source));
  if (!match) throw new Error(`set 的写法应为“状态路径 = 表达式”：${source}`);
  return { target: match[1], value: parseExpr(match[2]) };
}
