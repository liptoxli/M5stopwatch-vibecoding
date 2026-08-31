# StopWatch BLE microphone protocol

Status: introduced in firmware v0.7.0 and updated for firmware v0.10.6 / StopWatch BLE Bridge v1.3.6. The v1.3.6 change is Mac-side output routing only; the BLE packet format is unchanged.

The microphone is an additive service on the same bonded BLE connection used
by HID and the existing Bridge service. It does not replace the full StopWatch
firmware or create a second Bluetooth stack.

## GATT

| Role | UUID | Properties |
| --- | --- | --- |
| Service | `7D2F0001-5CF1-4F3C-9F42-A8C8F6A1B001` | Primary |
| Control | `7D2F0002-5CF1-4F3C-9F42-A8C8F6A1B001` | Write, Write Without Response |
| Audio | `7D2F0003-5CF1-4F3C-9F42-A8C8F6A1B001` | Notify |
| Stats | `7D2F0004-5CF1-4F3C-9F42-A8C8F6A1B001` | Read, Notify |

Control is one byte: `0x01` starts streaming and resets sequence counters;
`0x00` stops streaming. Streaming also requires an active Audio notification
subscription.

## Audio packet v2

All integer fields are little-endian.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Packet sequence (`uint16`) |
| 2 | 1 | Codec (`1` = IMA-ADPCM) |
| 3 | 1 | Flags, currently zero |
| 4 | 4 | First decoded sample index (`uint32`) |
| 8 | 2 | Decoded sample count (`320`) |
| 10 | 2 | ADPCM predictor (`int16`) |
| 12 | 1 | ADPCM step index |
| 13 | 1 | Reserved |
| 14 | 160 | Packed 4-bit ADPCM codes |

The stream is mono, 16 kHz, 20 ms per packet. A packet is 174 bytes and
requires ATT MTU 177 or larger; firmware requests MTU 247, 2 Mbit PHY, a
251-byte data length, and a 15 ms connection interval. Expected payload rate
is about 8.7 KB/s over BLE versus 32 KB/s after PCM decoding.

The full firmware captures 20 ms from its 44.1 kHz ES8311 input path and
resamples to 320 samples before encoding. Packet and sample gaps are detected
by the Mac Bridge and replaced with silence so the virtual device clock stays
continuous.

## Stats packet v4

All integer fields are little-endian. Older Bridge versions can continue to
read the first 20 bytes; v1.3.5 and later also expose transport diagnostics.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | Stats version (`4`) |
| 1 | 1 | Streaming flag |
| 2 | 1 | Audio notification subscribed flag |
| 4 | 4 | Sample rate (`uint32`) |
| 8 | 4 | Packets sent (`uint32`) |
| 12 | 4 | Packets dropped (`uint32`) |
| 16 | 4 | PCM bytes represented (`uint32`) |
| 20 | 4 | Last NimBLE notification error (`int32`) |
| 24 | 4 | Consecutive notification failures (`uint32`) |
| 28 | 4 | Current free NimBLE mbufs (`uint32`) |

Firmware keeps at least four shared mbufs available before it enqueues another
high-rate audio notification. This prevents audio from starving HID, status,
and subscription traffic when the controller queue is temporarily congested.

## Runtime behavior

Virtual microphone mode defaults to off. When enabled, the Mac keeps the local
Core Audio virtual input and BLE subscription ready, but the device does not
continuously capture audio. Starting voice input sends `Start`; ending voice
input sends `Stop` after a short tail. Disabling the menu also unsubscribes,
stops Core Audio, and restores the previous macOS default input. The active
audio path is a real-time stream; no WAV file is created and no recording is
uploaded asynchronously.

When the Bridge detects a sustained stall, it stops the current Typeless
dictation and asks the user to repeat the sentence. It then cycles the Audio
notification subscription; a second transport fault within 60 seconds rebuilds
the full BLE connection. Interrupted speech is never silently joined to a
later stream.

## Mac virtual output routing (Bridge v1.3.6)

Decoded 16 kHz mono PCM passes through an Apple AUConverter and an explicit
AUHAL output bound to `M5StopWatchMic_2_UID`. The visible input consumed by
Typeless is `M5StopWatchMic_UID`. The existing BlackHole fallback accepts only
`BlackHole2ch_UID` with virtual transport; physical outputs and name-only matches
are rejected. The Bridge does not use the system speaker as a fallback.

Output starts muted until its device is verified. The final callback checks the
actual device before and after conversion, so a route fault also suppresses
converter-buffered audio. Recording preflight, queued start commands and health
checks verify the output again. An active dictation with a route fault ends and
requires a new recording; rebuilding the output must not resume that utterance.

Use the virtual device's existing 48 kHz format; the BLE source stays 16 kHz.
Live switching to 16 kHz has a known silent-input case. Hardware tests and
validation limits are documented in the [Bridge README](../tools/typeless_bridge/README.md#output-routing-safety-v136).
