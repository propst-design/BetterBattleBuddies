// BBB #39: a gunner replacement is picked at once but seated later, through retries, and what
// changes hands in between must cancel it. Four trials with fresh actors (a USSR fire team, an
// M2 Humvee with a driver and two cargo passengers, empty turret): a control where a squad
// member ends up in the turret, then the pick revoked right after the request by the host
// (LeaveAlone) and by a player-level order on his squad (nobody is accepted, the turret stays
// empty, through the scanner's own retry 8 s on), and by his leaving the squad (he is never
// seated; the scanner may seat a squadmate instead). The pick is the cargo man the request
// starts ejecting; the seating callback gives his exit 400 ms and the scanner's retry finishes
// the job, as in seatdiag. -bbbSeatRevokeTest 1 (preset seatrevoke, ~3 min).
class BBB_SeatRevokeTest : BBB_AimTest
{
 static ref BBB_SeatRevokeTest s_Revoke;
 static ref BBB_AuditHooks s_Hooks;
 static ref BBB_Hooks s_SavedHooks;
 static const int TRIALS = 4;
 static const float OBSERVE_MS = 15000;
 static const float AFTER_ACCEPT_MS = 12000;
 static const float OBSERVE_MAX_MS = 30000;
 protected float m_fFirstAcceptedMs;
 static const ResourceName WP_DEFEND = "{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et";

 protected int m_iTrial;
 protected int m_iPassed;
 protected int m_iFailed;
 protected SCR_AIGroup m_Group;
 protected IEntity m_Truck;
 protected BaseCompartmentSlot m_Seat;
 protected ref array<ChimeraCharacter> m_Men = {};
 protected ChimeraCharacter m_Candidate;
 protected AIWaypoint m_Order;
 protected float m_fSetupAt;
 protected float m_fRequestAt;
 protected int m_iAcceptedBefore;
 protected bool m_bRequested;
 protected bool m_bBoarded;
 protected float m_fFirstBoardMs;

 static void MaybeStartRevoke()
 {
  if (s_Revoke || !Replication.IsServer() || !System.IsCLIParam("bbbSeatRevokeTest")) return;
  s_Revoke = new BBB_SeatRevokeTest();
  GetGame().GetCallqueue().CallLater(s_Revoke.BeginTrial, 12000);
 }

 protected string Kind()
 {
  if (m_iTrial == 1) return "leave-alone";
  if (m_iTrial == 2) return "player-order";
  if (m_iTrial == 3) return "left-squad";
  return "control";
 }

 protected void BeginTrial()
 {
  if (!s_Hooks)
  {
   s_SavedHooks = BBB.s_Hooks;
   s_Hooks = new BBB_AuditHooks();
   s_Hooks.m_Inner = s_SavedHooks;
   BBB.s_Hooks = s_Hooks;
  }
  m_bRequested = false;
  m_bBoarded = false;
  m_fFirstBoardMs = -1;
  m_fFirstAcceptedMs = -1;
  m_Candidate = null;
  vector position = BBB.Ground(Vector(2880, 0, 1580));
  m_Group = SCR_AIGroup.Cast(Spawn(TEAM_USSR, position, Vector(0, 0, 1)));
  m_Truck = Spawn(GUN_TRUCK, position + Vector(8, 0, 8), Vector(0, 0, 1));
  PrintFormat("[BBB] SEATREVOKE begin trial=%1 kind=%2 group=%3 truck=%4", m_iTrial, Kind(), m_Group, m_Truck);
  GetGame().GetCallqueue().CallLater(PrepareTrial, 3000);
 }

