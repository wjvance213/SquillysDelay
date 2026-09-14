/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
*/
class SquillysDelayAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    SquillysDelayAudioProcessor();
    ~SquillysDelayAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Builds the list of parameters this plugin exposes. Static because the APVTS
    // needs the finished layout while the processor is still being constructed,
    // so there is no "this" to call a normal member function on yet.
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // The single source of truth for every parameter: it owns the parameter
    // objects, talks to the host (automation, saved sessions), and gives the GUI
    // something to attach sliders to. Public so the editor can reach it.
    juce::AudioProcessorValueTreeState apvts;

private:
    //==============================================================================
    // Gain (step 2)
    //==============================================================================

    // Direct, lock-free handle on the "gain" parameter's current value (in dB).
    // Reading through this on the audio thread is cheap; looking the parameter up
    // by name in processBlock would not be.
    std::atomic<float>* gainParameter = nullptr;

    // The linear gain we finished the previous block on. Ramping from this value
    // to the new one spreads a knob move across the whole block instead of
    // applying it as one instant jump, which is what causes zipper clicks.
    float lastGain = 1.0f;

    //==============================================================================
    // Delay core (step 3) — values hardcoded for now; they become knobs in step 4
    //==============================================================================

    // How far back in time the read head sits behind the write head.
    static constexpr float delayTimeMs = 500.0f;

    // How much of each echo is fed back in to become the next echo. Must stay
    // below 1.0: at 1.0 the loop never loses energy, above it the level grows
    // every pass and runs away.
    static constexpr float feedbackAmount = 0.4f;

    // 0.0 = dry signal only, 1.0 = echoes only.
    static constexpr float wetMix = 0.5f;

    // How much delay-line headroom to allocate, independent of the current delay
    // time, so the buffer never has to be resized when the time knob arrives.
    static constexpr double maxDelaySeconds = 2.0;

    // The delay line itself: one long buffer per channel that we treat as a
    // circle. Sized once in prepareToPlay, never on the audio thread.
    juce::AudioBuffer<float> delayBuffer;

    // Where the write head currently sits in delayBuffer. Shared across channels
    // because every channel is written and read in lockstep.
    int writePosition = 0;

    // prepareToPlay hands us the sample rate; we need it in processBlock to turn
    // milliseconds into a number of samples.
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SquillysDelayAudioProcessor)
};
