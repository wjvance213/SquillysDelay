// Offline test harness for SquillysDelay. Not part of the plugin.
//
// Feeds a single-sample impulse through processBlock and checks that echoes
// land at the expected sample offsets with the expected amplitudes.
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <vector>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf ("  FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

// Run an impulse through the processor and return every output sample of channel 0
// (and check channel 1 matches along the way).
static std::vector<float> runImpulse (SquillysDelayAudioProcessor& proc, double sampleRate, int blockSize, int totalSamples)
{
    std::vector<float> out;
    out.reserve ((size_t) totalSamples);

    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi;

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const int n = juce::jmin (blockSize, totalSamples - start);
        block.clear();
        if (start == 0)
        {
            block.setSample (0, 0, 1.0f);
            block.setSample (1, 0, 1.0f);
        }

        // Hand the processor a view of exactly n samples, like a host would.
        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, n);
        proc.processBlock (view, midi);

        for (int i = 0; i < n; ++i)
        {
            const float l = view.getSample (0, i), r = view.getSample (1, i);
            CHECK (l == r, "channels differ at sample %d: L=%g R=%g", start + i, l, r);
            CHECK (std::isfinite (l), "non-finite sample at %d", start + i);
            out.push_back (l);
        }
    }
    juce::ignoreUnused (sampleRate);
    return out;
}

// Verify: dry impulse at 0 scaled by (1-mix); echoes at k*D scaled by mix*fb^(k-1); silence elsewhere.
static void checkEchoes (const std::vector<float>& out, int delaySamples, float feedback, float mix, float gainLinear, int numEchoes)
{
    const float tol = 1.0e-4f;
    std::vector<int> expectedIdx;

    CHECK (std::abs (out[0] - (1.0f - mix) * gainLinear) < tol, "dry sample: got %g expected %g", out[0], (1.0f - mix) * gainLinear);
    expectedIdx.push_back (0);

    float amp = mix * gainLinear;
    for (int k = 1; k <= numEchoes; ++k)
    {
        const int idx = k * delaySamples;
        if (idx >= (int) out.size()) break;
        CHECK (std::abs (out[(size_t) idx] - amp) < tol, "echo %d at sample %d: got %g expected %g", k, idx, out[(size_t) idx], amp);
        expectedIdx.push_back (idx);
        amp *= feedback;
    }

    // Everything that isn't an expected echo should be silent.
    int noisy = 0;
    for (int i = 0; i < (int) out.size(); ++i)
    {
        bool expected = false;
        for (int e : expectedIdx) if (e == i) { expected = true; break; }
        if (!expected && std::abs (out[(size_t) i]) > tol)
        {
            if (noisy < 5) std::printf ("  FAIL: unexpected energy at sample %d: %g\n", i, out[(size_t) i]);
            ++noisy;
        }
    }
    CHECK (noisy == 0, "%d samples had unexpected energy", noisy);
}

static void setParam (SquillysDelayAudioProcessor& proc, const char* id, float value)
{
    auto* p = proc.apvts.getParameter (id);
    if (p == nullptr) { std::printf ("  (no parameter '%s' - skipping)\n", id); return; }
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}

