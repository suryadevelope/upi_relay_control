"""
Protocol tests for the desktop admin app.

A small fake device implements the same contract as the ESP32 firmware
(AdminApi.h / DiscoveryService.h) so the client can be exercised end to end
without hardware. Run:  python -m unittest discover -s desktop_app/tests -v
"""

import hashlib
import hmac
import http.server
import json
import os
import secrets
import socket
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

import sgw_client as sgw  # noqa: E402
from app_auth import AppAuth  # noqa: E402

ADMIN_USER = "admin"
ADMIN_PASS = "secret123"


class FakeDevice:
    """Mirrors the firmware's admin API rules (login, lockout, auth, validation, hotspot-only Wi-Fi)."""

    def __init__(self):
        self.user = ADMIN_USER
        self.password = ADMIN_PASS
        self.name = "Test Gateway"
        self.service = True
        self.rules = [{"amount": 1.0, "duration": 3}, {"amount": 5.0, "duration": 15}]
        self.nonce = None
        self.nonce_time = 0.0
        self.token = None
        self.fails = 0
        self.locked_until = 0.0
        self.hotspot_clients = set()  # client IPs treated as hotspot (AP) clients
        self.wifi_saved = None
        self.lock = threading.Lock()

    # ---- helpers mirroring AdminApi.h -------------------------------------
    def handle(self, method, path, body, auth_header, client_ip):
        with self.lock:
            return self._handle(method, path, body, auth_header, client_ip)

    def _handle(self, method, path, body, auth_header, client_ip):
        if method == "GET" and path == "/api/v1/info":
            return 200, {"sgw": 1, "name": self.name, "fw": "test", "mac": "AA:BB", "port": 8080,
                         "serviceEnabled": self.service}
        if method == "GET" and path == "/api/v1/auth/challenge":
            if time.monotonic() < self.locked_until:
                return 429, {"success": False, "error": "Too many failed logins. Try again later."}
            self.nonce = secrets.token_hex(16)
            self.nonce_time = time.monotonic()
            return 200, {"nonce": self.nonce, "expiresInSec": 60}
        if method == "POST" and path == "/api/v1/auth/login":
            return self._login(body)

        # Everything below needs a valid bearer token
        if not self._authorized(auth_header):
            return 401, {"success": False, "error": "Login required"}

        if method == "GET" and path == "/api/v1/status":
            return 200, {"state": "READY", "serviceEnabled": self.service, "relayOn": False,
                         "relayRemainingSec": 0, "paymentActive": False, "staConnected": False}
        if method == "GET" and path == "/api/v1/config":
            return 200, {"name": self.name, "serviceEnabled": self.service, "backendUrl": "https://x",
                         "adminUser": self.user, "defaultCredentials": self.password == "admin123",
                         "rules": self.rules, "wifiSsid": "", "wifiConnected": False}
        if method == "POST" and path == "/api/v1/config":
            return self._set_config(body)
        if method == "POST" and path == "/api/v1/service":
            if not isinstance(body.get("enabled"), bool):
                return 400, {"success": False, "error": "Field 'enabled' (true/false) is required"}
            self.service = body["enabled"]
            return 200, {"success": True, "serviceEnabled": self.service}
        if method == "POST" and path == "/api/v1/relay/test":
            if not self.service:
                return 409, {"success": False, "error": "Service is stopped. Enable it first."}
            seconds = int(body.get("seconds", 0))
            if seconds < 1 or seconds > 60:
                return 400, {"success": False, "error": "Test seconds must be between 1 and 60"}
            return 200, {"success": True}
        if method == "GET" and path == "/api/v1/wifi/scan":
            if client_ip not in self.hotspot_clients:
                return 403, {"success": False, "error": "Wi-Fi settings can only be changed from the device hotspot"}
            return 200, [{"ssid": "HomeNet", "rssi": -40, "secured": True}]
        if method == "POST" and path == "/api/v1/wifi":
            if client_ip not in self.hotspot_clients:
                return 403, {"success": False, "error": "Wi-Fi settings can only be changed from the device hotspot"}
            self.wifi_saved = (body.get("ssid"), body.get("pass"))
            return 200, {"success": True}
        if method == "GET" and path == "/api/v1/transactions":
            return 200, []
        if method == "POST" and path in ("/api/v1/relay/off", "/api/v1/payment/cancel",
                                         "/api/v1/restart", "/api/v1/auth/logout"):
            if path == "/api/v1/auth/logout":
                self.token = None
            return 200, {"success": True}
        return 404, {"success": False, "error": "Unknown endpoint"}

    def _authorized(self, auth_header):
        if not self.token or not auth_header or not auth_header.startswith("Bearer "):
            return False
        return hmac.compare_digest(auth_header[7:], self.token)

    def _login(self, body):
        nonce = self.nonce
        fresh = nonce is not None and (time.monotonic() - self.nonce_time) <= 60
        self.nonce = None  # single use, whatever the outcome
        user = body.get("user", "")
        proof = body.get("proof", "")
        ok = False
        if fresh and user == self.user:
            expected = hmac.new(self.password.encode(), f"{user}:{nonce}".encode(), hashlib.sha256).hexdigest()
            ok = hmac.compare_digest(proof, expected)
        if not ok:
            self.fails += 1
            if self.fails >= 5:
                self.locked_until = time.monotonic() + 30
                self.fails = 0
            return 401, {"success": False, "error": "Invalid username or password"}
        self.fails = 0
        self.token = secrets.token_hex(16)
        return 200, {"success": True, "token": self.token, "expiresInSec": 900,
                     "defaultCredentials": self.password == "admin123"}

    def _set_config(self, body):
        if "name" in body:
            if not body["name"].strip() or len(body["name"]) > 32:
                return 400, {"success": False, "error": "Name must be 1-32 characters"}
            self.name = body["name"]
        if "adminPass" in body:
            if not 6 <= len(body["adminPass"]) <= 32:
                return 400, {"success": False, "error": "Admin password must be 6-32 characters"}
            self.password = body["adminPass"]
        if "rules" in body:
            if len(body["rules"]) > 10:
                return 400, {"success": False, "error": "At most 10 payment cards are allowed"}
            for r in body["rules"]:
                if not 1 <= r["duration"] <= 3600:
                    return 400, {"success": False, "error": "Relay time must be between 1 and 3600 seconds"}
                if not 1 <= r["amount"] <= 100000:
                    return 400, {"success": False, "error": "Card amount must be between 1 and 100000"}
            self.rules = body["rules"]
        return 200, {"success": True}


