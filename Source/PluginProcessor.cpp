/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    gainParameter = apvts.getRawParameterValue ("gain");
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
    return 0.0;
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
    juce::ignoreUnused (sampleRate, samplesPerBlock);

    // Start the ramp from wherever the knob currently sits, so the very first
    // block after playback starts doesn't sweep up from silence (or down from a
    // stale value left over from the last time this instance played).
    lastGain = juce::Decibels::decibelsToGain (gainParameter->load());
}

void SquillysDelayAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
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

    // The knob's position, in dB, as of right now. The host may have changed it
    // since the last block, either from the GUI or from an automation clip.
    const auto targetGain = juce::Decibels::decibelsToGain (gainParameter->load());

    // Slide smoothly from last block's gain to this one's across the whole
    // buffer, on every channel at once. A knob move therefore becomes a short
    // fade instead of a step discontinuity, which is what a click actually is.
    buffer.applyGainRamp (0, buffer.getNumSamples(), lastGain, targetGain);

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
