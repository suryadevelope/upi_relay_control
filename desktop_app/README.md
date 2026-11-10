# Smart Gateway Admin (local desktop app)

Configures every Smart Gateway device on your local network. Nothing is served
to the network as a webpage: devices answer only JSON on the LAN, and this app
is the only admin UI.

## Run

```
python main.py
```

Requires Python 3.9+ on Windows with tkinter (included in the standard installer).
No third-party packages.

First run asks you to create the app's admin account. It is stored only on this
PC as a salted PBKDF2 hash (`%APPDATA%\SmartGatewayAdmin\app_auth.json`).

## What you can do per device

| Tab | Actions |
|-----|---------|
| Overview | Live status, enable / stop service, cancel payment, restart, relay test, relay OFF |
| Payment cards | Amount + relay ON seconds per card (max 10), reorder, save to device |
| Wi-Fi | Scan and set the internet Wi-Fi. Only works while this PC is on the device hotspot |
| Device settings | Device name, backend URL, backend device key, device admin login |
| Transactions | Recent payments and relay results |

Device login is separate from the app login. Each device has its own admin
username and password (factory default `admin` / `admin123`; the app warns until
you change it).

## Finding devices

* **Scan network** sends a UDP broadcast on port 4210. Devices reply with name,
  service state and address.
* **Add device by IP** connects directly, for example to `192.168.4.1` when the PC
  is on a device hotspot, or across subnets where broadcast does not reach.

## Security model

* Device login is a challenge-response: the device sends a one-time nonce, the app
  returns `HMAC-SHA256(admin password, "user:nonce")`. The password is never sent.
* After 5 failed logins the device refuses logins for 30 s.
* Sessions expire after 15 minutes idle.
* Wi-Fi credential changes are refused unless the request comes over the device's
  own hotspot.
* Traffic after login uses a bearer token over plain HTTP on the LAN. Use a
  trusted network, or the device hotspot, when changing sensitive settings.

## Tests

```
python -m unittest discover -s tests -v
```

The tests run a fake device that implements the same contract as the firmware,
so they check this app's protocol code without hardware.
