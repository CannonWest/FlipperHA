#!/usr/bin/env python3
"""Tiny LAN bridge for the FlipperHTTP HA state probe.

The FlipperHTTP UART command format is too small for a full Home Assistant
bearer-token POST. This bridge keeps the HA token on the workstation and gives
the Flipper a compact GET endpoint that returns display-ready text.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ENV = REPO_ROOT / ".env"
DEFAULT_HA_URL = "http://homeassistant.local:8123"
DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8765
DEFAULT_KEY = "flipper"
DEFAULT_LOG_PATH = REPO_ROOT / "data" / "state" / "flipperha_bridge_commands.jsonl"
THERMOSTAT_ENTITY_ID = ""


@dataclass(frozen=True)
class BridgeAction:
    action_id: str
    label: str
    service_domain: str
    service_name: str
    entity_ids: tuple[str, ...]
    state_entity_id: str | None
    is_dimmer: bool = False


# Public builds start with no built-in rows. Users add entities from the on-device Add flow.
# Advanced forks can populate ACTIONS with BridgeAction(...) entries to support legacy /v1/action/<id>
# and BLE-style action IDs.
ACTIONS: list[BridgeAction] = []
ACTION_BY_ID = {action.action_id: action for action in ACTIONS}
THERMOSTAT_HVAC_MODES = {"off", "heat", "cool", "heat_cool"}
THERMOSTAT_FAN_MODES = {"Auto low", "Low", "Circulation"}
CATALOG_PAGE_SIZE = 4


@dataclass(frozen=True)
class CatalogEntity:
    entity_id: str
    label: str
    type_label: str
    capability: str


def read_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def normalize_ha_url(value: str) -> str:
    url = value.strip().rstrip("/")
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc:
        raise ValueError(
            "HA_BASE_URL must be an http(s) URL, for example http://homeassistant.local:8123"
        )
    return url


def setup_check_text(ha_url: str, token: str, timeout: float) -> str:
    states = fetch_all_states(ha_url, token, timeout)
    host = urllib.parse.urlparse(ha_url).netloc or ha_url
    return f"ok | ha={host} | states={len(states)} | builtins={len(ACTIONS)}"


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def compact_text(value: str | None, limit: int = 500) -> str | None:
    if value is None or len(value) <= limit:
        return value
    return value[: limit - 3] + "..."


def append_command_log(log_path: Path | None, event: dict) -> None:
    if not log_path:
        return
    try:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        with log_path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(event, sort_keys=True, separators=(",", ":")) + "\n")
    except Exception as exc:
        print(f"log_write_failed={type(exc).__name__}", file=sys.stderr, flush=True)


def tail_command_log(log_path: Path, count: int) -> str:
    if not log_path.exists():
        return ""
    count = max(1, min(count, 100))
    lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    return "\n".join(lines[-count:])


def dimmer_state_template(entity_id: str) -> str:
    return (
        f"{{% set s = states('{entity_id}') %}}"
        "{% if s == 'off' %}OFF"
        "{% elif s in ['unknown', 'unavailable'] %}{{ s }}"
        f"{{% else %}}{{{{ ((state_attr('{entity_id}', 'brightness') | int(0) * 100 / 255) | round(0) | int) }}}}%"
        "{% endif %}"
    )


def action_state_template(action: BridgeAction) -> str:
    if not action.state_entity_id:
        return "act"
    if action.is_dimmer:
        return dimmer_state_template(action.state_entity_id)
    return f"{{{{ states('{action.state_entity_id}') }}}}"


def build_template() -> str:
    parts = [
        f"{action.label}={action_state_template(action)}"
        for action in ACTIONS
        if action.state_entity_id
    ]
    return " | ".join(parts)


def build_v1_template() -> str:
    parts = [f"{action.action_id}={action_state_template(action)}" for action in ACTIONS]
    return " | ".join(parts)


def ha_request(
    ha_url: str,
    token: str,
    path: str,
    body: bytes | None,
    timeout: float,
) -> str:
    url = ha_url.rstrip("/") + path
    headers = {
        "Authorization": f"Bearer {token}",
        "Content-Type": "application/json",
    }
    request = urllib.request.Request(
        url,
        data=body,
        method="POST" if body is not None else "GET",
        headers=headers,
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read().decode("utf-8", errors="replace").strip()


def fetch_ha_state(ha_url: str, token: str, entity_id: str, timeout: float) -> dict:
    encoded_entity_id = urllib.parse.quote(entity_id, safe="")
    text = ha_request(ha_url, token, f"/api/states/{encoded_entity_id}", None, timeout)
    return json.loads(text)


def fetch_ha_template(ha_url: str, token: str, template: str, timeout: float) -> str:
    body = json.dumps({"template": template}).encode("utf-8")
    return ha_request(ha_url, token, "/api/template", body, timeout)


def fetch_ha_states(ha_url: str, token: str, timeout: float) -> str:
    return fetch_ha_template(ha_url, token, build_template(), timeout)


def fetch_v1_states(ha_url: str, token: str, timeout: float) -> str:
    return fetch_ha_template(ha_url, token, build_v1_template(), timeout)


def fetch_all_states(ha_url: str, token: str, timeout: float) -> list[dict]:
    text = ha_request(ha_url, token, "/api/states", None, timeout)
    parsed = json.loads(text)
    if not isinstance(parsed, list):
        raise ValueError("states response was not a list")
    return parsed


def sanitize_catalog_field(value: object, limit: int = 44) -> str:
    text = compact_value(value).strip()
    text = re.sub(r"[\r\n|~]+", " ", text)
    text = re.sub(r"\s+", " ", text).strip()
    if not text:
        text = "--"
    if len(text) > limit:
        text = text[: limit - 1].rstrip() + "."
    return text


def entity_label(state: dict) -> str:
    attributes = state.get("attributes") or {}
    return sanitize_catalog_field(attributes.get("friendly_name") or state.get("entity_id") or "--")


def light_is_dimmable(attributes: dict) -> bool:
    modes = attributes.get("supported_color_modes") or []
    if isinstance(modes, str):
        modes = [modes]
    if modes:
        return any(str(mode) != "onoff" for mode in modes)
    return attributes.get("brightness") is not None


def catalog_entity(category: str, state: dict) -> CatalogEntity | None:
    entity_id = str(state.get("entity_id") or "")
    if "." not in entity_id:
        return None

    domain = entity_id.split(".", 1)[0]
    attributes = state.get("attributes") or {}
    label = entity_label(state)

    if category in {"switch", "switches"} and domain == "switch":
        return CatalogEntity(entity_id, label, "switch", "toggle")

    if category in {"light", "lights"} and domain == "light":
        dimmable = light_is_dimmable(attributes)
        return CatalogEntity(entity_id, label, "light", "dim" if dimmable else "toggle")

    if category in {"routine", "routines"} and domain in {"scene", "script", "automation"}:
        return CatalogEntity(entity_id, label, domain, "run")

    if category in {"climate", "thermostat", "thermostats"} and domain == "climate":
        return CatalogEntity(entity_id, label, "climate", "thermo")

    return None


def catalog_line(entity: CatalogEntity) -> str:
    return "~".join(
        [
            sanitize_catalog_field(entity.entity_id, 72),
            sanitize_catalog_field(entity.label, 44),
            sanitize_catalog_field(entity.type_label, 16),
            sanitize_catalog_field(entity.capability, 8),
        ]
    )


def fetch_catalog_page(
    ha_url: str,
    token: str,
    timeout: float,
    category: str,
    offset: int,
) -> str:
    entities = [
        entity
        for state in fetch_all_states(ha_url, token, timeout)
        if (entity := catalog_entity(category, state)) is not None
    ]
    entities.sort(key=lambda entity: (entity.label.lower(), entity.entity_id))
    offset = max(0, min(offset, len(entities)))
    page = entities[offset : offset + CATALOG_PAGE_SIZE]
    parts = [f"T={len(entities)}", f"O={offset}"]
    parts.extend(catalog_line(entity) for entity in page)
    return "|".join(parts)


def display_state_from_state(state: dict) -> str:
    entity_id = str(state.get("entity_id") or "")
    domain = entity_id.split(".", 1)[0] if "." in entity_id else ""
    value = str(state.get("state") or "--")
    attributes = state.get("attributes") or {}

    if domain == "light" and light_is_dimmable(attributes):
        if value == "off":
            return "OFF"
        if value in {"unknown", "unavailable"}:
            return value
        brightness = int(attributes.get("brightness") or 0)
        return f"{round(brightness * 100 / 255)}%"

    if domain in {"scene", "script", "automation"}:
        return "Run"

    return value


def fetch_entity_display_state(ha_url: str, token: str, entity_id: str, timeout: float) -> str:
    return display_state_from_state(fetch_ha_state(ha_url, token, entity_id, timeout))


def call_entity_toggle(ha_url: str, token: str, entity_id: str, timeout: float) -> None:
    domain = entity_id.split(".", 1)[0]
    if domain not in {"switch", "light"}:
        raise ValueError(f"cannot toggle {domain}")
    call_ha_service(ha_url, token, domain, "toggle", {"entity_id": entity_id}, timeout)


def call_entity_run(ha_url: str, token: str, entity_id: str, timeout: float) -> None:
    domain = entity_id.split(".", 1)[0]
    if domain == "scene":
        call_ha_service(ha_url, token, "scene", "turn_on", {"entity_id": entity_id}, timeout)
        return
    if domain == "script":
        call_ha_service(ha_url, token, "script", "turn_on", {"entity_id": entity_id}, timeout)
        return
    if domain == "automation":
        call_ha_service(ha_url, token, "automation", "trigger", {"entity_id": entity_id}, timeout)
        return
    raise ValueError(f"cannot run {domain}")


def call_entity_dimmer(ha_url: str, token: str, entity_id: str, timeout: float, percent: int) -> None:
    if not entity_id.startswith("light."):
        raise ValueError("not light")
    percent = clamp_percent(percent)
    if percent <= 0:
        call_ha_service(ha_url, token, "light", "turn_off", {"entity_id": entity_id}, timeout)
        return
    call_ha_service(
        ha_url,
        token,
        "light",
        "turn_on",
        {"entity_id": entity_id, "brightness_pct": percent},
        timeout,
    )


def call_ha_service(
    ha_url: str,
    token: str,
    service_domain: str,
    service_name: str,
    service_data: dict,
    timeout: float,
) -> None:
    body = json.dumps(service_data).encode("utf-8")
    ha_request(
        ha_url,
        token,
        f"/api/services/{service_domain}/{service_name}",
        body,
        timeout,
    )


def call_ha_action(ha_url: str, token: str, action: BridgeAction, timeout: float) -> None:
    if action.is_dimmer:
        raise ValueError("use dimmer endpoint")
    call_ha_service(
        ha_url,
        token,
        action.service_domain,
        action.service_name,
        {"entity_id": list(action.entity_ids)},
        timeout,
    )


def clamp_percent(value: int) -> int:
    return max(0, min(100, value))


def call_ha_dimmer(ha_url: str, token: str, action: BridgeAction, timeout: float, percent: int) -> None:
    if not action.is_dimmer:
        raise ValueError("not a dimmer")

    percent = clamp_percent(percent)
    if percent <= 0:
        call_ha_service(
            ha_url,
            token,
            "light",
            "turn_off",
            {"entity_id": list(action.entity_ids)},
            timeout,
        )
        return

    call_ha_service(
        ha_url,
        token,
        "light",
        "turn_on",
        {"entity_id": list(action.entity_ids), "brightness_pct": percent},
        timeout,
    )


def compact_value(value: object) -> str:
    if value is None:
        return "--"
    if isinstance(value, float):
        return str(int(value)) if value.is_integer() else f"{value:.1f}"
    return str(value)


def compact_option(value: object, limit: int = 24) -> str:
    text = compact_value(value).strip()
    text = re.sub(r"[\r\n|,~]+", " ", text)
    text = re.sub(r"\s+", " ", text).strip()
    if len(text) > limit:
        text = text[:limit].rstrip()
    return text


def thermostat_option_list(attributes: dict, key: str) -> str:
    values = attributes.get(key) or []
    if isinstance(values, str):
        values = [values]
    compacted = [compact_option(value) for value in values]
    compacted = [value for value in compacted if value]
    return ",".join(compacted)


def thermostat_state_line(ha_url: str, token: str, timeout: float, entity_id: str) -> str:
    state = fetch_ha_state(ha_url, token, entity_id, timeout)
    attributes = state.get("attributes", {})
    return " | ".join(
        [
            f"temp={compact_value(attributes.get('current_temperature'))}",
            f"target={compact_value(attributes.get('temperature'))}",
            f"mode={compact_value(state.get('state'))}",
            f"action={compact_value(attributes.get('hvac_action'))}",
            f"fan={compact_value(attributes.get('fan_mode'))}",
            f"hum={compact_value(attributes.get('current_humidity'))}",
            f"modes={thermostat_option_list(attributes, 'hvac_modes')}",
            f"fans={thermostat_option_list(attributes, 'fan_modes')}",
        ]
    )


def thermostat_target(attributes: dict) -> float:
    for key in ("temperature", "target_temp_high", "target_temp_low", "current_temperature"):
        value = attributes.get(key)
        if value is not None:
            return float(value)
    raise ValueError("no thermostat target")


def clamp_temperature(value: float, attributes: dict) -> float:
    min_temp = float(attributes.get("min_temp") or 45)
    max_temp = float(attributes.get("max_temp") or 95)
    return max(min_temp, min(max_temp, value))


def default_thermostat_entity() -> str:
    if not THERMOSTAT_ENTITY_ID:
        raise ValueError("thermostat setup needed")
    return THERMOSTAT_ENTITY_ID


def decode_entity_path_part(value: str) -> str:
    entity_id = urllib.parse.unquote(value).strip()
    if not entity_id.startswith("climate."):
        raise ValueError("not climate")
    return entity_id


def call_thermostat_set_temperature(
    ha_url: str,
    token: str,
    timeout: float,
    entity_id: str,
    target: float,
) -> None:
    state = fetch_ha_state(ha_url, token, entity_id, timeout)
    attributes = state.get("attributes", {})
    new_target = clamp_temperature(target, attributes)
    call_ha_service(
        ha_url,
        token,
        "climate",
        "set_temperature",
        {"entity_id": entity_id, "temperature": new_target},
        timeout,
    )


def call_thermostat_temp_delta(
    ha_url: str,
    token: str,
    timeout: float,
    entity_id: str,
    delta: float,
) -> None:
    state = fetch_ha_state(ha_url, token, entity_id, timeout)
    attributes = state.get("attributes", {})
    call_thermostat_set_temperature(
        ha_url,
        token,
        timeout,
        entity_id,
        thermostat_target(attributes) + delta,
    )


def call_thermostat_hvac_mode(
    ha_url: str,
    token: str,
    timeout: float,
    entity_id: str,
    mode: str,
) -> None:
    state = fetch_ha_state(ha_url, token, entity_id, timeout)
    available = set(state.get("attributes", {}).get("hvac_modes") or THERMOSTAT_HVAC_MODES)
    if mode not in available:
        raise ValueError(f"unsupported hvac mode {mode}")
    call_ha_service(
        ha_url,
        token,
        "climate",
        "set_hvac_mode",
        {"entity_id": entity_id, "hvac_mode": mode},
        timeout,
    )


def call_thermostat_fan_mode(
    ha_url: str,
    token: str,
    timeout: float,
    entity_id: str,
    fan_mode: str,
) -> None:
    if fan_mode in {"", "--", "none"}:
        return
    state = fetch_ha_state(ha_url, token, entity_id, timeout)
    available = set(state.get("attributes", {}).get("fan_modes") or [])
    if available and fan_mode not in available:
        raise ValueError(f"unsupported fan mode {fan_mode}")
    call_ha_service(
        ha_url,
        token,
        "climate",
        "set_fan_mode",
        {"entity_id": entity_id, "fan_mode": fan_mode},
        timeout,
    )


def call_thermostat_apply(
    ha_url: str,
    token: str,
    timeout: float,
    entity_id: str,
    target: float,
    mode: str,
    fan_mode: str,
) -> None:
    call_thermostat_set_temperature(ha_url, token, timeout, entity_id, target)
    call_thermostat_hvac_mode(ha_url, token, timeout, entity_id, mode)
    call_thermostat_fan_mode(ha_url, token, timeout, entity_id, fan_mode)

def make_handler(
    ha_url: str,
    token: str,
    key: str,
    timeout: float,
    post_action_delay: float,
    log_path: Path | None,
):
    class Handler(BaseHTTPRequestHandler):
        server_version = "HAStateBridge/0.9"

        def log_message(self, format: str, *args) -> None:  # noqa: A002
            message = re.sub(r"([?&]k=)[^\s&]+", r"\1<redacted>", format % args)
            sys.stderr.write("%s - %s\n" % (self.address_string(), message))

        def send_text(self, status: int, text: str) -> None:
            encoded = text.encode("utf-8", errors="replace")
            self.send_response(status)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(encoded)))
            self.end_headers()
            self.wfile.write(encoded)

        def key_allowed(self, params: dict[str, list[str]]) -> bool:
            return not key or params.get("k", [""])[0] == key

        def command_event(
            self,
            started: float,
            *,
            kind: str,
            path: str,
            status: int,
            ok: bool,
            action: BridgeAction | None = None,
            entity_id: str | None = None,
            command: str | None = None,
            response: str | None = None,
            error: str | None = None,
        ) -> None:
            event = {
                "ts": utc_now_iso(),
                "elapsed_ms": round((time.perf_counter() - started) * 1000, 1),
                "client": self.client_address[0],
                "kind": kind,
                "path": path,
                "status": status,
                "ok": ok,
            }
            if action:
                event.update(
                    {
                        "action_id": action.action_id,
                        "label": action.label,
                        "service": f"{action.service_domain}.{action.service_name}",
                        "entity_ids": list(action.entity_ids),
                    }
                )
            if entity_id:
                event["entity_id"] = entity_id
            if command:
                event["command"] = command
            if response:
                event["response"] = compact_text(response)
            if error:
                event["error"] = compact_text(error, 200)
            append_command_log(log_path, event)

        def do_GET(self) -> None:
            parsed = urllib.parse.urlparse(self.path)
            params = urllib.parse.parse_qs(parsed.query)

            if parsed.path in {"/health", "/healthz"}:
                self.send_text(200, "ok")
                return

            if not self.key_allowed(params):
                if (
                    parsed.path.startswith("/v1/action/")
                    or parsed.path.startswith("/v1/dimmer/")
                    or parsed.path.startswith("/v1/entity/")
                    or parsed.path.startswith("/v1/thermostat/")
                ):
                    self.command_event(
                        time.perf_counter(),
                        kind="auth",
                        path=parsed.path,
                        status=403,
                        ok=False,
                        error="forbidden",
                    )
                self.send_text(403, "forbidden")
                return

            if parsed.path == "/v1/logs":
                try:
                    count = int(params.get("n", ["30"])[0])
                except ValueError:
                    count = 30
                self.send_text(200, tail_command_log(log_path, count) if log_path else "")
                return

            if parsed.path == "/v1/setup/check":
                try:
                    text = setup_check_text(ha_url, token, timeout)
                except urllib.error.HTTPError as exc:
                    self.send_text(502, f"ha http {exc.code}")
                    return
                except Exception as exc:
                    self.send_text(502, f"ha error {type(exc).__name__}")
                    return

                self.send_text(200, text)
                return

            if parsed.path == "/states":
                try:
                    text = fetch_ha_states(ha_url, token, timeout)
                except urllib.error.HTTPError as exc:
                    self.send_text(502, f"ha http {exc.code}")
                    return
                except Exception as exc:  # keep Flipper output compact
                    self.send_text(502, f"ha error {type(exc).__name__}")
                    return

                self.send_text(200, text or "empty")
                return

            if parsed.path == "/v1/states":
                try:
                    text = fetch_v1_states(ha_url, token, timeout)
                except urllib.error.HTTPError as exc:
                    self.send_text(502, f"ha http {exc.code}")
                    return
                except Exception as exc:
                    self.send_text(502, f"ha error {type(exc).__name__}")
                    return

                self.send_text(200, text or "empty")
                return

            if parsed.path.startswith("/v1/catalog/"):
                parts = parsed.path.split("/")
                category = parts[3] if len(parts) > 3 else ""
                try:
                    offset = int(parts[4]) if len(parts) > 4 else 0
                except ValueError:
                    offset = 0
                try:
                    text = fetch_catalog_page(ha_url, token, timeout, category, offset)
                except urllib.error.HTTPError as exc:
                    self.send_text(502, f"ha http {exc.code}")
                    return
                except Exception as exc:
                    self.send_text(502, f"ha error {type(exc).__name__}")
                    return

                self.send_text(200, text or "empty")
                return

            if parsed.path.startswith("/v1/entity/state/"):
                entity_id = urllib.parse.unquote(parsed.path.rsplit("/", 1)[-1])
                try:
                    text = fetch_entity_display_state(ha_url, token, entity_id, timeout)
                except urllib.error.HTTPError as exc:
                    self.send_text(502, f"ha http {exc.code}")
                    return
                except Exception as exc:
                    self.send_text(502, f"ha error {type(exc).__name__}")
                    return

                self.send_text(200, text or "--")
                return

            if parsed.path.startswith("/v1/entity/toggle/"):
                started = time.perf_counter()
                entity_id = urllib.parse.unquote(parsed.path.rsplit("/", 1)[-1])
                try:
                    call_entity_toggle(ha_url, token, entity_id, timeout)
                    if post_action_delay > 0:
                        time.sleep(post_action_delay)
                    text = fetch_entity_display_state(ha_url, token, entity_id, timeout)
                except urllib.error.HTTPError as exc:
                    self.command_event(
                        started,
                        kind="entity",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        entity_id=entity_id,
                        command="toggle",
                        error=f"ha http {exc.code}",
                    )
                    self.send_text(502, f"ha http {exc.code}")
                    return
                except Exception as exc:
                    self.command_event(
                        started,
                        kind="entity",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        entity_id=entity_id,
                        command="toggle",
                        error=f"ha error {type(exc).__name__}",
                    )
                    self.send_text(502, f"ha error {type(exc).__name__}")
                    return

                self.command_event(
                    started,
                    kind="entity",
                    path=parsed.path,
                    status=200,
                    ok=True,
                    entity_id=entity_id,
                    command="toggle",
                    response=text,
                )
                self.send_text(200, text or "ok")
                return

            if parsed.path.startswith("/v1/entity/run/"):
                started = time.perf_counter()
                entity_id = urllib.parse.unquote(parsed.path.rsplit("/", 1)[-1])
                try:
                    call_entity_run(ha_url, token, entity_id, timeout)
                    text = "Run"
                except urllib.error.HTTPError as exc:
                    self.command_event(
                        started,
                        kind="entity",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        entity_id=entity_id,
                        command="run",
                        error=f"ha http {exc.code}",
                    )
                    self.send_text(502, f"ha http {exc.code}")
                    return
                except Exception as exc:
                    self.command_event(
                        started,
                        kind="entity",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        entity_id=entity_id,
                        command="run",
                        error=f"ha error {type(exc).__name__}",
                    )
                    self.send_text(502, f"ha error {type(exc).__name__}")
                    return

                self.command_event(
                    started,
                    kind="entity",
                    path=parsed.path,
                    status=200,
                    ok=True,
                    entity_id=entity_id,
                    command="run",
                    response=text,
                )
                self.send_text(200, text)
                return

            if parsed.path.startswith("/v1/entity/dimmer/"):
                started = time.perf_counter()
                parts = parsed.path.split("/")
                entity_id = ""
                percent_text = ""
                if len(parts) == 6:
                    entity_id = urllib.parse.unquote(parts[4])
                    percent_text = urllib.parse.unquote(parts[5])

                if not entity_id:
                    self.command_event(
                        started,
                        kind="entity",
                        path=parsed.path,
                        status=400,
                        ok=False,
                        command="brightness",
                        error="bad dimmer",
                    )
                    self.send_text(400, "bad dimmer")
                    return

                try:
                    percent = clamp_percent(int(percent_text))
                except ValueError:
                    self.command_event(
                        started,
                        kind="entity",
                        path=parsed.path,
                        status=400,
                        ok=False,
                        entity_id=entity_id,
                        command="brightness",
                        error="bad percent",
                    )
                    self.send_text(400, "bad percent")
                    return

                try:
                    call_entity_dimmer(ha_url, token, entity_id, timeout, percent)
                    if post_action_delay > 0:
                        time.sleep(post_action_delay)
                    text = fetch_entity_display_state(ha_url, token, entity_id, timeout)
                except urllib.error.HTTPError as exc:
                    self.command_event(
                        started,
                        kind="entity",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        entity_id=entity_id,
                        command=f"brightness/{percent}",
                        error=f"ha http {exc.code}",
                    )
                    self.send_text(502, f"ha http {exc.code}")
                    return
                except Exception as exc:
                    self.command_event(
                        started,
                        kind="entity",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        entity_id=entity_id,
                        command=f"brightness/{percent}",
                        error=f"ha error {type(exc).__name__}",
                    )
                    self.send_text(502, f"ha error {type(exc).__name__}")
                    return

                self.command_event(
                    started,
                    kind="entity",
                    path=parsed.path,
                    status=200,
                    ok=True,
                    entity_id=entity_id,
                    command=f"brightness/{percent}",
                    response=text,
                )
                self.send_text(200, text or "ok")
                return

            if parsed.path == "/v1/thermostat" or parsed.path.startswith("/v1/thermostat/state/"):
                try:
                    entity_id = (
                        default_thermostat_entity()
                        if parsed.path == "/v1/thermostat"
                        else decode_entity_path_part(parsed.path.rsplit("/", 1)[-1])
                    )
                    text = thermostat_state_line(ha_url, token, timeout, entity_id)
                except urllib.error.HTTPError as exc:
                    self.send_text(502, f"ha http {exc.code}")
                    return
                except Exception as exc:
                    self.send_text(502, str(exc) if str(exc) else f"ha error {type(exc).__name__}")
                    return

                self.send_text(200, text or "empty")
                return

            if parsed.path in ("/v1/thermostat/temp_up", "/v1/thermostat/temp_down"):
                started = time.perf_counter()
                delta = 1.0 if parsed.path.endswith("temp_up") else -1.0
                try:
                    entity_id = default_thermostat_entity()
                    call_thermostat_temp_delta(ha_url, token, timeout, entity_id, delta)
                    if post_action_delay > 0:
                        time.sleep(post_action_delay)
                    text = thermostat_state_line(ha_url, token, timeout, entity_id)
                except urllib.error.HTTPError as exc:
                    self.command_event(
                        started,
                        kind="thermostat",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        entity_id=THERMOSTAT_ENTITY_ID or None,
                        command="temp_up" if delta > 0 else "temp_down",
                        error=f"ha http {exc.code}",
                    )
                    self.send_text(502, f"ha http {exc.code}")
                    return
                except Exception as exc:
                    self.command_event(
                        started,
                        kind="thermostat",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        entity_id=THERMOSTAT_ENTITY_ID or None,
                        command="temp_up" if delta > 0 else "temp_down",
                        error=str(exc) if str(exc) else f"ha error {type(exc).__name__}",
                    )
                    self.send_text(502, str(exc) if str(exc) else f"ha error {type(exc).__name__}")
                    return

                self.command_event(
                    started,
                    kind="thermostat",
                    path=parsed.path,
                    status=200,
                    ok=True,
                    entity_id=entity_id,
                    command="temp_up" if delta > 0 else "temp_down",
                    response=text,
                )
                self.send_text(200, text or "ok")
                return

            if parsed.path.startswith("/v1/thermostat/apply/"):
                started = time.perf_counter()
                parts = parsed.path.split("/", 7)
                try:
                    if len(parts) == 8:
                        entity_id = decode_entity_path_part(parts[4])
                        target = urllib.parse.unquote(parts[5])
                        mode = urllib.parse.unquote(parts[6])
                        fan_mode = urllib.parse.unquote(parts[7])
                    elif len(parts) == 7:
                        entity_id = default_thermostat_entity()
                        target = urllib.parse.unquote(parts[4])
                        mode = urllib.parse.unquote(parts[5])
                        fan_mode = urllib.parse.unquote(parts[6])
                    else:
                        raise ValueError("bad apply")
                except Exception as exc:
                    self.command_event(
                        started,
                        kind="thermostat",
                        path=parsed.path,
                        status=400,
                        ok=False,
                        command="apply",
                        error=str(exc) if str(exc) else "bad apply",
                    )
                    self.send_text(400, str(exc) if str(exc) else "bad apply")
                    return

                try:
                    call_thermostat_apply(ha_url, token, timeout, entity_id, float(target), mode, fan_mode)
                    if post_action_delay > 0:
                        time.sleep(post_action_delay)
                    text = thermostat_state_line(ha_url, token, timeout, entity_id)
                except urllib.error.HTTPError as exc:
                    self.command_event(
                        started,
                        kind="thermostat",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        entity_id=entity_id,
                        command=f"apply/{target}/{mode}/{fan_mode}",
                        error=f"ha http {exc.code}",
                    )
                    self.send_text(502, f"ha http {exc.code}")
                    return
                except Exception as exc:
                    self.command_event(
                        started,
                        kind="thermostat",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        entity_id=entity_id,
                        command=f"apply/{target}/{mode}/{fan_mode}",
                        error=str(exc) if str(exc) else f"ha error {type(exc).__name__}",
                    )
                    self.send_text(502, str(exc) if str(exc) else f"ha error {type(exc).__name__}")
                    return

                self.command_event(
                    started,
                    kind="thermostat",
                    path=parsed.path,
                    status=200,
                    ok=True,
                    entity_id=entity_id,
                    command=f"apply/{target}/{mode}/{fan_mode}",
                    response=text,
                )
                self.send_text(200, text or "ok")
                return

            if parsed.path.startswith("/v1/thermostat/mode/"):
                started = time.perf_counter()
                mode = urllib.parse.unquote(parsed.path.rsplit("/", 1)[-1])
                try:
                    entity_id = default_thermostat_entity()
                    call_thermostat_hvac_mode(ha_url, token, timeout, entity_id, mode)
                    if post_action_delay > 0:
                        time.sleep(post_action_delay)
                    text = thermostat_state_line(ha_url, token, timeout, entity_id)
                except urllib.error.HTTPError as exc:
                    self.command_event(
                        started,
                        kind="thermostat",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        entity_id=THERMOSTAT_ENTITY_ID or None,
                        command=f"mode/{mode}",
                        error=f"ha http {exc.code}",
                    )
                    self.send_text(502, f"ha http {exc.code}")
                    return
                except Exception as exc:
                    self.command_event(
                        started,
                        kind="thermostat",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        entity_id=THERMOSTAT_ENTITY_ID or None,
                        command=f"mode/{mode}",
                        error=str(exc) if str(exc) else f"ha error {type(exc).__name__}",
                    )
                    self.send_text(502, str(exc) if str(exc) else f"ha error {type(exc).__name__}")
                    return

                self.command_event(
                    started,
                    kind="thermostat",
                    path=parsed.path,
                    status=200,
                    ok=True,
                    entity_id=entity_id,
                    command=f"mode/{mode}",
                    response=text,
                )
                self.send_text(200, text or "ok")
                return

            if parsed.path.startswith("/v1/thermostat/fan/"):
                started = time.perf_counter()
                fan_mode = urllib.parse.unquote(parsed.path.rsplit("/", 1)[-1])
                try:
                    entity_id = default_thermostat_entity()
                    call_thermostat_fan_mode(ha_url, token, timeout, entity_id, fan_mode)
                    if post_action_delay > 0:
                        time.sleep(post_action_delay)
                    text = thermostat_state_line(ha_url, token, timeout, entity_id)
                except urllib.error.HTTPError as exc:
                    self.command_event(
                        started,
                        kind="thermostat",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        entity_id=THERMOSTAT_ENTITY_ID or None,
                        command=f"fan/{fan_mode}",
                        error=f"ha http {exc.code}",
                    )
                    self.send_text(502, f"ha http {exc.code}")
                    return
                except Exception as exc:
                    self.command_event(
                        started,
                        kind="thermostat",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        entity_id=THERMOSTAT_ENTITY_ID or None,
                        command=f"fan/{fan_mode}",
                        error=str(exc) if str(exc) else f"ha error {type(exc).__name__}",
                    )
                    self.send_text(502, str(exc) if str(exc) else f"ha error {type(exc).__name__}")
                    return

                self.command_event(
                    started,
                    kind="thermostat",
                    path=parsed.path,
                    status=200,
                    ok=True,
                    entity_id=entity_id,
                    command=f"fan/{fan_mode}",
                    response=text,
                )
                self.send_text(200, text or "ok")
                return
            if parsed.path.startswith("/v1/dimmer/"):
                started = time.perf_counter()
                parts = parsed.path.split("/")
                action: BridgeAction | None = None
                percent_text = ""
                if len(parts) == 5:
                    action = ACTION_BY_ID.get(parts[3].upper())
                    percent_text = urllib.parse.unquote(parts[4])

                if not action:
                    self.command_event(
                        started,
                        kind="dimmer",
                        path=parsed.path,
                        status=404,
                        ok=False,
                        error="unknown dimmer",
                    )
                    self.send_text(404, "unknown dimmer")
                    return

                if not action.is_dimmer:
                    self.command_event(
                        started,
                        kind="dimmer",
                        path=parsed.path,
                        status=400,
                        ok=False,
                        action=action,
                        error="not dimmer",
                    )
                    self.send_text(400, "not dimmer")
                    return

                try:
                    percent = clamp_percent(int(percent_text))
                except ValueError:
                    self.command_event(
                        started,
                        kind="dimmer",
                        path=parsed.path,
                        status=400,
                        ok=False,
                        action=action,
                        error="bad percent",
                    )
                    self.send_text(400, "bad percent")
                    return

                try:
                    call_ha_dimmer(ha_url, token, action, timeout, percent)
                    if post_action_delay > 0:
                        time.sleep(post_action_delay)
                    text = fetch_v1_states(ha_url, token, timeout)
                except urllib.error.HTTPError as exc:
                    self.command_event(
                        started,
                        kind="dimmer",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        action=action,
                        command=f"brightness/{percent}",
                        error=f"ha http {exc.code}",
                    )
                    self.send_text(502, f"ha http {exc.code}")
                    return
                except Exception as exc:
                    self.command_event(
                        started,
                        kind="dimmer",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        action=action,
                        command=f"brightness/{percent}",
                        error=f"ha error {type(exc).__name__}",
                    )
                    self.send_text(502, f"ha error {type(exc).__name__}")
                    return

                self.command_event(
                    started,
                    kind="dimmer",
                    path=parsed.path,
                    status=200,
                    ok=True,
                    action=action,
                    command=f"brightness/{percent}",
                    response=text,
                )
                self.send_text(200, text or "ok")
                return

            if parsed.path.startswith("/v1/action/") or parsed.path.startswith("/v1/toggle/"):
                started = time.perf_counter()
                action_id = parsed.path.rsplit("/", 1)[-1].upper()
                action = ACTION_BY_ID.get(action_id)
                if not action:
                    self.command_event(
                        started,
                        kind="action",
                        path=parsed.path,
                        status=404,
                        ok=False,
                        error="unknown action",
                    )
                    self.send_text(404, "unknown action")
                    return
                if action.is_dimmer:
                    self.command_event(
                        started,
                        kind="action",
                        path=parsed.path,
                        status=400,
                        ok=False,
                        action=action,
                        error="use dimmer endpoint",
                    )
                    self.send_text(400, "use dimmer")
                    return
                try:
                    call_ha_action(ha_url, token, action, timeout)
                    if post_action_delay > 0:
                        time.sleep(post_action_delay)
                    text = fetch_v1_states(ha_url, token, timeout)
                except urllib.error.HTTPError as exc:
                    self.command_event(
                        started,
                        kind="action",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        action=action,
                        error=f"ha http {exc.code}",
                    )
                    self.send_text(502, f"ha http {exc.code}")
                    return
                except Exception as exc:
                    self.command_event(
                        started,
                        kind="action",
                        path=parsed.path,
                        status=502,
                        ok=False,
                        action=action,
                        error=f"ha error {type(exc).__name__}",
                    )
                    self.send_text(502, f"ha error {type(exc).__name__}")
                    return

                self.command_event(
                    started,
                    kind="action",
                    path=parsed.path,
                    status=200,
                    ok=True,
                    action=action,
                    response=text,
                )
                self.send_text(200, text or "ok")
                return

            if parsed.path == "/v1/catalog":
                text = "|".join(f"{action.action_id}:{action.label}" for action in ACTIONS)
                self.send_text(200, text)
                return

            self.send_text(404, "not found")

    return Handler


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--env", type=Path, default=DEFAULT_ENV, help="Path to .env config")
    parser.add_argument(
        "--ha-url",
        default=DEFAULT_HA_URL,
        help="Home Assistant URL. Overrides HA_BASE_URL from .env when provided.",
    )
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--key", default=DEFAULT_KEY)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--post-action-delay", type=float, default=0.7)
    parser.add_argument("--log-path", type=Path, default=DEFAULT_LOG_PATH)
    parser.add_argument("--no-command-log", action="store_true")
    args = parser.parse_args()

    env = read_env(args.env)
    token = env.get("HA_TOKEN")
    if not token:
        raise SystemExit(
            f"HA_TOKEN missing from {args.env}; copy .env.example to .env and add a long-lived access token"
        )

    ha_url = args.ha_url
    if ha_url == DEFAULT_HA_URL:
        ha_url = env.get("HA_BASE_URL") or env.get("HOME_ASSISTANT_URL") or ha_url
    try:
        ha_url = normalize_ha_url(ha_url)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    bridge_key = args.key
    if bridge_key == DEFAULT_KEY:
        env_bridge_key = (env.get("FLIPPERHA_BRIDGE_KEY") or env.get("HA_REMOTE_BRIDGE_KEY", "")).strip()
        if env_bridge_key:
            bridge_key = env_bridge_key

    log_path = None if args.no_command_log else args.log_path
    handler = make_handler(
        ha_url,
        token,
        bridge_key,
        args.timeout,
        args.post_action_delay,
        log_path,
    )
    server = ThreadingHTTPServer((args.host, args.port), handler)
    print(f"serving=http://{args.host}:{args.port}/v1/states?k=<redacted>", flush=True)
    print(f"ha_url={ha_url}", flush=True)
    if log_path:
        print(f"command_log={log_path}", flush=True)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
