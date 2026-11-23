"""
Smart Gateway - local admin desktop app.

Run:  python main.py

* Logs in to this app with a local admin account (first run creates it).
* Finds every Smart Gateway on the local network (UDP broadcast, no webpage).
* Configures each device individually over the local JSON API:
    - device name, service enable / stop, restart, relay test
    - payment cards (amount -> relay seconds)
    - backend URL, device key, device admin login
    - Wi-Fi credentials (only while this PC is connected to the device hotspot)
    - transaction history
Uses only the Python standard library (tkinter included with Python on Windows).
"""

import queue
import threading
import tkinter as tk
from tkinter import ttk, messagebox

import sgw_client as sgw
from app_auth import AppAuth

APP_TITLE = "Smart Gateway Admin"

# Payment card relay behaviour modes (must match the firmware RelayMode values in Config.h)
MODE_TIMER = 0          # relay ON for the configured seconds once
MODE_PULSE_MESSAGE = 1  # ON seconds -> OFF -> show message -> wait seconds -> home
MODE_PULSE_COUNT = 2    # ON seconds / OFF seconds, repeated pulseCount times

MODE_LABELS = {
    MODE_TIMER: "Timer — ON for set seconds",
    MODE_PULSE_MESSAGE: "Pulse — ON → message → wait → home",
    MODE_PULSE_COUNT: "Pulse count — ON/OFF × N",
}

MAX_PULSE_COUNT = 200


