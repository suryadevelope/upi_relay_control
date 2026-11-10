"""
Smart Gateway local admin protocol client.

Talks to ESP32 devices over the local network only:
  * UDP discovery  (port 4210): broadcast "SGW_DISCOVER", devices reply with JSON
  * HTTP JSON API  (port 8080): /api/v1/... (never serves a webpage)

Login is a challenge-response: the admin password is never sent over the network.
    nonce  = GET /api/v1/auth/challenge
    proof  = HMAC-SHA256(key=device admin password, msg="<user>:<nonce>") as hex
    token  = POST /api/v1/auth/login {user, proof}
Every later request carries "Authorization: Bearer <token>".

Standard library only.
"""

import hashlib
import hmac
import json
import socket
import time
import urllib.error
import urllib.request

DISCOVERY_PORT = 4210
API_PORT = 8080
DISCOVER_PROBE = b"SGW_DISCOVER"


class DeviceError(Exception):
    """Device answered with an error (HTTP status and message from the device)."""

    def __init__(self, status, message):
        super().__init__(f"{status}: {message}")
        self.status = status
        self.message = message


class DeviceUnreachable(Exception):
    """Could not reach the device at all (network, timeout, wrong address)."""


def compute_proof(admin_user, admin_password, nonce):
    """HMAC proof expected by the firmware (AdminApi::handleLogin)."""
    message = f"{admin_user}:{nonce}".encode("utf-8")
    return hmac.new(admin_password.encode("utf-8"), message, hashlib.sha256).hexdigest()


def _local_ipv4():
    """Best-effort local LAN address (no packets are sent)."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("10.255.255.255", 1))
            return s.getsockname()[0]
    except OSError:
        return None


def discovery_targets(extra_broadcasts=()):
    """Broadcast addresses to probe: global, plus the local /24 directed broadcast."""
    targets = ["255.255.255.255"]
    local = _local_ipv4()
    if local:
        parts = local.split(".")
        targets.append(".".join(parts[:3] + ["255"]))
    for addr in extra_broadcasts:
        if addr and addr not in targets:
            targets.append(addr)
    return targets


def discover(timeout=2.0, port=DISCOVERY_PORT, extra_broadcasts=()):
    """
    Find devices on the local network.
    Returns a list of dicts with keys: ip, port, name, mac, fw, serviceEnabled, staIP, apIP.
    """
    found = {}
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.settimeout(0.3)
        for target in discovery_targets(extra_broadcasts):
            try:
                sock.sendto(DISCOVER_PROBE, (target, port))
            except OSError:
                continue

        # Listen for replies until the timeout expires
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            try:
                data, addr = sock.recvfrom(1024)
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                info = json.loads(data.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            if info.get("sgw") != 1:
                continue
            info["ip"] = addr[0]
            found[info.get("mac") or addr[0]] = info
    return sorted(found.values(), key=lambda d: (d.get("name", ""), d["ip"]))


class DeviceClient:
    """One device connection. Holds the session token after login()."""

    def __init__(self, host, port=API_PORT, timeout=6.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.base = f"http://{host}:{port}"
        self.token = None
        self.session_expires_in = None
        self.default_credentials = False

    # ----- low level -------------------------------------------------------
    def _request(self, method, path, body=None, auth=False):
        data = None
        headers = {"Accept": "application/json"}
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            headers["Content-Type"] = "application/json"
        if auth:
            if not self.token:
                raise DeviceError(401, "Not logged in")
            headers["Authorization"] = f"Bearer {self.token}"

        req = urllib.request.Request(self.base + path, data=data, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                raw = resp.read().decode("utf-8") or "{}"
                return json.loads(raw)
        except urllib.error.HTTPError as err:
            message = f"HTTP {err.code}"
            try:
                payload = json.loads(err.read().decode("utf-8"))
                message = payload.get("error", message)
            except (ValueError, UnicodeDecodeError):
                pass
            raise DeviceError(err.code, message) from None
        except (urllib.error.URLError, socket.timeout, ConnectionError, OSError) as err:
            raise DeviceUnreachable(f"Cannot reach {self.host}:{self.port} ({err})") from None

    # ----- session ---------------------------------------------------------
    def info(self):
        return self._request("GET", "/api/v1/info")

    def login(self, user, password):
        challenge = self._request("GET", "/api/v1/auth/challenge")
        proof = compute_proof(user, password, challenge["nonce"])
        reply = self._request("POST", "/api/v1/auth/login", {"user": user, "proof": proof})
        self.token = reply["token"]
        self.session_expires_in = reply.get("expiresInSec")
        self.default_credentials = bool(reply.get("defaultCredentials"))
        return reply

    def logout(self):
        if self.token:
            try:
                self._request("POST", "/api/v1/auth/logout", {}, auth=True)
            finally:
                self.token = None

    # ----- status and configuration ---------------------------------------
    def status(self):
        return self._request("GET", "/api/v1/status", auth=True)

    def get_config(self):
        cfg = self._request("GET", "/api/v1/config", auth=True)
        self.default_credentials = bool(cfg.get("defaultCredentials"))
        return cfg

    def set_config(self, **fields):
        """Partial update. Accepted keys: name, backendUrl, deviceKey, adminUser, adminPass, rules."""
        return self._request("POST", "/api/v1/config", fields, auth=True)

    def set_service(self, enabled):
        return self._request("POST", "/api/v1/service", {"enabled": bool(enabled)}, auth=True)

    def relay_test(self, seconds):
        return self._request("POST", "/api/v1/relay/test", {"seconds": int(seconds)}, auth=True)

    def relay_off(self):
        return self._request("POST", "/api/v1/relay/off", {}, auth=True)

    def cancel_payment(self):
        return self._request("POST", "/api/v1/payment/cancel", {}, auth=True)

    def restart(self):
        return self._request("POST", "/api/v1/restart", {}, auth=True)

    def transactions(self):
        return self._request("GET", "/api/v1/transactions", auth=True)

    # ----- hotspot-only Wi-Fi settings (device rejects these over the LAN) -
    def wifi_scan(self):
        return self._request("GET", "/api/v1/wifi/scan", auth=True)

    def set_wifi(self, ssid, password):
        return self._request("POST", "/api/v1/wifi", {"ssid": ssid, "pass": password}, auth=True)
