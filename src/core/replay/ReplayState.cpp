// license:GPLv3+

#include "core/stdafx.h"

#include "ReplayState.h"

#include "physics/collideex.h"  // HitSpinner / HitGate / LineSegSlingshot definitions
#include "physics/hitplunger.h" // PlungerMoverObject

void BuildReplayStateRegistry(Player *player, std::vector<ReplayStateField> &fields)
{
   fields.clear();
   for (IEditable *const pedit : player->m_ptable->m_vedit)
   {
      switch (pedit->GetItemType())
      {
      case eItemLight:
         fields.push_back({ pedit, RSF_LIGHT_STATE, 0 });
         fields.push_back({ pedit, RSF_LIGHT_INTENSITY_SCALE, 0 });
         fields.push_back({ pedit, RSF_LIGHT_COLOR, 0 });
         fields.push_back({ pedit, RSF_LIGHT_COLOR2, 0 });
         fields.push_back({ pedit, RSF_LIGHT_INTENSITY, 0 });
         break;
      case eItemPrimitive:
         for (uint8_t i = 0; i < 9; i++)
            fields.push_back({ pedit, RSF_PRIMITIVE_ROTTRA, i });
         fields.push_back({ pedit, RSF_PRIMITIVE_VISIBLE, 0 });
         fields.push_back({ pedit, RSF_PRIMITIVE_DISABLE_LIGHTING_TOP, 0 });
         fields.push_back({ pedit, RSF_PRIMITIVE_DISABLE_LIGHTING_BELOW, 0 });
         for (uint8_t i = 0; i < 3; i++)
            fields.push_back({ pedit, RSF_PRIMITIVE_POSITION, i });
         break;
      case eItemFlasher:
         fields.push_back({ pedit, RSF_FLASHER_VISIBLE, 0 });
         fields.push_back({ pedit, RSF_FLASHER_INTENSITY_SCALE, 0 });
         fields.push_back({ pedit, RSF_FLASHER_ALPHA, 0 });
         fields.push_back({ pedit, RSF_FLASHER_COLOR, 0 });
         break;
      case eItemSpinner:
         fields.push_back({ pedit, RSF_SPINNER_ANGLE, 0 });
         break;
      case eItemGate:
         fields.push_back({ pedit, RSF_GATE_ANGLE, 0 });
         break;
      case eItemHitTarget:
         fields.push_back({ pedit, RSF_TARGET_DROPPED, 0 });
         break;
      case eItemPlunger:
         fields.push_back({ pedit, RSF_PLUNGER_POS, 0 });
         break;
      case eItemBumper:
         fields.push_back({ pedit, RSF_BUMPER_RING_OFFSET, 0 });
         break;
      case eItemSurface:
         for (uint8_t i = 0; i < REPLAY_SLING_SLOTS_PER_SURFACE; i++)
            fields.push_back({ pedit, RSF_SLING_FRAME, i });
         break;
      default:
         break;
      }
   }
   // Materials are table-global, not editables; scripts animate flasher-dome
   // brightness through UpdateMaterial opacity (Flupper) and tint inserts via
   // MaterialColor. The list order comes from the table file, so it is the
   // same at record and playback time.
   for (Material *const mat : player->m_ptable->GetMaterialList())
   {
      fields.push_back({ nullptr, RSF_MATERIAL_OPACITY, 0, mat });
      fields.push_back({ nullptr, RSF_MATERIAL_OPACITY_ACTIVE, 0, mat });
      fields.push_back({ nullptr, RSF_MATERIAL_BASE_COLOR, 0, mat });
   }
}

