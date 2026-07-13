// license:GPLv3+

#pragma once

#include <cstdint>
#include <vector>

class Player;
class IEditable;
class Material;

// One recordable script/physics-driven visual state field of a table part.
// The registry is built by walking the table's editables in order, which is
// deterministic for a given table file, so the recorder (at record time) and
// the replay player (at playback time) assign identical slot numbers.
//
// This channel is what reproduces script-driven visuals during playback:
// lights and GI (scripts drive them from the controller), flipper/toy
// primitives (modern tables hide the core flipper and rotate a primitive from
// script), flashers, and the physics-animated spinner/gate/drop-target state.
//
// v4 additions close the gaps found on Goonies VPW / AC-DC LUCI: modern
// tables render most insert and flasher-dome lighting through primitive
// BlendDisableLighting, flasher Opacity and material opacity (Flupper domes,
// nFozzy Lampz), none of which the v3 registry carried, and the plunger /
// bumper ring / slingshot visuals are physics-driven so they never move when
// playback runs without the physics engine.
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
   RSF_LIGHT_COLOR = 9,            // Light::m_d.m_color (24-bit RGB, exact in f32)
   RSF_LIGHT_COLOR2 = 10,          // Light::m_d.m_color2 (full/center color)
   RSF_LIGHT_INTENSITY = 11,       // Light::m_d.m_intensity (scripts set .Intensity directly)
   RSF_PRIMITIVE_DISABLE_LIGHTING_TOP = 12,   // Primitive::m_d.m_disableLightingTop (Lampz/VPW insert emission)
   RSF_PRIMITIVE_DISABLE_LIGHTING_BELOW = 13, // Primitive::m_d.m_disableLightingBelow
   RSF_PRIMITIVE_POSITION = 14,    // Primitive::m_d.m_vPosition[sub], sub = 0..2 (script-moved toys, ball shadows)
   RSF_FLASHER_ALPHA = 15,         // Flasher::m_d.m_alpha (.Opacity, Flupper dome fades)
   RSF_FLASHER_COLOR = 16,         // Flasher::m_d.m_color (24-bit RGB)
   RSF_MATERIAL_OPACITY = 17,      // Material::m_fOpacity (UpdateMaterial flash levels)
   RSF_MATERIAL_OPACITY_ACTIVE = 18, // Material::m_bOpacityActive
   RSF_MATERIAL_BASE_COLOR = 19,   // Material::m_cBase (MaterialColor tints, 24-bit RGB)
   RSF_PLUNGER_POS = 20,           // PlungerMoverObject::m_pos (rod visual is pure physics)
   RSF_BUMPER_RING_OFFSET = 21,    // BumperHitCircle::m_bumperanim_ringAnimOffset (hit-driven ring pop)
   RSF_SLING_FRAME = 22,           // LineSegSlingshot::m_iframe, sub = sling seg index
};

// Slingshot segments are physics hit objects that do not exist yet when the
// registry is built (recorder/player construct before PhysicSetup), so each
// surface gets a fixed number of sling slots and access bounds-checks against
// the live segment list.
constexpr uint8_t REPLAY_SLING_SLOTS_PER_SURFACE = 4;

struct ReplayStateField
{
   IEditable *editable;
   uint8_t kind; // ReplayStateFieldKind
   uint8_t sub;  // RotAndTra / position axis / sling seg index, else 0
   Material *material = nullptr; // RSF_MATERIAL_* only (materials are not editables)
};

void BuildReplayStateRegistry(Player *player, std::vector<ReplayStateField> &fields);
float GetReplayStateField(const ReplayStateField &f);
void SetReplayStateField(const ReplayStateField &f, float v);
