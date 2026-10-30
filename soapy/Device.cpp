/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The BirriaSDR Authors
 */
#include "SoapyBirria.hpp"
#include <SoapySDR/Logger.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

/* Construction has no front end to report to, so a failed open throws. */
static void check(int result, const char *what)
{
    if (result != 0)
        throw std::runtime_error(std::string("birriasdr: ") + what + " failed (" +
                                 std::to_string(result) + ")");
}

/* Setters called while an application runs log a refusal instead of throwing:
 * some front ends (SDR++) do not catch exceptions from these calls and abort.
 */
static bool report(int result, const char *what)
{
    if (result != 0)
        SoapySDR::logf(SOAPY_SDR_ERROR, "birria: %s failed: %s (%d)", what,
                       birriasdr_strerror(result), result);
    return result == 0;
}

/* Open the dongle at its enumeration index. Device arguments are not used.
 * Recovery is configured here because the library accepts it only while idle.
 * It keeps a stream running across overruns and reports each gap.
 */
SoapyBirria::SoapyBirria(uint32_t index, const SoapySDR::Kwargs &)
{
    check(birriasdr_open(&dev_, index), "open");
    const int rc = birriasdr_set_recovery(dev_, kMaxRestarts, &SoapyBirria::recoveryCallback, this);
    if (rc != 0) {
        birriasdr_close(dev_);
        check(rc, "set_recovery");
    }
    SoapySDR::logf(SOAPY_SDR_INFO, "birria: opened device %u", index);
}

/* Stop any stream first: the library requires the reader to be joined before
 * close, and destroying a joinable thread terminates the process.
 */
SoapyBirria::~SoapyBirria()
{
    deactivateStream(nullptr);
    if (dev_)
        birriasdr_close(dev_);
}

/* Warn front ends that the gains are raw codes. */
SoapySDR::Kwargs SoapyBirria::getHardwareInfo() const
{
    SoapySDR::Kwargs info;
    info["note"] = "experimental; raw gain codes, not dB";
    return info;
}

/* The receiver has one RX channel and no TX path. */
size_t SoapyBirria::getNumChannels(const int direction) const
{
    return direction == SOAPY_SDR_RX ? 1 : 0;
}

/* Raw hardware codes (the library has no calibrated dB gain yet). */
std::vector<std::string> SoapyBirria::listGains(const int, const size_t) const
{
    return {"LNA", "LPF", "DIG"};
}

/* Code limits for each gain element, in steps of one. The library accepts LNA
 * 0 to 5, and 5 GHz needs 4 or 5 for full sensitivity.
 */
SoapySDR::Range SoapyBirria::getGainRange(const int, const size_t, const std::string &name) const
{
    if (name == "LNA")
        return SoapySDR::Range(0, 5, 1);
    if (name == "LPF")
        return SoapySDR::Range(0, 8, 1);
    if (name == "DIG")
        return SoapySDR::Range(0, 12, 1);
    throw std::runtime_error("birria: unknown gain element " + name);
}

/* The library starts in automatic mode and accepts codes only in manual mode,
 * so codes set during AGC are kept and applied when AGC is turned off.
 */
bool SoapyBirria::hasGainMode(const int, const size_t) const
{
    return true;
}

/* Switch modes, then send the stored codes when entering manual mode. */
void SoapyBirria::setGainMode(const int, const size_t, const bool automatic)
{
    if (report(birriasdr_set_tuner_gain_mode(dev_, automatic ? 0 : 1), "set_tuner_gain_mode") &&
        !automatic)
        report(birriasdr_set_gain_codes(dev_, &gain_), "set_gain_codes");
}

/* Report automatic mode unless the library confirms manual mode. */
bool SoapyBirria::getGainMode(const int, const size_t) const
{
    birriasdr_gain gain;
    int manual = 0;
    return birriasdr_get_gain_codes(dev_, &gain, &manual) != 0 || !manual;
}

