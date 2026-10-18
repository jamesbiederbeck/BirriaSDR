# Qualification: EDUP AX3000M (MT7921AU), USB `0e8d:7961`

This records tests of one `0e8d:7961` board. The tests ran on one unit, bought as an EDUP
"WiFi 6E Adapter AX3000M" (listing says MT7921AU, tri-band). `0e8d:7961` is
MediaTek's generic ID, so other dongles may share it with different RF wiring.
Only this unit was tested.

## Relation to upstream generic-ID support

Upstream commit `297d38d` (Tim Douglas, PR #4) enables `0e8d:7961` for any
board, tested on an EDUP AX3000. This page covers a different unit and
differs in these ways:

| | Upstream `297d38d` | This page |
| --- | --- | --- |
| Board | EDUP AX3000 | EDUP AX3000M ("WiFi 6E", tri-band) |
| Lowest working center | about 2179 MHz (fails to calibrate below) | 2169 MHz (2168 MHz fails with `BIRRIASDR_ERROR_STATE`) |
| Library floor for `0e8d:7961` | 2190 MHz, set in the device table | not changed; the upstream 2190 MHz floor applies |
| Matching | vid:pid plus the chip ID check | the same. A USB product string check was tried and dropped, because it would reject generic boards upstream accepts |
| 5 GHz and 6 GHz | not covered | measured on this unit, see [5 GHz and 6 GHz](#5-ghz-and-6-ghz) |

The two units disagree about the floor by 10 MHz, so the limit is per board,
not per ID.

## USB descriptors

The tested unit reports `iManufacturer` "MediaTek Inc.", `iProduct`
"Wireless_Device", `iSerial` "000000000" and `bcdDevice` 1.00. These are
MediaTek's reference-design defaults, so they do not identify the EDUP model,
and other vendors' boards may report the same. The library matches
`0e8d:7961` on the USB ID and on the digital chip ID at MMIO `0x70010200`
(low 16 bits `0x7961`), which `identify()` reads before any boot or reset
write.

## Result

The 2.4 GHz receive path works on this board with the existing channel 6
profile. Capture is clean from **2169 to 2610 MHz**. At 2168 MHz and below,
retuning fails, so the library's 2156 MHz lower limit does not hold on this unit.
5 GHz and 6 GHz are not implemented and were not tested.

## Test setup

- Host: x86-64 PC, Debian 12, Linux 6.12.95, USB 3.2 root port (5000M).
- `mt7921u` unloaded and blocked, then the dongle was replugged so no vendor ROM
  patch was loaded. Binaries were run with `sudo`.
- Identity: the digital chip ID at `0x70010200` has low 16 bits `0x7961`
  (`identify()` in `src/chips/mt7921_capture.c` accepts it). The analog-die
  check during bring-up passes.
- The tests originally ran with only the `qualified` flag and display name
  changed for this ID. Upstream's `297d38d` now provides that entry.

## Evidence

| Check | Command | Result |
| --- | --- | --- |
| Offline tests | `ctest --test-dir build` | 10 of 10 pass |
| Enumeration | `birria_rx --list` | `0e8d:7961 ... [RX implemented]` |
| Capture | `birria_rx cap.cs16 8000000 manual` | exit 0, "stream=success; firmware state=3; stage=8; reset=success" |
| Spectrum | 4096-point averaged FFT of the 8M samples | noise floor about 63 dB at the band edges, signals up to about 80 dB, strongest peak near -5 MHz from 2437 MHz |
| Tuning sweep | `birria_rx --frequency <hz> out.cs16 65536 manual` at 2200, 2250, 2300, 2350, 2400, 2437, 2480, 2500, 2550, 2600, 2610 MHz | all exit 0, "stream=success" |
| Lower limit | same command, bisected | 2169 MHz passes, 2168 MHz fails; 2156 and 2160 MHz fail with `BIRRIASDR_ERROR_STATE` (-12), "firmware state=0; stage=5" |
| Soak | `test_stability --run soak` (5 minutes, 2437 MHz) | two runs passed: about 12.0 G samples, 0 DMA errors. A third earlier run overran after about 18 s (-11, 0 DMA errors) and did not repeat |
| Control recovery | `test_control_recovery --run` | passes |

## Known failures

- `test_stability --run restart` and `--run live` fail at the first tune to
  2156 MHz (`MT_RF_MIN_HZ`), the same limit as above.
- `test_recovery --run` and `--run auto` fail at their final retune to 2156 MHz.
- The cause of the 2168 MHz limit was not investigated. It may be the
  oscillator calibration or the readback check in `mt_tune_2g`
  (`src/chips/mt7921_tune.c`). It is unknown whether the COMFAST board has the
  same limit.
- Sensitivity, frequency accuracy and filter response were not measured. As on
  the reference board, they are uncalibrated.
