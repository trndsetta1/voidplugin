// VOIDCARVE - spectral sidechain carving engine
// Pure C++17, no framework dependencies (so it can be unit-tested anywhere).
//
// How it works:
//   * Main input and sidechain are analysed with a 2048-point STFT (Hann, 8x overlap).
//   * The sidechain spectrum is split into 32 log-spaced bands (20 Hz - 20 kHz).
//   * Each band has its own attack/release envelope follower.
//   * The louder a sidechain band is, the more the SAME band of the main signal is
//     turned down - "carving" space for the sidechain (vocal, kick, lead...).
//   * Gains are interpolated per FFT bin and applied, then overlap-added back.
//   * Stereo pairs are packed into one complex FFT (L + iR) for efficiency; because the
//     gain curve is real and symmetric this is exact.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

namespace voidcarve
{
constexpr double kPi = 3.14159265358979323846;

//==============================================================================
class RadixFFT
{
public:
    void prepare (int order)
    {
        size = 1 << order;
        rev.assign ((size_t) size, 0);
        for (int i = 0; i < size; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b)
                if (i & (1 << b))
                    r |= 1 << (order - 1 - b);
            rev[(size_t) i] = r;
        }
        tw.resize ((size_t) size / 2);
        for (int i = 0; i < size / 2; ++i)
            tw[(size_t) i] = std::polar (1.0f, (float) (-2.0 * kPi * i / size));
    }

    void forward (std::complex<float>* d) const { run (d, false); }
    void inverse (std::complex<float>* d) const { run (d, true); } // scaled by 1/N

private:
    void run (std::complex<float>* d, bool inv) const
    {
        for (int i = 0; i < size; ++i)
            if (i < rev[(size_t) i])
                std::swap (d[i], d[rev[(size_t) i]]);

        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len / 2, step = size / len;
            for (int i = 0; i < size; i += len)
                for (int j = 0; j < half; ++j)
                {
                    auto w = tw[(size_t) (j * step)];
                    if (inv) w = std::conj (w);
                    const auto u = d[i + j];
                    const auto v = d[i + j + half] * w;
                    d[i + j] = u + v;
                    d[i + j + half] = u - v;
                }
        }

        if (inv)
        {
            const float s = 1.0f / (float) size;
            for (int i = 0; i < size; ++i)
                d[i] *= s;
        }
    }

    int size = 0;
    std::vector<int> rev;
    std::vector<std::complex<float>> tw;
};

//==============================================================================
struct CarveParams
{
    float amount        = 0.5f;    // 0..1
    float sensitivityDb = 0.0f;    // sidechain gain into the detector
    float attackMs      = 10.0f;
    float releaseMs     = 150.0f;
    float lowHz         = 20.0f;   // carving only happens between low & high
    float highHz        = 20000.0f;
    float mix           = 1.0f;    // 0..1
    float outputDb      = 0.0f;
};

