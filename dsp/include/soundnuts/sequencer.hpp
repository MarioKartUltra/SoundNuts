// SoundNuts — sequencer.hpp
// Map -> MIDI step sequencer. One iterate per tempo-synced clock tick, value
// quantized to a scale/CC. The discrete-time counterpart to ModEngine (flows).
// Status: INTERFACE STUB.

#pragma once

#include <cstdint>
#include <vector>

#include "map_system.hpp"

namespace soundnuts {

enum class QuantTarget : std::uint8_t { Pitch, Velocity, CC };

struct Scale {
    std::vector<int> degrees{0, 2, 4, 5, 7, 9, 11};  // semitone offsets (major)
    int root = 60;                                    // MIDI note of degree 0
    int octaves = 2;                                  // span
};

struct Quantizer {
    QuantTarget target = QuantTarget::Pitch;
    Scale scale{};
    int ccNumber = 1;

    // REMINDER (see PLAN §): internal float mod is unquantized. bitDepth bites
    // ONLY on a MIDI-CC output path. 7 = standard CC, 14 = MSB/LSB CC or pitch-bend.
    int bitDepth = 7;

    // Normalized iterate value in [0,1] -> a MIDI datum (note / velocity / CC). STUB.
    [[nodiscard]] int quantize(double value) const;
};

struct MidiStep {
    int  note = -1;        // -1 if target != Pitch
    int  velocity = 100;
    int  cc = -1, ccVal = 0;
    bool gate = true;      // allow gating out steps (e.g. value below a threshold)
};

// One map iterate per tick -> one MidiStep. Clock is a tempo subdivision set by host.
class SequencerEngine {
public:
    SequencerEngine(const MapSystem& sys, Quantizer q)
        : sys_(sys), quant_(std::move(q)) {}

    void retrigger();                              // reset to sys.seedIC(). STUB
    void setQuantizer(const Quantizer& q);         // STUB

    // Advance one iteration at macro `bifurcation`, return the emitted step. STUB.
    MidiStep tick(double bifurcation);

    // Implied pattern length from the current regime (period-k window -> k;
    // chaos -> 0/aperiodic). Drives a "pattern length" UI readout. STUB.
    [[nodiscard]] int patternLength(double bifurcation) const;

private:
    const MapSystem& sys_;
    Quantizer quant_;
    State     state_{};
};

}  // namespace soundnuts
