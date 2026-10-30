# SoapySDR module

The `soapy/` directory holds a SoapySDR module (`driver=birria`) so any
SoapySDR application can use a BirriaSDR receiver. The module wraps the
library's RX path. It runs the dongle at its fixed 40 MSps and decimates to
the requested rate with a host-side filter.

## What the module provides

| Item | Value |
| --- | --- |
| Driver key | `driver=birria`. A dongle is selected by enumeration order with `index=N` |
| Sample rates | 40 MSps divided by 1, 2, 4, 5, 8, 10, 16, 20 or 40, giving 40 MSps down to 1 MSps. The dongle always runs at 40 MSps and the module decimates with a host-side filter |
| Stream formats | `CS16` (native) and `CF32` |
| Frequency range | One range per band, from `birriasdr_get_tuning_band()`, so front ends do not offer the gap between bands. The bands match the frequency tuning row of the README [specifications](../README.md#specifications) |
| Gains | `LNA` 0 to 5, `LPF` 0 to 8 and `DIG` 0 to 12. These are raw codes, not dB. The 5 GHz band needs `LNA` 4 or 5 for full sensitivity |
| Gain mode | Automatic (the library default) or manual. Gain codes apply only in manual mode. In automatic mode they are stored and sent once manual mode is selected |

Each frequency range is a command interval from the library, not measured RF
coverage. A band that the board cannot reach is left out. The sample rate can
change only while the stream is stopped, and a rate outside the list is
refused. The module has no hardware timestamps, no serial number, no bandwidth
control and no sensors.

Errors from tuning, sample-rate changes and gain changes are logged, not
thrown, because some front ends do not catch exceptions from these calls and
would abort. A rejected frequency leaves the receiver where it was.

The module enables the library's automatic recovery, so a stream keeps running
across overruns. Each gap reaches the application as `SOAPY_SDR_OVERFLOW` from
the read call, and the samples that follow are not continuous with the ones
before it. The number of missing samples is unknown. Clean overruns always
recover. Other device or transport faults get three restarts per stream, after
which reads return `SOAPY_SDR_STREAM_ERROR` and the module logs the cause. A
stream that is not running also returns `SOAPY_SDR_STREAM_ERROR`.

## Build

The module builds as part of the normal BirriaSDR build whenever SoapySDR is
installed. No extra flag is needed:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The build links the module against the in-tree `libbirriasdr`, so no separate
library install is required. The module is written to `build/soapy/`. If
SoapySDR is not installed, CMake prints a note and skips the module. Pass
`-DBIRRIASDR_SOAPY=OFF` to skip it on purpose.

Check that SoapySDR finds the dongle:

```sh
SOAPY_SDR_PLUGIN_PATH=$PWD/build/soapy SoapySDRUtil --find=driver=birria
```

Every command below sets `SOAPY_SDR_PLUGIN_PATH`. To avoid that, install the
module into the system module directory:

```sh
sudo cmake --install build
```

## USB access without root

Install the udev rule as the [build steps](../README.md#build-and-try)
describe, so the `plugdev` group can open the dongle without `sudo`.

The `mt7921u` kernel driver must not be bound to the dongle. A run under
`sudo` can leave root-owned lock files in `/tmp`, and a later unprivileged run
cannot open the device until those are removed. Only one program can open the
dongle at a time, so stop one client before you start another.

## Using the module from an application

A SoapySDR application loads the module when it starts. Set
`SOAPY_SDR_PLUGIN_PATH` when you launch it, or install the module as the build
steps describe. The application must link the same SoapySDR version that the
module builds against, because SoapySDR loads only modules with a matching ABI.

```sh
SOAPY_SDR_PLUGIN_PATH=$PWD/build/soapy <application>
```

Select the `driver=birria` device in the application's device list. Turn
automatic gain off to use the `LNA`, `LPF` and `DIG` controls. A reasonable
start is `LNA` 2, `LPF` 8 and `DIG` 0, or `LNA` 4 in the 5 GHz band. The sample
rate sets the visible span, and 5 MSps is a good start. Enter frequencies in
Hz, for example `2437000000` for Wi-Fi channel 6. General-purpose SDR
applications cannot demodulate Wi-Fi, so use their spectrum, waterfall or raw
I/Q views.

An application that serves the stream over a network needs a link that carries
the selected rate. Each complex sample takes 4 bytes in `CS16` and 8 bytes in
`CF32`, so 5 MSps needs 20 MB/s in `CS16`.

## Manual stream test

The `soapy/tests/test_soapy_stream.cpp` program captures through the module and
reports the averaged spectrum peak, to check decimation and the frequency axis.
It builds with the module when testing is enabled, but CTest does not run it
because it needs a dongle:

```sh
SOAPY_SDR_PLUGIN_PATH=$PWD/build/soapy build/soapy/test_soapy_stream <freq_hz> <rate> <seconds> [out.cf32]
```

To reproduce an application's problem, replay its call sequence against the
module in the same order: gain mode, gains, frequency, sample rate, then the
stream.
