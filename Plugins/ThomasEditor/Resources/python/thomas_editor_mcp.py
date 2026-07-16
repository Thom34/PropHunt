"""Dependency-free MCP stdio server for the project-local ThomasEditor bridge."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any
from urllib.error import URLError
from urllib.request import Request, urlopen


PROJECT_ROOT = Path(__file__).resolve().parents[4]
TOKEN_PATH = PROJECT_ROOT / "Saved" / "ThomasEditor" / "session.token"
REMOTE_URL = "http://127.0.0.1:30010/remote/object/call"
REMOTE_OBJECT = "/Script/ThomasEditor.Default__ThomasEditorBridge"
SERVER_INFO = {"name": "ThomasEditor", "version": "0.1.0"}
PROTOCOL_VERSION = "2025-06-18"

TOOLS = [
    {
        "name": "editor_status",
        "description": "Confirm the target project, engine, map, PIE state, and bridge version.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
        "annotations": {
            "readOnlyHint": True,
            "destructiveHint": False,
            "idempotentHint": True,
            "openWorldHint": False,
        },
    },
    {
        "name": "blueprint_summary",
        "description": "Read one Blueprint and return only requested properties or components.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "blueprint_path": {"type": "string"},
                "property_names": {"type": "array", "items": {"type": "string"}, "maxItems": 16},
                "include_components": {"type": "boolean", "default": False},
            },
            "required": ["blueprint_path"],
            "additionalProperties": False,
        },
        "annotations": {
            "readOnlyHint": True,
            "destructiveHint": False,
            "idempotentHint": True,
            "openWorldHint": False,
        },
    },
    {
        "name": "blueprint_patch",
        "description": "Apply a guarded parent or class-reference patch, then optionally compile and save once.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "blueprint_path": {"type": "string"},
                "new_parent_class": {"type": "string", "default": ""},
                "class_references": {
                    "type": "object",
                    "additionalProperties": {"type": "string"},
                    "maxProperties": 16,
                },
                "allow_destructive_reparent": {"type": "boolean", "default": False},
                "compile_and_save": {"type": "boolean", "default": True},
            },
            "required": ["blueprint_path"],
            "additionalProperties": False,
        },
        "annotations": {
            "readOnlyHint": False,
            "destructiveHint": True,
            "idempotentHint": False,
            "openWorldHint": False,
        },
    },
    {
        "name": "recent_messages",
        "description": "Return up to 50 recent warnings or errors, optionally filtered.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "limit": {"type": "integer", "minimum": 1, "maximum": 50, "default": 20},
                "filter_text": {"type": "string", "default": ""},
            },
            "additionalProperties": False,
        },
        "annotations": {
            "readOnlyHint": True,
            "destructiveHint": False,
            "idempotentHint": True,
            "openWorldHint": False,
        },
    },
]


def _call(function: str, parameters: dict[str, Any]) -> dict[str, Any]:
    try:
        token = TOKEN_PATH.read_text(encoding="utf-8").strip()
    except OSError as exc:
        return {"ok": False, "code": "session_unavailable", "message": str(exc)}

    parameters["SecurityToken"] = token
    payload = json.dumps(
        {
            "objectPath": REMOTE_OBJECT,
            "functionName": function,
            "parameters": parameters,
            "generateTransaction": False,
        },
        separators=(",", ":"),
    ).encode("utf-8")
    request = Request(REMOTE_URL, data=payload, method="PUT", headers={"Content-Type": "application/json"})
    try:
        with urlopen(request, timeout=30) as response:
            remote_response = json.loads(response.read().decode("utf-8"))
        return json.loads(remote_response["ReturnValue"])
    except (OSError, URLError, KeyError, ValueError) as exc:
        return {"ok": False, "code": "editor_unreachable", "message": str(exc)}


def _invoke_tool(name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    if name == "editor_status":
        return _call("EditorStatus", {})
    if name == "blueprint_summary":
        return _call(
            "BlueprintSummary",
            {
                "BlueprintPath": arguments["blueprint_path"],
                "PropertyNamesJson": json.dumps(arguments.get("property_names", []), separators=(",", ":")),
                "bIncludeComponents": arguments.get("include_components", False),
            },
        )
    if name == "blueprint_patch":
        return _call(
            "BlueprintPatch",
            {
                "BlueprintPath": arguments["blueprint_path"],
                "NewParentClass": arguments.get("new_parent_class", ""),
                "ClassReferencesJson": json.dumps(arguments.get("class_references", {}), separators=(",", ":")),
                "bAllowDestructiveReparent": arguments.get("allow_destructive_reparent", False),
                "bCompileAndSave": arguments.get("compile_and_save", True),
            },
        )
    if name == "recent_messages":
        return _call(
            "RecentMessages",
            {"Limit": arguments.get("limit", 20), "Filter": arguments.get("filter_text", "")},
        )
    raise KeyError(name)


def _success(request_id: Any, result: dict[str, Any]) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request_id, "result": result}


def _error(request_id: Any, code: int, message: str) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}


def _handle(message: dict[str, Any]) -> dict[str, Any] | None:
    request_id = message.get("id")
    method = message.get("method")
    params = message.get("params") or {}

    if request_id is None:
        return None
    if method == "initialize":
        return _success(
            request_id,
            {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": SERVER_INFO,
                "instructions": (
                    "PropHunt-only Editor bridge. Call editor_status first, inspect a Blueprint with "
                    "blueprint_summary before blueprint_patch, and never force a destructive reparent "
                    "without explicit user confirmation. Gameplay authority remains in C++."
                ),
            },
        )
    if method == "ping":
        return _success(request_id, {})
    if method == "tools/list":
        return _success(request_id, {"tools": TOOLS})
    if method == "tools/call":
        name = params.get("name", "")
        arguments = params.get("arguments") or {}
        try:
            value = _invoke_tool(name, arguments)
        except (KeyError, TypeError) as exc:
            return _error(request_id, -32602, f"Invalid tool or arguments: {exc}")
        return _success(
            request_id,
            {
                "content": [{"type": "text", "text": json.dumps(value, separators=(",", ":"))}],
                "structuredContent": value,
                "isError": not value.get("ok", False),
            },
        )
    return _error(request_id, -32601, f"Method not found: {method}")


def main() -> None:
    for line in sys.stdin:
        try:
            message = json.loads(line)
            response = _handle(message)
        except (json.JSONDecodeError, TypeError, ValueError) as exc:
            response = _error(None, -32700, f"Parse error: {exc}")
        if response is not None:
            sys.stdout.write(json.dumps(response, separators=(",", ":")) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
