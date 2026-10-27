// A/B for BBB_CrewStays (-bbbCrewStaysTest 1, Test-BBB.ps1 -Preset crewstays).
// Two LAV-25s, each crewed by driver, gunner and commander of one US team, a fourth man on
// foot beside it. Each lane gets the exact vanilla reaction sequence (behaviour, then
// WrapBehaviorOutsideOfVehicle, then AddAction) for a gate run (driver) and a heal (gunner,
// for the man on foot): lane A with the feature off, lane B with it on. 500 ms seat samples
// over 8 s show who actually left his seat. A real Arland gate (smart action "OpenGate") is
// used for the gate run; lane B must open it by script.
// Plough: a gunner-only LAV 55 m in front of the closed gate with a move order. Unhit, the
// gate must stay shut (outside the normal 25+25 m look); once the LAV counts as hit it must
// open within 3 s.
// Not covered: the heal activity re-picking an on-foot medic (no group heal activity here),
// the vanilla group tree sending the gate message by itself, and the gate swing under a
// moving vehicle.
// The host says the group's player-level order is its own (an AI commander's Fast move).
class BBB_CrewStaysTestHooks : BBB_Hooks
{
 override bool HostOrder(SCR_AIGroup group) { return true; }
}

class BBB_CrewStaysLane
{
 SCR_AIGroup m_Group;
 IEntity m_Lav;
 ref array<ChimeraCharacter> m_aCrew = {};
 ChimeraCharacter m_OnFoot;
 bool m_bDriverOut;
 bool m_bGunnerOut;
}

class BBB_CrewStaysTest : BBB_AimTest
{
 static ref BBB_CrewStaysTest s_CrewTest;
 static const ResourceName LAV = "{0FBF8F010F81A4E5}Prefabs/Vehicles/Wheeled/LAV25/LAV25.et";
 static const ResourceName WP_MOVE = "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et";
 static const int EXPECTED = 16;
 protected int m_iChecks;
 protected int m_iFailures;
 protected ref BBB_CrewStaysLane m_A = new BBB_CrewStaysLane();
 protected ref BBB_CrewStaysLane m_B = new BBB_CrewStaysLane();
 protected int m_iSamples;
 protected IEntity m_Gate;
 protected BaseDoorComponent m_GateDoor;
 protected IEntity m_GateDoorEntity;
 protected SCR_AISmartActionComponent m_GateAction;
 protected float m_fGateDistance = float.MAX;
 protected vector m_SearchFrom;
 protected SCR_AIGroup m_PloughGroup;
 protected IEntity m_PloughLav;
 protected bool m_bShutBefore;

 static void MaybeStartCrewStays()
 {
  if (s_CrewTest || !Replication.IsServer() || !System.IsCLIParam("bbbCrewStaysTest")) return;
  s_CrewTest = new BBB_CrewStaysTest();
  GetGame().GetCallqueue().CallLater(s_CrewTest.Begin, 12000);
 }

 override protected void Check(string name, bool ok, string detail)
 {
  m_iChecks++;
  string res = "PASS";
  if (!ok)
  {
   res = "FAIL";
   m_iFailures++;
  }
  PrintFormat("[BBB] CREWSTAYSTEST CHECK %1 %2 %3", name, res, detail);
 }

 protected bool CollectGate(IEntity entity)
 {
  SCR_AISmartActionComponent action = SCR_AISmartActionHandling.FindSmartAction(entity, "OpenGate", false);
  if (!action) return true;
  IEntity doorEntity = entity;
  BaseDoorComponent door = BaseDoorComponent.Cast(entity.FindComponent(BaseDoorComponent));
  if (!door && entity.GetParent())
  {
   doorEntity = entity.GetParent();
   door = BaseDoorComponent.Cast(doorEntity.FindComponent(BaseDoorComponent));
  }
  if (!door) return true;
  float distance = vector.DistanceXZ(entity.GetOrigin(), m_SearchFrom);
  if (distance >= m_fGateDistance) return true;
  m_fGateDistance = distance;
  m_Gate = entity;
  m_GateDoor = door;
  m_GateDoorEntity = doorEntity;
  m_GateAction = action;
  return true;
 }

