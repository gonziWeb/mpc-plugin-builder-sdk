// rig_dsp.h - the non-neural building blocks of Monolith (Gonzi Audio). MIT.
//
//   Resampler    streaming polyphase windowed-sinc resampler (44.1 <-> 48 kHz around a NAM model)
//   resampleOffline()  the same kernel, one shot, for impulse responses
//   Convolver    zero-latency cabinet convolution: 64-tap direct head + FFT-partitioned tail
//   Gate, Drive (5 circuits), Eq3, Echo (4 kinds), Verb (4 kinds)   the effects; mono, except
//                Verb and the Ping Pong echo, whose output is stereo
//
// Everything allocates in init()/prepare() only; process() calls are realtime safe.

#ifndef MONOLITH_RIG_DSP_H
#define MONOLITH_RIG_DSP_H

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "dsp/biquad.h"
#include "dsp/delay_line.h"
#include "dsp/math.h"
#include "dsp/onepole.h"
#include "dsp/smoother.h"

namespace rig {

constexpr double kPiD = 3.14159265358979323846;

// ---------------------------------------------------------------------------------------------
// Kaiser-windowed sinc kernel shared by the two resamplers
// ---------------------------------------------------------------------------------------------
inline double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; if (term < 1e-12 * sum) break; }
    return sum;
}
// Weight of an input sample at distance d (in input samples) from the output instant.
// fc = cutoff relative to the input Nyquist (<= 1), half = half-width of the kernel in input samples.
inline double sincKernel(double d, double fc, double half)
{
    const double a = std::fabs(d);
    if (a >= half) return 0.0;
    const double beta = 9.0;
    const double r = a / half;
    const double w = besselI0(beta * std::sqrt(1.0 - r * r)) / besselI0(beta);
    const double x = kPiD * fc * d;
    const double s = a < 1e-9 ? 1.0 : std::sin(x) / x;
    return fc * s * w;
}

inline int gcdInt(int a, int b) { while (b != 0) { const int t = a % b; a = b; b = t; } return a < 0 ? -a : a; }

// ---------------------------------------------------------------------------------------------
// Streaming rational resampler. inRate/outRate is reduced to P/Q; one coefficient row per phase
// (Q rows of kTaps). process() may be called with any block size and returns the number of output
// samples written (it varies by one from call to call). Delay: kTaps/2 - 1 input samples.
// ---------------------------------------------------------------------------------------------
class Resampler
{
public:
    static constexpr int kTaps = 32;

    // False when the ratio needs an unreasonable number of phases (the caller then skips resampling).
    bool init(double inRate, double outRate, int maxInput)
    {
        const int in = (int)std::lround(inRate), out = (int)std::lround(outRate);
        if (in <= 0 || out <= 0) return false;
        const int g = gcdInt(in, out);
        const int p = in / g, q = out / g;
        if (q > 1024) return false;
        phases_ = q; stepInt_ = p / q; stepFrac_ = p % q;
        const double fc = 0.94 * std::min(1.0, (double)out / (double)in);
        const double half = kTaps / 2;
        coef_.assign((size_t)phases_ * kTaps, 0.0f);
        for (int f = 0; f < phases_; ++f)
        {
            double sum = 0.0, row[kTaps];
            for (int t = 0; t < kTaps; ++t)
            {
                const double d = (half - 1.0 + (double)f / (double)phases_) - (double)t;
                row[t] = sincKernel(d, fc, half); sum += row[t];
            }
            for (int t = 0; t < kTaps; ++t) coef_[(size_t)f * kTaps + (size_t)t] = (float)(row[t] / sum);
        }
        work_.assign((size_t)(maxInput + kTaps), 0.0f);
        maxInput_ = maxInput;
        reset();
        return true;
    }
    void reset() { std::fill(work_.begin(), work_.end(), 0.0f); pos_ = 0; frac_ = 0; }

