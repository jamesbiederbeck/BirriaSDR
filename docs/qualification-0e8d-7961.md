# Qualification: EDUP AX3000M (MT7921AU), USB `0e8d:7961`

This records how the `0e8d:7961` entry in `src/usb.c` was tested before its
`qualified` flag was set. The tests ran on one unit, bought as an EDUP
"WiFi 6E Adapter AX3000M" (listing says MT7921AU, tri-band). `0e8d:7961` is
MediaTek's generic ID, so other dongles may share it with different RF wiring.
Only this unit was tested.

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
- The only code change is the `qualified` flag and display name for this ID.

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
