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
    // v1.14: the real tail (reverb decay, x1.8 in BLACK, + predelay + echo), so offline exports never cut it
    double getTailLengthSeconds() const override
    {
        auto v = [this] (const char* id) { auto* a = apvts.getRawParameterValue (id); return a != nullptr ? (double) a->load() : 0.0; };
        if (v ("revFreeze") > 0.5) return 60.0;   // FREEZE holds the tail
        const double rt = v ("decay") * (v ("revBlack") > 0.5 ? 1.8 : 1.0);
        const double echo = v ("dlyOn") > 0.5 ? 4.0 : 0.0;
        return juce::jlimit (2.0, 60.0, v ("predelay") * 0.001 + 1.3 * rt + echo + 0.5);
    }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;
    bool advancedOpen = false;   // editor drawer state (saved with the session)
    float uiScale = 1.0f;        // editor size (saved with the session)
    juce::File currentPresetFile; // for stepping through the preset folder
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::File presetFolder();
    void installFactoryPresets();

    // preset handling (message thread)
    bool loadPresetFile (const juce::File& f, juce::String& error);
    bool savePresetFile (const juce::File& f, juce::String& error);
    juce::String getPresetName() const;
    bool hasPreset() const;

    // learn (message thread)
    void startLearn();
    bool stopLearn (juce::String& message);   // summarises and applies; false if not enough material
    bool isLearning() const { return chain.learner.active.load(); }
    double learnSeconds() const;
    bool hasLearned() const;
    void clearLearn();

    // Match Loudness (v1.8, message thread): measure the output while you play, then hold the gain that
    // brings it to the Suno vocal's loudness (from the preset). Press again to re-measure.
    void startLoudMatch();
    bool stopLoudMatch (juce::String& message);
    bool isLoudMatching() const { return chain.loud.active.load(); }
    double loudSeconds() const { return chain.loud.seconds(); }
    std::atomic<int> listenMode { 0 };   // LISTEN (editor only, not saved)
    void markDirty() { dirty = true; }
    float getTargetLufs() const { const juce::SpinLock::ScopedLockType sl (presetLock); return targetLufs; }

    // for the editor (message thread)
    struct CurveSnapshot
    {
        std::array<float, sc::kNumBands> target {}, source {}, correction {};
        bool hasTarget = false, hasSource = false;
        float sourceLevel = 0, sourceCrest = 0, targetCrest = 0;
        std::array<float, 3> widthTargetSing {}, widthTargetGap {};
        bool hasWidthTarget = false;
    };
    CurveSnapshot getCurves() const;
    struct Meters { float gr1, gr2, duck, in, out, width; double bpm; float delayMs; uint32_t blocks; float deess;
                    std::array<float, 6> widthBands; };   // output side-mid per band: singing low/mid/high, pauses low/mid/high
    Meters getMeters() const;
    void pumpMeters();   // v2.0.1: runs the WIDTH analysis (message thread, editor timer)

private:
    void parameterChanged (const juce::String&, float) override { dirty = true; }
    sc::Params buildParams() const;   // call with presetLock held
    void restorePresetFromTree();     // presetJson -> hidden (lock held)

    sc::Chain chain;
    std::atomic<bool> dirty { true };
    // v2.0.1: while a preset or session is being applied (several parameters, one after the other) the audio thread
    // keeps the previous settings, so it never runs a mix of old and new values
    std::atomic<int> paramBatch { 0 };
    struct BatchScope
    {
        explicit BatchScope (SunoChainProcessor& p) : o (p) { ++o.paramBatch; }
        ~BatchScope() { o.dirty = true; --o.paramBatch; }
        SunoChainProcessor& o;
    };
    static constexpr int kStateVersion = 2;   // EXTRA.stateVersion (v2.0.1); absent = v2.0 or older
    std::atomic<double> hostBpm { 120.0 };
    mutable juce::SpinLock presetLock;
    juce::SpinLock meterLock;          // prepareToPlay vs pumpMeters
    sc::Params hidden;                 // preset/learned values that are not host parameters
    std::array<float, sc::kNumBands> learnAppliedCorr {};   // EQ correction active while Learn listened
    juce::String presetName { "No preset (load a .json)" };
    juce::var presetJson;              // full preset, kept in the session
    bool presetLoaded = false;
    std::array<float, 3> widthTargetSing {}, widthTargetGap {};   // Suno width per band (for the WIDTH graph)
    bool hasWidthTarget = false;
    float targetLufs = -19.41f;        // Suno vocal integrated loudness (Suno Lead 01 = -19.4 LUFS)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SunoChainProcessor)
};