class FakeDeviceServer:
    def __init__(self, device, client_ip="127.0.0.1"):
        self.device = device
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def _run(self, method):
                length = int(self.headers.get("Content-Length") or 0)
                raw = self.rfile.read(length) if length else b""
                body = json.loads(raw.decode() or "{}") if raw else {}
                status, payload = owner.device.handle(method, self.path, body,
                                                      self.headers.get("Authorization"),
                                                      self.client_address[0])
                data = json.dumps(payload).encode()
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def do_GET(self):
                self._run("GET")

            def do_POST(self):
                self._run("POST")

        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self.httpd.server_address[1]
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()


def free_udp_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("0.0.0.0", 0))
        return s.getsockname()[1]


class DeviceProtocolTests(unittest.TestCase):
    def setUp(self):
        self.device = FakeDevice()
        self.server = FakeDeviceServer(self.device)
        self.client = sgw.DeviceClient("127.0.0.1", port=self.server.port, timeout=3)

    def tearDown(self):
        self.server.close()

    def test_info_is_public(self):
        info = self.client.info()
        self.assertEqual(info["name"], "Test Gateway")

    def test_status_requires_login(self):
        with self.assertRaises(sgw.DeviceError) as ctx:
            self.client.status()
        self.assertEqual(ctx.exception.status, 401)

    def test_wrong_password_rejected(self):
        with self.assertRaises(sgw.DeviceError) as ctx:
            self.client.login(ADMIN_USER, "wrong-password")
        self.assertEqual(ctx.exception.status, 401)

    def test_login_then_status(self):
        self.client.login(ADMIN_USER, ADMIN_PASS)
        self.assertTrue(self.client.token)
        self.assertEqual(self.client.status()["state"], "READY")

    def test_password_is_never_sent_in_clear(self):
        # The login body must carry only the HMAC proof, never the password
        seen = []
        original = self.device.handle

        def spy(method, path, body, auth, ip):
            seen.append(json.dumps(body))
            return original(method, path, body, auth, ip)

        self.device.handle = spy
        self.client.login(ADMIN_USER, ADMIN_PASS)
        self.assertFalse(any(ADMIN_PASS in s for s in seen))

    def test_nonce_is_single_use(self):
        challenge = self.client._request("GET", "/api/v1/auth/challenge")
        proof = sgw.compute_proof(ADMIN_USER, ADMIN_PASS, challenge["nonce"])
        self.client._request("POST", "/api/v1/auth/login", {"user": ADMIN_USER, "proof": proof})
        with self.assertRaises(sgw.DeviceError):
            self.client._request("POST", "/api/v1/auth/login", {"user": ADMIN_USER, "proof": proof})

    def test_lockout_after_repeated_failures(self):
        for _ in range(5):
            with self.assertRaises(sgw.DeviceError):
                self.client.login(ADMIN_USER, "nope-nope")
        with self.assertRaises(sgw.DeviceError) as ctx:
            self.client.login(ADMIN_USER, ADMIN_PASS)
        self.assertEqual(ctx.exception.status, 429)

    def test_set_config_round_trip_and_validation(self):
        self.client.login(ADMIN_USER, ADMIN_PASS)
        self.client.set_config(rules=[{"amount": 10, "duration": 30}], name="Counter 1")
        cfg = self.client.get_config()
        self.assertEqual(cfg["name"], "Counter 1")
        self.assertEqual(cfg["rules"], [{"amount": 10, "duration": 30}])

        with self.assertRaises(sgw.DeviceError) as ctx:
            self.client.set_config(rules=[{"amount": 10, "duration": 0}])
        self.assertEqual(ctx.exception.status, 400)

    def test_service_stop_and_enable(self):
        self.client.login(ADMIN_USER, ADMIN_PASS)
        self.client.set_service(False)
        self.assertFalse(self.client.get_config()["serviceEnabled"])
        with self.assertRaises(sgw.DeviceError) as ctx:
            self.client.relay_test(3)
        self.assertEqual(ctx.exception.status, 409)
        self.client.set_service(True)
        self.assertTrue(self.client.get_config()["serviceEnabled"])

    def test_wifi_only_from_hotspot(self):
        self.client.login(ADMIN_USER, ADMIN_PASS)
        with self.assertRaises(sgw.DeviceError) as ctx:
            self.client.set_wifi("HomeNet", "pw12345678")
        self.assertEqual(ctx.exception.status, 403)

        self.device.hotspot_clients.add("127.0.0.1")
        self.client.set_wifi("HomeNet", "pw12345678")
        self.assertEqual(self.device.wifi_saved, ("HomeNet", "pw12345678"))

    def test_admin_password_change_applies_to_next_login(self):
        self.client.login(ADMIN_USER, ADMIN_PASS)
        self.client.set_config(adminPass="brand-new-pass")
        self.client.logout()
        with self.assertRaises(sgw.DeviceError):
            self.client.login(ADMIN_USER, ADMIN_PASS)
        self.client.login(ADMIN_USER, "brand-new-pass")

    def test_unreachable_device_is_reported(self):
        dead = sgw.DeviceClient("127.0.0.1", port=free_udp_port(), timeout=1)
        with self.assertRaises(sgw.DeviceUnreachable):
            dead.info()