//==============================================================================
class CarveEngine
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;  // 2048
    static constexpr int hopSize  = fftSize / 8;    // 256
    static constexpr int numBands = 32;
    static constexpr float minHz  = 20.0f;
    static constexpr float maxHz  = 20000.0f;
    static constexpr float maxDepthDb = 24.0f;      // reduction at 100% amount, full sidechain
    static constexpr float detFloorDb = -54.0f;     // detector range
    static constexpr float detTopDb   = -12.0f;

    CarveEngine()
    {
        for (int b = 0; b < numBands; ++b)
        {
            meterSc[(size_t) b].store (-100.0f);
            meterMain[(size_t) b].store (-100.0f);
            meterGr[(size_t) b].store (0.0f);
        }
    }

    int getLatencySamples() const noexcept { return fftSize; }

    static float bandEdgeHz (int j)   { return minHz * std::pow (maxHz / minHz, (float) j / numBands); }
    static float bandCenterHz (int b) { return minHz * std::pow (maxHz / minHz, ((float) b + 0.5f) / numBands); }

    void prepare (double sampleRate, int maxMainChannels)
    {
        sr = sampleRate;
        numMainCh = std::max (1, maxMainChannels);
        fft.prepare (fftOrder);

        win.resize (fftSize);
        for (int i = 0; i < fftSize; ++i)
            win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / fftSize)); // periodic Hann

        // Hann^2 summed over 8x overlap == 3
        olaScale = 1.0f / 3.0f;
        powNorm  = 3.0f * (float) fftSize * (float) fftSize / 32.0f; // sine of amp A -> level A

        inFifo.assign ((size_t) numMainCh, std::vector<float> (fftSize, 0.0f));
        outFifo.assign ((size_t) numMainCh, std::vector<float> (fftSize, 0.0f));
        scFifo.assign (2, std::vector<float> (fftSize, 0.0f));
        buf.assign (fftSize, {});
        binPow.assign (fftSize / 2 + 1, 0.0f);
        binPowMain.assign (fftSize / 2 + 1, 0.0f);
        binGain.assign (fftSize / 2 + 1, 1.0f);

        // band -> bin ranges for detection
        const double binHz = sr / fftSize;
        for (int b = 0; b < numBands; ++b)
        {
            int k0 = (int) std::ceil (bandEdgeHz (b) / binHz);
            int k1 = (int) std::ceil (bandEdgeHz (b + 1) / binHz);
            k0 = std::clamp (k0, 1, fftSize / 2 + 1);
            k1 = std::clamp (k1, 1, fftSize / 2 + 1);
            if (k0 >= k1)
            {
                const int k = std::clamp ((int) std::lround (bandCenterHz (b) / binHz), 1, fftSize / 2);
                k0 = k; k1 = k + 1;
            }
            bandK0[(size_t) b] = k0;
            bandK1[(size_t) b] = k1;
        }

        // bin -> band interpolation (log-frequency)
        binB0.assign (fftSize / 2 + 1, 0);
        binFrac.assign (fftSize / 2 + 1, 0.0f);
        const float logSpan = std::log (maxHz / minHz);
        for (int k = 0; k <= fftSize / 2; ++k)
        {
            const float f = std::max ((float) (k * binHz), 1.0f);
            float p = std::log (f / minHz) / logSpan * numBands - 0.5f;
            p = std::clamp (p, 0.0f, (float) (numBands - 1));
            const int i0 = std::min ((int) p, numBands - 2);
            binB0[(size_t) k] = i0;
            binFrac[(size_t) k] = p - (float) i0;
        }

        setParams (params);
        reset();
    }

    void reset()
    {
        for (auto& v : inFifo)  std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : outFifo) std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : scFifo)  std::fill (v.begin(), v.end(), 0.0f);
        env.fill (0.0f);
        pos = 0;
        hopCount = 0;
        scChCount = 0;
    }

    void setParams (const CarveParams& p)
    {
        params = p;
        const double hopSec = hopSize / sr;
        attCoef = (float) std::exp (-hopSec / std::max (0.0005, p.attackMs * 0.001));
        relCoef = (float) std::exp (-hopSec / std::max (0.0005, p.releaseMs * 0.001));
        lo = std::min (p.lowHz, p.highHz);
        hi = std::max (p.lowHz, p.highHz);
    }

    // main: in-place, sidechain: read only (may be nullptr / 0 channels)
    void process (float* const* main, int mainCh, const float* const* sc, int scCh, int numSamples)
    {
        mainCh = std::min (mainCh, numMainCh);
        scChCount = (sc == nullptr) ? 0 : std::min (scCh, 2);
        constexpr int mask = fftSize - 1;

        for (int s = 0; s < numSamples; ++s)
        {
            for (int c = 0; c < mainCh; ++c)
            {
                const float in = main[c][s];
                main[c][s] = outFifo[(size_t) c][(size_t) pos];
                outFifo[(size_t) c][(size_t) pos] = 0.0f;
                inFifo[(size_t) c][(size_t) pos] = in;
            }
            scFifo[0][(size_t) pos] = scChCount > 0 ? sc[0][s] : 0.0f;
            scFifo[1][(size_t) pos] = scChCount > 1 ? sc[1][s] : 0.0f;

            pos = (pos + 1) & mask;
            if (++hopCount == hopSize)
            {
                hopCount = 0;
                processFrame (mainCh);
            }
        }
    }

    // ---- meters for the GUI (written by audio thread, read by UI) ----
    std::array<std::atomic<float>, numBands> meterSc;    // dB (after sensitivity)
    std::array<std::atomic<float>, numBands> meterMain;  // dB
    std::array<std::atomic<float>, numBands> meterGr;    // dB of reduction (positive)
    std::atomic<float> sidechainPeakDb { -100.0f };

