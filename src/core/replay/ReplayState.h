// license:GPLv3+

#pragma once

#include <cstdint>
#include <vector>

class Player;
class IEditable;

// One recordable script/physics-driven visual state field of a table part.
// The registry is built by walking the table's editables in order, which is
// deterministic for a given table file, so the recorder (at record time) and
// the replay player (at playback time) assign identical slot numbers.
//
// This channel is what reproduces script-driven visuals during playback:
// lights and GI (scripts drive them from the controller), flipper/toy
// primitives (modern tables hide the core flipper and rotate a primitive from
// script), flashers, and the physics-animated spinner/gate/drop-target state.
enum ReplayStateFieldKind : uint8_t
{
   RSF_LIGHT_STATE = 0,            // Light::m_inPlayState (0..1 modulated, 2 blinking)
   RSF_LIGHT_INTENSITY_SCALE = 1,  // Light::m_d.m_intensity_scale (GI/PWM fading)
   RSF_PRIMITIVE_ROTTRA = 2,       // Primitive::m_d.m_aRotAndTra[sub], sub = 0..8
   RSF_PRIMITIVE_VISIBLE = 3,      // Primitive::m_d.m_visible
   RSF_FLASHER_VISIBLE = 4,        // Flasher::m_d.m_isVisible
   RSF_FLASHER_INTENSITY_SCALE = 5,// Flasher::m_d.m_intensity_scale
   RSF_SPINNER_ANGLE = 6,          // HitSpinner::m_spinnerMover.m_angle
   RSF_GATE_ANGLE = 7,             // HitGate::m_gateMover.m_angle
   RSF_TARGET_DROPPED = 8,         // HitTarget::m_d.m_isDropped
};

struct ReplayStateField
{
   IEditable *editable;
   uint8_t kind; // ReplayStateFieldKind
   uint8_t sub;  // RotAndTra index for RSF_PRIMITIVE_ROTTRA, else 0
};

void BuildReplayStateRegistry(Player *player, std::vector<ReplayStateField> &fields);
float GetReplayStateField(const ReplayStateField &f);
void SetReplayStateField(const ReplayStateField &f, float v);
