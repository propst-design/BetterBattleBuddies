// Crew stay mounted (21 Oct 2026, Jacob live on Kolguyev). Vanilla wraps some soldier tasks
// in "get out of the vehicle first" (SCR_AIUtilityComponent.WrapBehaviorOutsideOfVehicle):
// opening a gate on the route (OpenNavlinkDoor) and healing a buddy (Heal). A BTR-70 at
// Berezovka lost its driver to a gate run under fire, then its commander and gunner to heal
// orders that BBB's medic hold could not stop (the get-out is its own action, at heal
// priority + 100), and was written off with a working gun.
// What BBB changes, for AI crew (driver, gunner, commander seats) of a working vehicle:
//  - gate: no get-out. The gate is opened by script and the walk to it is dropped.
//  - heal: crew of an armed or armoured vehicle keep their seats. The heal activity is told
//    this medic failed, so vanilla picks someone on foot instead.
// Unarmed vehicle crews still get out to heal. Players and Game Master orders are left alone.
// -bbbCrewStays 0 restores vanilla.
class BBB_CrewStays
{
 static bool s_bOn = true;
 protected static int s_iRead;
 static int s_iGatesKept;
 static int s_iHealsKept;
 protected static ref map<IEntity, float> s_mLogged = new map<IEntity, float>();

 static bool On()
 {
  if (BBB_Settings.Due(s_iRead))
  {
   string val;
   s_bOn = !(BBB_Settings.Get("bbbCrewStays", val) && val == "0");
  }
  return s_bOn;
 }

 // The working vehicle this AI soldier crews (driver, gunner or commander seat), else null.
 static IEntity CrewedBy(IEntity soldier)
 {
  if (!soldier || BBB.PlayerOf(soldier)) return null;
  IEntity vehicle = BBB_Transport.VehicleOf(soldier);
  if (!vehicle || BBB_Transport.SeatOf(soldier) == ECompartmentType.CARGO) return null;
  if (BBB.Health(vehicle) <= 0 || BBB.s_Hooks.LeaveAlone(vehicle)) return null;
  return vehicle;
 }

 static bool Fighting(IEntity vehicle)
 {
  return BBB_VehicleHelpers.HasTurret(vehicle) || BBB_Armour.Armoured(vehicle);
 }

 // Opens the gate a smart action belongs to (the action may sit on the gate, its parent or a child).
 static bool OpenGate(IEntity entity)
 {
  if (!entity) return false;
  array<IEntity> candidates = {entity};
  if (entity.GetParent()) candidates.Insert(entity.GetParent());
  IEntity child = entity.GetChildren();
  while (child)
  {
   candidates.Insert(child);
   child = child.GetSibling();
  }
  bool opened = false;
  foreach (IEntity candidate : candidates)
  {
   BaseDoorComponent door = BaseDoorComponent.Cast(candidate.FindComponent(BaseDoorComponent));
   if (!door) continue;
   if (!door.IsOpen() && !door.IsOpening()) door.SetControlValue(1, RplId.Invalid());
   opened = true;
  }
  return opened;
 }

 // At most one log line per soldier every 10 s (the route keeps asking while the gate swings).
 protected static void Log(IEntity soldier, string text)
 {
  float now = GetGame().GetWorld().GetWorldTime();
  if (s_mLogged.Contains(soldier) && now - s_mLogged.Get(soldier) < 10000) return;
  s_mLogged.Set(soldier, now);
  PrintFormat("[BBB] CREWSTAYS %1", text);
 }

 // Called where vanilla would add its get-out. True = he keeps his seat (no get-out added).
 static bool Keep(SCR_AIUtilityComponent utility, SCR_AIActionBase action)
 {
  if (!On() || !utility || !action) return false;
  IEntity soldier = utility.m_OwnerEntity;
  // A player's or Game Master's order is left alone; a host's own order at that level is not.
  float level = action.EvaluatePriorityLevel();
  if (level >= SCR_AIActionBase.PRIORITY_LEVEL_GAMEMASTER) return false;
  if (level >= SCR_AIActionBase.PRIORITY_LEVEL_PLAYER && !BBB.s_Hooks.HostOrder(BBB_Medic.GroupOf(soldier))) return false;
  IEntity vehicle = CrewedBy(soldier);
  if (!vehicle) return false;
  string seat = typename.EnumToString(ECompartmentType, BBB_Transport.SeatOf(soldier));
  // Only the gate reaction wraps a perform-action behaviour.
  SCR_AIPerformActionBehavior gate = SCR_AIPerformActionBehavior.Cast(action);
  if (gate)
  {
   SCR_AISmartActionComponent smart = gate.m_SmartActionComponent.m_Value;
   if (smart) OpenGate(smart.GetOwner());
   gate.m_bBBBKept = true;
   s_iGatesKept++;
   BBB_Perf.Count("crewStays.gate");
   Log(soldier, string.Format("%1 %2 stays aboard %3: gate opened by script", BBB.s_Hooks.NameOf(BBB_Medic.GroupOf(soldier)), seat, BBB_Armour.Short(BBB_Armour.PrefabKey(vehicle))));
   BBB_Think.Note(soldier, "Vehicle", "gate ahead: opened it from the seat");
   return true;
  }
  SCR_AIMedicHealBehavior heal = SCR_AIMedicHealBehavior.Cast(action);
  if (heal && Fighting(vehicle))
  {
   heal.m_bBBBKept = true;
   s_iHealsKept++;
   BBB_Perf.Count("crewStays.heal");
   Log(soldier, string.Format("%1 %2 stays aboard %3: someone on foot heals %4", BBB.s_Hooks.NameOf(BBB_Medic.GroupOf(soldier)), seat, BBB_Armour.Short(BBB_Armour.PrefabKey(vehicle)), BBB_Medic.Name(heal.m_EntityToHeal.m_Value)));
   BBB_Think.Note(soldier, "Medic", "stays on the gun: someone on foot heals " + BBB_Medic.Name(heal.m_EntityToHeal.m_Value));
   return true;
  }
  return false;
 }
}

modded class SCR_AIUtilityComponent
{
 override void WrapBehaviorOutsideOfVehicle(SCR_AIActionBase action)
 {
  if (BBB_CrewStays.Keep(this, action)) return;
  super.WrapBehaviorOutsideOfVehicle(action);
 }
}

// A kept gate run is dropped before it starts: the gate is already opening.
modded class SCR_AIPerformActionBehavior
{
 bool m_bBBBKept;

 override float CustomEvaluate()
 {
  if (m_bBBBKept)
  {
   Fail();
   return 0;
  }
  return super.CustomEvaluate();
 }
}

// A kept heal fails at once and tells the group's heal activity not to pick this crewman again.
modded class SCR_AIMedicHealBehavior
{
 bool m_bBBBKept;

 override float CustomEvaluate()
 {
  if (!m_bBBBKept) return super.CustomEvaluate();
  SCR_AIHealActivity activity = SCR_AIHealActivity.Cast(GetRelatedGroupActivity());
  AIAgent agent;
  if (m_Utility) agent = AIAgent.Cast(m_Utility.GetOwner());
  if (activity && agent && !activity.m_aMedicsExclude.Contains(agent))
  {
   activity.m_aMedicsExclude.Insert(agent);
   activity.SetActionIsSuspended(false);
  }
  Fail();
  return 0;
 }
}
