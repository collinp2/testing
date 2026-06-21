#pragma once

// ============================================================================
//  PresetManager
//  A minimal on-disk preset browser for NECRONAM MAX. Presets are the full
//  plugin state (APVTS tree, including the namPath*/irPath* file references)
//  serialised to XML files under:
//      ~/Library/Application Support/CP Software/NECRONAM MAX/Presets   (macOS)
//      <userAppData>/CP Software/NECRONAM MAX/Presets                   (other)
//
//  Note: presets store the *paths* of the .nam models and .wav IRs, not their
//  contents — the referenced files must still exist on disk to recall a tone.
// ============================================================================

#include <juce_core/juce_core.h>

#include "PluginProcessor.h"

class PresetManager
{
public:
    static constexpr const char* kExt = "necronammax";

    explicit PresetManager (NecronamAudioProcessor& p) : processor (p)
    {
        auto dir = getPresetDir();
        if (! dir.isDirectory())
            dir.createDirectory();
        refresh();
    }

    static juce::File getPresetDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("CP Software")
                 .getChildFile ("NECRONAM MAX")
                 .getChildFile ("Presets");
    }

    void refresh()
    {
        presets.clearQuick();
        const auto pattern = juce::String ("*.") + kExt;
        for (const auto& f : getPresetDir().findChildFiles (juce::File::findFiles, false, pattern))
            presets.add (f.getFileNameWithoutExtension());
        presets.sort (true);
    }

    const juce::StringArray& getPresets() const { return presets; }
    juce::String getCurrentName() const         { return currentName; }

    bool save (const juce::String& name)
    {
        const auto clean = name.trim();
        if (clean.isEmpty())
            return false;

        auto file = getPresetDir().getChildFile (clean + "." + kExt);
        if (auto xml = processor.getStateTree().createXml())
        {
            if (file.replaceWithText (xml->toString()))
            {
                currentName = clean;
                refresh();
                return true;
            }
        }
        return false;
    }

    bool load (const juce::String& name)
    {
        auto file = getPresetDir().getChildFile (name + "." + kExt);
        if (! file.existsAsFile())
            return false;

        if (auto xml = juce::parseXML (file))
        {
            processor.setStateTree (juce::ValueTree::fromXml (*xml));
            currentName = name;
            return true;
        }
        return false;
    }

    bool deletePreset (const juce::String& name)
    {
        auto file = getPresetDir().getChildFile (name + "." + kExt);
        const bool ok = file.existsAsFile() && file.deleteFile();
        if (ok)
        {
            if (currentName == name)
                currentName = {};
            refresh();
        }
        return ok;
    }

    // dir = -1 previous, +1 next (wraps). Loads it and returns the new name.
    juce::String step (int dir)
    {
        if (presets.isEmpty())
            return {};

        int index = presets.indexOf (currentName);
        if (index < 0)
            index = (dir > 0 ? -1 : 0);   // first step lands on first / last
        index = (index + dir + presets.size()) % presets.size();

        load (presets[index]);
        return currentName;
    }

private:
    NecronamAudioProcessor& processor;
    juce::StringArray presets;
    juce::String currentName;
};
