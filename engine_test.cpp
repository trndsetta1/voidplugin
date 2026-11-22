// Engine sanity tests (build: g++ -O2 -std=c++17 -I../Source engine_test.cpp)
#include "CarveEngine.h"
#include <cstdio>
#include <random>

using namespace voidcarve;

static double toneAmp (const std::vector<float>& x, int start, int len, double f, double sr)
{
    double re = 0, im = 0;
    for (int i = 0; i < len; ++i)
    {
        const double ph = 2 * kPi * f * (start + i) / sr;
        re += x[(size_t) (start + i)] * std::cos (ph);
        im += x[(size_t) (start + i)] * std::sin (ph);
    }
    return 2 * std::sqrt (re * re + im * im) / len;
}

int main()
{
    const double sr = 48000;
    const int N = 48000 * 2, block = 480;
    int fails = 0;

    // 1) transparency: amount 0 -> output == input delayed by latency
    {
        CarveEngine e; e.prepare (sr, 2);
        CarveParams p; p.amount = 0; e.setParams (p);
        std::mt19937 rng (1); std::uniform_real_distribution<float> d (-1, 1);
        std::vector<float> L (N), R (N), oL, oR;
        for (int i = 0; i < N; ++i) { L[i] = d (rng); R[i] = d (rng); }
        oL = L; oR = R;
        std::vector<float> sc (N, 0.5f);
        for (int s = 0; s < N; s += block)
        {
            float* ch[2] = { oL.data() + s, oR.data() + s };
            const float* scp[1] = { sc.data() + s };
            e.process (ch, 2, scp, 1, block);
        }
        const int lat = e.getLatencySamples();
        double err = 0;
        for (int i = lat; i < N; ++i)
            err = std::max ({ err, (double) std::abs (oL[i] - L[i - lat]), (double) std::abs (oR[i] - R[i - lat]) });
        std::printf ("transparency max err: %.2e  %s\n", err, err < 1e-4 ? "OK" : "FAIL");
        fails += err >= 1e-4;
    }

    // 2) carving: sidechain at 1 kHz should duck main's 1 kHz but leave 100 Hz alone
    {
        CarveEngine e; e.prepare (sr, 2);
        CarveParams p; p.amount = 1.0f; e.setParams (p);
        std::vector<float> L (N), R (N), sc (N);
        for (int i = 0; i < N; ++i)
        {
            const float m = 0.3f * std::sin (2 * kPi * 1000 * i / sr) + 0.3f * std::sin (2 * kPi * 100 * i / sr);
            L[i] = R[i] = m;
            sc[i] = 0.5f * std::sin (2 * kPi * 1000 * i / sr);
        }
        for (int s = 0; s < N; s += block)
        {
            float* ch[2] = { L.data() + s, R.data() + s };
            const float* scp[1] = { sc.data() + s };
            e.process (ch, 2, scp, 1, block);
        }
        const double a1k = toneAmp (L, N / 2, 48000 / 2, 1000, sr);
        const double a100 = toneAmp (L, N / 2, 48000 / 2, 100, sr);
        const double r1k = 20 * std::log10 (a1k / 0.3), r100 = 20 * std::log10 (a100 / 0.3);
        std::printf ("1 kHz change: %.1f dB, 100 Hz change: %.1f dB  %s\n", r1k, r100,
                     (r1k < -12 && r100 > -1.5) ? "OK" : "FAIL");
        fails += !(r1k < -12 && r100 > -1.5);
        std::printf ("GR meter band @1k: ");
        for (int b = 0; b < CarveEngine::numBands; ++b)
            if (std::abs (CarveEngine::bandCenterHz (b) - 1000) < 150)
                std::printf ("%.0fHz=%.1fdB ", CarveEngine::bandCenterHz (b), e.meterGr[b].load());
        std::printf ("\n");
    }

    // 3) no sidechain connected -> transparent
    {
        CarveEngine e; e.prepare (sr, 1);
        CarveParams p; p.amount = 1.0f; e.setParams (p);
        std::vector<float> x (N), o;
        for (int i = 0; i < N; ++i) x[i] = 0.5f * std::sin (2 * kPi * 440 * i / sr);
        o = x;
        for (int s = 0; s < N; s += block) { float* ch[1] = { o.data() + s }; e.process (ch, 1, nullptr, 0, block); }
        double err = 0; const int lat = e.getLatencySamples();
        for (int i = lat; i < N; ++i) err = std::max (err, (double) std::abs (o[i] - x[i - lat]));
        std::printf ("no-sidechain max err: %.2e  %s\n", err, err < 1e-4 ? "OK" : "FAIL");
        fails += err >= 1e-4;
    }

    std::printf (fails ? "\nSOME TESTS FAILED\n" : "\nALL TESTS PASSED\n");
    return fails;
}
