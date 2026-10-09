#pragma once
#include <JuceHeader.h>
#include "dsp/SunoChainDSP.h"

// Suno Chain: makes a dry vocal behave like a Suno vocal, using presets measured from Suno stems.
class SunoChainProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorValueTreeState::Listener
{
public:
    SunoChainProcessor();
    ~SunoChainProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Suno Chain"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // preset handling (message thread)
    bool loadPresetFile (const juce::File& f, juce::String& error);
    juce::String getPresetName() const;
    bool hasPreset() const;

    // learn (message thread)
    void startLearn();
    bool stopLearn (juce::String& message);   // summarises and applies; false if not enough material
    bool isLearning() const { return chain.learner.active.load(); }
    double learnSeconds() const;
    bool hasLearned() const;
    void clearLearn();

    // for the editor graph (message thread)
    struct CurveSnapshot
    {
        std::array<float, sc::kNumBands> target {}, source {}, correction {};
        bool hasTarget = false, hasSource = false;
        float sourceLevel = 0, sourceCrest = 0, targetCrest = 0;
    };
    CurveSnapshot getCurves() const;

private:
    void parameterChanged (const juce::String&, float) override { dirty = true; }
    sc::Params buildParams() const;   // call with presetLock held
    void restorePresetFromTree();

    sc::Chain chain;
    std::atomic<bool> dirty { true };
    mutable juce::SpinLock presetLock;
    sc::Params hidden;                 // preset/learned values that are not host parameters
    juce::String presetName { "No preset (load a .json)" };
    juce::var presetJson;              // full preset, kept in the session
    bool presetLoaded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SunoChainProcessor)
};
