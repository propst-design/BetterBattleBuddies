// Shared conservative guards for optional crew posture and driver alignment.
// Only the server mutates native compartment/vehicle controls.
class BBB_VehiclePosture
{
 static SCR_AIUtilityComponent Utility(IEntity body)
 {
  if (!body) return null;
  AIControlComponent control = AIControlComponent.Cast(body.FindComponent(AIControlComponent));
  if (!control || !control.GetAIAgent()) return null;
  return SCR_AIUtilityComponent.Cast(control.GetAIAgent().FindComponent(SCR_AIUtilityComponent));
 }

 static bool Controlled(ChimeraCharacter body)
 {
  if (!body || !BBB_VehicleHelpers.IsAble(body)) return false;
  CharacterControllerComponent controller = body.GetCharacterController();
  return controller && !controller.IsPlayerControlled() && !BBB.PlayerOf(body);
 }

 static bool Transition(CompartmentAccessComponent access)
 {
  return !access || access.IsGettingIn() || access.IsGettingOut() || access.IsSwitchingSeatsAnim();
 }

 static bool UnderFire(SCR_AIUtilityComponent utility)
 {
  if (!utility || !utility.m_ThreatSystem) return true;
  float suppression, shots, injury, endangered;
  utility.m_ThreatSystem.GetThreatValues(suppression, shots, injury, endangered);
  return suppression > 0.001 || shots > 0.001 || injury > 0.001 || endangered > 0.001;
 }

 // Generic danger-event counts also include benign vehicle/crew events.
 // Alignment needs actual incoming danger, not just a nonempty event queue.
 static bool IncomingFire(SCR_AIUtilityComponent utility)
 {
  if (UnderFire(utility) || !utility.m_SectorThreatFilter) return true;
  int major, minor;
  utility.m_SectorThreatFilter.GetActiveSectors(major, minor);
  array<int> sectors = {major, minor};
  foreach (int sector : sectors)
  {
   if (!BBB_Combat.ValidSector(sector)) continue;
   SCR_EAIThreatSectorFlags flags = utility.m_SectorThreatFilter.GetSectorFlags(sector);
   if ((flags & SCR_EAIThreatSectorFlags.DIRECTED_AT_ME) || (flags & SCR_EAIThreatSectorFlags.CAUSED_DAMAGE)) return true;
  }
  return false;
 }

 static bool KnownThreat(SCR_AIUtilityComponent utility, vector origin, float radius)
 {
  if (!utility || !utility.m_PerceptionComponent || !utility.m_CombatComponent) return true;
  BaseTarget current = utility.m_CombatComponent.GetCurrentTarget();
  if (current && vector.DistanceXZ(current.GetLastSeenPosition(), origin) < radius) return true;
  array<BaseTarget> targets = {};
  utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
  array<BaseTarget> detected = {};
  utility.m_PerceptionComponent.GetTargetsList(detected, ETargetCategory.DETECTED);
  foreach (BaseTarget unknown : detected) targets.Insert(unknown);
  foreach (BaseTarget target : targets)
  {
   if (!target) continue;
   // Use remembered positions; never inspect the unseen enemy's live transform.
   vector knownPosition = target.GetLastDetectedPosition();
   if (target.GetTargetCategory() == ETargetCategory.ENEMY) knownPosition = target.GetLastSeenPosition();
   if (vector.DistanceXZ(knownPosition, origin) < radius) return true;
  }
  return false;
 }

 static bool CrewAllows(IEntity vehicle, bool rejectOrders = true)
 {
  if (!vehicle || BBB.Health(vehicle) <= 0 || BBB_Armour.Aircraft(vehicle) || BBB.s_Hooks.LeaveAlone(vehicle)) return false;
  array<BaseCompartmentSlot> slots = {};
  BBB_VehicleHelpers.GetVehicleCompartments(vehicle, slots);
  bool crew = false;
  foreach (BaseCompartmentSlot slot : slots)
  {
   ChimeraCharacter body = ChimeraCharacter.Cast(slot.GetOccupant());
   if (!body) continue;
   if (!Controlled(body)) return false;
   SCR_AIUtilityComponent utility = Utility(body);
   if (!utility || (rejectOrders && BBB_OrderIntent.Blocks(utility))) return false;
   if (Transition(body.GetCompartmentAccessComponent())) return false;
   crew = true;
  }
  return crew;
 }