static float getParam (SquillysDelayAudioProcessor& proc, const char* id, float fallback)
{
    if (auto* v = proc.apvts.getRawParameterValue (id)) return v->load();
    return fallback;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // gives us a MessageManager etc.

    const double sr = 48000.0;
    const int blockSize = 512;
    const int totalSamples = (int) (5.0 * sr);   // > 2 s so the circular buffer wraps at least once

    int sectionsRun = 0;

    // ---- Section 1: defaults ------------------------------------------------
    {
        std::printf ("[1] impulse response at default settings\n");
        SquillysDelayAudioProcessor proc;
        proc.setPlayConfigDetails (2, 2, sr, blockSize);
        proc.prepareToPlay (sr, blockSize);

        // Defaults: 500 ms, fb 0.4, mix 0.5 (whether hardcoded or parameter defaults).
        const float timeMs = getParam (proc, "time", 500.0f);
        const float fb     = getParam (proc, "feedback", 0.4f);
        const float mix    = getParam (proc, "mix", 0.5f);
        const int   D      = juce::roundToInt (timeMs * 0.001 * sr);
        std::printf ("    time=%g ms (%d samples) feedback=%g mix=%g\n", timeMs, D, fb, mix);

        auto out = runImpulse (proc, sr, blockSize, totalSamples);
        checkEchoes (out, D, fb, mix, 1.0f, totalSamples / D);
        proc.releaseResources();
        ++sectionsRun;
    }

    // ---- Section 2: knobs moved (only meaningful once step 4 lands) ---------
    if (SquillysDelayAudioProcessor().apvts.getParameter ("time") != nullptr)
    {
        std::printf ("[2] impulse response with time=250ms feedback=0.6 mix=0.3 gain=-6dB\n");
        SquillysDelayAudioProcessor proc;
        setParam (proc, "time", 250.0f);
        setParam (proc, "feedback", 0.6f);
        setParam (proc, "mix", 0.3f);
        setParam (proc, "gain", -6.0f);
        proc.setPlayConfigDetails (2, 2, sr, blockSize);
        proc.prepareToPlay (sr, blockSize);

        const int D = juce::roundToInt (250.0 * 0.001 * sr);
        auto out = runImpulse (proc, sr, blockSize, totalSamples);
        checkEchoes (out, D, 0.6f, 0.3f, juce::Decibels::decibelsToGain (-6.0f), totalSamples / D);
        proc.releaseResources();
        ++sectionsRun;

        // ---- Section 3: a delay longer than the buffer is clamped, not crashed --
        std::printf ("[3] max delay time survives (clamp check)\n");
        SquillysDelayAudioProcessor proc2;
        setParam (proc2, "time", 2000.0f);
        setParam (proc2, "feedback", 0.0f);
        setParam (proc2, "mix", 1.0f);
        proc2.setPlayConfigDetails (2, 2, sr, blockSize);
        proc2.prepareToPlay (sr, blockSize);
        auto out2 = runImpulse (proc2, sr, blockSize, totalSamples);
        // Buffer is exactly 2 s, so a 2 s delay clamps to size-1 samples.
        const int Dclamped = (int) std::ceil (2.0 * sr) - 1;
        CHECK (std::abs (out2[(size_t) Dclamped] - 1.0f) < 1e-4f, "clamped echo at %d: got %g", Dclamped, out2[(size_t) Dclamped]);
        proc2.releaseResources();
        ++sectionsRun;
    }
    else
    {
        std::printf ("[2] skipped: no time/feedback/mix parameters yet (step 4 not landed)\n");
    }

    // ---- Section 4: processBlock before prepareToPlay must not crash --------
    {
        std::printf ("[4] processBlock before prepareToPlay is a no-op\n");
        SquillysDelayAudioProcessor proc;
        proc.setPlayConfigDetails (2, 2, sr, blockSize);
        juce::AudioBuffer<float> b (2, 64);
        b.clear(); b.setSample (0, 3, 0.5f);
        juce::MidiBuffer midi;
        proc.processBlock (b, midi);
        CHECK (b.getSample (0, 3) == 0.5f, "input was modified without a delay line");
        ++sectionsRun;
    }

    // ---- Section 5: a mix knob move ramps instead of jumping ----------------
    if (SquillysDelayAudioProcessor().apvts.getParameter ("mix") != nullptr)
    {
        std::printf ("[5] mix change is smoothed (no step)\n");
        SquillysDelayAudioProcessor proc;
        setParam (proc, "time", 2000.0f);    // echo won't arrive during this test
        setParam (proc, "feedback", 0.0f);
        setParam (proc, "mix", 0.0f);        // dry only: DC in -> 1.0 out
        proc.setPlayConfigDetails (2, 2, sr, blockSize);
        proc.prepareToPlay (sr, blockSize);

        juce::AudioBuffer<float> b (2, blockSize);
        juce::MidiBuffer midi;
        auto runDC = [&] { for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (b.getWritePointer (c), 1.0f, blockSize); proc.processBlock (b, midi); };

        runDC();
        CHECK (std::abs (b.getSample (0, blockSize - 1) - 1.0f) < 1e-6f, "dry DC should pass at 1.0, got %g", b.getSample (0, blockSize - 1));

        setParam (proc, "mix", 1.0f);        // wet only: the delayed signal is still silence, so output should ramp to 0
        float prev = 1.0f, maxDelta = 0.0f;
        for (int blk = 0; blk < 10; ++blk)   // 10 * 512 = ~107 ms, well past the 20 ms ramp
        {
            runDC();
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = b.getSample (0, i);
                maxDelta = juce::jmax (maxDelta, std::abs (v - prev));
                prev = v;
            }
        }
        std::printf ("    largest sample-to-sample step during the move: %g\n", maxDelta);
        CHECK (maxDelta < 0.01f, "mix change stepped by %g in one sample - not smoothed", maxDelta);
        CHECK (std::abs (prev) < 1e-6f, "after the ramp output should be 0, got %g", prev);
        proc.releaseResources();
        ++sectionsRun;
    }

    std::printf ("%d sections, %d failures -> %s\n", sectionsRun, failures, failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
