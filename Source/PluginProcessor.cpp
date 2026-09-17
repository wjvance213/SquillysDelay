/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
namespace
{
    // The note lengths the "division" parameter can pick from, in the order the
    // host will list them. Each is expressed in quarter notes (beats), because a
    // beat is the unit tempo is measured in: at N BPM one quarter note lasts
    // 60000 / N ms, so any division is just that scaled by its quarterNotes.
    //
    //   T = triplet: three fit where two plain notes would, so 2/3 the length
    //   D = dotted:  the plain note plus half again, so 1.5x the length
    struct NoteDivision
    {
        const char* label;
        double      quarterNotes;
    };

    constexpr NoteDivision noteDivisions[] =
    {
        { "1/32",  0.125 },
        { "1/16T", 0.25 * 2.0 / 3.0 },
        { "1/16",  0.25 },
        { "1/16D", 0.25 * 1.5 },
        { "1/8T",  0.5 * 2.0 / 3.0 },
        { "1/8",   0.5 },
        { "1/8D",  0.5 * 1.5 },
        { "1/4T",  1.0 * 2.0 / 3.0 },
        { "1/4",   1.0 },
        { "1/4D",  1.0 * 1.5 },
        { "1/2T",  2.0 * 2.0 / 3.0 },
        { "1/2",   2.0 },
        { "1/2D",  2.0 * 1.5 },
        { "1/1",   4.0 },
    };

    constexpr int numNoteDivisions    = juce::numElementsInArray (noteDivisions);
    constexpr int defaultNoteDivision = 5;   // "1/8"
}

//==============================================================================
SquillysDelayAudioProcessor::SquillysDelayAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
       apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
#else
     : apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
#endif
{
    // Look the parameter up by ID exactly once, here on the message thread, and
    // keep the pointer. From now on the audio thread just reads the atomic.
    gainParameter     = apvts.getRawParameterValue ("gain");
    timeParameter     = apvts.getRawParameterValue ("time");
    feedbackParameter = apvts.getRawParameterValue ("feedback");
    mixParameter      = apvts.getRawParameterValue ("mix");
    syncParameter     = apvts.getRawParameterValue ("sync");
    divisionParameter = apvts.getRawParameterValue ("division");
}

SquillysDelayAudioProcessor::~SquillysDelayAudioProcessor()
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SquillysDelayAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gain", 1 },            // ID used in code + version hint for future changes
        "Gain",                                     // name the host shows the user
        juce::NormalisableRange<float> (-48.0f,     // minimum, in decibels
                                          6.0f,     // maximum, in decibels
                                          0.1f),    // step size
        0.0f));                                     // default: 0 dB = unity, no change

    // Delay time. The last NormalisableRange argument is a skew factor: values
    // below 1 give the low end of the range more knob travel, so the useful
    // 20-400 ms zone isn't crammed into the first tenth of the knob. With 0.4
    // the halfway point lands around 350 ms instead of 1000 ms.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "time", 1 },
        "Time",
        juce::NormalisableRange<float> (1.0f,                              // min ms
                                        (float) (maxDelaySeconds * 1000.0), // max ms: the buffer's headroom
                                        1.0f,                              // step
                                        0.4f),                             // skew
        500.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    // Feedback. Capped at 0.95 rather than 1.0 at the range level, so no host
    // automation or GUI can ever push the loop into runaway.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "feedback", 1 },
        "Feedback",
        juce::NormalisableRange<float> (0.0f, 0.95f, 0.01f),
        0.4f));

    // Wet/dry mix: 0 = dry only, 1 = echoes only.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 },
        "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.5f));

    // Tempo sync on/off. An AudioParameterBool is a parameter with exactly two
    // states; the generic editor draws it as a toggle button, and the host
    // treats it as an on/off automation lane.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "sync", 1 },
        "Tempo Sync",
        false));

    // Note division, only used while sync is on. An AudioParameterChoice is an
    // integer index that the host displays using the strings we give it, so
    // its label list is built straight from the noteDivisions table to keep
    // the two in step.
    juce::StringArray divisionLabels;
    for (const auto& division : noteDivisions)
        divisionLabels.add (division.label);

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "division", 1 },
        "Division",
        divisionLabels,
        defaultNoteDivision));

    return layout;
}