 protected void PrepareTrial()
 {
  m_Men.Clear();
  array<AIAgent> agents = {};
  if (m_Group) m_Group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   ChimeraCharacter man = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!man) continue;
   m_Men.Insert(man);
   man.GetDamageManager().EnableDamageHandling(false);
  }
  array<BaseCompartmentSlot> pilots = {};
  array<BaseCompartmentSlot> cargo = {};
  array<BaseCompartmentSlot> turrets = {};
  BBB_VehicleHelpers.GetVehicleCompartments(m_Truck, pilots, ECompartmentType.PILOT);
  BBB_VehicleHelpers.GetVehicleCompartments(m_Truck, cargo, ECompartmentType.CARGO);
  BBB_VehicleHelpers.GetVehicleCompartments(m_Truck, turrets, ECompartmentType.TURRET);
  if (!m_Group || !m_Truck || m_Men.Count() < 3 || pilots.IsEmpty() || cargo.Count() < 2 || turrets.IsEmpty())
  {
   Outcome(false, "setup: entities missing");
   return;
  }
  m_Seat = turrets[0];
  m_Group.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.HOLD_FIRE);
  m_Group.GetGroupUtilityComponent().EvaluateCombatMode();
  // Keep the one-second scanner from racing the explicit request.
  BBB_VehicleHelpers.s_mGunnerReplaceNext.Set(m_Truck, GetGame().GetWorld().GetWorldTime() + 120000);
  m_Men[0].GetCompartmentAccessComponent().GetInVehicle(pilots[0].GetOwner(), pilots[0], true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false);
  m_Men[1].GetCompartmentAccessComponent().GetInVehicle(cargo[0].GetOwner(), cargo[0], true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false);
  m_Men[2].GetCompartmentAccessComponent().GetInVehicle(cargo[1].GetOwner(), cargo[1], true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false);
  m_fSetupAt = GetGame().GetWorld().GetWorldTime();
  GetGame().GetCallqueue().CallLater(AwaitSetup, 100);
 }

 protected bool SeatedInTruck(ChimeraCharacter man)
 {
  CompartmentAccessComponent access = man.GetCompartmentAccessComponent();
  return man.IsInVehicle() && access && access.GetCompartment() && access.GetCompartment().GetVehicle() == m_Truck && !access.IsGettingIn() && !access.IsGettingOut();
 }

 protected void AwaitSetup()
 {
  float elapsed = GetGame().GetWorld().GetWorldTime() - m_fSetupAt;
  bool seated = SeatedInTruck(m_Men[0]) && SeatedInTruck(m_Men[1]) && SeatedInTruck(m_Men[2]);
  if (!seated && elapsed < 20000)
  {
   GetGame().GetCallqueue().CallLater(AwaitSetup, 100);
   return;
  }
  if (!seated)
  {
   Outcome(false, "setup: crew never seated");
   return;
  }
  GetGame().GetCallqueue().CallLater(Request, 500);
 }

 protected void Request()
 {
  m_iAcceptedBefore = BBB_VehicleHelpers.s_iGunnerReplacements;
  m_fRequestAt = GetGame().GetWorld().GetWorldTime();
  m_bRequested = BBB_VehicleHelpers.TryReplaceGunner(m_Truck, m_Group);
  // The pick is the cargo passenger the request started ejecting.
  foreach (ChimeraCharacter man : m_Men)
  {
   CompartmentAccessComponent access = man.GetCompartmentAccessComponent();
   if (access && access.IsGettingOut()) { m_Candidate = man; break; }
  }
  if (!m_Candidate) m_Candidate = m_Men[1];
  PrintFormat("[BBB] SEATREVOKE request trial=%1 kind=%2 requested=%3 candidate=%4", m_iTrial, Kind(), m_bRequested, m_Candidate);
  if (!m_bRequested)
  {
   Outcome(false, "replacement not requested");
   return;
  }
  // Revoke the pick before the seating callback runs (100 ms after the request).
  if (m_iTrial == 1) s_Hooks.m_LeaveAlone = m_Truck;
  if (m_iTrial == 2)
  {
   m_Order = AIWaypoint.Cast(Spawn(WP_DEFEND, m_Truck.GetOrigin() + Vector(0, 0, 6), Vector(0, 0, 1)));
   SCR_AIWaypoint scripted = SCR_AIWaypoint.Cast(m_Order);
   if (scripted) scripted.SetPriorityLevel(SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
   if (m_Order) m_Group.AddWaypoint(m_Order);
   else { Outcome(false, "no defend waypoint"); return; }
  }
  if (m_iTrial == 3)
  {
   AIControlComponent control = AIControlComponent.Cast(m_Candidate.FindComponent(AIControlComponent));
   if (control && control.GetControlAIAgent()) m_Group.RemoveAgent(control.GetControlAIAgent());
   else { Outcome(false, "no agent to detach"); return; }
  }
  GetGame().GetCallqueue().CallLater(Sample, 100);
 }

 protected void Sample()
 {
  float elapsed = GetGame().GetWorld().GetWorldTime() - m_fRequestAt;
  IEntity occupant = m_Seat.GetOccupant();
  if (BBB_VehicleHelpers.IsAble(occupant))
  {
   if (!m_bBoarded) m_fFirstBoardMs = elapsed;
   m_bBoarded = true;
  }
  int accepted = BBB_VehicleHelpers.s_iGunnerReplacements - m_iAcceptedBefore;
  if (m_fFirstAcceptedMs < 0 && accepted > 0) m_fFirstAcceptedMs = elapsed;
  // An accepted request needs its own window: the seat switch takes several seconds.
  float observeEnd = OBSERVE_MS;
  if (m_fFirstAcceptedMs >= 0) observeEnd = Math.Max(observeEnd, m_fFirstAcceptedMs + AFTER_ACCEPT_MS);
  if (elapsed < observeEnd && elapsed < OBSERVE_MAX_MS)
  {
   GetGame().GetCallqueue().CallLater(Sample, 100);
   return;
  }
  bool candidateInTurret = m_Candidate && m_Seat.GetOccupant() == m_Candidate;
  bool squadInTurret = m_Men.Contains(ChimeraCharacter.Cast(m_Seat.GetOccupant()));
  bool alive = m_Candidate && BBB.IsAlive(m_Candidate);
  PrintFormat("[BBB] SEATREVOKE observed trial=%1 kind=%2 boarded=%3 firstBoardMs=%4 firstAcceptedMs=%5 accepted=%6 candidateInTurret=%7 occupant=%8", m_iTrial, Kind(), m_bBoarded, m_fFirstBoardMs, m_fFirstAcceptedMs, accepted, candidateInTurret, m_Seat.GetOccupant());
  if (m_iTrial == 0) Outcome(m_bBoarded && accepted > 0 && squadInTurret, "control: a squad member boards the turret");
  else if (m_iTrial == 3) Outcome(!candidateInTurret && alive, "left squad: the detached pick is never seated");
  else Outcome(!m_bBoarded && accepted == 0 && !candidateInTurret && alive, "revoked: nobody accepted, turret empty, pick alive");
 }

 protected void Outcome(bool ok, string what)
 {
  string verdict = "FAIL";
  if (ok) { verdict = "PASS"; m_iPassed++; }
  else m_iFailed++;
  PrintFormat("[BBB] SEATREVOKE CHECK trial=%1 kind=%2 %3 (%4)", m_iTrial, Kind(), verdict, what);
  EndTrial();
 }

 protected void EndTrial()
 {
  if (s_Hooks) s_Hooks.m_LeaveAlone = null;
  if (m_Truck) BBB_VehicleHelpers.s_mGunnerReplaceNext.Remove(m_Truck);
  if (m_Order) SCR_EntityHelper.DeleteEntityAndChildren(m_Order);
  foreach (ChimeraCharacter man : m_Men) if (man) SCR_EntityHelper.DeleteEntityAndChildren(man);
  if (m_Group) SCR_EntityHelper.DeleteEntityAndChildren(m_Group);
  if (m_Truck) SCR_EntityHelper.DeleteEntityAndChildren(m_Truck);
  m_Men.Clear();
  m_Order = null;
  m_iTrial++;
  if (m_iTrial < TRIALS) { GetGame().GetCallqueue().CallLater(BeginTrial, 1000); return; }
  if (s_SavedHooks) BBB.s_Hooks = s_SavedHooks;
  s_Hooks = null;
  s_SavedHooks = null;
  string verdict = "FAIL";
  if (m_iFailed == 0 && m_iPassed == TRIALS) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbseatrevoke passed=%2 failed=%3 expected=%4", verdict, m_iPassed, m_iFailed, TRIALS);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_SeatRevokeTest.MaybeStartRevoke();
 }
}
