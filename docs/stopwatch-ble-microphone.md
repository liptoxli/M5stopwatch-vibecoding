# StopWatch BLE microphone protocol

Status: implemented in firmware v0.7.0 and StopWatch BLE Bridge v1.1.2.

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

## Runtime behavior

Virtual microphone mode defaults to off. The Mac menu starts the local Core
Audio output before subscribing and then sends `Start`. Disabling the menu
sends `Stop`, unsubscribes, stops Core Audio, and restores the previous macOS
default input. This is a continuous real-time stream; no WAV file is created
and no start/stop recording is uploaded asynchronously.
