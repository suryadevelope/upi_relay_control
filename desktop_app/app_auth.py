"""
Login for the desktop admin app itself.

The first run asks you to create an admin account. Only a salted PBKDF2-SHA256
hash is stored, in the user's profile folder (never inside the project).
"""

import hashlib
import json
import os
import secrets
import time
from pathlib import Path

ITERATIONS = 310_000
MAX_FAILURES = 5
LOCK_SECONDS = 60


def default_store_path():
    base = os.environ.get("APPDATA") or str(Path.home())
    return Path(base) / "SmartGatewayAdmin" / "app_auth.json"


class AppAuth:
    def __init__(self, path=None):
        self.path = Path(path) if path else default_store_path()
        self.failures = 0
        self.locked_until = 0.0

    def has_account(self):
        return self.path.exists()

    def username(self):
        if not self.has_account():
            return None
        return self._load()["username"]

    def create_account(self, username, password):
        if not username or not username.strip():
            raise ValueError("Username is required")
        if len(password) < 8:
            raise ValueError("Password must be at least 8 characters")
        salt = secrets.token_bytes(16)
        digest = self._hash(password, salt, ITERATIONS)
        record = {
            "username": username.strip(),
            "salt": salt.hex(),
            "hash": digest.hex(),
            "iterations": ITERATIONS,
        }
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(record, indent=2), encoding="utf-8")

    def verify(self, username, password):
        """True when the credentials match. Short lockout after repeated failures."""
        now = time.monotonic()
        if now < self.locked_until:
            return False
        if not self.has_account():
            return False

        record = self._load()
        salt = bytes.fromhex(record["salt"])
        expected = bytes.fromhex(record["hash"])
        digest = self._hash(password, salt, int(record["iterations"]))
        ok = secrets.compare_digest(digest, expected) and username.strip() == record["username"]

        if ok:
            self.failures = 0
            return True
        self.failures += 1
        if self.failures >= MAX_FAILURES:
            self.locked_until = now + LOCK_SECONDS
            self.failures = 0
        return False

    def seconds_locked(self):
        return max(0, int(self.locked_until - time.monotonic()))

    def change_password(self, old_password, new_password):
        username = self.username()
        if not self.verify(username, old_password):
            raise ValueError("Current password is wrong")
        self.create_account(username, new_password)

    def _load(self):
        return json.loads(self.path.read_text(encoding="utf-8"))

    @staticmethod
    def _hash(password, salt, iterations):
        return hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, iterations)