/* Store the rounded, clamped code and send it only in manual mode. */
void SoapyBirria::setGain(const int, const size_t, const std::string &name, const double value)
{
    const auto range = getGainRange(SOAPY_SDR_RX, 0, name);
    const uint8_t code = static_cast<uint8_t>(
        std::lround(std::clamp(value, range.minimum(), range.maximum())));
    if (name == "LNA")
        gain_.lna = code;
    else if (name == "LPF")
        gain_.lpf = code;
    else
        gain_.digital = code;
    if (!getGainMode(SOAPY_SDR_RX, 0))
        report(birriasdr_set_gain_codes(dev_, &gain_), "set_gain_codes");
}

/* Return the stored code, which the library may not have applied yet. */
double SoapyBirria::getGain(const int, const size_t, const std::string &name) const
{
    if (name == "LNA")
        return gain_.lna;
    if (name == "LPF")
        return gain_.lpf;
    if (name == "DIG")
        return gain_.digital;
    throw std::runtime_error("birria: unknown gain element " + name);
}

/* Request a center in Hz. The library rejects it without retuning if invalid. */
void SoapyBirria::setFrequency(const int, const size_t, const std::string &, const double frequency,
                               const SoapySDR::Kwargs &)
{
    report(birriasdr_set_center_freq(dev_, static_cast<uint64_t>(std::llround(frequency))),
           "set_center_freq");
}

/* Return the committed center, not the frequency of samples already queued. */
double SoapyBirria::getFrequency(const int, const size_t, const std::string &) const
{
    return static_cast<double>(birriasdr_get_center_freq(dev_));
}

/* The only tunable element is the RF center. */
std::vector<std::string> SoapyBirria::listFrequencies(const int, const size_t) const
{
    return {"RF"};
}

/* Report one range per contiguous band, so front ends cannot offer the gaps
 * between them. A band this board cannot reach returns UNSUPPORTED and is
 * skipped. INVALID marks the end of the list.
 */
SoapySDR::RangeList SoapyBirria::getFrequencyRange(const int, const size_t,
                                                   const std::string &) const
{
    SoapySDR::RangeList ranges;
    for (uint32_t index = 0;; ++index) {
        uint64_t minimum = 0, maximum = 0;
        const int rc = birriasdr_get_tuning_band(dev_, index, &minimum, &maximum);
        if (rc == BIRRIASDR_ERROR_INVALID)
            break;
        if (rc == BIRRIASDR_ERROR_UNSUPPORTED)
            continue;
        if (rc != 0) {
            report(rc, "get_tuning_band");
            break;
        }
        ranges.push_back(
            SoapySDR::Range(static_cast<double>(minimum), static_cast<double>(maximum)));
    }
    return ranges;
}

/* The hardware rate is fixed at 40 MSps. Lower rates are 40 MSps / N, decimated here. */
static const unsigned kFactors[] = {1, 2, 4, 5, 8, 10, 16, 20, 40};

/* Select the decimation factor for the next stream. The rate is idle-only. */
void SoapyBirria::setSampleRate(const int, const size_t, const double rate)
{
    if (running_) {
        SoapySDR::log(SOAPY_SDR_ERROR, "birria: cannot change the sample rate while streaming");
        return;
    }
    for (unsigned f : kFactors) {
        if (std::fabs(rate - kBaseRate / f) < 1.0) {
            decim_ = f;
            return;
        }
    }
    SoapySDR::log(SOAPY_SDR_ERROR,
                  "birria: sample rate must be 40 MSps divided by 1, 2, 4, 5, 8, 10, 16, 20 or 40");
}

/* Return the output rate after decimation. */
double SoapyBirria::getSampleRate(const int, const size_t) const
{
    return kBaseRate / decim_;
}

/* List the output rates from lowest to highest. */
std::vector<double> SoapyBirria::listSampleRates(const int, const size_t) const
{
    std::vector<double> rates;
    for (auto it = std::rbegin(kFactors); it != std::rend(kFactors); ++it)
        rates.push_back(kBaseRate / *it);
    return rates;
}