 static bool Armored(IEntity vehicle)
 {
  if (!vehicle) return false;
  PerceivableComponent perceivable = PerceivableComponent.Cast(vehicle.FindComponent(PerceivableComponent));
  if (!perceivable) return false;
  EAIUnitType unitType = perceivable.GetUnitType();
  return unitType == EAIUnitType.UnitType_VehicleMedium || unitType == EAIUnitType.UnitType_VehicleHeavy;
 }
}

class BBB_CrewTurnoutState : Managed
{
 IEntity m_Vehicle;
 ChimeraCharacter m_Body;
 BaseCompartmentSlot m_Seat;
 float m_fQuietSince;
 float m_fNextAction;
 bool m_bOpened;
}

class BBB_CrewTurnout
{
 static int s_iRead;
 static bool s_bOn;
 static const float QUIET_MS = 30000;
 static const float THREAT_RADIUS = 600;
 static ref array<ref BBB_CrewTurnoutState> s_States = {};
 static int s_iRequestsOut;
 static int s_iRequestsIn;

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbCrewTurnout", value) && value == "0");
 }

 static BBB_CrewTurnoutState State(ChimeraCharacter body, BaseCompartmentSlot seat)
 {
  foreach (BBB_CrewTurnoutState found : s_States)
   if (found.m_Body == body && found.m_Seat == seat) return found;
  if (s_States.Count() >= 128) return null;
  BBB_CrewTurnoutState created = new BBB_CrewTurnoutState();
  created.m_Body = body;
  created.m_Seat = seat;
  created.m_Vehicle = seat.GetVehicle();
  created.m_fQuietSince = GetGame().GetWorld().GetWorldTime();
  s_States.Insert(created);
  return created;
 }

 static bool Supported(BaseCompartmentSlot seat)
 {
  if (!seat || !seat.CanTurnOut() || seat.GetTurnOutDoorIndex() < 0) return false;
  // An open cargo seat or an external gun position is not an armored hatch.
  if (seat.GetType() != ECompartmentType.PILOT && seat.GetType() != ECompartmentType.TURRET) return false;
  BaseCompartmentManagerComponent manager = seat.GetManager();
  return manager && !manager.IsDoorFake(seat.GetTurnOutDoorIndex());
 }

 static bool Danger(SCR_AIGroup group, IEntity vehicle)
 {
  if (BBB_Transport.VehicleHitRecently(vehicle, QUIET_MS)) return true;
  array<BaseCompartmentSlot> compartments = {};
  BBB_VehicleHelpers.GetVehicleCompartments(vehicle, compartments);
  foreach (BaseCompartmentSlot compartment : compartments)
  {
   IEntity occupant = compartment.GetOccupant();
   if (!occupant) continue;
   SCR_AIUtilityComponent crewUtility = BBB_VehiclePosture.Utility(occupant);
   if (BBB_VehiclePosture.UnderFire(crewUtility) || BBB_VehiclePosture.KnownThreat(crewUtility, vehicle.GetOrigin(), THREAT_RADIUS)) return true;
  }
  array<AIAgent> members = {};
  BBB.s_Hooks.GetMembers(group, members);
  foreach (AIAgent member : members)
  {
   if (!member || !BBB.IsAlive(member.GetControlledEntity())) continue;
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(member.FindComponent(SCR_AIUtilityComponent));
   if (member.GetDangerEventsCount() > 0 || BBB_VehiclePosture.UnderFire(utility) || BBB_VehiclePosture.KnownThreat(utility, vehicle.GetOrigin(), THREAT_RADIUS)) return true;
  }
  return false;
 }

 // Native calls are requests. State assertions use IsOccupantTurnedOut later.
 static void Step(BBB_CrewTurnoutState state, bool quiet, float now)
 {
  if (!state || !state.m_Body || !state.m_Seat) return;
  if (!state.m_Vehicle || BBB.Health(state.m_Vehicle) <= 0) { state.m_bOpened = false; return; }
  ChimeraCharacter body = state.m_Body;
  BaseCompartmentSlot seat = state.m_Seat;
  CompartmentAccessComponent access = body.GetCompartmentAccessComponent();
  if (!BBB_VehiclePosture.Controlled(body) || !access || access.GetCompartment() != seat)
  { state.m_bOpened = false; state.m_fQuietSince = now; return; }
  if (BBB_VehiclePosture.Transition(access)) return;
  SCR_AIUtilityComponent utility = BBB_VehiclePosture.Utility(body);
  // An explicit order or a host claim on the vehicle ends BBB's turn-out, but only after BBB
  // undoes its own: the man it turned out goes back in and the hatch closes, then the hatch is
  // theirs. Letting go with him still out left him out under fire for good (BBB #7).
  bool release = !utility || BBB_OrderIntent.Blocks(utility) || BBB.s_Hooks.LeaveAlone(state.m_Vehicle);
  if (release) { state.m_fQuietSince = now; if (!state.m_bOpened) return; }
  if (!quiet) state.m_fQuietSince = now;
  bool wantOut = !release && s_bOn && quiet && now - state.m_fQuietSince >= QUIET_MS;
  if (now < state.m_fNextAction) return;
  BaseCompartmentManagerComponent manager = seat.GetManager();
  if (!manager) return;
  int doorIndex = seat.GetTurnOutDoorIndex();
  IEntity doorVehicle = manager.GetOwner();
  if (wantOut)
  {
   if (seat.IsOccupantTurnedOut()) return;
   // Ownership starts with opening the hatch; CloseVehicleDoors must not undo it.
   state.m_bOpened = true;
   if (!manager.IsDoorOpen(doorIndex))
   {
    if (!manager.GetDoorUser(doorIndex) && access.CanAccessDoor(doorVehicle, manager, doorIndex))
     access.OpenDoor(doorVehicle, ECharacterDoorAnimType.NO_CHARACTER_ANIM, doorIndex);
   }
   else if (access.TurnOut(true)) s_iRequestsOut++;
  }
  else if (state.m_bOpened)
  {
   if (seat.IsOccupantTurnedOut())
   {
    if (access.TurnOut(false)) s_iRequestsIn++;
   }
   else
   {
    if (manager.IsDoorOpen(doorIndex) && !manager.GetDoorUser(doorIndex) && access.CanAccessDoor(doorVehicle, manager, doorIndex))
     access.CloseDoor(doorVehicle, ECharacterDoorAnimType.NO_CHARACTER_ANIM, doorIndex);
    if (!manager.IsDoorOpen(doorIndex)) state.m_bOpened = false;
   }
  }
  // Short retry for danger, longer retry while opening; never interrupt native animations.
  state.m_fNextAction = now + 500;
 }

 static bool KeepsHatch(BaseCompartmentSlot seat)
 {
  foreach (BBB_CrewTurnoutState state : s_States)
   if (state.m_Seat == seat && state.m_bOpened) return true;
  return false;
 }

 static void Tick()
 {
  Read();
  if (!s_bOn && s_States.IsEmpty()) return;
  float now = GetGame().GetWorld().GetWorldTime();
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  array<BBB_CrewTurnoutState> visited = {};
  array<IEntity> handled = {};
  foreach (SCR_AIGroup group : groups)
  {
   if (!group) continue;
   IEntity vehicle = BBB_Transport.CrewedVehicle(group);
   if (!vehicle || handled.Contains(vehicle)) continue;
   handled.Insert(vehicle);
   bool quiet = BBB_VehiclePosture.Armored(vehicle) && !BBB.HasPlayer(group) && BBB_VehiclePosture.CrewAllows(vehicle) && !Danger(group, vehicle);
   array<BaseCompartmentSlot> seats = {};
   BBB_VehicleHelpers.GetVehicleCompartments(vehicle, seats);
   foreach (BaseCompartmentSlot seat : seats)
   {
    ChimeraCharacter body = ChimeraCharacter.Cast(seat.GetOccupant());
    if (!Supported(seat) || !BBB_VehiclePosture.Controlled(body)) continue;
    BBB_CrewTurnoutState state = State(body, seat);
    if (!state) continue;
    visited.Insert(state);
    Step(state, quiet, now);
   }
  }
  for (int i = s_States.Count() - 1; i >= 0; i--)
  {
   BBB_CrewTurnoutState old = s_States[i];
   if (visited.Contains(old)) continue;
   Step(old, false, now);
   if (!old.m_bOpened || !old.m_Body || !old.m_Seat || old.m_Seat.GetOccupant() != old.m_Body) s_States.RemoveOrdered(i);
  }
 }

 static void Stop()
 {
  bool wasOn = s_bOn;
  s_bOn = false;
  foreach (BBB_CrewTurnoutState state : s_States) Step(state, false, GetGame().GetWorld().GetWorldTime() + 1000);
  s_States.Clear();
  s_bOn = wasOn;
 }
}
