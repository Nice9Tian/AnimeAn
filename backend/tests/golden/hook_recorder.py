import json
import math
import os
import re
import python_hooks

# To 3D invokes os.startfile on an HTML viewer, which would pop up a browser
# during automated runs if anything ever reached that action.
os.startfile = lambda *args, **kwargs: None

_ADDR_RE = re.compile(r"\b0x[0-9a-fA-F]+\b")
_WIN_PATH_RE = re.compile(r"[A-Za-z]:[\\/][^\s\"'<>|]+")

_records = []
_depth = 0


def _sanitize_str(value):
    text = _ADDR_RE.sub("0xADDR", value)
    tmp_dir = os.environ.get("ANIMEAN_GOLDEN_TMP_DIR", "")
    if tmp_dir:
        for variant in (tmp_dir, tmp_dir.replace("\\", "/"), tmp_dir.replace("/", "\\")):
            if variant:
                text = text.replace(variant, "<TMP_DIR>")
    return text


def _to_jsonable(value):
    if value is None or isinstance(value, (bool, int)):
        return value
    if isinstance(value, float):
        if math.isnan(value):
            return "NaN"
        if math.isinf(value):
            return "Infinity" if value > 0 else "-Infinity"
        # Collapse -0.0 and tiny ULP drift so cross-backend float comparisons stay stable.
        return 0.0 if value == 0.0 else round(float(value), 6)
    if isinstance(value, str):
        return _sanitize_str(value)
    if isinstance(value, (bytes, bytearray)):
        return bytes(value).hex()
    if isinstance(value, (list, tuple)):
        return [_to_jsonable(item) for item in value]
    if isinstance(value, (set, frozenset)):
        items = [_to_jsonable(item) for item in value]
        return sorted(items, key=lambda x: json.dumps(x, sort_keys=True, ensure_ascii=False))
    if isinstance(value, dict):
        out = {}
        for k, v in sorted(value.items(), key=lambda kv: str(kv[0])):
            out[ _sanitize_str(str(k)) ] = _to_jsonable(v)
        return out
    try:
        raw_repr = repr(value)
    except Exception as exc:
        raw_repr = f"<unreprable {type(value).__name__}: {exc}>"
    return _sanitize_str(raw_repr)


def _flush_to_disk():
    log_path = os.environ.get("ANIMEAN_GOLDEN_HOOK_LOG", "")
    if not log_path or not _records:
        return
    try:
        with open(log_path, "a", encoding="utf-8") as fh:
            for item in _records:
                fh.write(json.dumps(item, ensure_ascii=False, sort_keys=True) + "\n")
            fh.flush()
        _records.clear()
    except Exception:
        pass


_orig_dispatch = getattr(python_hooks, "_golden_orig_dispatch", python_hooks.dispatch)
python_hooks._golden_orig_dispatch = _orig_dispatch


def _wrapped_dispatch(message):
    global _depth
    event_name = ""
    if isinstance(message, dict):
        event_name = str(message.get("event", "")).strip().lower()

    # Reserve a slot before running callbacks so nested dispatches (e.g. a hook
    # mutating current layer and triggering layerchange) appear in entry order.
    slot_index = len(_records)
    _records.append(None)
    _depth += 1
    try:
        result = _orig_dispatch(message)
        return result
    finally:
        _depth -= 1
        _records[slot_index] = {
            "event": event_name,
            "message": _to_jsonable(message),
        }
        if _depth == 0:
            _flush_to_disk()


python_hooks.dispatch = _wrapped_dispatch


def pop_golden_records():
    items = [item for item in _records if item is not None]
    _records.clear()
    return items


python_hooks.pop_golden_records = pop_golden_records
print("GOLDEN_HOOK_RECORDER_READY")
