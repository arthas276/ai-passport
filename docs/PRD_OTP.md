<p align="right"><a href="PRD_OTP.zh_CN.md">简体中文</a> | English</p>

# OTP - Offline Authenticator

> Status: Taken over from an external delivery bundle, maintained in this repo since 2026-10-25; pending on-device verification
> Hardware: FoloToy AI Passport (ESP32-C3, 240x320 ST7789P3, 3 ADC buttons, ES8311)
> Target branch: feature/otp (from upstream main)
> PRD version: v1.3 (2026-10-27: entry polish - the secret page groups the draft into four-character blocks; with the clock calibrated, finishing entry shows the new account's current code for confirmation (OK saves, long OK cancels), so a mistyped secret is caught before it is stored; the PC-link page shows the live code after a USB add; otp_tool.py accepts otpauth:// URIs and decodes QR screenshots via --qr); v1.2 (2026-10-26: USB link console - the new "PC link" menu page (item 4) accepts ADD/TIME/DEL/LIST lines over the USB serial console while open, with tools/otp_tool.py on the host; keyboard secret entry and PC-clock sync replace the button-typed Base32 slog for USB users; replies never echo secrets); v1.1 (2026-10-25: takeover redesign — RTC-backed system time, full-range first calibration search, 5-minute timer wake removed)
> Mode: written by an external program, maintained here; the RFC 6238 core is pure logic and host-testable
<a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Offline OTP

This FoloToy AI Passport application shows a six-digit TOTP without a network. It is suitable for a demonstration or a low-risk account. It has no secure element, stores secrets in ordinary NVS Flash, and is not a replacement for a hardware token used by a bank or employer.

## Use

The device opens the account list directly. It does not enter the hardware-test menu.

- UP and DOWN move a selection or change the current character.
- A short OK confirms, enters, or appends one character.
- A long OK returns. On the secret page it finishes entry - with the clock calibrated the device then shows the new account's current code for confirmation (OK saves, long OK cancels) - and on the clock page it checks the code.
- The backlight dims after 20 idle seconds and the device deep-sleeps after 60. Any button wakes it.

Names use uppercase letters, digits, and spaces. Select > to finish the name. Secrets use Base32; while typing, the draft is displayed in four-character groups for easier proofreading. After entry finishes, a calibrated device shows the account's current code first (OK saves, long OK cancels): compare it with the phone's authenticator and save only on a match, so a typo is caught on the spot. To set the clock, add one account first, then enter the current six-digit code from another trusted device. A calibrated device searches only a two-minute window around the entry; an uncalibrated one (fresh device, or after battery removal) runs a full-range search from the anchor time — usually seconds, at most a minute or two, with an on-screen progress. On a match the system time is set, and the RTC slow clock keeps it running across deep sleep and reboots.

Typing a Base32 secret on three buttons is error-prone, so USB users have a better path (v1.2): open the "PC link" page (menu item 4) and run `tools/otp_tool.py` on the connected computer — `add` sends the name and secret typed on a real keyboard, and also accepts a pasted `otpauth://` URI or a QR screenshot via `--qr` (v1.3); `time` syncs the clock from the computer (no code search needed), `del` and `list` manage accounts. After a USB add the link page shows that account's live code for an immediate comparison against the phone (v1.3). The console only listens while that page is open, and replies never contain secrets. The on-device button flow stays fully usable offline.

Accounts, secrets, and the time survive reboots (including deep-sleep wake). A full flash or erased NVS clears them; flashing a merged image at `0x0` can do that — a normal flashing effect, not a reset of the OTP algorithm. Only removing the battery loses the time; recalibrate afterwards.

## Implementation

The application lives in `main/otp_core.c`, `otp_store.c`, `otp_power.c`, `otp_link.c`, and `otp_app.c`. The algorithm and the link-line parser are separate from LVGL, so the RFC 6238 and protocol tests run without a board. The screens are a new dark UI using a 20 px LXGW WenKai subset; they do not reuse the baseline test menu.

Pins, button windows, the display, and the deep-sleep shutdown order remain owned by the BSP. GPIO0 stays with the button BSP, and sleep uses the existing terminal shutdown calls.

## Limits

- There is no RTC. The RTC slow clock keeps time across deep sleep and reboots (slight drift; recalibrate to correct). Removing the battery loses the time and requires recalibration; a device shelved for many years may exceed the search window and fail the check.
- There is no secure element. Someone who can read Flash or attach USB may extract a secret.
- It is offline, so it cannot stop a phishing page or revoke a copied secret remotely.
- On a calibrated device the clock search covers roughly two minutes around the entered code; outside that window it reports failure and never saves a bad time. The full-range search takes the first matching window and immediately shows the current code for cross-checking against the trusted device.

Host tests cover the RFC vectors, Base32, and the idle-sleep threshold. On-device checks still need to cover Chinese rendering, button feel, deep-sleep wake, and records after power loss.
