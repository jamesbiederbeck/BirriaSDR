# Qualification: EDUP AX3000M (MT7921AU), USB `0e8d:7961`

This page records tests of one board that uses MediaTek's generic USB ID
`0e8d:7961`. The tests ran on one unit, bought as an EDUP "WiFi 6E Adapter
AX3000M" (the listing says MT7921AU, tri-band). Other dongles may share the ID
with different RF wiring. Only this unit was tested.

## Relation to upstream generic-ID support

Upstream commit `297d38d` (PR #4) enables `0e8d:7961` for any board and was
tested on an EDUP AX3000. This page covers a different unit. The table lists
the differences.

| | Upstream `297d38d` | This page |
| --- | --- | --- |
| Board | EDUP AX3000 | EDUP AX3000M ("WiFi 6E", tri-band) |
| Lowest working center | About 2179 MHz. Calibration fails below it | 2169 MHz. At 2168 MHz, tuning fails with `BIRRIASDR_ERROR_STATE` |
| Library floor for `0e8d:7961` | 2190 MHz, set in the device table | Unchanged. The upstream 2190 MHz floor applies |
| Matching | USB ID and chip ID | The same. A USB product string check was tried and dropped, because it would reject generic boards that upstream accepts |
| 5 GHz and 6 GHz | Not covered | Measured on this unit |

The two units disagree about the floor by 10 MHz, so the limit belongs to the
board, not to the USB ID.

## USB descriptors

The tested unit reports `iManufacturer` "MediaTek Inc.", `iProduct`
"Wireless_Device", `iSerial` "000000000" and `bcdDevice` 1.00. These look like
MediaTek reference-design defaults, so they do not identify the EDUP model,
and other vendors may report the same strings. The library matches
`0e8d:7961` on the USB ID and on the chip ID that `identify()` reads from MMIO
`0x70010200` (low 16 bits `0x7961`). The chip ID check runs before any boot or
reset write.

## Result

The 2.4 GHz receive path works on this board with the existing channel 6
profile. Capture is clean from **2169 to 2610 MHz**. At 2168 MHz and below,
retuning fails, so the 2156 MHz lower limit of the COMFAST board does not hold
on this unit. Upstream's 2190 MHz floor for generic-ID boards keeps requests
above the failure. The 5 GHz and 6 GHz bands are not implemented and were not
tested.

## Test setup

- The host is an x86-64 PC running Debian 12 and Linux 6.12.95, with the
  dongle on a USB 3.2 root port (`5000M`).
- The `mt7921u` kernel module was unloaded and blocked, then the dongle was
  replugged so that no vendor ROM patch was loaded. The binaries ran with
  `sudo`.
- The chip ID check in `src/chips/mt7921_capture.c` accepts the unit, and the
  analog die check during bring-up passes.
- The tests ran before upstream `297d38d`, with only the `qualified` flag and
  display name changed for this ID. Upstream's entry now provides the same
  thing, plus the 2190 MHz floor. The tests were not rerun against it.

## Evidence

| Check | Command | Result |
| --- | --- | --- |
| Offline tests | `ctest --test-dir build` | All ten tests pass |
| Enumeration | `birria_rx --list` | The list shows `0e8d:7961` with RX implemented |
| Capture | `birria_rx cap.cs16 8000000 manual` | Exit 0, stream status success, firmware state 3, reset success |
| Spectrum | 4,096-point averaged FFT of the 8,000,000 samples | Noise floor about 63 dB at the band edges, signals up to about 80 dB, strongest peak near −5 MHz from 2437 MHz |
| Tuning sweep | `birria_rx --frequency <hz> out.cs16 65536 manual` at 2200, 2250, 2300, 2350, 2400, 2437, 2480, 2500, 2550, 2600 and 2610 MHz | All exit 0 with stream status success |
| Lower limit | The same command, bisected | 2169 MHz passes and 2168 MHz fails. The 2156 and 2160 MHz points fail with `BIRRIASDR_ERROR_STATE` (−12) at stage 5 |
| Soak | `test_stability --run soak` (five minutes at 2437 MHz) | Two runs passed with about 12 billion samples and no DMA errors. An earlier run overran after about 18 seconds (−11, no DMA errors) and did not repeat |
| Control recovery | `test_control_recovery --run` | Passes |

## Known failures

- Before upstream `297d38d`, the `test_stability --run restart` and
  `--run live` phases and the `test_recovery --run` and `--run auto` commands
  failed at their tunes to 2156 MHz (`MT_RF_MIN_HZ`), the same limit as above.
  Both tests now take their endpoints from `birriasdr_get_tuning_range()`,
  which reports 2190 MHz for this ID. They are expected to pass but were not
  rerun.
- The cause of the 2168 MHz limit was not investigated. It may be the
  oscillator calibration or the readback check in `mt_tune_2g()` in
  `src/chips/mt7921_tune.c`. It is unknown whether the COMFAST board has the
  same limit.
- Sensitivity, frequency accuracy and filter response were not measured. As on
  the reference board, they are uncalibrated.
