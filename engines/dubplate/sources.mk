# DUBPLATE arrangement core, ported JUCE-free from semierendonmez-commits/dubplate Source/Model
# (Roles, Arrangement, GenreTemplates, PatternGenerator: juce -> std, juce::Random -> Random.h
# with the same generator; samples / analysis / JSON state dropped) and Source/Audio/Timeline.h.
# Sequencer.* is new: the step logic of ArrangementEngine / EventFx without the audio.
# Not ported: sample store, analysis, lane renderer, master chain, event FX audio, recorder,
# exporter. Namespace dubplate.
ENGINE_SOURCES += engines/dubplate/Roles.cpp engines/dubplate/Arrangement.cpp engines/dubplate/GenreTemplates.cpp
ENGINE_SOURCES += engines/dubplate/PatternGenerator.cpp engines/dubplate/Sequencer.cpp