float GetReplayStateField(const ReplayStateField &f)
{
   switch (f.kind)
   {
   case RSF_LIGHT_STATE: return static_cast<Light *>(f.editable)->m_inPlayState;
   case RSF_LIGHT_INTENSITY_SCALE: return static_cast<Light *>(f.editable)->m_d.m_intensity_scale;
   case RSF_LIGHT_COLOR: return (float)(static_cast<Light *>(f.editable)->m_d.m_color & 0x00FFFFFF);
   case RSF_LIGHT_COLOR2: return (float)(static_cast<Light *>(f.editable)->m_d.m_color2 & 0x00FFFFFF);
   case RSF_LIGHT_INTENSITY: return static_cast<Light *>(f.editable)->m_d.m_intensity;
   case RSF_PRIMITIVE_ROTTRA: return static_cast<Primitive *>(f.editable)->m_d.m_aRotAndTra[f.sub];
   case RSF_PRIMITIVE_VISIBLE: return static_cast<Primitive *>(f.editable)->m_d.m_visible ? 1.f : 0.f;
   case RSF_PRIMITIVE_DISABLE_LIGHTING_TOP: return static_cast<Primitive *>(f.editable)->m_d.m_disableLightingTop;
   case RSF_PRIMITIVE_DISABLE_LIGHTING_BELOW: return static_cast<Primitive *>(f.editable)->m_d.m_disableLightingBelow;
   case RSF_PRIMITIVE_POSITION:
   {
      const Vertex3Ds &pos = static_cast<Primitive *>(f.editable)->m_d.m_vPosition;
      return f.sub == 0 ? pos.x : (f.sub == 1 ? pos.y : pos.z);
   }
   case RSF_FLASHER_VISIBLE: return static_cast<Flasher *>(f.editable)->m_d.m_isVisible ? 1.f : 0.f;
   case RSF_FLASHER_INTENSITY_SCALE: return static_cast<Flasher *>(f.editable)->m_d.m_intensity_scale;
   case RSF_FLASHER_ALPHA: return (float)static_cast<Flasher *>(f.editable)->m_d.m_alpha;
   case RSF_FLASHER_COLOR: return (float)(static_cast<Flasher *>(f.editable)->m_d.m_color & 0x00FFFFFF);
   case RSF_SPINNER_ANGLE:
   {
      const HitSpinner *const hit = static_cast<Spinner *>(f.editable)->GetHitSpinner();
      return hit ? hit->m_spinnerMover.m_angle : 0.f;
   }
   case RSF_GATE_ANGLE:
   {
      const HitGate *const hit = static_cast<Gate *>(f.editable)->GetHitGate();
      return hit ? hit->m_gateMover.m_angle : 0.f;
   }
   case RSF_TARGET_DROPPED: return static_cast<HitTarget *>(f.editable)->m_d.m_isDropped ? 1.f : 0.f;
   case RSF_MATERIAL_OPACITY: return f.material->m_fOpacity;
   case RSF_MATERIAL_OPACITY_ACTIVE: return f.material->m_bOpacityActive ? 1.f : 0.f;
   case RSF_MATERIAL_BASE_COLOR: return (float)(f.material->m_cBase & 0x00FFFFFF);
   case RSF_PLUNGER_POS:
   {
      const HitPlunger *const hit = static_cast<Plunger *>(f.editable)->GetHitPlunger();
      return hit ? hit->m_plungerMover.m_pos : 0.f;
   }
   case RSF_BUMPER_RING_OFFSET: return static_cast<Bumper *>(f.editable)->GetReplayRingOffset();
   case RSF_SLING_FRAME:
   {
      const vector<LineSegSlingshot *> &segs = static_cast<Surface *>(f.editable)->GetSlingshotSegs();
      return (f.sub < segs.size() && segs[f.sub]->m_iframe) ? 1.f : 0.f;
   }
   default: return 0.f;
   }
}

