// license:GPLv3+

#include "core/stdafx.h"

#include "ReplayState.h"

#include "physics/collideex.h" // HitSpinner / HitGate definitions

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
         break;
      case eItemPrimitive:
         for (uint8_t i = 0; i < 9; i++)
            fields.push_back({ pedit, RSF_PRIMITIVE_ROTTRA, i });
         fields.push_back({ pedit, RSF_PRIMITIVE_VISIBLE, 0 });
         break;
      case eItemFlasher:
         fields.push_back({ pedit, RSF_FLASHER_VISIBLE, 0 });
         fields.push_back({ pedit, RSF_FLASHER_INTENSITY_SCALE, 0 });
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
      default:
         break;
      }
   }
}

float GetReplayStateField(const ReplayStateField &f)
{
   switch (f.kind)
   {
   case RSF_LIGHT_STATE: return static_cast<Light *>(f.editable)->m_inPlayState;
   case RSF_LIGHT_INTENSITY_SCALE: return static_cast<Light *>(f.editable)->m_d.m_intensity_scale;
   case RSF_PRIMITIVE_ROTTRA: return static_cast<Primitive *>(f.editable)->m_d.m_aRotAndTra[f.sub];
   case RSF_PRIMITIVE_VISIBLE: return static_cast<Primitive *>(f.editable)->m_d.m_visible ? 1.f : 0.f;
   case RSF_FLASHER_VISIBLE: return static_cast<Flasher *>(f.editable)->m_d.m_isVisible ? 1.f : 0.f;
   case RSF_FLASHER_INTENSITY_SCALE: return static_cast<Flasher *>(f.editable)->m_d.m_intensity_scale;
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
   default: return 0.f;
   }
}

void SetReplayStateField(const ReplayStateField &f, float v)
{
   switch (f.kind)
   {
   case RSF_LIGHT_STATE: static_cast<Light *>(f.editable)->setInPlayState(v); break;
   case RSF_LIGHT_INTENSITY_SCALE: static_cast<Light *>(f.editable)->m_d.m_intensity_scale = v; break;
   case RSF_PRIMITIVE_ROTTRA: static_cast<Primitive *>(f.editable)->m_d.m_aRotAndTra[f.sub] = v; break;
   case RSF_PRIMITIVE_VISIBLE: static_cast<Primitive *>(f.editable)->m_d.m_visible = (v != 0.f); break;
   case RSF_FLASHER_VISIBLE: static_cast<Flasher *>(f.editable)->m_d.m_isVisible = (v != 0.f); break;
   case RSF_FLASHER_INTENSITY_SCALE: static_cast<Flasher *>(f.editable)->m_d.m_intensity_scale = v; break;
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
   default: break;
   }
}