 override protected void Begin()
 {
  m_SearchFrom = Vector(1340, 0, 2770);
  GetGame().GetWorld().QueryEntitiesBySphere(m_SearchFrom, 6000, CollectGate);
  string where = "none";
  if (m_Gate) where = string.Format("%1 at %2 (%3 m)", m_Gate.GetPrefabData().GetPrefabName(), m_Gate.GetOrigin(), Math.Round(m_fGateDistance));
  Check("gate-found", m_Gate != null, where);
  if (m_GateDoor) m_GateDoor.SetControlValue(0, RplId.Invalid());
  vector at = BBB.Ground(m_SearchFrom);
  SpawnLane(m_A, at);
  SpawnLane(m_B, at + Vector(25, 0, 0));
  GetGame().GetCallqueue().CallLater(SeatLanes, 4000);
 }

 protected void SpawnLane(BBB_CrewStaysLane lane, vector at)
 {
  lane.m_Group = SCR_AIGroup.Cast(Spawn(TEAM_US, at + Vector(0, 0, -10), Vector(0, 0, 1)));
  lane.m_Lav = Spawn(LAV, at, Vector(0, 0, 1));
 }

 protected bool SeatLane(BBB_CrewStaysLane lane)
 {
  if (!lane.m_Group || !lane.m_Lav) return false;
  array<BaseCompartmentSlot> pilots = {};
  BBB_VehicleHelpers.GetVehicleCompartments(lane.m_Lav, pilots, ECompartmentType.PILOT);
  array<BaseCompartmentSlot> turrets = {};
  BBB_VehicleHelpers.GetVehicleCompartments(lane.m_Lav, turrets, ECompartmentType.TURRET);
  array<BaseCompartmentSlot> cargo = {};
  BBB_VehicleHelpers.GetVehicleCompartments(lane.m_Lav, cargo, ECompartmentType.CARGO);
  BaseCompartmentSlot commander;
  foreach (BaseCompartmentSlot slot : cargo)
   if (BBB_Transport.CommanderSeat(slot)) { commander = slot; break; }
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(lane.m_Group, agents);
  if (pilots.IsEmpty() || turrets.IsEmpty() || !commander || agents.Count() < 4) return false;
  array<BaseCompartmentSlot> seats = {pilots[0], turrets[0], commander};
  bool accepted = true;
  for (int i = 0; i < 3; i++)
  {
   ChimeraCharacter man = ChimeraCharacter.Cast(agents[i].GetControlledEntity());
   if (!man) return false;
   lane.m_aCrew.Insert(man);
   if (!man.GetCompartmentAccessComponent().GetInVehicle(lane.m_Lav, seats[i], true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false)) accepted = false;
  }
  lane.m_OnFoot = ChimeraCharacter.Cast(agents[3].GetControlledEntity());
  SCR_AIGroupUtilityComponent utility = SCR_AIGroupUtilityComponent.Cast(lane.m_Group.FindComponent(SCR_AIGroupUtilityComponent));
  SCR_AIVehicleUsageComponent usage = SCR_AIVehicleUsageComponent.Cast(lane.m_Lav.FindComponent(SCR_AIVehicleUsageComponent));
  if (utility && usage) utility.AddUsableVehicle(usage);
  return accepted && lane.m_OnFoot != null;
 }

 protected void SeatLanes()
 {
  bool a = SeatLane(m_A);
  bool b = SeatLane(m_B);
  Check("seating-accepted", a && b, string.Format("A=%1 B=%2", a, b));
  if (!a || !b)
  {
   Finish();
   return;
  }
  GetGame().GetCallqueue().CallLater(Request, 5000);
 }

 static SCR_AIUtilityComponent UtilityOf(IEntity man)
 {
  AIControlComponent control = AIControlComponent.Cast(man.FindComponent(AIControlComponent));
  if (!control || !control.GetControlAIAgent()) return null;
  return SCR_AIUtilityComponent.Cast(control.GetControlAIAgent().FindComponent(SCR_AIUtilityComponent));
 }

 protected bool Seated(ChimeraCharacter man, IEntity vehicle)
 {
  return man && man.IsInVehicle() && BBB_Transport.VehicleOf(man) == vehicle;
 }

 // What SCR_AIGoalReaction_OpenNavlinkDoor and SCR_AIGoalReaction_Heal do with a message.
 protected void VanillaRequests(BBB_CrewStaysLane lane)
 {
  SCR_AIUtilityComponent driver = UtilityOf(lane.m_aCrew[0]);
  SCR_AIUtilityComponent gunner = UtilityOf(lane.m_aCrew[1]);
  SCR_AIPerformActionBehavior gate = new SCR_AIPerformActionBehavior(driver, null, m_GateAction, priority: SCR_AIActionBase.PRIORITY_BEHAVIOR_OPEN_NAVLINK_DOOR);
  driver.WrapBehaviorOutsideOfVehicle(gate);
  driver.AddAction(gate);
  SCR_AIMedicHealBehavior heal = new SCR_AIMedicHealBehavior(gunner, null, lane.m_OnFoot, false);
  gunner.WrapBehaviorOutsideOfVehicle(heal);
  gunner.AddAction(heal);
 }