void SetReplayStateField(const ReplayStateField &f, float v)
{
   switch (f.kind)
   {
   case RSF_LIGHT_STATE: static_cast<Light *>(f.editable)->setInPlayState(v); break;
   case RSF_LIGHT_INTENSITY_SCALE: static_cast<Light *>(f.editable)->m_d.m_intensity_scale = v; break;
   case RSF_LIGHT_COLOR: static_cast<Light *>(f.editable)->m_d.m_color = (COLORREF)(uint32_t)v; break;
   case RSF_LIGHT_COLOR2: static_cast<Light *>(f.editable)->m_d.m_color2 = (COLORREF)(uint32_t)v; break;
   // put_Intensity also refreshes m_currentIntensity, matching the live script path
   case RSF_LIGHT_INTENSITY: static_cast<Light *>(f.editable)->put_Intensity(v); break;
   case RSF_PRIMITIVE_ROTTRA: static_cast<Primitive *>(f.editable)->m_d.m_aRotAndTra[f.sub] = v; break;
   case RSF_PRIMITIVE_VISIBLE: static_cast<Primitive *>(f.editable)->m_d.m_visible = (v != 0.f); break;
   case RSF_PRIMITIVE_DISABLE_LIGHTING_TOP: static_cast<Primitive *>(f.editable)->m_d.m_disableLightingTop = v; break;
   case RSF_PRIMITIVE_DISABLE_LIGHTING_BELOW: static_cast<Primitive *>(f.editable)->m_d.m_disableLightingBelow = v; break;
   case RSF_PRIMITIVE_POSITION:
   {
      Vertex3Ds &pos = static_cast<Primitive *>(f.editable)->m_d.m_vPosition;
      (f.sub == 0 ? pos.x : (f.sub == 1 ? pos.y : pos.z)) = v;
      break;
   }
   case RSF_FLASHER_VISIBLE: static_cast<Flasher *>(f.editable)->m_d.m_isVisible = (v != 0.f); break;
   case RSF_FLASHER_INTENSITY_SCALE: static_cast<Flasher *>(f.editable)->m_d.m_intensity_scale = v; break;
   case RSF_FLASHER_ALPHA: static_cast<Flasher *>(f.editable)->m_d.m_alpha = std::max((int)v, 0); break;
   case RSF_FLASHER_COLOR: static_cast<Flasher *>(f.editable)->m_d.m_color = (COLORREF)(uint32_t)v; break;
   case RSF_SPINNER_ANGLE:
   {
      HitSpinner *const hit = static_cast<Spinner *>(f.editable)->GetHitSpinner();
      if (hit)
         hit->m_spinnerMover.m_angle = v;
      break;
   }
   case RSF_GATE_ANGLE:
   {
      HitGate *const hit = static_cast<Gate *>(f.editable)->GetHitGate();
      if (hit)
         hit->m_gateMover.m_angle = v;
      break;
   }
   case RSF_TARGET_DROPPED: static_cast<HitTarget *>(f.editable)->m_d.m_isDropped = (v != 0.f); break;
   case RSF_MATERIAL_OPACITY: f.material->m_fOpacity = v; break;
   case RSF_MATERIAL_OPACITY_ACTIVE: f.material->m_bOpacityActive = (v != 0.f); break;
   case RSF_MATERIAL_BASE_COLOR: f.material->m_cBase = (COLORREF)(uint32_t)v; break;
   case RSF_PLUNGER_POS:
   {
      HitPlunger *const hit = static_cast<Plunger *>(f.editable)->GetHitPlunger();
      if (hit)
         hit->m_plungerMover.m_pos = v;
      break;
   }
   case RSF_BUMPER_RING_OFFSET: static_cast<Bumper *>(f.editable)->SetReplayRingOffset(v); break;
   case RSF_SLING_FRAME:
   {
      const vector<LineSegSlingshot *> &segs = static_cast<Surface *>(f.editable)->GetSlingshotSegs();
      if (f.sub < segs.size())
      {
         LineSegSlingshot *const seg = segs[f.sub];
         seg->m_iframe = (v != 0.f);
         // Pin the reset time so the frame survives Animate() until the
         // recorded release; Animate() runs every frame during playback too.
         seg->m_TimeReset = (v != 0.f) ? 0xFFFFFFFFu : 0u;
      }
      break;
   }
   default: break;
   }
}