# ---------------------------------------------------------------------------
# Small reusable dialog: a form with labelled fields, returns a dict or None
# ---------------------------------------------------------------------------
def form_dialog(parent, title, fields, initial=None, note=None):
    result = {}
    initial = initial or {}
    top = tk.Toplevel(parent)
    top.title(title)
    top.transient(parent)
    top.resizable(False, False)
    top.grab_set()

    frame = ttk.Frame(top, padding=16)
    frame.grid(sticky="nsew")
    if note:
        ttk.Label(frame, text=note, wraplength=340, foreground="#555").grid(
            row=0, column=0, columnspan=2, sticky="w", pady=(0, 10))

    entries = {}
    for row, (label, key, secret) in enumerate(fields, start=1):
        ttk.Label(frame, text=label).grid(row=row, column=0, sticky="w", pady=4, padx=(0, 10))
        entry = ttk.Entry(frame, width=32, show="*" if secret else "")
        entry.insert(0, initial.get(key, ""))
        entry.grid(row=row, column=1, sticky="ew", pady=4)
        entries[key] = entry

    def ok(_event=None):
        result.update({key: entry.get() for key, entry in entries.items()})
        top.destroy()

    def cancel(_event=None):
        result.clear()
        top.destroy()

    buttons = ttk.Frame(frame)
    buttons.grid(row=len(fields) + 1, column=0, columnspan=2, sticky="e", pady=(12, 0))
    ttk.Button(buttons, text="Cancel", command=cancel).pack(side="right", padx=(6, 0))
    ttk.Button(buttons, text="OK", command=ok).pack(side="right")
    top.bind("<Return>", ok)
    top.bind("<Escape>", cancel)

    first = next(iter(entries.values()))
    first.focus_set()
    parent.wait_window(top)
    return result or None


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1080x660")
        self.minsize(960, 600)

        self.auth = AppAuth()
        self.ui_queue = queue.Queue()
        self.devices = {}          # ip -> info dict from discovery / add-by-IP
        self.clients = {}          # ip -> DeviceClient (logged in)
        self.selected_ip = None
        self.rules = []            # payment cards being edited: [{"amount","duration","mode","pulseCount","pulseOnSec","pulseOffSec","pulseWaitSec","pulseMessage"}]
        self.mode2Enabled = False  # 2nd mode: fixed 5s relay + "enjoy the game" on device
        self.scan_results = []

        self.build_ui()
        self.after(60, self._pump)
        self.protocol("WM_DELETE_WINDOW", self.on_close)
        self.after(100, self.app_login)

    # ------------------------------------------------------------------
    # Threading helpers: workers never touch Tk; results go through a queue
    # ------------------------------------------------------------------
    def _pump(self):
        try:
            while True:
                self.ui_queue.get_nowait()()
        except queue.Empty:
            pass
        self.after(60, self._pump)

    def run_bg(self, work, on_done, on_error=None):
        def worker():
            try:
                result = work()
            except Exception as exc:  # reported on the UI thread
                self.ui_queue.put(lambda exc=exc: (on_error or self.show_error)(exc))
            else:
                self.ui_queue.put(lambda: on_done(result))
        threading.Thread(target=worker, daemon=True).start()

    def set_status(self, text):
        self.status_var.set(text)

    # ------------------------------------------------------------------
    # App login (local admin account)
    # ------------------------------------------------------------------
    def app_login(self):
        if self.auth.has_account():
            while True:
                creds = form_dialog(self, "Admin login",
                                    [("Username", "user", False), ("Password", "password", True)],
                                    note="Sign in to the Smart Gateway admin app.")
                if creds is None:
                    self.destroy()
                    return
                if self.auth.verify(creds["user"], creds["password"]):
                    break
                if self.auth.seconds_locked() > 0:
                    messagebox.showerror(APP_TITLE, f"Too many attempts. Wait {self.auth.seconds_locked()} s.")
                else:
                    messagebox.showerror(APP_TITLE, "Wrong username or password.")
        else:
            while True:
                creds = form_dialog(self, "Create admin account",
                                    [("Username", "user", False),
                                     ("Password (min 8)", "password", True),
                                     ("Confirm password", "confirm", True)],
                                    note="First run: create the local admin account for this app. "
                                         "It is stored only on this PC.")
                if creds is None:
                    self.destroy()
                    return
                if creds["password"] != creds["confirm"]:
                    messagebox.showerror(APP_TITLE, "Passwords do not match.")
                    continue
                try:
                    self.auth.create_account(creds["user"], creds["password"])
                    break
                except ValueError as exc:
                    messagebox.showerror(APP_TITLE, str(exc))

        self.set_status(f"Logged in as {self.auth.username()}. Click 'Scan network' to find devices.")
        self.scan_network()

    def app_logout(self):
        for client in self.clients.values():
            try:
                client.logout()
            except Exception:
                pass
        self.clients.clear()
        self.selected_ip = None
        self.device_tree.selection_remove(self.device_tree.selection())
        self.clear_device_views()
        self.app_login()

    def on_close(self):
        for client in self.clients.values():
            try:
                client.logout()
            except Exception:
                pass
        self.destroy()

    # ------------------------------------------------------------------
    # Layout
    # ------------------------------------------------------------------
    def build_ui(self):
        top = ttk.Frame(self, padding=(10, 8))
        top.pack(fill="x")
        ttk.Button(top, text="Scan network", command=self.scan_network).pack(side="left")
        ttk.Button(top, text="Add device by IP", command=self.add_by_ip).pack(side="left", padx=6)
        ttk.Button(top, text="Log out of app", command=self.app_logout).pack(side="right")

        self.status_var = tk.StringVar(value="Starting...")
        ttk.Label(self, textvariable=self.status_var, padding=(10, 0, 10, 6),
                  foreground="#333").pack(fill="x")

        body = ttk.PanedWindow(self, orient="horizontal")
        body.pack(fill="both", expand=True, padx=10, pady=(0, 10))

        # Left: device list
        left = ttk.Frame(body)
        body.add(left, weight=1)
        ttk.Label(left, text="Devices on this network", font=("Segoe UI", 10, "bold")).pack(anchor="w")
        cols = ("name", "ip", "service", "wifi")
        self.device_tree = ttk.Treeview(left, columns=cols, show="headings", selectmode="browse", height=18)
        for col, title, width in (("name", "Name", 130), ("ip", "IP", 110),
                                  ("service", "Service", 80), ("wifi", "Wi-Fi", 110)):
            self.device_tree.heading(col, text=title)
            self.device_tree.column(col, width=width, anchor="w")
        self.device_tree.pack(fill="both", expand=True, pady=(4, 0))
        self.device_tree.bind("<<TreeviewSelect>>", self.on_device_selected)

        # Right: per-device configuration
        right = ttk.Frame(body)
        body.add(right, weight=3)
        self.notebook = ttk.Notebook(right)
        self.notebook.pack(fill="both", expand=True)

        self.tab_overview = ttk.Frame(self.notebook, padding=12)
        self.tab_cards = ttk.Frame(self.notebook, padding=12)
        self.tab_wifi = ttk.Frame(self.notebook, padding=12)
        self.tab_security = ttk.Frame(self.notebook, padding=12)
        self.tab_history = ttk.Frame(self.notebook, padding=12)
        self.notebook.add(self.tab_overview, text="Overview")
        self.notebook.add(self.tab_cards, text="Payment cards")
        self.notebook.add(self.tab_wifi, text="Wi-Fi")
        self.notebook.add(self.tab_security, text="Device settings")
        self.notebook.add(self.tab_history, text="Transactions")
        self.notebook.bind("<<NotebookTabChanged>>", self.on_tab_changed)

        self.build_overview()
        self.build_cards()
        self.build_wifi()
        self.build_security()
        self.build_history()

    def build_overview(self):
        self.info_labels = {}
        grid = ttk.Frame(self.tab_overview)
        grid.pack(anchor="w", fill="x")
        for row, (key, title) in enumerate([
            ("name", "Device name"), ("mac", "MAC address"), ("fw", "Firmware"),
            ("ip", "Device address"), ("service", "Service"), ("wifi", "Internet (Wi-Fi)"),
            ("relay", "Relay"), ("payment", "Payment in progress"),
        ]):
            ttk.Label(grid, text=title, width=20).grid(row=row, column=0, sticky="w", pady=2)
            value = ttk.Label(grid, text="-", font=("Segoe UI", 10, "bold"))
            value.grid(row=row, column=1, sticky="w", pady=2)
            self.info_labels[key] = value

        actions = ttk.LabelFrame(self.tab_overview, text="Service and device", padding=10)
        actions.pack(anchor="w", fill="x", pady=(14, 0))
        ttk.Button(actions, text="Refresh", command=self.refresh_device).grid(row=0, column=0, padx=4, pady=4)
        ttk.Button(actions, text="Enable service", command=lambda: self.set_service(True)).grid(row=0, column=1, padx=4)
        ttk.Button(actions, text="Stop service", command=lambda: self.set_service(False)).grid(row=0, column=2, padx=4)
        ttk.Button(actions, text="Cancel active payment", command=self.cancel_payment).grid(row=0, column=3, padx=4)
        ttk.Button(actions, text="Restart device", command=self.restart_device).grid(row=0, column=4, padx=4)

        self.mode2_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            actions,
            text="2nd mode: 5 s relay + 'enjoy the game' (device only)",
            variable=self.mode2_var,
            command=self.set_mode2,
        ).grid(row=1, column=0, columnspan=5, sticky="w", padx=4, pady=(8, 0))

        relay_box = ttk.LabelFrame(self.tab_overview, text="Relay test", padding=10)
        relay_box.pack(anchor="w", fill="x", pady=(12, 0))
        ttk.Label(relay_box, text="Run relay for (seconds):").grid(row=0, column=0, padx=4)
        self.test_seconds = tk.IntVar(value=3)
        ttk.Spinbox(relay_box, from_=1, to=60, width=6, textvariable=self.test_seconds).grid(row=0, column=1, padx=4)
        ttk.Button(relay_box, text="Test relay", command=self.test_relay).grid(row=0, column=2, padx=4)
        ttk.Button(relay_box, text="Relay OFF now", command=self.relay_off).grid(row=0, column=3, padx=4)

    def build_cards(self):
        ttk.Label(self.tab_cards,
                  text="Payment cards: each card is an amount plus the relay behaviour that runs after payment. "
                       "Configured here and saved to the selected device only.",
                  wraplength=640).pack(anchor="w")

        body = ttk.Frame(self.tab_cards)
        body.pack(fill="both", expand=True, pady=(8, 0))

        self.cards_tree = ttk.Treeview(
            body, columns=("amount", "mode", "on", "count", "off", "wait"), show="headings",
            height=10, selectmode="browse")
        self.cards_tree.heading("amount", text="Amount (Rs.)")
        self.cards_tree.heading("mode", text="Relay behaviour")
        self.cards_tree.heading("on", text="ON (s)")
        self.cards_tree.heading("count", text="Pulses")
        self.cards_tree.heading("off", text="OFF (s)")
        self.cards_tree.heading("wait", text="Wait (s)")
        self.cards_tree.column("amount", width=120, anchor="center")
        self.cards_tree.column("mode", width=210, anchor="w")
        self.cards_tree.column("on", width=70, anchor="center")
        self.cards_tree.column("count", width=70, anchor="center")
        self.cards_tree.column("off", width=70, anchor="center")
        self.cards_tree.column("wait", width=70, anchor="center")
        self.cards_tree.pack(side="left", fill="both", expand=True)

        side = ttk.LabelFrame(body, text="Card settings", padding=10)
        side.pack(side="left", fill="y", padx=(10, 0))

        self.card_amount = tk.StringVar()
        self.card_mode = tk.StringVar()
        self.card_on = tk.StringVar()
        self.card_count = tk.StringVar()
        self.card_off = tk.StringVar()
        self.card_wait = tk.StringVar()
        self.card_message = tk.StringVar()

        def field(label, var, width=14, pady=(8, 0)):
            ttk.Label(side, text=label).pack(anchor="w", pady=pady)
            return ttk.Entry(side, textvariable=var, width=width)

        ttk.Label(side, text="Amount (Rs.)").pack(anchor="w")
        field("Amount (Rs.)", self.card_amount).pack(anchor="w")

        ttk.Label(side, text="Relay behaviour").pack(anchor="w", pady=(8, 0))
        self.mode_combo = ttk.Combobox(side, textvariable=self.card_mode, width=30,
                                       state="readonly", values=list(MODE_LABELS.values()))
        self.mode_combo.pack(anchor="w")
        self.mode_combo.bind("<<ComboboxSelected>>", lambda _e: self.on_mode_change())

        field("ON seconds", self.card_on).pack(anchor="w")
        field("Pulse count", self.card_count).pack(anchor="w")
        field("OFF seconds", self.card_off).pack(anchor="w")
        field("Wait seconds", self.card_wait).pack(anchor="w")
        field("Message (pulse mode)", self.card_message).pack(anchor="w")

        btns = ttk.Frame(side)
        btns.pack(anchor="w", fill="x", pady=(10, 0))
        ttk.Button(btns, text="Add card", command=self.add_card).pack(fill="x", pady=2)
        ttk.Button(btns, text="Update selected", command=self.update_card).pack(fill="x", pady=2)
        ttk.Button(btns, text="Delete selected", command=self.delete_card).pack(fill="x", pady=2)
        ttk.Button(btns, text="Move up", command=lambda: self.move_card(-1)).pack(fill="x", pady=2)
        ttk.Button(btns, text="Move down", command=lambda: self.move_card(1)).pack(fill="x", pady=2)

        sep = ttk.Separator(side)
        sep.pack(fill="x", pady=8)
        ttk.Button(side, text="Save cards to device", command=self.save_cards).pack(fill="x", pady=2)
        ttk.Button(side, text="Reload from device", command=self.refresh_device).pack(fill="x", pady=2)

        self.cards_tree.bind("<<TreeviewSelect>>", self.on_card_selected)

    def on_mode_change(self, _event=None):
        """Enable/disable the card fields depending on the chosen relay behaviour."""
        mode = self._mode_from_label(self.card_mode.get())
        timer = (mode == MODE_TIMER)
        message = (mode == MODE_PULSE_MESSAGE)
        count = (mode == MODE_PULSE_COUNT)

        # Sensible defaults when switching mode
        if timer:
            if not self.card_on.get():
                self.card_on.set("30")
            self.card_count.set(""); self.card_off.set(""); self.card_wait.set(""); self.card_message.set("")
        elif message:
            if not self.card_on.get():
                self.card_on.set("3")
            if not self.card_wait.get():
                self.card_wait.set("60")
            if not self.card_message.get():
                self.card_message.set("Best of luck for the game")
            self.card_count.set(""); self.card_off.set("")
        else:  # count
            if not self.card_on.get():
                self.card_on.set("2")
            if not self.card_off.get():
                self.card_off.set("1")
            if not self.card_count.get():
                self.card_count.set("5")
            self.card_wait.set(""); self.card_message.set("")

    def _rule_values(self, rule):
        mode = rule.get("mode", MODE_TIMER)
        amount = f"{float(rule.get('amount', 0)):g}"
        mode_label = MODE_LABELS.get(mode, str(mode))
        on_sec = str(rule.get("duration", ""))
        if mode == MODE_TIMER:
            count, off, wait = "-", "-", "-"
        elif mode == MODE_PULSE_MESSAGE:
            count, off, wait = "-", "-", str(rule.get("pulseWaitSec", ""))
        else:
            count = str(rule.get("pulseCount", ""))
            off = str(rule.get("pulseOffSec", ""))
            wait = "-"
        return (amount, mode_label, on_sec, count, off, wait)

    def render_cards(self):
        self.cards_tree.delete(*self.cards_tree.get_children())
        for i, card in enumerate(self.rules):
            self.cards_tree.insert("", "end", iid=str(i), values=self._rule_values(card))

    def on_card_selected(self, _event=None):
        sel = self.cards_tree.selection()
        if not sel:
            return
        card = self.rules[int(sel[0])]
        mode = card.get("mode", MODE_TIMER)
        self.card_mode.set(MODE_LABELS.get(mode, MODE_LABELS[MODE_TIMER]))
        self.card_amount.set(f"{float(card.get('amount', 0)):g}")
        self.card_on.set(str(card.get("duration", "")))
        self.card_count.set(str(card.get("pulseCount", "")))
        self.card_off.set(str(card.get("pulseOffSec", "")))
        self.card_wait.set(str(card.get("pulseWaitSec", "")))
        self.card_message.set(card.get("pulseMessage", "Best of luck for the game"))
        self.on_mode_change()

    def _parse_card(self):
        """Validate and build a full rule dict from the form fields."""
        try:
            amount = float(self.card_amount.get())
        except ValueError:
            messagebox.showerror(APP_TITLE, "Enter a numeric amount.")
            return None
        if amount < 1 or amount > 100000:
            messagebox.showerror(APP_TITLE, "Amount must be between 1 and 100000.")
            return None

        mode_label = self.card_mode.get()
        mode = self._mode_from_label(mode_label)
        rule = {"amount": amount, "mode": mode}

        def int_field(var, label, lo, hi):
            try:
                v = int(var.get())
            except ValueError:
                messagebox.showerror(APP_TITLE, f"Enter a whole number for {label}.")
                raise ValueError
            if v < lo or v > hi:
                messagebox.showerror(APP_TITLE, f"{label} must be between {lo} and {hi}.")
                raise ValueError
            return v

        try:
            if mode == MODE_TIMER:
                on_sec = int_field(self.card_on, "ON seconds", 1, 3600)
                rule.update(duration=on_sec, pulseCount=1, pulseOnSec=2, pulseOffSec=1,
                            pulseWaitSec=60, pulseMessage="Best of luck for the game")
            elif mode == MODE_PULSE_MESSAGE:
                on_sec = int_field(self.card_on, "ON seconds", 1, 3600)
                wait = int_field(self.card_wait, "Wait seconds", 1, 3600)
                msg = self.card_message.get().strip() or "Best of luck for the game"
                if len(msg) > 31:
                    messagebox.showerror(APP_TITLE, "Message must be at most 31 characters.")
                    return None
                rule.update(duration=on_sec, pulseCount=1, pulseOnSec=on_sec, pulseOffSec=1,
                            pulseWaitSec=wait, pulseMessage=msg)
            else:  # MODE_PULSE_COUNT
                count = int_field(self.card_count, "Pulse count", 1, MAX_PULSE_COUNT)
                on_sec = int_field(self.card_on, "ON seconds", 1, 3600)
                off = int_field(self.card_off, "OFF seconds", 1, 3600)
                rule.update(duration=on_sec, pulseCount=count, pulseOnSec=on_sec, pulseOffSec=off,
                            pulseWaitSec=60, pulseMessage="Best of luck for the game")
        except ValueError:
            return None

        return rule

    def _mode_from_label(self, label):
        for m, l in MODE_LABELS.items():
            if l == label:
                return m
        return MODE_TIMER

    def add_card(self):
        card = self._parse_card()
        if card is None:
            return
        if len(self.rules) >= 10:
            messagebox.showerror(APP_TITLE, "A device supports at most 10 payment cards.")
            return
        self.rules.append(card)
        self.render_cards()

    def update_card(self):
        sel = self.cards_tree.selection()
        card = self._parse_card()
        if not sel or card is None:
            return
        self.rules[int(sel[0])] = card
        self.render_cards()
        self.cards_tree.selection_set(sel[0])

    def delete_card(self):
        sel = self.cards_tree.selection()
        if sel:
            del self.rules[int(sel[0])]
            self.render_cards()

    def move_card(self, step):
        sel = self.cards_tree.selection()
        if not sel:
            return
        i = int(sel[0])
        j = i + step
        if 0 <= j < len(self.rules):
            self.rules[i], self.rules[j] = self.rules[j], self.rules[i]
            self.render_cards()
            self.cards_tree.selection_set(str(j))

    def save_cards(self):
        if not self.rules:
            if not messagebox.askyesno(APP_TITLE, "Save with NO payment cards? The device will show "
                                                   "'No cards' on the menu."):
                return
        cards = [dict(c) for c in self.rules]
        self.with_device("Saving payment cards...", lambda c: c.set_config(rules=cards),
                         lambda _r: messagebox.showinfo(APP_TITLE, "Payment cards saved to the device."))

    def build_wifi(self):
        ttk.Label(self.tab_wifi, text=(
            "Wi-Fi can only be changed while this PC is connected to the device's own hotspot "
            "(for example 'SmartGateway-Setup'). Scan works the same way."), wraplength=640).pack(anchor="w")

        self.wifi_current = ttk.Label(self.tab_wifi, text="Current Wi-Fi: -")
        self.wifi_current.pack(anchor="w", pady=(10, 6))

        row = ttk.Frame(self.tab_wifi)
        row.pack(anchor="w", fill="x")
        ttk.Button(row, text="Scan networks (hotspot)", command=self.scan_wifi).pack(side="left")

        body = ttk.Frame(self.tab_wifi)
        body.pack(fill="both", expand=True, pady=(8, 0))
        self.wifi_list = tk.Listbox(body, height=8, exportselection=False)
        self.wifi_list.pack(side="left", fill="both", expand=True)
        self.wifi_list.bind("<<ListboxSelect>>", self.on_wifi_pick)

        form = ttk.Frame(body, padding=(12, 0, 0, 0))
        form.pack(side="left", fill="y")
        self.wifi_ssid = tk.StringVar()
        self.wifi_pass = tk.StringVar()
        ttk.Label(form, text="Wi-Fi name (SSID)").pack(anchor="w")
        ttk.Entry(form, textvariable=self.wifi_ssid, width=28).pack(anchor="w")
        ttk.Label(form, text="Wi-Fi password").pack(anchor="w", pady=(8, 0))
        ttk.Entry(form, textvariable=self.wifi_pass, width=28, show="*").pack(anchor="w")
        ttk.Button(form, text="Save Wi-Fi and connect", command=self.save_wifi).pack(anchor="w", pady=(12, 0))

    def build_security(self):
        self.default_warning = ttk.Label(self.tab_security, text="", foreground="#b45309", wraplength=640)
        self.default_warning.pack(anchor="w")

        form = ttk.LabelFrame(self.tab_security, text="Device identity and backend", padding=10)
        form.pack(anchor="w", fill="x", pady=(6, 0))
        self.sec_vars = {key: tk.StringVar() for key in
                         ("name", "backendUrl", "deviceKey", "adminUser", "adminPass", "adminPass2")}
        rows = [
            ("Device name", "name", False),
            ("Backend URL", "backendUrl", False),
            ("Backend device key (leave blank to keep)", "deviceKey", True),
        ]
        for r, (label, key, secret) in enumerate(rows):
            ttk.Label(form, text=label).grid(row=r, column=0, sticky="w", pady=3, padx=(0, 10))
            ttk.Entry(form, textvariable=self.sec_vars[key], width=52,
                      show="*" if secret else "").grid(row=r, column=1, sticky="w", pady=3)

        login = ttk.LabelFrame(self.tab_security, text="Device admin login", padding=10)
        login.pack(anchor="w", fill="x", pady=(12, 0))
        ttk.Label(login, text="Admin username").grid(row=0, column=0, sticky="w", pady=3, padx=(0, 10))
        ttk.Entry(login, textvariable=self.sec_vars["adminUser"], width=30).grid(row=0, column=1, sticky="w")
        ttk.Label(login, text="New admin password (6-32)").grid(row=1, column=0, sticky="w", pady=3, padx=(0, 10))
        ttk.Entry(login, textvariable=self.sec_vars["adminPass"], width=30, show="*").grid(row=1, column=1, sticky="w")
        ttk.Label(login, text="Confirm new password").grid(row=2, column=0, sticky="w", pady=3, padx=(0, 10))
        ttk.Entry(login, textvariable=self.sec_vars["adminPass2"], width=30, show="*").grid(row=2, column=1, sticky="w")

        ttk.Button(self.tab_security, text="Save device settings", command=self.save_security).pack(anchor="w", pady=(14, 0))

    def build_history(self):
        ttk.Button(self.tab_history, text="Refresh history", command=self.load_history).pack(anchor="w")
        self.history_tree = ttk.Treeview(
            self.tab_history, columns=("time", "qr", "pay", "amount", "secs", "ok"),
            show="headings", height=14)
        for col, title, width in (("time", "Uptime (s)", 100), ("qr", "QR ID", 170), ("pay", "Payment ID", 170),
                                  ("amount", "Amount", 80), ("secs", "Relay (s)", 80), ("ok", "Result", 80)):
            self.history_tree.heading(col, text=title)
            self.history_tree.column(col, width=width, anchor="w")
        self.history_tree.pack(fill="both", expand=True, pady=(8, 0))

    # ------------------------------------------------------------------
    # Device discovery and selection
    # ------------------------------------------------------------------
    def scan_network(self):
        self.set_status("Scanning the local network for Smart Gateway devices...")
        self.run_bg(lambda: sgw.discover(timeout=2.5), self.on_discovered)

    def on_discovered(self, found):
        for info in found:
            self.devices[info["ip"]] = info
        self.refresh_device_list()
        self.set_status(f"Found {len(found)} device(s). Select one to configure it.")

    def add_by_ip(self):
        creds = form_dialog(self, "Add device by IP", [("Device IP address", "ip", False)],
                            note="Use this when discovery cannot see the device (for example across routers "
                                 "or on the device hotspot at 192.168.4.1).")
        if not creds or not creds["ip"].strip():
            return
        ip = creds["ip"].strip()
        self.set_status(f"Connecting to {ip}...")
        self.run_bg(lambda: sgw.DeviceClient(ip).info(),
                    lambda info: self._device_added(ip, info))

    def _device_added(self, ip, info):
        info["ip"] = ip
        self.devices[ip] = info
        self.refresh_device_list()
        self.set_status(f"Added {info.get('name', ip)} ({ip}).")

    def refresh_device_list(self):
        keep = self.selected_ip
        self.device_tree.delete(*self.device_tree.get_children())
        for ip, info in sorted(self.devices.items(), key=lambda kv: (kv[1].get("name", ""), kv[0])):
            wifi = info.get("staIP") or "offline"
            service = "ON" if info.get("serviceEnabled", True) else "STOPPED"
            self.device_tree.insert("", "end", iid=ip, values=(info.get("name", "?"), ip, service, wifi))
        if keep and self.device_tree.exists(keep):
            self.device_tree.selection_set(keep)

    def on_device_selected(self, _event=None):
        sel = self.device_tree.selection()
        if not sel:
            return
        ip = sel[0]
        if ip == self.selected_ip:
            return
        self.selected_ip = ip
        self.clear_device_views()
        self.with_device("Logging in / loading device...", lambda c: (c.status(), c.get_config()),
                         self.render_device)

    def clear_device_views(self):
        for label in self.info_labels.values():
            label.config(text="-")
        self.cards_tree.delete(*self.cards_tree.get_children())
        self.history_tree.delete(*self.history_tree.get_children())
        self.wifi_current.config(text="Current Wi-Fi: -")
        self.default_warning.config(text="")

    # ------------------------------------------------------------------
    # Device calls: login on demand, run in background, render on UI thread
    # ------------------------------------------------------------------
    def with_device(self, text, func, on_done=None):
        ip = self.selected_ip
        if not ip:
            messagebox.showinfo(APP_TITLE, "Select a device first.")
            return

        def run():
            client = self.clients[ip]
            self.set_status(text)
            self.run_bg(lambda: func(client), lambda res: self._action_done(res, on_done), self.show_error)

        if ip in self.clients and self.clients[ip].token:
            run()
        else:
            self.ensure_login(ip, run)

    def _action_done(self, result, on_done):
        self.set_status("Done.")
        if on_done:
            on_done(result)

    def ensure_login(self, ip, then):
        info = self.devices.get(ip, {})
        creds = form_dialog(self, f"Device login - {info.get('name', ip)}",
                            [("Device admin username", "user", False),
                             ("Device admin password", "password", True)],
                            initial={"user": "admin"},
                            note="Log in with the device admin account. This is the device's own login, "
                                 "not the app login.")
        if not creds:
            self.set_status("Login cancelled.")
            return
        client = sgw.DeviceClient(ip, port=info.get("port", sgw.API_PORT))

        def done(_result):
            self.clients[ip] = client
            then()

        self.set_status(f"Logging in to {ip}...")
        self.run_bg(lambda: client.login(creds["user"], creds["password"]), done)

    def show_error(self, exc):
        if isinstance(exc, sgw.DeviceError):
            if exc.status == 401 and self.selected_ip in self.clients:
                self.clients[self.selected_ip].token = None
                self.set_status("Session expired. Select the device again to log in.")
            elif exc.status == 403:
                self.set_status("Blocked by device.")
            else:
                self.set_status("Device rejected the request.")
            messagebox.showerror(APP_TITLE, exc.message)
        elif isinstance(exc, sgw.DeviceUnreachable):
            self.set_status("Device not reachable.")
            messagebox.showerror(APP_TITLE, str(exc))
        else:
            self.set_status("Error.")
            messagebox.showerror(APP_TITLE, str(exc))

    # ------------------------------------------------------------------
    # Rendering
    # ------------------------------------------------------------------
    def render_device(self, result):
        status, cfg = result
        info = self.devices.get(self.selected_ip, {})
        info.update({"name": cfg.get("name"), "serviceEnabled": cfg.get("serviceEnabled", True),
                     "staIP": cfg.get("wifiIP") if cfg.get("wifiConnected") else ""})
        self.refresh_device_list()

        labels = self.info_labels
        labels["name"].config(text=cfg.get("name", "-"))
        labels["mac"].config(text=cfg.get("mac", "-"))
        labels["fw"].config(text=cfg.get("fw", "-"))
        labels["ip"].config(text=self.selected_ip)
        labels["service"].config(text="ENABLED" if cfg.get("serviceEnabled") else "STOPPED",
                                 foreground="green" if cfg.get("serviceEnabled") else "red")
        labels["wifi"].config(text=(cfg.get("wifiIP") or "offline") if cfg.get("wifiConnected") else "offline")
        labels["relay"].config(text="ON" if status.get("relayOn") else "OFF")
        labels["payment"].config(text=("Yes - " + str(status.get("amount", "")) + " Rs")
                                 if status.get("paymentActive") else "No")

        self.rules = [{
            "amount": r.get("amount", 0),
            "duration": r.get("duration", 0),
            "mode": r.get("mode", MODE_TIMER),
            "pulseCount": r.get("pulseCount", 1),
            "pulseOnSec": r.get("pulseOnSec", 2),
            "pulseOffSec": r.get("pulseOffSec", 1),
            "pulseWaitSec": r.get("pulseWaitSec", 60),
            "pulseMessage": r.get("pulseMessage", "Best of luck for the game"),
        } for r in cfg.get("rules", [])]
        self.render_cards()

        self.mode2Enabled = bool(cfg.get("mode2Enabled", False))
        self.mode2_var.set(self.mode2Enabled)

        self.wifi_current.config(text=f"Current Wi-Fi: {cfg.get('wifiSsid') or '(not set)'}"
                                      + (" - connected" if cfg.get("wifiConnected") else " - offline"))
        self.sec_vars["name"].set(cfg.get("name", ""))
        self.sec_vars["backendUrl"].set(cfg.get("backendUrl", ""))
        self.sec_vars["deviceKey"].set("")
        self.sec_vars["adminUser"].set(cfg.get("adminUser", "admin"))
        self.sec_vars["adminPass"].set("")
        self.sec_vars["adminPass2"].set("")
        self.default_warning.config(text=(
            "WARNING: this device still uses the factory admin password. Set a new one under "
            "'Device settings'.") if cfg.get("defaultCredentials") else "")

    # ------------------------------------------------------------------
    # Overview actions
    # ------------------------------------------------------------------
    def refresh_device(self):
        self.with_device("Loading device...", lambda c: (c.status(), c.get_config()), self.render_device)

    def set_service(self, enabled):
        word = "enable" if enabled else "STOP"
        if not enabled and not messagebox.askyesno(
                APP_TITLE, "Stop the service on this device?\n\nNo payments will be accepted, any "
                           "pending QR is cancelled and the relay is switched OFF."):
            return
        self.with_device(f"{word.capitalize()}ing service...", lambda c: c.set_service(enabled),
                         lambda _r: self.refresh_device())

    def cancel_payment(self):
        self.with_device("Cancelling payment...", lambda c: c.cancel_payment(),
                         lambda _r: self.refresh_device())

    def restart_device(self):
        if not messagebox.askyesno(APP_TITLE, "Restart this device now?"):
            return
        ip = self.selected_ip
        self.with_device("Restarting device...", lambda c: c.restart(),
                         lambda _r: self._after_restart(ip))

    def _after_restart(self, ip):
        if ip in self.clients:
            self.clients[ip].token = None
        messagebox.showinfo(APP_TITLE, "The device is restarting. Scan the network again in about 20 seconds.")

    def test_relay(self):
        seconds = max(1, min(60, int(self.test_seconds.get() or 1)))
        self.with_device(f"Relay ON for {seconds} s...", lambda c: c.relay_test(seconds), None)

    def relay_off(self):
        self.with_device("Relay OFF...", lambda c: c.relay_off(), None)

    def set_mode2(self):
        enabled = bool(self.mode2_var.get())
        self.with_device(
            f"2nd mode {'on' if enabled else 'off'}...",
            lambda c: c.set_config(mode2Enabled=enabled),
            lambda _r: self.refresh_device(),
        )

    # ------------------------------------------------------------------
    # Wi-Fi (hotspot only)
    # ------------------------------------------------------------------
    def scan_wifi(self):
        self.with_device("Scanning Wi-Fi...", lambda c: c.wifi_scan(), self.render_scan)

    def render_scan(self, networks):
        self.scan_results = networks
        self.wifi_list.delete(0, "end")
        for net in sorted(networks, key=lambda n: -n.get("rssi", -100)):
            lock = "" if not net.get("secured") else " *"
            self.wifi_list.insert("end", f"{net['ssid']}  ({net.get('rssi', '?')} dBm){lock}")

    def on_wifi_pick(self, _event=None):
        sel = self.wifi_list.curselection()
        if not sel:
            return
        ssid = self.scan_results[sel[0]]["ssid"] if sel[0] < len(self.scan_results) else ""
        self.wifi_ssid.set(ssid)

    def save_wifi(self):
        ssid = self.wifi_ssid.get().strip()
        password = self.wifi_pass.get()
        if not ssid:
            messagebox.showerror(APP_TITLE, "Enter the Wi-Fi name.")
            return
        self.with_device("Saving Wi-Fi...", lambda c: c.set_wifi(ssid, password),
                         lambda _r: messagebox.showinfo(APP_TITLE, "Wi-Fi saved. The device is connecting."))

    # ------------------------------------------------------------------
    # Device settings
    # ------------------------------------------------------------------
    def save_security(self):
        v = {k: var.get().strip() for k, var in self.sec_vars.items()}
        fields = {}
        if v["name"]:
            fields["name"] = v["name"]
        if v["backendUrl"]:
            fields["backendUrl"] = v["backendUrl"]
        if v["deviceKey"]:
            fields["deviceKey"] = v["deviceKey"]
        if v["adminUser"]:
            fields["adminUser"] = v["adminUser"]
        if v["adminPass"] or v["adminPass2"]:
            if v["adminPass"] != v["adminPass2"]:
                messagebox.showerror(APP_TITLE, "The new admin passwords do not match.")
                return
            if not 6 <= len(v["adminPass"]) <= 32:
                messagebox.showerror(APP_TITLE, "Admin password must be 6-32 characters.")
                return
            fields["adminPass"] = v["adminPass"]
        if not fields:
            messagebox.showinfo(APP_TITLE, "Nothing to save.")
            return
        self.with_device("Saving device settings...", lambda c: c.set_config(**fields),
                         lambda _r: self._security_saved(fields))

    def _security_saved(self, fields):
        if "adminPass" in fields:
            messagebox.showinfo(APP_TITLE, "Saved. Use the new admin password next time you connect to this device.")
        else:
            messagebox.showinfo(APP_TITLE, "Device settings saved.")
        self.refresh_device()

    # ------------------------------------------------------------------
    # Transactions
    # ------------------------------------------------------------------
    def on_tab_changed(self, _event=None):
        if self.notebook.select() == str(self.tab_history):
            self.load_history()

    def load_history(self):
        if not self.selected_ip:
            return
        self.with_device("Loading transactions...", lambda c: c.transactions(), self.render_history)

    def render_history(self, rows):
        self.history_tree.delete(*self.history_tree.get_children())
        for i, tx in enumerate(rows):
            self.history_tree.insert("", "end", iid=str(i), values=(
                tx.get("timestampSec", ""), tx.get("qrId", ""), tx.get("paymentId", ""),
                f"{tx.get('amount', 0):g}", tx.get("duration", ""),
                "OK" if tx.get("success") else "FAILED"))


def main():
    app = App()
    app.mainloop()


if __name__ == "__main__":
    main()