/* Both formats come from the same decimated stream. */
std::vector<std::string> SoapyBirria::getStreamFormats(const int, const size_t) const
{
    return {SOAPY_SDR_CF32, SOAPY_SDR_CS16};
}

/* Report the native format and its full-scale value. */
std::string SoapyBirria::getNativeStreamFormat(const int, const size_t, double &fullScale) const
{
    fullScale = 8192.0; /* signed 14-bit samples carried in int16 */
    return SOAPY_SDR_CS16;
}

/* Windowed-sinc low-pass, Blackman, cutoff at 0.45 of the output rate. */
void SoapyBirria::buildFilter()
{
    taps_.clear();
    if (decim_ == 1)
        return;
    const size_t n = 16 * decim_ + 1;
    const double fc = 0.45 / decim_; /* cycles per input sample */
    const double mid = (n - 1) / 2.0;
    double sum = 0;
    for (size_t k = 0; k < n; ++k) {
        const double x = k - mid;
        const double s = x == 0 ? 2 * fc : std::sin(2 * M_PI * fc * x) / (M_PI * x);
        const double w = 0.42 - 0.5 * std::cos(2 * M_PI * k / (n - 1)) +
                         0.08 * std::cos(4 * M_PI * k / (n - 1));
        taps_.push_back(static_cast<float>(s * w));
        sum += s * w;
    }
    for (auto &t : taps_)
        t = static_cast<float>(t / sum);
}

/* Accept one RX stream in CS16 or CF32. The device is the stream handle. */
SoapySDR::Stream *SoapyBirria::setupStream(const int direction, const std::string &format,
                                           const std::vector<size_t> &, const SoapySDR::Kwargs &)
{
    if (direction != SOAPY_SDR_RX || (format != SOAPY_SDR_CS16 && format != SOAPY_SDR_CF32))
        throw std::runtime_error("birria: only RX CF32 and CS16 streams are supported");
    cs16_ = format == SOAPY_SDR_CS16;
    return reinterpret_cast<SoapySDR::Stream *>(this);
}

/* Stop the stream if it is running. */
void SoapyBirria::closeStream(SoapySDR::Stream *)
{
    deactivateStream(nullptr);
}

/* Forward the library callback to the device instance. */
void SoapyBirria::asyncCallback(const int16_t *iq, uint32_t count, uint64_t, void *ctx)
{
    static_cast<SoapyBirria *>(ctx)->onSamples(iq, count);
}

/* Forward the library's gap notification to the device instance. */
void SoapyBirria::recoveryCallback(const birriasdr_recovery_event *, void *ctx)
{
    static_cast<SoapyBirria *>(ctx)->onGap();
}

/* Runs on the read thread: copy and return. Never block here or the device SRAM overruns. */
void SoapyBirria::onSamples(const int16_t *iq, uint32_t count)
{
    if (count == 0)
        return; /* an empty chunk would read as a gap */
    std::lock_guard<std::mutex> lock(mutex_);
    if (queued_ + count > kMaxQueuedSamples) {
        overflow_ = true;
        return;
    }
    queue_.emplace_back(iq, iq + 2 * static_cast<size_t>(count));
    queued_ += count;
    cond_.notify_one();
}

/* Runs on the read thread after the library has delivered every sample from
 * before the gap, so a marker queued here sits at the gap's position.
 */
void SoapyBirria::onGap()
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.emplace_back();
    cond_.notify_one();
}

/* Reset the filter and queue, then run the blocking library read on a worker.
 * A worker whose read already ended on its own is joined before replacing it.
 */
