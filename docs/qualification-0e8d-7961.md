# Qualification: EDUP AX3000M (MT7921AU), USB `0e8d:7961`

This page records how the `0e8d:7961` entry in `src/usb.c` was tested before
its `qualified` flag was set. The tests ran on one unit, bought as an EDUP
"WiFi 6E Adapter AX3000M" (the listing says MT7921AU, tri-band). The ID
`0e8d:7961` is MediaTek's generic ID, so other dongles may share it with
different RF wiring. Only this unit was tested.

## Qualification key

A board is qualified only when all three of these match:

1. The chip ID that `identify()` reads from MMIO `0x70010200` has low 16 bits
   `0x7961`. The check runs before any boot or reset write.
2. The vendor and product ID is `0e8d:7961`, as `lsusb` prints it.
3. The `iProduct` string is exactly `Wireless_Device`. The library reads it
   after opening the device and before claiming the interface. A mismatch
   returns `BIRRIASDR_ERROR_UNSUPPORTED`.

The product string narrows the match but does not uniquely identify this EDUP
model. The tested unit reports `iManufacturer` "MediaTek Inc.", `iProduct`
"Wireless_Device", `iSerial` "000000000" and `bcdDevice` 1.00. These look like
MediaTek reference-design defaults, so other vendors may report the same
strings. The `--list` option still keys on the USB ID alone, because it does
not open the device.

## Result

The 2.4 GHz receive path works on this board with the existing channel 6
profile. Capture is clean from **2169 to 2610 MHz**. At 2168 MHz and below,
retuning fails, so the library's 2156 MHz lower limit does not hold on this
unit. The 5 GHz and 6 GHz bands are not implemented and were not tested.

## Test setup

- The host is an x86-64 PC running Debian 12 and Linux 6.12.95, with the
  dongle on a USB 3.2 root port (`5000M`).
- The `mt7921u` kernel module was unloaded and blocked, then the dongle was
  replugged so that no vendor ROM patch was loaded. The binaries ran with
  `sudo`.
- The chip ID check in `src/chips/mt7921_capture.c` accepts the unit, and the
  analog die check during bring-up passes.
- The only code change is the `qualified` flag, the display name and the
  product string for this ID.

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

- The `test_stability --run restart` and `--run live` phases fail at the first
  tune to 2156 MHz (`MT_RF_MIN_HZ`), the same limit as above.
- The `test_recovery --run` and `--run auto` commands fail at their final
  retune to 2156 MHz.
- The cause of the 2168 MHz limit was not investigated. It may be the
  oscillator calibration or the readback check in `mt_tune_2g()` in
  `src/chips/mt7921_tune.c`. It is unknown whether the COMFAST board has the
  same limit.
- Sensitivity, frequency accuracy and filter response were not measured. As on
  the reference board, they are uncalibrated.