 protected void Request()
 {
  bool aboard = true;
  array<ref BBB_CrewStaysLane> lanes = {m_A, m_B};
  foreach (BBB_CrewStaysLane lane : lanes)
   foreach (ChimeraCharacter man : lane.m_aCrew)
    if (!Seated(man, lane.m_Lav)) aboard = false;
  Check("aboard-before-request", aboard, "");

  BBB_CrewStays.s_bOn = false;
  VanillaRequests(m_A);
  BBB_CrewStays.s_bOn = true;
  bool shutBefore = !m_GateDoor || (!m_GateDoor.IsOpen() && !m_GateDoor.IsOpening());
  int gates = BBB_CrewStays.s_iGatesKept;
  int heals = BBB_CrewStays.s_iHealsKept;
  VanillaRequests(m_B);
  Check("on-gate-kept", BBB_CrewStays.s_iGatesKept == gates + 1, string.Format("%1 -> %2", gates, BBB_CrewStays.s_iGatesKept));
  Check("on-heal-kept", BBB_CrewStays.s_iHealsKept == heals + 1, string.Format("%1 -> %2", heals, BBB_CrewStays.s_iHealsKept));
  // The same armed-crew heal, ordered by a player, keeps vanilla's get-out.
  SCR_AIUtilityComponent ordered = UtilityOf(m_B.m_aCrew[1]);
  SCR_AIMedicHealBehavior playerHeal = new SCR_AIMedicHealBehavior(ordered, null, m_B.m_OnFoot, false, priorityLevel: SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
  int keptBefore = BBB_CrewStays.s_iHealsKept;
  bool playerKept = BBB_CrewStays.Keep(ordered, playerHeal);
  Check("on-player-heal-not-kept", !playerKept && BBB_CrewStays.s_iHealsKept == keptBefore, string.Format("kept=%1", playerKept));
  // At the player's level but the host's own order (ACA #57): kept like an ordinary one. A Game
  // Master's order never is.
  BBB_Hooks savedHooks = BBB.s_Hooks;
  BBB.s_Hooks = new BBB_CrewStaysTestHooks();
  SCR_AIMedicHealBehavior hostHeal = new SCR_AIMedicHealBehavior(ordered, null, m_B.m_OnFoot, false, priorityLevel: SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
  bool hostKept = BBB_CrewStays.Keep(ordered, hostHeal);
  SCR_AIMedicHealBehavior gmHeal = new SCR_AIMedicHealBehavior(ordered, null, m_B.m_OnFoot, false, priorityLevel: SCR_AIActionBase.PRIORITY_LEVEL_GAMEMASTER);
  bool gmKept = BBB_CrewStays.Keep(ordered, gmHeal);
  BBB.s_Hooks = savedHooks;
  Check("on-host-order-heal-kept", hostKept && BBB_CrewStays.s_iHealsKept == keptBefore + 1, string.Format("kept=%1", hostKept));
  Check("on-gm-heal-not-kept", !gmKept, string.Format("kept=%1", gmKept));
  m_bShutBefore = shutBefore;
  GetGame().GetCallqueue().CallLater(Sample, 500, true);
 }

 protected void Sample()
 {
  m_iSamples++;
  // 1 s after the request the gate must be swinging open; lane A's driver is still far from it.
  if (m_iSamples == 2) Check("on-gate-opened-by-script", m_GateDoor && m_bShutBefore && GateOpen(), string.Format("shutBefore=%1", m_bShutBefore));
  array<ref BBB_CrewStaysLane> lanes = {m_A, m_B};
  foreach (BBB_CrewStaysLane lane : lanes)
  {
   if (!Seated(lane.m_aCrew[0], lane.m_Lav)) lane.m_bDriverOut = true;
   if (!Seated(lane.m_aCrew[1], lane.m_Lav)) lane.m_bGunnerOut = true;
  }
  if (m_iSamples < 16) return;
  GetGame().GetCallqueue().Remove(Sample);
  // Physical seat occupancy over 8 s, not the counters above.
  Check("off-driver-out", m_A.m_bDriverOut, "");
  Check("off-gunner-out", m_A.m_bGunnerOut, "");
  Check("on-driver-stayed", !m_B.m_bDriverOut, "");
  Check("on-gunner-stayed", !m_B.m_bGunnerOut, "");
  bool still = true;
  foreach (ChimeraCharacter man : m_B.m_aCrew)
   if (!Seated(man, m_B.m_Lav) || !BBB.IsAlive(man)) still = false;
  Check("on-crew-seated-at-end", still, "");
  StartPlough();
 }

 protected void StartPlough()
 {
  if (!m_Gate)
  {
   Check("plough-unhit-gate-shut", false, "no gate");
   Check("plough-hit-gate-open", false, "no gate");
   Finish();
   return;
  }
  m_GateDoor.SetControlValue(0, RplId.Invalid());
  vector away = m_Gate.GetTransformAxis(2);
  away[1] = 0;
  away.Normalize();
  vector at = m_Gate.GetOrigin() + away * 55;
  m_PloughGroup = SCR_AIGroup.Cast(Spawn(TEAM_US, at + away * 10, away * -1));
  m_PloughLav = Spawn(LAV, at, away * -1);
  GetGame().GetCallqueue().CallLater(SeatPlough, 4000);
 }

 protected void SeatPlough()
 {
  array<BaseCompartmentSlot> turrets = {};
  BBB_VehicleHelpers.GetVehicleCompartments(m_PloughLav, turrets, ECompartmentType.TURRET);
  array<AIAgent> agents = {};
  if (m_PloughGroup) BBB.s_Hooks.GetMembers(m_PloughGroup, agents);
  if (!turrets.IsEmpty() && !agents.IsEmpty())
  {
   ChimeraCharacter gunner = ChimeraCharacter.Cast(agents[0].GetControlledEntity());
   if (gunner) gunner.GetCompartmentAccessComponent().GetInVehicle(m_PloughLav, turrets[0], true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false);
  }
  // An order through the gate (gate handling only runs for groups with somewhere to go).
  AIWaypoint order = AIWaypoint.Cast(Spawn(WP_MOVE, m_Gate.GetOrigin(), Vector(0, 0, 1)));
  if (order && m_PloughGroup) m_PloughGroup.AddWaypoint(order);
  GetGame().GetCallqueue().CallLater(PloughUnhit, 4000);
 }

 protected bool GateOpen()
 {
  return m_GateDoor && (m_GateDoor.IsOpen() || m_GateDoor.IsOpening());
 }

 protected void PloughUnhit()
 {
  float distance = vector.DistanceXZ(m_PloughLav.GetOrigin(), m_Gate.GetOrigin());
  Check("plough-unhit-gate-shut", !GateOpen() && BBB_Transport.CrewedVehicle(m_PloughGroup) == m_PloughLav, string.Format("%1 m, armoured=%2", Math.Round(distance), BBB_Armour.Armoured(m_PloughLav)));
  BBB_Transport.OnVehicleHit(m_PloughLav);
  GetGame().GetCallqueue().CallLater(PloughHit, 3000);
 }

 protected void PloughHit()
 {
  float distance = vector.DistanceXZ(m_PloughLav.GetOrigin(), m_Gate.GetOrigin());
  vector toGate = m_Gate.GetOrigin() - m_PloughLav.GetOrigin();
  toGate[1] = 0;
  toGate.Normalize();
  vector forward = m_PloughLav.GetTransformAxis(2);
  vector mins, maxs;
  m_Gate.GetBounds(mins, maxs);
  IEntity doorOwner = m_GateDoorEntity;
  vector dmins, dmaxs;
  doorOwner.GetBounds(dmins, dmaxs);
  Check("plough-hit-gate-open", GateOpen(), string.Format("%1 m, heading dot %2, gate width %3, door width %4, door=gate %5, hit %6", Math.Round(distance), vector.Dot(forward, toGate), Math.Max(maxs[0] - mins[0], maxs[2] - mins[2]), Math.Max(dmaxs[0] - dmins[0], dmaxs[2] - dmins[2]), doorOwner == m_Gate, BBB_Transport.VehicleHitRecently(m_PloughLav, 30000)));
  Finish();
 }

 override protected void Finish()
 {
  string verdict = "FAIL";
  if (m_iFailures == 0 && m_iChecks == EXPECTED) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbcrewstays passed=%2 failed=%3 expected=%4", verdict, m_iChecks - m_iFailures, m_iFailures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_CrewStaysTest.MaybeStartCrewStays();
 }
}