//==============================================================================
const juce::String SquillysDelayAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SquillysDelayAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SquillysDelayAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SquillysDelayAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SquillysDelayAudioProcessor::getTailLengthSeconds() const
{
    // Echoes keep ringing after the input stops. Telling the host we have a
    // tail stops it from cutting a render off the moment the source ends.
    return 4.0;
}

int SquillysDelayAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SquillysDelayAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SquillysDelayAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String SquillysDelayAudioProcessor::getProgramName (int index)
{
    return {};
}

void SquillysDelayAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void SquillysDelayAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    currentSampleRate = sampleRate;

    // Size the delay line for the *maximum* delay we will ever offer, not the
    // current one. Allocation is only allowed here, off the audio thread, so
    // reserving the headroom now means a time knob later never has to resize.
    const auto numChannels    = juce::jmax (2, getTotalNumInputChannels());
    const auto delayBufferSize = (int) std::ceil (maxDelaySeconds * sampleRate);

    delayBuffer.setSize (numChannels, delayBufferSize);

    // Freshly allocated memory holds whatever was there before. Inside a
    // feedback loop that garbage would be recirculated as a loud burst.
    delayBuffer.clear();
    writePosition = 0;

    // reset() tells each smoother the sample rate and ramp length, so it knows
    // how many samples a ramp spans. Then snap both to the knobs' current
    // positions: on the first block we want to *be* at the right value, not
    // ramp toward it from whatever was left over last time.
    feedbackSmoothed.reset (sampleRate, parameterRampSeconds);
    mixSmoothed.reset (sampleRate, parameterRampSeconds);
    feedbackSmoothed.setCurrentAndTargetValue (feedbackParameter->load());
    mixSmoothed.setCurrentAndTargetValue (mixParameter->load());

    // Same idea for gain: start the ramp from wherever the knob currently sits,
    // so the very first block after playback starts doesn't sweep up from
    // silence (or down from a stale value left over from the last time this
    // instance played).
    lastGain = juce::Decibels::decibelsToGain (gainParameter->load());
}