private:
    static float toDb (float lin) { return 20.0f * std::log10 (lin + 1.0e-9f); }
    static float fromDb (float db) { return std::pow (10.0f, db * 0.05f); }

    float rangeWeight (float fc) const
    {
        // half-octave soft fade outside [lo, hi]
        float w = 1.0f;
        if (fc < lo) w = std::min (w, 1.0f - 2.0f * std::log2 (lo / fc));
        if (fc > hi) w = std::min (w, 1.0f - 2.0f * std::log2 (fc / hi));
        return std::clamp (w, 0.0f, 1.0f);
    }

    void processFrame (int mainCh)
    {
        constexpr int mask = fftSize - 1;
        constexpr int half = fftSize / 2;

        // ---------- sidechain analysis ----------
        for (int i = 0; i < fftSize; ++i)
        {
            const int idx = (pos + i) & mask;
            buf[(size_t) i] = { scFifo[0][(size_t) idx] * win[(size_t) i],
                                scFifo[1][(size_t) idx] * win[(size_t) i] };
        }
        fft.forward (buf.data());

        const float chDiv = scChCount >= 2 ? 2.0f : 1.0f;
        for (int k = 0; k <= half; ++k)
            binPow[(size_t) k] = 0.5f * (std::norm (buf[(size_t) k]) + std::norm (buf[(size_t) ((fftSize - k) & mask)])) / chDiv;

        float peak = -100.0f;
        std::array<float, numBands> gainDb {};
        for (int b = 0; b < numBands; ++b)
        {
            float sum = 0.0f;
            for (int k = bandK0[(size_t) b]; k < bandK1[(size_t) b]; ++k)
                sum += binPow[(size_t) k];
            const float level = std::sqrt (sum / powNorm);

            float& e = env[(size_t) b];
            const float c = level > e ? attCoef : relCoef;
            e = level + c * (e - level);

            const float db = toDb (e) + params.sensitivityDb;
            peak = std::max (peak, db);
            const float x = std::clamp ((db - detFloorDb) / (detTopDb - detFloorDb), 0.0f, 1.0f);
            const float red = maxDepthDb * params.amount * x * rangeWeight (bandCenterHz (b));
            gainDb[(size_t) b] = -red;

            meterSc[(size_t) b].store (db, std::memory_order_relaxed);
            meterGr[(size_t) b].store (red * params.mix, std::memory_order_relaxed);
        }
        sidechainPeakDb.store (scChCount > 0 ? peak : -100.0f, std::memory_order_relaxed);

        // ---------- per-bin gain curve ----------
        const float out = fromDb (params.outputDb);
        const float mix = params.mix;
        for (int k = 0; k <= half; ++k)
        {
            const int b0 = binB0[(size_t) k];
            const float fr = binFrac[(size_t) k];
            const float gDb = gainDb[(size_t) b0] + fr * (gainDb[(size_t) b0 + 1] - gainDb[(size_t) b0]);
            binGain[(size_t) k] = out * ((1.0f - mix) + mix * fromDb (gDb));
        }

        // ---------- main signal, channel pairs packed as L + iR ----------
        std::fill (binPowMain.begin(), binPowMain.end(), 0.0f);
        for (int c = 0; c < mainCh; c += 2)
        {
            const bool hasB = (c + 1) < mainCh;
            const auto& A = inFifo[(size_t) c];
            for (int i = 0; i < fftSize; ++i)
            {
                const int idx = (pos + i) & mask;
                buf[(size_t) i] = { A[(size_t) idx] * win[(size_t) i],
                                    hasB ? inFifo[(size_t) c + 1][(size_t) idx] * win[(size_t) i] : 0.0f };
            }
            fft.forward (buf.data());

            for (int k = 0; k <= half; ++k)
                binPowMain[(size_t) k] += 0.5f * (std::norm (buf[(size_t) k]) + std::norm (buf[(size_t) ((fftSize - k) & mask)]));

            for (int k = 0; k < fftSize; ++k)
                buf[(size_t) k] *= binGain[(size_t) (k <= half ? k : fftSize - k)];

            fft.inverse (buf.data());

            auto& OA = outFifo[(size_t) c];
            for (int i = 0; i < fftSize; ++i)
            {
                const int idx = (pos + i) & mask;
                const float w = win[(size_t) i] * olaScale;
                OA[(size_t) idx] += buf[(size_t) i].real() * w;
                if (hasB)
                    outFifo[(size_t) c + 1][(size_t) idx] += buf[(size_t) i].imag() * w;
            }
        }

        for (int b = 0; b < numBands; ++b)
        {
            float sum = 0.0f;
            for (int k = bandK0[(size_t) b]; k < bandK1[(size_t) b]; ++k)
                sum += binPowMain[(size_t) k];
            meterMain[(size_t) b].store (toDb (std::sqrt (sum / powNorm / (float) std::max (1, mainCh))),
                                         std::memory_order_relaxed);
        }
    }

    double sr = 44100.0;
    int numMainCh = 2;
    RadixFFT fft;
    std::vector<float> win;
    float olaScale = 1.0f, powNorm = 1.0f;

    std::vector<std::vector<float>> inFifo, outFifo, scFifo;
    std::vector<std::complex<float>> buf;
    std::vector<float> binPow, binPowMain, binGain, binFrac;
    std::vector<int> binB0;
    std::array<int, numBands> bandK0 {}, bandK1 {};
    std::array<float, numBands> env {};

    int pos = 0, hopCount = 0, scChCount = 0;
    CarveParams params;
    float attCoef = 0.0f, relCoef = 0.0f, lo = 20.0f, hi = 20000.0f;
};

} // namespace voidcarve