class DiscoveryTests(unittest.TestCase):
    def test_discovery_finds_device_that_answers_probe(self):
        port = free_udp_port()
        stop = threading.Event()
        identity = {"sgw": 1, "name": "Counter 2", "mac": "CC:DD", "port": 8080,
                    "serviceEnabled": True, "fw": "test", "staIP": "10.0.0.5", "apIP": "192.168.4.1"}

        def responder():
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
                s.bind(("0.0.0.0", port))
                s.settimeout(0.2)
                while not stop.is_set():
                    try:
                        data, addr = s.recvfrom(256)
                    except socket.timeout:
                        continue
                    if data == b"SGW_DISCOVER":
                        s.sendto(json.dumps(identity).encode(), addr)

        t = threading.Thread(target=responder, daemon=True)
        t.start()
        try:
            found = sgw.discover(timeout=1.5, port=port)
        finally:
            stop.set()
            t.join(timeout=2)

        names = [d["name"] for d in found]
        self.assertIn("Counter 2", names)
        device = [d for d in found if d["name"] == "Counter 2"][0]
        # The reply comes from the probe's source interface (LAN address), not loopback
        self.assertTrue(device["ip"] and device["ip"] != "")
        self.assertEqual(device["port"], 8080)


class AppAuthTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / "app_auth.json"

    def tearDown(self):
        self.tmp.cleanup()

    def test_create_and_verify(self):
        auth = AppAuth(self.path)
        self.assertFalse(auth.has_account())
        auth.create_account("owner", "longpassword")
        self.assertTrue(auth.has_account())
        self.assertTrue(auth.verify("owner", "longpassword"))
        self.assertFalse(auth.verify("owner", "bad-password"))
        self.assertFalse(auth.verify("someone", "longpassword"))

    def test_password_is_not_stored_in_plain_text(self):
        AppAuth(self.path).create_account("owner", "longpassword")
        self.assertNotIn("longpassword", self.path.read_text(encoding="utf-8"))

    def test_short_password_rejected(self):
        with self.assertRaises(ValueError):
            AppAuth(self.path).create_account("owner", "short")

    def test_lockout_after_failures(self):
        auth = AppAuth(self.path)
        auth.create_account("owner", "longpassword")
        for _ in range(5):
            auth.verify("owner", "wrong-pass")
        self.assertFalse(auth.verify("owner", "longpassword"))
        self.assertGreater(auth.seconds_locked(), 0)


if __name__ == "__main__":
    unittest.main()