    int maxOutput(int n) const { return (int)((long long)n * phases_ / std::max(1, stepInt_ * phases_ + stepFrac_)) + 2; }

    // Advance the counters only (no audio): how many samples process() would write for n inputs.
    int count(int n)
    {
        int m = 0;
        while (pos_ + kTaps <= kTaps - 1 + n) { ++m; advance(); }
        pos_ -= n;
        return m;
    }

    int process(const float* in, int n, float* out)
    {
        if (n > maxInput_) n = maxInput_;
        float* w = work_.data();
        std::memcpy(w + (kTaps - 1), in, sizeof(float) * (size_t)n);
        int m = 0;
        while (pos_ + kTaps <= kTaps - 1 + n)
        {
            const float* c = &coef_[(size_t)frac_ * kTaps];
            const float* x = w + pos_;
            float acc = 0.0f;
            for (int t = 0; t < kTaps; ++t) acc += x[t] * c[t];
            out[m++] = acc;
            advance();
        }
        std::memmove(w, w + n, sizeof(float) * (size_t)(kTaps - 1));
        pos_ -= n;
        return m;
    }

private:
    void advance() { frac_ += stepFrac_; pos_ += stepInt_; if (frac_ >= phases_) { frac_ -= phases_; ++pos_; } }

    std::vector<float> coef_, work_;
    int phases_ = 1, stepInt_ = 1, stepFrac_ = 0, pos_ = 0, frac_ = 0, maxInput_ = 0;
};