void SquillysDelayAudioProcessor::releaseResources()
{
    // Playback has stopped; hand the delay line's memory back. prepareToPlay
    // will rebuild it before the next run.
    delayBuffer.setSize (0, 0);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SquillysDelayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void SquillysDelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const auto numSamples      = buffer.getNumSamples();
    const auto delayBufferSize = delayBuffer.getNumSamples();

    // Nothing to do if the host hands us an empty block, or if it calls us
    // before prepareToPlay (some hosts do) and the delay line doesn't exist yet.
    if (numSamples == 0 || delayBufferSize == 0)
        return;

    // Which delay time to use this block, in milliseconds: the time knob, or
    // a note length worked out from the host tempo when sync is on.
    double delayMilliseconds = timeParameter->load();

    if (syncParameter->load() > 0.5f)
    {
        // Ask the host where its transport is. getPlayHead() is the host's
        // window into that: it can be null (no host, as in the offline test),
        // getPosition() can come back empty, and the position may not carry a
        // tempo, so every hop is an Optional we check before dereferencing.
        if (auto* playHead = getPlayHead())
            if (auto position = playHead->getPosition())
                if (auto bpm = position->getBpm())
                    if (*bpm > 0.0)
                        lastKnownBpm = *bpm;

        // The choice parameter's raw value is its index, stored as a float.
        const auto divisionIndex = juce::jlimit (0, numNoteDivisions - 1,
                                                 juce::roundToInt (divisionParameter->load()));

        // One quarter note is 60000 / BPM ms; scale by how many quarter notes
        // the chosen division is. A slow tempo with a long division can exceed
        // the buffer; the jlimit below clamps that to the maximum delay.
        delayMilliseconds = noteDivisions[divisionIndex].quarterNotes * 60000.0 / lastKnownBpm;
    }

    // Milliseconds -> whole samples, read once per block. Clamped so the read
    // head can never land on the write head (a 0-sample delay) or lap it
    // (>= buffer size).
    const auto delayInSamples = juce::jlimit (1, delayBufferSize - 1,
                                              juce::roundToInt (delayMilliseconds * 0.001 * currentSampleRate));

    // Point the smoothers at the knobs' latest positions. If nothing moved this
    // is a no-op; if something did, the ramp starts from wherever it is now.
    feedbackSmoothed.setTargetValue (feedbackParameter->load());
    mixSmoothed.setTargetValue (mixParameter->load());

    const auto numChannelsToProcess = juce::jmin (totalNumInputChannels, delayBuffer.getNumChannels());

    for (int channel = 0; channel < numChannelsToProcess; ++channel)
    {
        auto* channelData = buffer.getWritePointer (channel);
        auto* delayData   = delayBuffer.getWritePointer (channel);

        // Every channel walks the same stretch of the circle, so each one
        // starts from a local copy of the shared write position...
        int writePos = writePosition;

        // ...and, for the same reason, from a local copy of each smoother, so
        // every channel walks the same ramp instead of channel 0 consuming it.
        auto feedbackRamp = feedbackSmoothed;
        auto mixRamp      = mixSmoothed;

        for (int i = 0; i < numSamples; ++i)
        {
            // The read head trails the write head by the delay time. If that
            // puts it before index 0, wrap round to the end: this is the
            // "circular" part of the circular buffer.
            int readPos = writePos - delayInSamples;
            if (readPos < 0)
                readPos += delayBufferSize;

            const auto inputSample   = channelData[i];
            const auto delayedSample = delayData[readPos];

            // This sample's smoothed knob values.
            const auto feedbackAmount = feedbackRamp.getNextValue();
            const auto wetMix         = mixRamp.getNextValue();

            // Feedback loop: what we write now is the dry input plus a scaled
            // copy of the echo we just read, so each echo seeds the next.
            delayData[writePos] = inputSample + delayedSample * feedbackAmount;

            // Output is a crossfade between dry input and the echo.
            channelData[i] = inputSample * (1.0f - wetMix) + delayedSample * wetMix;

            if (++writePos >= delayBufferSize)
                writePos = 0;
        }
    }

    // Advance the shared write head and the shared smoothers once, after every
    // channel has consumed the same numSamples. skip() moves a smoother forward
    // by N samples without the per-sample work the channel loops already did.
    writePosition = (writePosition + numSamples) % delayBufferSize;
    feedbackSmoothed.skip (numSamples);
    mixSmoothed.skip (numSamples);

    // Gain runs last, after the delay, so the knob is a master output level
    // rather than an input trim that would also scale what enters the loop.
    //
    // The knob's position, in dB, as of right now. The host may have changed it
    // since the last block, either from the GUI or from an automation clip.
    const auto targetGain = juce::Decibels::decibelsToGain (gainParameter->load());

    // Slide smoothly from last block's gain to this one's across the whole
    // buffer, on every channel at once. A knob move therefore becomes a short
    // fade instead of a step discontinuity, which is what a click actually is.
    buffer.applyGainRamp (0, numSamples, lastGain, targetGain);

    // Remember where the ramp ended so the next block picks up seamlessly.
    lastGain = targetGain;
}

//==============================================================================
bool SquillysDelayAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SquillysDelayAudioProcessor::createEditor()
{
    // JUCE builds a plain GUI automatically from whatever is in the APVTS. Free
    // controls for every parameter we add, which keeps the focus on DSP until
    // the custom editor step at the end of the roadmap.
    return new juce::GenericAudioProcessorEditor (*this);
}

//==============================================================================
void SquillysDelayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // The APVTS keeps every parameter in a ValueTree, so saving the whole plugin
    // state is just: tree -> XML -> the block of bytes the host hands us.
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SquillysDelayAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // ...and loading is the same trip in reverse. The tag-name check guards
    // against a host handing us a block written by some other plugin.
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SquillysDelayAudioProcessor();
}