int SoapyBirria::activateStream(SoapySDR::Stream *, const int, const long long, const size_t)
{
    if (running_)
        return 0;
    if (worker_.joinable())
        worker_.join();
    buildFilter();
    in_.assign(taps_.empty() ? 0 : 2 * (taps_.size() - 1), 0.0f);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        queued_ = 0;
        overflow_ = false;
    }
    running_ = true;
    worker_ = std::thread([this] {
        report(birriasdr_read_async(dev_, &SoapyBirria::asyncCallback, this, 0, 0), "read_async");
        running_ = false;
        cond_.notify_all();
    });
    return 0;
}

/* Cancel the library read and join its worker. Right after activation the
 * worker may not have started the read yet, and cancel returns STATE, so keep
 * retrying until the read accepts it or the worker has finished.
 */
int SoapyBirria::deactivateStream(SoapySDR::Stream *, const int, const long long)
{
    if (worker_.joinable()) {
        while (birriasdr_cancel_async(dev_) == BIRRIASDR_ERROR_STATE && running_)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        worker_.join();
    }
    running_ = false;
    return 0;
}

/* Wait for enough input to fill the request, then filter and decimate it.
 * A gap, or an overflow of this module's queue, returns OVERFLOW and restarts
 * the filter history, so no output sample spans it. Input waiting from before
 * the gap is dropped. A stream that is not running returns STREAM_ERROR.
 */
int SoapyBirria::readStream(SoapySDR::Stream *, void *const *buffs, const size_t numElems,
                            int &flags, long long &timeNs, const long timeoutUs)
{
    flags = 0;
    timeNs = 0;
    const size_t history = taps_.empty() ? 0 : taps_.size() - 1;
    const size_t need = history + numElems * decim_; /* complex samples */
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::microseconds(timeoutUs);

    while (in_.size() / 2 < need) {
        std::vector<int16_t> chunk;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (queue_.empty() && !overflow_) {
                if (!running_)
                    return SOAPY_SDR_STREAM_ERROR;
                if (!cond_.wait_until(lock, deadline,
                                      [this] { return !queue_.empty() || overflow_ || !running_; }))
                    return SOAPY_SDR_TIMEOUT;
                if (queue_.empty() && !overflow_)
                    continue;
            }
            if (overflow_) {
                overflow_ = false;
                queue_.clear();
                queued_ = 0;
                in_.assign(2 * history, 0.0f); /* restart the filter after a gap */
                return SOAPY_SDR_OVERFLOW;
            }
            chunk = std::move(queue_.front());
            queue_.pop_front();
            queued_ -= chunk.size() / 2;
        }
        if (chunk.empty()) {
            in_.assign(2 * history, 0.0f);
            return SOAPY_SDR_OVERFLOW;
        }
        const size_t old = in_.size();
        in_.resize(old + chunk.size());
        for (size_t i = 0; i < chunk.size(); ++i)
            in_[old + i] = chunk[i];
    }

    const size_t outCount = std::min(numElems, (in_.size() / 2 - history) / decim_);
    constexpr float kScale = 1.0f / 8192.0f;
    float *outF = static_cast<float *>(buffs[0]);
    int16_t *outS = static_cast<int16_t *>(buffs[0]);
    for (size_t o = 0; o < outCount; ++o) {
        float ar, ai;
        if (taps_.empty()) {
            ar = in_[2 * o];
            ai = in_[2 * o + 1];
        } else {
            const float *p = &in_[2 * o * decim_];
            ar = ai = 0;
            for (size_t k = 0; k < taps_.size(); ++k) {
                ar += taps_[k] * p[2 * k];
                ai += taps_[k] * p[2 * k + 1];
            }
        }
        if (cs16_) {
            outS[2 * o] = static_cast<int16_t>(std::lrintf(ar));
            outS[2 * o + 1] = static_cast<int16_t>(std::lrintf(ai));
        } else {
            outF[2 * o] = ar * kScale;
            outF[2 * o + 1] = ai * kScale;
        }
    }
    in_.erase(in_.begin(), in_.begin() + 2 * outCount * decim_);
    return static_cast<int>(outCount);
}