// One-shot resampling for impulse responses (not realtime).
inline std::vector<float> resampleOffline(const std::vector<float>& in, double inRate, double outRate)
{
    if (in.empty() || inRate <= 0.0 || outRate <= 0.0 || std::fabs(inRate - outRate) < 0.5) return in;
    const double ratio = inRate / outRate;                 // input samples per output sample
    const double fc = 0.96 * std::min(1.0, outRate / inRate);
    const double half = 24.0 / std::min(1.0, outRate / inRate);
    const size_t n = (size_t)std::ceil((double)in.size() / ratio);
    std::vector<float> out(n, 0.0f);
    for (size_t j = 0; j < n; ++j)
    {
        const double x = (double)j * ratio;
        const long lo = (long)std::ceil(x - half), hi = (long)std::floor(x + half);
        double acc = 0.0;
        for (long i = std::max(0L, lo); i <= hi && i < (long)in.size(); ++i) acc += (double)in[(size_t)i] * sincKernel(x - (double)i, fc, half);
        out[j] = (float)acc;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Radix-2 complex FFT of a fixed size (128), split re/im arrays.
// ---------------------------------------------------------------------------------------------
class Fft128
{
public:
    static constexpr int N = 128, LOG2N = 7;
    Fft128()
    {
        for (int i = 0; i < N; ++i)
        {
            int r = 0; for (int b = 0; b < LOG2N; ++b) if (i & (1 << b)) r |= 1 << (LOG2N - 1 - b);
            rev_[i] = r;
        }
        for (int i = 0; i < N / 2; ++i) { cos_[i] = (float)std::cos(2.0 * kPiD * i / N); sin_[i] = (float)std::sin(2.0 * kPiD * i / N); }
    }
    // inverse = true leaves the result unscaled (the caller folds 1/N into the filter spectra).
    void transform(float* re, float* im, bool inverse) const
    {
        for (int i = 0; i < N; ++i) { const int j = rev_[i]; if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
        for (int len = 2; len <= N; len <<= 1)
        {
            const int halfLen = len >> 1, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int k = 0; k < halfLen; ++k)
                {
                    const float wr = cos_[k * step], wi = inverse ? sin_[k * step] : -sin_[k * step];
                    const int a = i + k, b = a + halfLen;
                    const float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
                }
        }
    }
private:
    int rev_[N]; float cos_[N / 2], sin_[N / 2];
};

// ---------------------------------------------------------------------------------------------
// Zero-latency convolution with an impulse response of up to kMaxIr samples.
//   y = head (first 64 taps, direct form) + tail (the rest, overlap-save FFT partitions of 64).
// The tail of block m is computed when block m is complete and played during block m+1, which is
// exactly the 64-sample offset of the tail inside the impulse response, so nothing is delayed.
// ---------------------------------------------------------------------------------------------
class Convolver
{
public:
    static constexpr int B = 64, N = 128, BINS = B + 1, kMaxIr = 8192;

    // ir: mono impulse response at the host sample rate (already trimmed and normalised).
    void init(const std::vector<float>& ir)
    {
        const int len = (int)std::min<size_t>(ir.size(), (size_t)kMaxIr);
        for (int i = 0; i < B; ++i) headRev_[B - 1 - i] = i < len ? ir[(size_t)i] : 0.0f;
        parts_ = len > B ? (len - B + B - 1) / B : 0;
        hRe_.assign((size_t)parts_ * BINS, 0.0f); hIm_.assign((size_t)parts_ * BINS, 0.0f);
        xRe_.assign((size_t)std::max(1, parts_) * BINS, 0.0f); xIm_.assign((size_t)std::max(1, parts_) * BINS, 0.0f);
        float re[N], im[N];
        for (int k = 0; k < parts_; ++k)
        {
            std::memset(re, 0, sizeof(re)); std::memset(im, 0, sizeof(im));
            for (int i = 0; i < B; ++i) { const int s = B + k * B + i; if (s < len) re[i] = ir[(size_t)s] * (1.0f / N); }
            fft_.transform(re, im, false);
            std::memcpy(&hRe_[(size_t)k * BINS], re, sizeof(float) * BINS);
            std::memcpy(&hIm_[(size_t)k * BINS], im, sizeof(float) * BINS);
        }
        reset();
    }
    void reset()
    {
        std::memset(hist_, 0, sizeof(hist_)); std::memset(prev_, 0, sizeof(prev_)); std::memset(cur_, 0, sizeof(cur_));
        std::memset(tail_, 0, sizeof(tail_));
        std::fill(xRe_.begin(), xRe_.end(), 0.0f); std::fill(xIm_.begin(), xIm_.end(), 0.0f);
        w_ = 0; p_ = 0; fdl_ = 0;
    }
    int length() const { return B + parts_ * B; }

    void process(float* inout, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float x = inout[i];
            hist_[w_] = x; hist_[w_ + B] = x;
            w_ = (w_ + 1) & (B - 1);
            const float* win = hist_ + w_;              // oldest .. newest, 64 samples
            float acc = 0.0f;
            for (int t = 0; t < B; ++t) acc += win[t] * headRev_[t];
            inout[i] = acc + tail_[p_];
            cur_[p_] = x;
            if (++p_ == B) { p_ = 0; if (parts_ > 0) computeTail(); std::memcpy(prev_, cur_, sizeof(prev_)); }
        }
    }

private:
    void computeTail()
    {
        float re[N], im[N];
        std::memcpy(re, prev_, sizeof(prev_)); std::memcpy(re + B, cur_, sizeof(cur_));
        std::memset(im, 0, sizeof(im));
        fft_.transform(re, im, false);
        fdl_ = (fdl_ + parts_ - 1) % parts_;            // newest spectrum goes in front
        std::memcpy(&xRe_[(size_t)fdl_ * BINS], re, sizeof(float) * BINS);
        std::memcpy(&xIm_[(size_t)fdl_ * BINS], im, sizeof(float) * BINS);
        float aRe[BINS], aIm[BINS];
        std::memset(aRe, 0, sizeof(aRe)); std::memset(aIm, 0, sizeof(aIm));
        for (int k = 0; k < parts_; ++k)
        {
            int slot = fdl_ + k; if (slot >= parts_) slot -= parts_;
            const float* xr = &xRe_[(size_t)slot * BINS]; const float* xi = &xIm_[(size_t)slot * BINS];
            const float* hr = &hRe_[(size_t)k * BINS];    const float* hi = &hIm_[(size_t)k * BINS];
            for (int b = 0; b < BINS; ++b)
            {
                aRe[b] += xr[b] * hr[b] - xi[b] * hi[b];
                aIm[b] += xr[b] * hi[b] + xi[b] * hr[b];
            }
        }
        for (int b = 0; b < BINS; ++b) { re[b] = aRe[b]; im[b] = aIm[b]; }
        for (int b = 1; b < B; ++b) { re[N - b] = aRe[b]; im[N - b] = -aIm[b]; }   // real signal: conjugate symmetry
        fft_.transform(re, im, true);
        std::memcpy(tail_, re + B, sizeof(tail_));
    }

    Fft128 fft_;
    float headRev_[B] = {};
    float hist_[2 * B] = {}, prev_[B] = {}, cur_[B] = {}, tail_[B] = {};
    std::vector<float> hRe_, hIm_, xRe_, xIm_;
    int parts_ = 0, w_ = 0, p_ = 0, fdl_ = 0;
};

// Prepare a loaded impulse response: host rate, leading silence removed, tail trimmed where it no
// longer matters, short fade-out, level normalised. Returns false for an unusable file.
inline bool conditionIr(std::vector<float>& ir, double fileRate, double hostRate)
{
    if (ir.empty()) return false;
    ir = resampleOffline(ir, fileRate, hostRate);
    float peak = 0.0f; for (float v : ir) { if (!std::isfinite(v)) return false; peak = std::max(peak, std::fabs(v)); }
    if (peak < 1e-9f) return false;
    size_t first = 0; while (first < ir.size() && std::fabs(ir[first]) < peak * 0.001f) ++first;   // -60 dB
    if (first > 0) ir.erase(ir.begin(), ir.begin() + (long)first);
    double total = 0.0; for (float v : ir) total += (double)v * v;
    double rest = 0.0; size_t end = ir.size();
    while (end > 256 && rest + (double)ir[end - 1] * ir[end - 1] < total * 1e-7) { rest += (double)ir[end - 1] * ir[end - 1]; --end; }   // -70 dB of energy
    end = std::min(end, (size_t)Convolver::kMaxIr);
    ir.resize(end);
    const size_t fade = std::min<size_t>(64, ir.size() / 4);
    for (size_t i = 0; i < fade; ++i) ir[ir.size() - 1 - i] *= (float)i / (float)fade;
    total = 0.0; for (float v : ir) total += (double)v * v;
    const float g = (float)(0.5 / std::sqrt(std::max(total, 1e-18)));   // unit energy, less 6 dB of headroom
    for (float& v : ir) v *= g;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Noise gate: opens above the threshold, closes 6 dB below it after a short hold.
// ---------------------------------------------------------------------------------------------
class Gate
{
public:
    void prepare(double sr) { sr_ = sr; attack_ = mpc::dsp::onePoleCoeff(1.0f, sr); envRel_ = mpc::dsp::onePoleCoeff(20.0f, sr); reset(); }
    void reset() { env_ = 0.0f; gain_ = 0.0f; hold_ = 0; open_ = false; }
    void set(float threshDb, float releaseMs)
    {
        openLin_ = mpc::dsp::dbToGain(threshDb); closeLin_ = openLin_ * 0.5f;
        release_ = mpc::dsp::onePoleCoeff(releaseMs, sr_);
    }
    void process(float* x, int n)
    {
        const int holdSamples = (int)(0.04 * sr_);
        for (int i = 0; i < n; ++i)
        {
            const float a = std::fabs(x[i]);
            env_ = a > env_ ? a : env_ + envRel_ * (a - env_);
            if (env_ >= openLin_) { open_ = true; hold_ = holdSamples; }
            else if (env_ < closeLin_) { if (hold_ > 0) --hold_; else open_ = false; }
            const float target = open_ ? 1.0f : 0.0f;
            gain_ += (target > gain_ ? attack_ : release_) * (target - gain_);
            x[i] *= gain_;
        }
        if (gain_ < 1e-6f && !open_) gain_ = 0.0f;
    }
private:
    double sr_ = 44100.0;
    float env_ = 0.0f, gain_ = 0.0f, openLin_ = 0.001f, closeLin_ = 0.0005f, attack_ = 1.0f, release_ = 0.01f, envRel_ = 0.01f;
    int hold_ = 0; bool open_ = false;
};

// ---------------------------------------------------------------------------------------------
// Drive pedals. Five circuits in the spirit of the classics, all: low cut ("Tight") -> gain ->
// clipper -> tone low-pass -> mix.
//   0 Screamer  mid-forward overdrive: soft, slightly asymmetric clipping
//   1 Blues     open, low-gain overdrive: gentle x/(1+|x|) curve, wider tone range
//   2 Rodent    distortion: lots of gain into a hard clipper, dark "filter" style tone
//   3 Fuzz      very asymmetric, splatty, with the gain to match
//   4 Boost     clean gain up to +24 dB with a treble tilt, only rounding the very top
// ---------------------------------------------------------------------------------------------
class Drive
{
public:
    void prepare(double sr) { sr_ = sr; dc_.prepare(sr, 25.0f); reset(); }
    void reset() { hp_.reset(); lp_.reset(); lp2_.reset(); dc_.reset(); tilt_.reset(); }
    void set(int mode, float drive01, float tone01, float tightHz, float mix01)
    {
        mode_ = mode;
        static const float gainLo[5] = { 0.0f, 0.0f, 10.0f, 18.0f, 0.0f }, gainHi[5] = { 42.0f, 30.0f, 54.0f, 62.0f, 24.0f };
        static const float toneLo[5] = { 900.0f, 1200.0f, 600.0f, 700.0f, 3000.0f }, toneHi[5] = { 9000.0f, 14000.0f, 8000.0f, 7000.0f, 18000.0f };
        gain_ = mpc::dsp::dbToGain(gainLo[mode] + (gainHi[mode] - gainLo[mode]) * drive01);
        makeup_ = mode == 4 ? 1.0f : (mode == 2 ? 0.55f : (mode == 3 ? 0.5f : 1.0f)) / (1.0f + 2.2f * drive01);
        hp_.setCoeffs(mpc::dsp::BiquadCoeffs::highpass(sr_, tightHz));
        const float hz = std::min(toneLo[mode] * std::pow(toneHi[mode] / toneLo[mode], tone01), 0.45f * (float)sr_);
        lp_.setCutoff(hz, sr_); lp2_.setCutoff(std::min(hz * 1.6f, 0.45f * (float)sr_), sr_);
        tiltAmt_ = mode == 4 ? 2.0f * tone01 : 0.0f;       // Boost: Tone adds highs instead of removing them
        tilt_.setCutoff(1200.0f, sr_);
        mix_ = mix01;
    }
    void process(float* x, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float dry = x[i];
            float v = hp_.process(dry) * gain_;
            switch (mode_)
            {
                case 1:  v = 1.5f * v / (1.0f + std::fabs(v)); break;
                case 2:  v = mpc::dsp::clamp(v * 0.8f, -1.0f, 1.0f); v = v - 0.12f * v * v * v; break;
                case 3:  v = dc_.process(v > 0.0f ? mpc::dsp::fastTanh(v) : 0.55f * mpc::dsp::fastTanh(2.2f * v) - 0.08f * v / (1.0f + std::fabs(v))); break;
                case 4:  v += tiltAmt_ * (v - tilt_.process(v)); v = v * 0.5f; v = 2.0f * (std::fabs(v) < 0.6f ? v : mpc::dsp::fastTanh(v)); break;
                default: v = mpc::dsp::fastTanh(v + 0.08f * v * v * (v > 0.0f ? 1.0f : 0.4f)); break;
            }
            v = lp2_.process(lp_.process(v)) * makeup_;
            x[i] = dry + mix_ * (v - dry);
        }
    }
private:
    double sr_ = 44100.0; float gain_ = 1.0f, makeup_ = 1.0f, mix_ = 1.0f, tiltAmt_ = 0.0f; int mode_ = 0;
    mpc::dsp::Biquad hp_; mpc::dsp::OnePoleLP lp_, lp2_, tilt_; mpc::dsp::DCBlocker dc_;
};

// ---------------------------------------------------------------------------------------------
// Three-band tone control: low shelf 110 Hz, sweepable mid, high shelf 4 kHz.
// ---------------------------------------------------------------------------------------------
class Eq3
{
public:
    void prepare(double sr) { sr_ = sr; reset(); }
    void reset() { lo_.reset(); mid_.reset(); hi_.reset(); }
    void set(float bassDb, float midDb, float midHz, float trebleDb)
    {
        if (bassDb == b_ && midDb == m_ && midHz == f_ && trebleDb == t_) return;
        b_ = bassDb; m_ = midDb; f_ = midHz; t_ = trebleDb;
        lo_.setCoeffs(mpc::dsp::BiquadCoeffs::lowShelf(sr_, 110.0f, 0.7071f, bassDb));
        mid_.setCoeffs(mpc::dsp::BiquadCoeffs::peak(sr_, midHz, 0.8f, midDb));
        hi_.setCoeffs(mpc::dsp::BiquadCoeffs::highShelf(sr_, 4000.0f, 0.7071f, trebleDb));
    }
    void process(float* x, int n) { for (int i = 0; i < n; ++i) x[i] = hi_.process(mid_.process(lo_.process(x[i]))); }
private:
    double sr_ = 44100.0; float b_ = 1e9f, m_ = 1e9f, f_ = 1e9f, t_ = 1e9f;
    mpc::dsp::Biquad lo_, mid_, hi_;
};

// ---------------------------------------------------------------------------------------------
// Delays.
//   0 Digital    clean repeats, the Tone low-pass only
//   1 Tape       wow and flutter on the delay time, saturating and darkening repeats, a little low cut
//   2 Analog     bucket-brigade style: two low-passes in the loop, soft clipping, darker every time
//   3 Ping Pong  two lines, repeats alternate left / right (makes the signal stereo)
// ---------------------------------------------------------------------------------------------
class Echo
{
public:
    void prepare(double sr)
    {
        sr_ = sr;
        line_.prepare((int)(sr * 1.3) + 64); lineB_.prepare((int)(sr * 1.3) + 64);
        time_.setTime(120.0f, sr); loCut_.setCutoff(140.0f, sr);
        reset();
    }
    void reset() { line_.reset(); lineB_.reset(); lp_.reset(); lp2_.reset(); lpB_.reset(); loCut_.reset(); time_.snap(time_.target()); wow_ = flutter_ = 0.0f; }
    void set(int mode, float timeMs, float feedback, float toneHz, float mix)
    {
        mode_ = mode;
        time_.setTarget(mpc::dsp::clamp(timeMs * 0.001f * (float)sr_, 8.0f, (float)line_.maxDelay() - 40.0f));
        fb_ = feedback; mix_ = mix;
        const float hz = mode == 2 ? toneHz * 0.5f : (mode == 1 ? toneHz * 0.7f : toneHz);
        lp_.setCutoff(hz, sr_); lp2_.setCutoff(hz * 1.3f, sr_); lpB_.setCutoff(hz, sr_);
    }
    bool stereoOut() const { return mode_ == 3; }
    // L: mono in, left (or mono) out. R: written only in Ping Pong mode.
    void process(float* L, float* R, int n)
    {
        const float wowInc = 0.55f / (float)sr_, flutterInc = 6.3f / (float)sr_;
        for (int i = 0; i < n; ++i)
        {
            const float x = L[i];
            float d = time_.process();
            if (mode_ == 1)
            {
                wow_ += wowInc; if (wow_ >= 1.0f) wow_ -= 1.0f;
                flutter_ += flutterInc; if (flutter_ >= 1.0f) flutter_ -= 1.0f;
                d *= 1.0f + 0.0035f * mpc::dsp::fastSin01(wow_) + 0.0006f * mpc::dsp::fastSin01(flutter_);
            }
            float wet = lp_.process(line_.readLinear(d));
            if (mode_ == 3)
            {
                const float wetB = lpB_.process(lineB_.readLinear(d));
                line_.write(x + wetB * fb_);
                lineB_.write(wet);
                L[i] = x + mix_ * wet; R[i] = x + mix_ * wetB;
                continue;
            }
            float back = wet * fb_;
            if (mode_ == 1) { wet = loCut_.process(wet); back = mpc::dsp::fastTanh(1.4f * wet * fb_) * (1.0f / 1.4f); }
            else if (mode_ == 2) { wet = lp2_.process(wet); back = mpc::dsp::softClip(wet * fb_); }
            line_.write(x + back);
            L[i] = x + mix_ * wet;
        }
    }
private:
    double sr_ = 44100.0; float fb_ = 0.3f, mix_ = 0.25f, wow_ = 0.0f, flutter_ = 0.0f; int mode_ = 0;
    mpc::dsp::DelayLine line_, lineB_; mpc::dsp::OnePoleLP lp_, lp2_, lpB_; mpc::dsp::OnePoleHP loCut_; mpc::dsp::Smoother time_;
};

// ---------------------------------------------------------------------------------------------
// Reverbs: pre-delay, two allpass diffusers, an 8-line feedback delay network (Householder mix,
// one-pole damping per line). Mono in, stereo wet (even lines left, odd lines right).
//   0 Room    short lines, quick build-up
//   1 Hall    long lines, slow and wide
//   2 Plate   medium lines, dense diffusion, bright (little damping), the lows kept out of the tail
//   3 Spring  short lines behind a chain of allpasses that smears the attack into the "boing",
//             band-limited like a real tank
// ---------------------------------------------------------------------------------------------
class Verb
{
public:
    static constexpr int kLines = 8, kChirp = 14;
    void prepare(double sr)
    {
        sr_ = sr;
        for (int i = 0; i < kLines; ++i) line_[i].prepare((int)std::lround(kBase[i] * 1.45 * sr / 44100.0) + 8);
        apLen_[0] = std::max(4, (int)std::lround(142 * sr / 44100.0)); apLen_[1] = std::max(4, (int)std::lround(379 * sr / 44100.0));
        ap_[0].prepare(apLen_[0] + 4); ap_[1].prepare(apLen_[1] + 4);
        pre_.prepare((int)(sr * 0.13) + 8);
        mode_ = -1;
        reset();
    }
    void reset()
    {
        for (int i = 0; i < kLines; ++i) { line_[i].reset(); damp_[i] = 0.0f; }
        ap_[0].reset(); ap_[1].reset(); pre_.reset();
        for (int i = 0; i < kChirp; ++i) chirpX_[i] = chirpY_[i] = 0.0f;
        wetLoL_.reset(); wetLoR_.reset(); wetHiL_.reset(); wetHiR_.reset();
    }
    void set(int mode, float decaySec, float damp01, float mix, float preMs)
    {
        static const float scale[4] = { 0.55f, 1.4f, 0.85f, 0.42f }, decayMul[4] = { 0.5f, 1.2f, 1.0f, 0.6f };
        static const float dampMul[4] = { 1.0f, 1.0f, 0.45f, 0.8f }, diffusion[4] = { 0.55f, 0.6f, 0.72f, 0.5f };
        if (mode != mode_)
        {
            mode_ = mode;
            for (int i = 0; i < kLines; ++i) len_[i] = mpc::dsp::clamp((int)std::lround(kBase[i] * scale[mode] * sr_ / 44100.0), 8, line_[i].maxDelay());
            wetLoL_.setCutoff(mode == 3 ? 180.0f : (mode == 2 ? 160.0f : 30.0f), sr_); wetLoR_.setCutoff(mode == 3 ? 180.0f : (mode == 2 ? 160.0f : 30.0f), sr_);
            wetHiL_.setCutoff(mode == 3 ? 4200.0f : 16000.0f, sr_); wetHiR_.setCutoff(mode == 3 ? 4200.0f : 16000.0f, sr_);
        }
        const float t60 = std::max(0.1f, decaySec * decayMul[mode]);
        for (int i = 0; i < kLines; ++i) g_[i] = std::pow(10.0f, -3.0f * (float)len_[i] / ((float)sr_ * t60));
        dampCoef_ = 1.0f - 0.85f * damp01 * dampMul[mode];   // 1 = no damping
        apGain_ = diffusion[mode];
        mix_ = mix;
        preSamples_ = mpc::dsp::clamp((int)(preMs * 0.001f * (float)sr_), 1, pre_.maxDelay() - 1);
    }
    // in: mono (also the dry signal). Writes dry + wet to outL / outR.
    void process(const float* in, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float dry = in[i];
            pre_.write(dry);
            float v = pre_.readInt(preSamples_);
            if (mode_ == 3)                              // the spring's dispersion: first-order allpasses in series
                for (int k = 0; k < kChirp; ++k)
                {
                    const float y = -0.62f * v + chirpX_[k] + 0.62f * chirpY_[k];
                    chirpX_[k] = v; chirpY_[k] = y; v = y;
                }
            for (int a = 0; a < 2; ++a)                  // Schroeder allpass diffusers
            {
                const float d = ap_[a].readInt(apLen_[a]);
                const float w = v + apGain_ * d;
                ap_[a].write(w);
                v = d - apGain_ * w;
            }
            float o[kLines], sum = 0.0f;
            for (int k = 0; k < kLines; ++k)
            {
                damp_[k] += dampCoef_ * (line_[k].readInt(len_[k]) - damp_[k]);
                o[k] = damp_[k] * g_[k]; sum += o[k];
            }
            sum *= 2.0f / kLines;
            float l = 0.0f, r = 0.0f;
            for (int k = 0; k < kLines; ++k)
            {
                line_[k].write(o[k] - sum + ((k & 1) ? -v : v));
                if (k & 1) r += o[k]; else l += o[k];
            }
            l = wetHiL_.process(wetLoL_.process(l)); r = wetHiR_.process(wetLoR_.process(r));
            outL[i] = dry + mix_ * 0.4f * l;
            outR[i] = dry + mix_ * 0.4f * r;
        }
    }
private:
    static constexpr int kBase[kLines] = { 1117, 1361, 1559, 1789, 2011, 2269, 2503, 2741 };
    double sr_ = 44100.0;
    mpc::dsp::DelayLine line_[kLines], ap_[2], pre_;
    mpc::dsp::OnePoleHP wetLoL_, wetLoR_; mpc::dsp::OnePoleLP wetHiL_, wetHiR_;
    int len_[kLines] = {}, apLen_[2] = {}, preSamples_ = 1, mode_ = -1;
    float g_[kLines] = {}, damp_[kLines] = {}, chirpX_[kChirp] = {}, chirpY_[kChirp] = {}, dampCoef_ = 0.5f, apGain_ = 0.6f, mix_ = 0.2f;
};

} // namespace rig

#endif // MONOLITH_RIG_DSP_H
