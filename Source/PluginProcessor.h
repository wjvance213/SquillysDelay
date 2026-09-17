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
    // Delay core (steps 3 + 4)
    //==============================================================================

    // How much delay-line headroom to allocate, independent of the current delay
    // time, so the buffer never has to be resized when the time knob moves.
    // Also sets the top of the "time" parameter's range.
    static constexpr double maxDelaySeconds = 2.0;

    // The three delay knobs, read the same lock-free way as gain. Each is cached
    // once in the constructor and never looked up by name on the audio thread.
    //
    //   time     - how far back the read head sits behind the write head, in ms
    //   feedback - how much of each echo is fed back in to become the next echo;
    //              the parameter range caps it below 1.0 so the loop always loses
    //              energy and can never run away
    //   mix      - 0.0 = dry signal only, 1.0 = echoes only
    std::atomic<float>* timeParameter     = nullptr;
    std::atomic<float>* feedbackParameter = nullptr;
    std::atomic<float>* mixParameter      = nullptr;

    // Tempo sync. While "sync" is on the time knob is ignored and the delay is
    // a note length worked out from the host's tempo instead:
    //
    //   sync     - AudioParameterBool; the raw value is 0.0 (off) or 1.0 (on)
    //   division - AudioParameterChoice; the raw value is the *index* of the
    //              chosen entry in the noteDivisions table in the .cpp
    std::atomic<float>* syncParameter     = nullptr;
    std::atomic<float>* divisionParameter = nullptr;

    // The last tempo the host reported. A host doesn't have to report one every
    // block (and the offline test has no host at all), so when it's missing we
    // keep using the previous value rather than snapping to some default.
    double lastKnownBpm = 120.0;

    // Feedback and mix are applied per sample, so a knob move would otherwise be
    // a step change on every sample from then on - a zipper click. SmoothedValue
    // turns setTargetValue() into a short linear ramp that getNextValue() walks
    // one sample at a time. (Time is deliberately not smoothed yet: moving an
    // integer read head still jumps; the fix is fractional reads in step 5.)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;

    // How long the feedback / mix ramps take. Long enough to hide a click,
    // short enough that the knob still feels immediate.
    static constexpr double parameterRampSeconds = 0.02;

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
