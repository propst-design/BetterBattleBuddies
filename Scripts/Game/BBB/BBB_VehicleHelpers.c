// Three helpers for AI vehicles, ticked once a second on the server (BBB.Tick).
// Gates: the game's AI driver stops, gets out and opens a closed gate on its
// route, which often wrecked rides (the transport counted as lost). AI-driven
// vehicles now open gates just ahead of them, without stopping (wide doors and any door the
// game marks "OpenGate"; armour that isn't safe looks further ahead, see BBB_CrewStays).
// Truck beds: soldiers getting out of cargo seats sometimes end up standing on
// the bed and never get down. One still up there after 4 s is set down on
// the ground beside the vehicle.
// Doors & hatches: AI crews close vehicle doors and hatches once back aboard.
class BBB_VehicleHelpers : Managed
{
 static const float LOOK_AHEAD = 25;    // m in front of the vehicle: the AI driver stops
 static const float LOOK_RADIUS = 25;   // for a gate up to ~45 m ahead
 static const float GATE_MIN_WIDTH = 2.2; // smaller doors are house doors: left alone
 static const float LANE_HALF_WIDTH = 4.5; // unmarked wide doors: only this far either side of the vehicle's line
 static const float ARMOUR_LOOK_AHEAD = 50;  // armour not safe: a gate up to ~80 m ahead
 static const float ARMOUR_LOOK_RADIUS = 30;
 static const float ARMOUR_ALERT_M = 400;
 static const float BED_HEIGHT = 0.9;    // m above the ground
 static const float STALL_SECONDS = 8.0;
 static const float STALL_SECONDS_FIGHTING = 25.0;
 static const float STALL_SECONDS_HELPLESS = 3.0;
 static const int UNSTICK_TRIES = 2;
 static const float GUNNER_REPLACE_COOLDOWN_MS = 8000.0;
 static const float GUNNER_FOOT_REACH_M = 8.0;

 protected ref array<IEntity> m_Found = {};
 protected ref map<IEntity, int> m_OnBed = new map<IEntity, int>();
 protected ref map<IEntity, vector> m_OnBedAt = new map<IEntity, vector>(); // where he was at the last sample
 static const float BED_MOVING_M = 0.6;  // m moved in a second: still getting down, not stuck
 protected ref map<IEntity, vector> m_StallPos = new map<IEntity, vector>();
 protected ref map<IEntity, float> m_StallSince = new map<IEntity, float>();
 protected ref map<IEntity, int> m_Unsticks = new map<IEntity, int>();
 static ref map<IEntity, float> s_mGunnerReplaceNext = new map<IEntity, float>();
 protected ref array<IEntity> m_Unsticking = {};
 protected ref BBB_VehicleHelpersUnstick m_UnstickListener;

 protected static bool s_bDoors = true;
 protected static bool s_bUnstick = true;
 protected static bool s_bTacticalDismount = true;
 static bool s_bGunnerReplace = true;
 // Counts accepted native boarding requests, not confirmed occupied seats.
 static int s_iGunnerReplacements;
 protected static bool s_bSeatTrace;
 protected static ref array<IEntity> s_aReplacingVehicles = {};

 void BBB_VehicleHelpers()
 {
  s_iGunnerReplacements = 0;
  s_bSeatTrace = System.IsCLIParam("bbbTurretTrace");
  string val;
  s_bDoors = !(BBB_Settings.Get("bbbDoors", val) && val == "0");
  s_bUnstick = !(BBB_Settings.Get("bbbUnstick", val) && val == "0");
  s_bTacticalDismount = !(BBB_Settings.Get("bbbTacticalDismount", val) && val == "0");
  s_bGunnerReplace = !(BBB_Settings.Get("bbbGunnerReplace", val) && val == "0");
 }

 // BBB.Stop: the per-vehicle gunner cooldowns hold world times, which start again next mission.
 static void Reset()
 {
  if (GetGame()) GetGame().GetCallqueue().Remove(DoSeatInTurret);
  s_mGunnerReplaceNext.Clear();
  s_aReplacingVehicles.Clear();
 }

 void Tick()
 {
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  array<IEntity> handledVehicles = {};
  array<IEntity> replacedVehicles = {};
  array<IEntity> watchedVehicles = {};
  // Per-part timings (Everon soak 23 Oct: the stage averaged 1.4-2.9 ms per tick on its own).
  int gates, beds, doors, gunners, driving;
  foreach (SCR_AIGroup group : groups)
  {
   if (!group) continue;
   int t = System.GetTickCount();
   OpenGatesFor(group);
   int now = System.GetTickCount();
   gates += now - t;
   t = now;
   GetOffBeds(group);
   now = System.GetTickCount();
   beds += now - t;
   t = now;
   CloseDoors(group, handledVehicles);
   now = System.GetTickCount();
   doors += now - t;
   t = now;
   ReplaceGunnerFor(group, replacedVehicles);
   now = System.GetTickCount();
   gunners += now - t;
   t = now;
   if (!BBB.s_Hooks.ManagesVehicleDriving(group))
   {
    CheckTacticalDismountFor(group);
    WatchDrivingFor(group, watchedVehicles);
   }
   driving += System.GetTickCount() - t;
  }
  BBB_Perf.Add("tick.vh.gates", gates);
  BBB_Perf.Add("tick.vh.beds", beds);
  BBB_Perf.Add("tick.vh.doors", doors);
  BBB_Perf.Add("tick.vh.gunners", gunners);
  BBB_Perf.Add("tick.vh.driving", driving);
 }

 // ---------------------------------------------------------------- gates

 protected bool CollectDoor(IEntity entity)
 {
  if (entity.FindComponent(BaseDoorComponent) && !m_Found.Contains(entity)) m_Found.Insert(entity);
  return true;
 }

 // The AI driver's reported navlink obstacle, or one of its child leaves.
 protected static bool IsNavlinkDoor(IEntity entity, IEntity navlink)
 {
  if (!navlink) return false;
  return entity == navlink || entity.GetParent() == navlink;
 }

 // The opening a door closes: its own leaf or, for a double gate, the thin door set it sits in
 // (Arland ammo-dump gate: 1.48 m leaves in a 2.97 m frame). A deeper parent is the building
 // itself, not the opening.
 static const float FRAME_MAX_DEPTH = 2.5;
 static float OpeningWidth(IEntity door)
 {
  vector mins, maxs;
  door.GetBounds(mins, maxs);
  float width = Math.Max(maxs[0] - mins[0], maxs[2] - mins[2]);
  IEntity frame = door.GetParent();
  if (!frame) return width;
  frame.GetBounds(mins, maxs);
  if (Math.Min(maxs[0] - mins[0], maxs[2] - mins[2]) >= FRAME_MAX_DEPTH) return width;
  return Math.Max(width, Math.Max(maxs[0] - mins[0], maxs[2] - mins[2]));
 }

 // Why a door was opened and what it is, for the GATE log line (only built when printing).
 protected static string Describe(IEntity entity, IEntity navlink)
 {
  string why = "in lane";
  if (IsNavlinkDoor(entity, navlink)) why = "navlink";
  why = string.Format("%1, %2 m opening", why, OpeningWidth(entity).ToString(3, 1));
  IEntity named = entity;
  while (named && (!named.GetPrefabData() || named.GetPrefabData().GetPrefabName().IsEmpty())) named = named.GetParent();
  string prefab = "?";
  if (named) prefab = FilePath.StripPath(named.GetPrefabData().GetPrefabName());
  return why + ", " + prefab;
 }

 // Whether a door found ahead of the vehicle is one to open for it: the obstacle the AI driver
 // reports, or a vehicle-wide opening (GATE_MIN_WIDTH) right in the vehicle's lane. The
 // look-ahead sphere alone swung every door in town open as convoys passed (Everon soaks 23-24
 // Oct: 1080 doors in 20 min, 2831 in 2 h; the "OpenGate" action it trusted is on house doors).
 static bool OpensFor(IEntity vehicle, IEntity entity, IEntity navlink, float reach)
 {
  if (IsNavlinkDoor(entity, navlink)) return true;
  if (OpeningWidth(entity) < GATE_MIN_WIDTH) return false;
  vector wmins, wmaxs;
  entity.GetWorldBounds(wmins, wmaxs);
  vector offset = (wmins + wmaxs) * 0.5 - vehicle.GetOrigin();
  float along = vector.Dot(offset, vehicle.GetTransformAxis(2));
  float across = Math.AbsFloat(vector.Dot(offset, vehicle.GetTransformAxis(0)));
  return along >= 0 && along <= reach && across <= LANE_HALF_WIDTH;
 }

 protected void OpenGatesFor(SCR_AIGroup group)
 {
  IEntity vehicle = BBB_Transport.CrewedVehicle(group);
  // The driver may already be out, walking to the gate: still open it, he gets back in.
  IEntity own = BBB.s_Hooks.OwnVehicleOf(group);
  IEntity anchor = BBB.s_Hooks.AnchorOf(group);
  if (!vehicle && own && BBB.Health(own) > 0 && anchor && vector.DistanceXZ(own.GetOrigin(), anchor.GetOrigin()) < 80) vehicle = own;
  if (!vehicle || BBB.s_Hooks.LeaveAlone(vehicle) || BBB.HasPlayer(group)) return;
  ChimeraCharacter driver = BBB.DriverOf(vehicle);
  if (driver && BBB.PlayerOf(driver)) return;
  array<AIWaypoint> waypoints = {};
  group.GetWaypoints(waypoints);
  if (waypoints.IsEmpty()) return;
  // Armour that isn't safe (hit, or enemies known within ARMOUR_ALERT_M) drives straight on:
  // gates up to ~80 m ahead are thrown open before it gets there, so it never slows for one.
  float lookAhead = LOOK_AHEAD;
  float lookRadius = LOOK_RADIUS;
  bool plough = BBB_CrewStays.On() && BBB_Armour.Armoured(vehicle) && (BBB_Transport.VehicleHitRecently(vehicle, 30000) || KnownEnemiesNear(group, vehicle.GetOrigin(), ARMOUR_ALERT_M) > 0);
  if (plough)
  {
   lookAhead = ARMOUR_LOOK_AHEAD;
   lookRadius = ARMOUR_LOOK_RADIUS;
  }
  vector ahead = vehicle.GetOrigin() + vehicle.GetTransformAxis(2) * lookAhead;
  m_Found.Clear();
  GetGame().GetWorld().QueryEntitiesBySphere(ahead, lookRadius, CollectDoor);
  // The obstacle the game's AI driver is stopping for, if it says.
  AICarMovementComponent movement = AICarMovementComponent.Cast(vehicle.FindComponent(AICarMovementComponent));
  IEntity navlink;
  if (movement) navlink = movement.GetLastNavlinkEntity();
  if (navlink && vector.Distance(navlink.GetOrigin(), vehicle.GetOrigin()) < 80)
  {
   CollectDoor(navlink);
   IEntity child = navlink.GetChildren();
   while (child)
   {
    CollectDoor(child);
    child = child.GetSibling();
   }
  }
  foreach (IEntity entity : m_Found)
  {
   BaseDoorComponent door = BaseDoorComponent.Cast(entity.FindComponent(BaseDoorComponent));
   if (!door || door.IsOpen() || door.IsOpening()) continue;
   if (!OpensFor(vehicle, entity, navlink, lookAhead + lookRadius)) continue;
   door.SetControlValue(1, RplId.Invalid());
   if (plough) BBB_Perf.Count("crewStays.armourGate");
   if (plough) PrintFormat("[ACR] GATE opened ahead of %1 at %2 (armour, not safe: drives through; %3)", BBB.s_Hooks.NameOf(group), entity.GetOrigin(), Describe(entity, navlink));
   else PrintFormat("[ACR] GATE opened ahead of %1 at %2 (%3)", BBB.s_Hooks.NameOf(group), entity.GetOrigin(), Describe(entity, navlink));
  }
 }

 // ---------------------------------------------------------------- truck beds

 // Inside the vehicle's outline and no higher than standing on it, in the vehicle's own frame:
 // its world box also covers men standing beside a vehicle parked at an angle, and men on a
 // balcony above one (Everon soak 23 Oct: 13 teleports in 5 drop-offs).
 static bool OnTopOf(IEntity vehicle, vector at)
 {
  vector mins, maxs;
  vehicle.GetBounds(mins, maxs);
  vector local = vehicle.CoordToLocal(at);
  if (local[0] < mins[0] || local[0] > maxs[0] || local[2] < mins[2] || local[2] > maxs[2]) return false;
  return local[1] >= mins[1] && local[1] <= maxs[1] + 0.5;
 }

 protected IEntity m_BedVehicle;
 protected vector m_BedSoldier;
 protected bool CollectVehicleUnder(IEntity entity)
 {
  if (!Vehicle.Cast(entity) || !OnTopOf(entity, m_BedSoldier)) return true;
  m_BedVehicle = entity;
  return false;
 }

 protected void ForgetBed(IEntity soldier)
 {
  m_OnBed.Remove(soldier);
  m_OnBedAt.Remove(soldier);
 }

 protected void GetOffBeds(SCR_AIGroup group)
 {
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(group, agents);
  foreach (AIAgent agent : agents)
  {
   ChimeraCharacter soldier = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!soldier || !BBB.IsAlive(soldier) || BBB_Transport.VehicleOf(soldier) || BBB.PlayerOf(soldier)) { ForgetBed(soldier); continue; }
   vector at = soldier.GetOrigin();
   if (at[1] - GetGame().GetWorld().GetSurfaceY(at[0], at[2]) < BED_HEIGHT) { ForgetBed(soldier); continue; }
   m_BedVehicle = null;
   m_BedSoldier = at;
   GetGame().GetWorld().QueryEntitiesBySphere(at, 6, CollectVehicleUnder);
   if (!m_BedVehicle) { ForgetBed(soldier); continue; }
   // Climbing down takes him somewhere: only a man who stays put up there is stuck.
   vector last;
   if (m_OnBedAt.Find(soldier, last) && vector.DistanceXZ(last, at) > BED_MOVING_M) m_OnBed.Remove(soldier);
   m_OnBedAt.Set(soldier, at);
   int seen = m_OnBed.Get(soldier) + 1;
   m_OnBed.Set(soldier, seen);
   if (seen < 4) continue; // getting out takes a moment: only those still up there after 4 s
   ForgetBed(soldier);
   // Down beside the vehicle, on the side he is nearer to.
   vector side = m_BedVehicle.GetTransformAxis(0);
   if (vector.Dot(at - m_BedVehicle.GetOrigin(), side) < 0) side = side * -1;
   vector mins, maxs;
   m_BedVehicle.GetBounds(mins, maxs);
   vector ground = m_BedVehicle.GetOrigin() + side * (Math.Max(maxs[0], -mins[0]) + 1.2);
   ground[1] = GetGame().GetWorld().GetSurfaceY(ground[0], ground[2]);
   vector transform[4];
   soldier.GetWorldTransform(transform);
   transform[3] = ground;
   soldier.Teleport(transform);
   PrintFormat("[ACR] %1: soldier stuck on a vehicle set down beside it", BBB.s_Hooks.NameOf(group));
  }
 }

 // ---------------------------------------------------------------- vehicle doors & hatches

 protected void CloseDoors(SCR_AIGroup group, notnull array<IEntity> handledVehicles)
 {
  if (!s_bDoors || !group || BBB.HasPlayer(group)) return;
  IEntity vehicle = BBB_Transport.CrewedVehicle(group);
  if (!vehicle)
  {
   array<AIAgent> agents = {};
   BBB.s_Hooks.GetMembers(group, agents);
   foreach (AIAgent agent : agents)
   {
    IEntity body = agent.GetControlledEntity();
    vehicle = BBB_Transport.VehicleOf(body);
    if (vehicle) break;
   }
  }
  if (!vehicle || handledVehicles.Contains(vehicle) || BBB.s_Hooks.LeaveAlone(vehicle)) return;
  handledVehicles.Insert(vehicle);
  CloseVehicleDoors(vehicle);
 }

 static int CloseVehicleDoors(IEntity vehicle)
 {
  if (!s_bDoors || !vehicle) return 0;
  BaseCompartmentManagerComponent manager = BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(BaseCompartmentManagerComponent));
  if (!manager) return 0;

  array<BaseCompartmentSlot> compartments = {};
  manager.GetCompartments(compartments);
  if (compartments.IsEmpty()) return 0;

  CompartmentAccessComponent actorAccess;
  foreach (BaseCompartmentSlot comp : compartments)
  {
   if (comp.IsGetInLocked()) continue;
   IEntity occ = comp.GetOccupant();
   ChimeraCharacter ch = ChimeraCharacter.Cast(occ);
   if (ch)
   {
    CompartmentAccessComponent acc = ch.GetCompartmentAccessComponent();
    if (acc)
    {
     if (acc.IsGettingIn() || acc.IsGettingOut()) continue;
     if (!actorAccess && !BBB.PlayerOf(ch) && BBB.IsAlive(ch)) actorAccess = acc;
    }
   }
  }
  if (!actorAccess) return 0;

  int closed = 0;
  array<int> checkedDoors = {};
  foreach (BaseCompartmentSlot comp : compartments)
  {
   if (comp.IsGetInLocked()) continue;
   IEntity occ = comp.GetOccupant();
   ChimeraCharacter ch = ChimeraCharacter.Cast(occ);
   if (ch && BBB.PlayerOf(ch)) continue;

   CompartmentAccessComponent access = actorAccess;
   if (ch && BBB.IsAlive(ch))
   {
    CompartmentAccessComponent occAcc = ch.GetCompartmentAccessComponent();
    if (occAcc)
    {
     if (occAcc.IsGettingIn() || occAcc.IsGettingOut()) continue;
     access = occAcc;
    }
   }

   int turnOut = comp.GetTurnOutDoorIndex();
   if (turnOut >= 0 && BBB_CrewTurnout.KeepsHatch(comp) && !checkedDoors.Contains(turnOut)) checkedDoors.Insert(turnOut);
   if (turnOut >= 0 && !checkedDoors.Contains(turnOut))
   {
    checkedDoors.Insert(turnOut);
    if (!comp.IsOccupantTurnedOut() && manager.IsDoorOpen(turnOut) && !manager.IsDoorFake(turnOut) && !manager.GetDoorUser(turnOut))
    {
     bool didClose = false;
     if (access.CanAccessDoor(vehicle, manager, turnOut))
      didClose = access.CloseDoor(vehicle, ECharacterDoorAnimType.NO_CHARACTER_ANIM, turnOut);
     if (!didClose && access != actorAccess && actorAccess.CanAccessDoor(vehicle, manager, turnOut))
      didClose = actorAccess.CloseDoor(vehicle, ECharacterDoorAnimType.NO_CHARACTER_ANIM, turnOut);
     if (didClose) closed++;
    }
   }

   array<int> doorIndices = {};
   comp.GetAvailableDoorIndices(doorIndices);
   foreach (int doorIndex : doorIndices)
   {
    if (doorIndex < 0 || checkedDoors.Contains(doorIndex)) continue;
    checkedDoors.Insert(doorIndex);
    if (manager.IsDoorOpen(doorIndex) && !manager.IsDoorFake(doorIndex) && !manager.GetDoorUser(doorIndex))
    {
     bool didCloseDoor = false;
     if (access.CanAccessDoor(vehicle, manager, doorIndex))
      didCloseDoor = access.CloseDoor(vehicle, ECharacterDoorAnimType.NO_CHARACTER_ANIM, doorIndex);
     if (!didCloseDoor && access != actorAccess && actorAccess.CanAccessDoor(vehicle, manager, doorIndex))
      didCloseDoor = actorAccess.CloseDoor(vehicle, ECharacterDoorAnimType.NO_CHARACTER_ANIM, doorIndex);
     if (didCloseDoor) closed++;
    }
   }
  }
  return closed;
 }

 // ---------------------------------------------------------------- standalone tactical dismount & unstick

 static int KnownEnemiesNear(SCR_AIGroup group, vector origin, float radius)
 {
  if (!group) return 0;
  SCR_AIGroupUtilityComponent utility = SCR_AIGroupUtilityComponent.Cast(group.FindComponent(SCR_AIGroupUtilityComponent));
  if (!utility || !utility.m_Perception) return 0;
  int count = 0;
  foreach (IEntity enemy : utility.m_Perception.m_aTargetEntities)
  {
   if (!BBB.IsAlive(enemy)) continue;
   if (vector.DistanceXZ(enemy.GetOrigin(), origin) <= radius) count++;
  }
  return count;
 }

 static void GetVehicleCompartments(IEntity vehicle, array<BaseCompartmentSlot> slots, int filterType = -1)
 {
  slots.Clear();
  if (!vehicle) return;
  array<IEntity> entities = {vehicle};
  IEntity child = vehicle.GetChildren();
  while (child)
  {
   entities.Insert(child);
   child = child.GetSibling();
  }
  foreach (IEntity ent : entities)
  {
   SCR_BaseCompartmentManagerComponent cm = SCR_BaseCompartmentManagerComponent.Cast(ent.FindComponent(SCR_BaseCompartmentManagerComponent));
   if (!cm) continue;
   array<BaseCompartmentSlot> cur = {};
   cm.GetCompartments(cur);
   foreach (BaseCompartmentSlot s : cur)
   {
    if (filterType < 0 || s.GetType() == filterType)
     slots.Insert(s);
   }
  }
 }

 static bool HasTurret(IEntity vehicle)
 {
  if (!vehicle) return false;
  array<BaseCompartmentSlot> turrets = {};
  GetVehicleCompartments(vehicle, turrets, ECompartmentType.TURRET);
  return !turrets.IsEmpty();
 }

 static bool IsAble(IEntity entity)
 {
  if (!BBB.IsAlive(entity)) return false;
  ChimeraCharacter ch = ChimeraCharacter.Cast(entity);
  if (!ch || !ch.GetCharacterController()) return false;
  if (ch.GetCharacterController().IsUnconscious()) return false;
  if (BBB_Drag.Unconscious(entity)) return false;
  return true;
 }

 static bool HasGunner(IEntity vehicle)
 {
  if (!vehicle) return false;
  array<BaseCompartmentSlot> turrets = {};
  GetVehicleCompartments(vehicle, turrets, ECompartmentType.TURRET);
  foreach (BaseCompartmentSlot slot : turrets)
   if (IsAble(slot.GetOccupant())) return true;
  return false;
 }

 // ---------------------------------------------------------------- turret gunner replacement

 static TurretControllerComponent TurretControllerOf(BaseCompartmentSlot slot)
 {
  if (!slot) return null;
  TurretControllerComponent controller = TurretControllerComponent.Cast(slot.GetController());
  if (controller) return controller;
  controller = slot.GetAttachedTurret();
  if (controller) return controller;
  IEntity owner = slot.GetOwner();
  if (!owner) return null;
  controller = TurretControllerComponent.Cast(owner.FindComponent(TurretControllerComponent));
  if (controller) return controller;
  string turretName = slot.GetAutoConnectTurret();
  IEntity child = owner.GetChildren();
  while (child)
  {
   TurretControllerComponent tc = TurretControllerComponent.Cast(child.FindComponent(TurretControllerComponent));
   if (tc && (turretName.IsEmpty() || tc.GetUniqueName() == turretName))
    return tc;
   child = child.GetSibling();
  }
  return null;
 }

 static bool TurretHasAmmo(BaseCompartmentSlot slot)
 {
  if (!slot || slot.GetType() != ECompartmentType.TURRET) return false;
  if (slot.IsGetInLocked()) return false;
  IEntity owner = slot.GetOwner();
  if (owner && BBB.Health(owner) <= 0) return false;
  TurretControllerComponent controller = TurretControllerOf(slot);
  if (!controller) return false;
  IEntity turretEnt = controller.GetOwner();
  if (turretEnt && BBB.Health(turretEnt) <= 0) return false;
  BaseWeaponManagerComponent wm = controller.GetWeaponManager();
  if (!wm) return false;
  BaseWeaponComponent weapon = wm.GetCurrentWeapon();
  if (!weapon)
  {
   array<IEntity> weapons = {};
   wm.GetWeaponsList(weapons);
   foreach (IEntity wEnt : weapons)
   {
    weapon = BaseWeaponComponent.Cast(wEnt.FindComponent(BaseWeaponComponent));
    if (weapon) break;
   }
  }
  if (!weapon) return false;
  if (weapon.GetCurrentMagazine() && weapon.GetCurrentMagazine().GetAmmoCount() > 0) return true;
  if (weapon.GetCurrentMuzzle() && weapon.GetCurrentMuzzle().GetAmmoCount() > 0) return true;
  if (turretEnt)
  {
   InventoryStorageManagerComponent inv = InventoryStorageManagerComponent.Cast(turretEnt.FindComponent(InventoryStorageManagerComponent));
   if (inv && inv.GetMagazineCountByWeapon(weapon) > 0) return true;
  }
  return false;
 }

 // Opt-in transition evidence. Request acceptance is not confirmed occupancy.
 static void SeatTrace(string stage, IEntity vehicle, BaseCompartmentSlot slot, ChimeraCharacter candidate, int retry = 0, bool accepted = false)
 {
  if (!s_bSeatTrace) return;
  IEntity occupant = null;
  if (slot) occupant = slot.GetOccupant();
  if (candidate && candidate.GetCompartmentAccessComponent())
  {
   CompartmentAccessComponent access = candidate.GetCompartmentAccessComponent();
   PrintFormat("[BBB] SEATTRACE actor inVehicle=%1 entering=%2 exiting=%3 compartment=%4", candidate.IsInVehicle(), access.IsGettingIn(), access.IsGettingOut(), access.GetCompartment());
  }
  if (slot) PrintFormat("[BBB] SEATTRACE slot name=%1 owner=%2 vehicle=%3", slot.GetCompartmentName(), slot.GetOwner(), slot.GetVehicle());
  PrintFormat("[BBB] SEATTRACE t=%1 stage=%2 vehicle=%3 candidate=%4 occupant=%5 retry=%6 accepted=%7", GetGame().GetWorld().GetWorldTime(), stage, vehicle, candidate, occupant, retry, accepted);
 }

 protected static bool EjectFromCompartment(IEntity vehicle, ChimeraCharacter ch)
 {
  if (!vehicle || !ch) return false;
  CompartmentAccessComponent acc = ch.GetCompartmentAccessComponent();
  if (!acc) return false;
  BaseCompartmentSlot curSlot = acc.GetCompartment();
  if (!curSlot && !ch.IsInVehicle()) return true;
  SeatTrace("eject-before", vehicle, curSlot, ch);
  bool ok = acc.GetOutVehicle(EGetOutType.TELEPORT, -1, ECloseDoorAfterActions.LEAVE_OPEN, false);
  if (!ok)
  {
   vector ejectMat[4];
   ch.GetWorldTransform(ejectMat);
   vector side = vehicle.GetTransformAxis(0);
   ejectMat[3] = BBB.Ground(vehicle.GetOrigin() + side * 2.5);
   acc.GetOutVehicle_NoDoor(ejectMat, false, false);
  }
  SeatTrace("eject-request", vehicle, curSlot, ch, 0, ok);
  return true;
 }

 protected static bool EjectIncapacitatedOccupant(IEntity vehicle, BaseCompartmentSlot slot)
 {
  if (!vehicle || !slot) return false;
  IEntity occ = slot.GetOccupant();
  if (!occ) return true;
  if (BBB.PlayerOf(occ) || IsAble(occ)) return false;
  ChimeraCharacter ch = ChimeraCharacter.Cast(occ);
  if (!ch) return false;
  return EjectFromCompartment(vehicle, ch);
 }

 static void DoSeatInTurret(IEntity vehicle, BaseCompartmentSlot slot, ChimeraCharacter candidate, SCR_AIGroup group, int retries = 0)
 {
  SeatTrace("seat-attempt", vehicle, slot, candidate, retries);
  if (!vehicle || !slot || !candidate || !IsAble(candidate))
  {
   s_aReplacingVehicles.RemoveItem(vehicle);
   return;
  }
  CompartmentAccessComponent acc = candidate.GetCompartmentAccessComponent();
  if (!acc)
  {
   s_aReplacingVehicles.RemoveItem(vehicle);
   return;
  }
  if (candidate.IsInVehicle() || acc.GetCompartment() != null || acc.IsGettingOut() || acc.IsGettingIn())
  {
   if (retries < 10)
   {
    GetGame().GetCallqueue().CallLater(DoSeatInTurret, 40, false, vehicle, slot, candidate, group, retries + 1);
    return;
   }
   SeatTrace("abandon-transition", vehicle, slot, candidate, retries);
   s_aReplacingVehicles.RemoveItem(vehicle);
   return;
  }
  IEntity occ = slot.GetOccupant();
  if (occ && occ != candidate)
  {
   if (IsAble(occ))
   {
    s_aReplacingVehicles.RemoveItem(vehicle);
    return;
   }
   EjectFromCompartment(vehicle, ChimeraCharacter.Cast(occ));
   if (retries < 10)
   {
    GetGame().GetCallqueue().CallLater(DoSeatInTurret, 40, false, vehicle, slot, candidate, group, retries + 1);
    return;
   }
   s_aReplacingVehicles.RemoveItem(vehicle);
   return;
  }

  IEntity targetVeh = slot.GetVehicle();
  if (!targetVeh) targetVeh = vehicle;
  if (!targetVeh) targetVeh = slot.GetOwner();
  SeatTrace("get-in-target", targetVeh, slot, candidate, retries);
  bool ok = acc.GetInVehicle(targetVeh, slot, true, -1, ECloseDoorAfterActions.LEAVE_OPEN, false);
  if (!ok && targetVeh != vehicle && vehicle)
   ok = acc.GetInVehicle(vehicle, slot, true, -1, ECloseDoorAfterActions.LEAVE_OPEN, false);
  if (!ok && targetVeh != slot.GetOwner() && slot.GetOwner())
   ok = acc.GetInVehicle(slot.GetOwner(), slot, true, -1, ECloseDoorAfterActions.LEAVE_OPEN, false);

  SeatTrace("get-in-return", vehicle, slot, candidate, retries, ok);
  if (!ok && retries < 8)
  {
   GetGame().GetCallqueue().CallLater(DoSeatInTurret, 50, false, vehicle, slot, candidate, group, retries + 1);
   return;
  }

  s_aReplacingVehicles.RemoveItem(vehicle);

  if (ok)
  {
   s_iGunnerReplacements++;
   BBB_Perf.Count("vehicle.gunnerReplace");
   AIControlComponent ai = AIControlComponent.Cast(candidate.FindComponent(AIControlComponent));
   if (ai && !ai.IsAIActivated()) ai.ActivateAI();
   string gName = "vehicle";
   if (group) gName = BBB.s_Hooks.NameOf(group);
   PrintFormat("[BBB] TURRET %1: boarding request accepted for %2 into %3", gName, candidate, slot.GetCompartmentName());
  }
 }

 static bool TryReplaceGunner(IEntity vehicle, SCR_AIGroup group = null)
 {
  if (!s_bGunnerReplace || !vehicle || BBB.Health(vehicle) <= 0 || BBB.s_Hooks.LeaveAlone(vehicle)) return false;
  if (group && BBB.HasPlayer(group)) return false;
  if (s_aReplacingVehicles.Contains(vehicle)) return false;
  Physics phys = vehicle.GetPhysics();
  float speed = 0;
  if (phys) speed = phys.GetVelocity().Length();
  if (speed > 12.0) return false;

  SCR_BaseCompartmentManagerComponent compartments = SCR_BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(SCR_BaseCompartmentManagerComponent));
  if (!compartments) return false;

  // Cheapest first: almost every tick the turret is manned by an able gunner (or there is none).
  array<BaseCompartmentSlot> allSlots = {};
  GetVehicleCompartments(vehicle, allSlots);
  BaseCompartmentSlot targetTurret = null;
  foreach (BaseCompartmentSlot slot : allSlots)
  {
   if (slot.GetType() != ECompartmentType.TURRET) continue;
   IEntity gunner = slot.GetOccupant();
   if (gunner && (BBB.PlayerOf(gunner) || IsAble(gunner))) continue;
   if (!TurretHasAmmo(slot)) continue;
   targetTurret = slot;
   break;
  }
  if (!targetTurret) return false;

  foreach (BaseCompartmentSlot s : allSlots)
  {
   ChimeraCharacter occ = ChimeraCharacter.Cast(s.GetOccupant());
   if (!occ) continue;
   if (BBB.PlayerOf(occ) && s.GetType() != ECompartmentType.CARGO) return false;
   CompartmentAccessComponent occAcc = occ.GetCompartmentAccessComponent();
   if (occAcc && occAcc.IsGettingOut()) return false;
  }

  IEntity leaderEnt = null;
  if (group && group.GetLeaderAgent()) leaderEnt = group.GetLeaderAgent().GetControlledEntity();

  // Priority 1: Fit AI cargo passenger already aboard the vehicle (prefer non-leader).
  ChimeraCharacter candidate = null;
  ChimeraCharacter fallbackLeader = null;
  array<BaseCompartmentSlot> cargoSlots = {};
  GetVehicleCompartments(vehicle, cargoSlots, ECompartmentType.CARGO);
  foreach (BaseCompartmentSlot cSlot : cargoSlots)
  {
   ChimeraCharacter ch = ChimeraCharacter.Cast(cSlot.GetOccupant());
   if (!ch || !IsAble(ch) || BBB.PlayerOf(ch)) continue;
   CompartmentAccessComponent acc = ch.GetCompartmentAccessComponent();
   if (!acc || acc.IsGettingIn() || acc.IsGettingOut()) continue;
   AIControlComponent ai = AIControlComponent.Cast(ch.FindComponent(AIControlComponent));
   if (!ai || !ai.GetControlAIAgent()) continue;
   if (ch == leaderEnt)
   {
    if (!fallbackLeader) fallbackLeader = ch;
    continue;
   }
   candidate = ch;
   break;
  }
  if (!candidate) candidate = fallbackLeader;

  // Priority 2: Nearby dismounted AI squadmate within 8 m when vehicle is crewed by this group and stationary.
  if (!candidate && group && speed < 2.0 && BBB_Transport.CrewedVehicle(group) == vehicle)
  {
   bool allowFoot = true;
   SCR_AIGroupUtilityComponent grpUtil = SCR_AIGroupUtilityComponent.Cast(group.FindComponent(SCR_AIGroupUtilityComponent));
   SCR_AIVehicleUsageComponent usage = SCR_AIVehicleUsageComponent.Cast(vehicle.FindComponent(SCR_AIVehicleUsageComponent));
   if (grpUtil && usage && !grpUtil.IsUsableVehicle(usage)) allowFoot = false;
   if (allowFoot)
   {
    array<AIAgent> agents = {};
    BBB.s_Hooks.GetMembers(group, agents);
    float bestDist = GUNNER_FOOT_REACH_M;
    foreach (AIAgent agent : agents)
    {
     ChimeraCharacter ch = ChimeraCharacter.Cast(agent.GetControlledEntity());
     if (!ch || ch.IsInVehicle() || !IsAble(ch) || BBB.PlayerOf(ch)) continue;
     if (BBB_Drag.Busy(ch) || BBB_Medic.s_mGoingTo.Contains(ch) || BBB_Claims.Committed(ch)) continue;
     CompartmentAccessComponent acc = ch.GetCompartmentAccessComponent();
     if (!acc || acc.IsGettingIn() || acc.IsGettingOut()) continue;
     float d = vector.DistanceXZ(ch.GetOrigin(), vehicle.GetOrigin());
     if (d <= bestDist)
     {
      bestDist = d;
      candidate = ch;
     }
    }
   }
  }

  if (!candidate) return false;

  SeatTrace("selected", vehicle, targetTurret, candidate);
  bool needsEject = false;
  if (targetTurret.GetOccupant())
  {
   EjectIncapacitatedOccupant(vehicle, targetTurret);
   needsEject = true;
  }
  CompartmentAccessComponent candAcc = candidate.GetCompartmentAccessComponent();
  if (candidate.IsInVehicle() || (candAcc && candAcc.GetCompartment() != null))
  {
   EjectFromCompartment(vehicle, candidate);
   needsEject = true;
  }

  s_aReplacingVehicles.Insert(vehicle);
  float now = GetGame().GetWorld().GetWorldTime();
  s_mGunnerReplaceNext.Set(vehicle, now + GUNNER_REPLACE_COOLDOWN_MS);
  if (needsEject)
  {
   GetGame().GetCallqueue().CallLater(DoSeatInTurret, 100, false, vehicle, targetTurret, candidate, group, 0);
  }
  else
  {
   DoSeatInTurret(vehicle, targetTurret, candidate, group, 0);
  }
  return true;
 }

 protected void ReplaceGunnerFor(SCR_AIGroup group, notnull array<IEntity> replacedVehicles)
 {
  if (!s_bGunnerReplace || !group || BBB.HasPlayer(group)) return;
  IEntity vehicle = BBB_Transport.CrewedVehicle(group);
  if (!vehicle)
  {
   array<AIAgent> agents = {};
   BBB.s_Hooks.GetMembers(group, agents);
   foreach (AIAgent agent : agents)
   {
    IEntity body = agent.GetControlledEntity();
    vehicle = BBB_Transport.VehicleOf(body);
    if (vehicle) break;
   }
  }
  if (!vehicle || replacedVehicles.Contains(vehicle) || s_aReplacingVehicles.Contains(vehicle) || BBB.s_Hooks.LeaveAlone(vehicle)) return;
  replacedVehicles.Insert(vehicle);

  float now = GetGame().GetWorld().GetWorldTime();
  if (s_mGunnerReplaceNext.Contains(vehicle) && now < s_mGunnerReplaceNext.Get(vehicle)) return;

  TryReplaceGunner(vehicle, group);
 }

 static int DismountGroupFromVehicle(SCR_AIGroup group, IEntity vehicle, bool cargoOnly = false)
 {
  if (!group || !vehicle) return 0;
  if (!cargoOnly || !BBB_Transport.CrewedVehicle(group))
  {
   SCR_AIGroupUtilityComponent utility = SCR_AIGroupUtilityComponent.Cast(group.FindComponent(SCR_AIGroupUtilityComponent));
   SCR_AIVehicleUsageComponent usage = SCR_AIVehicleUsageComponent.Cast(vehicle.FindComponent(SCR_AIVehicleUsageComponent));
   if (utility && usage && utility.IsUsableVehicle(usage))
    utility.RemoveUsableVehicle(usage);
  }
  int dropped = 0;
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(group, agents);
  foreach (AIAgent agent : agents)
  {
   ChimeraCharacter ch = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!ch || !BBB.IsAlive(ch) || BBB.PlayerOf(ch)) continue;
   if (BBB_Transport.VehicleOf(ch) != vehicle) continue;
   if (cargoOnly && BBB_Transport.SeatOf(ch) != ECompartmentType.CARGO) continue;
   CompartmentAccessComponent access = ch.GetCompartmentAccessComponent();
   if (!access) continue;
   if (!access.GetOutVehicle(EGetOutType.ANIMATED, -1, ECloseDoorAfterActions.LEAVE_OPEN, false))
    access.GetOutVehicle(EGetOutType.TELEPORT, -1, ECloseDoorAfterActions.LEAVE_OPEN, false);
   dropped++;
  }
  return dropped;
 }

 protected void CheckTacticalDismountFor(SCR_AIGroup group)
 {
  if (!s_bTacticalDismount || !group || BBB.HasPlayer(group)) return;
  IEntity vehicle = null;
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(group, agents);
  foreach (AIAgent agent : agents)
  {
   IEntity body = agent.GetControlledEntity();
   if (body && BBB.IsAlive(body) && BBB_Transport.SeatOf(body) == ECompartmentType.CARGO)
   {
    vehicle = BBB_Transport.VehicleOf(body);
    if (vehicle) break;
   }
  }
  if (!vehicle || BBB.s_Hooks.LeaveAlone(vehicle)) return;
  if (!CarControllerComponent.Cast(vehicle.FindComponent(CarControllerComponent))) return;

  bool hitRecently = BBB_Transport.VehicleHitRecently(vehicle, 6000);
  bool underFire = hitRecently || KnownEnemiesNear(group, vehicle.GetOrigin(), 250) > 0;
  if (!underFire) return;

  ChimeraCharacter driver = BBB.DriverOf(vehicle);
  bool driverLost = !driver || !BBB.IsAlive(driver);
  Physics phys = vehicle.GetPhysics();
  float speed = 0;
  if (phys) speed = phys.GetVelocity().Length();

  bool dismount = (driverLost && speed < 3.0) || (hitRecently && speed < 5.0) || speed < 3.5;
  if (!dismount) return;

  bool hasGunner = HasGunner(vehicle);
  if (hasGunner && BBB_Transport.CrewedVehicle(group) == vehicle) return;
  if (DismountGroupFromVehicle(group, vehicle, hasGunner) > 0)
   PrintFormat("[BBB] TACTICS %1: tactical dismount under fire", BBB.s_Hooks.NameOf(group));
   BBB_Think.NoteGroup(group, "Vehicle", "tactical dismount under fire");
 }

 protected void ClearStallState(IEntity vehicle)
 {
  m_StallPos.Remove(vehicle);
  m_StallSince.Remove(vehicle);
  m_Unsticks.Remove(vehicle);
 }

 void OnUnstickEnded(IEntity vehicle)
 {
  m_Unsticking.RemoveItem(vehicle);
  if (vehicle && GetGame() && GetGame().GetWorld())
  {
   m_StallPos.Set(vehicle, vehicle.GetOrigin());
   m_StallSince.Set(vehicle, GetGame().GetWorld().GetWorldTime());
  }
 }

 protected void WatchDrivingFor(SCR_AIGroup group, notnull array<IEntity> watchedVehicles)
 {
  if (!s_bUnstick || !group || BBB.HasPlayer(group)) return;
  IEntity vehicle = BBB_Transport.CrewedVehicle(group);
  if (!vehicle || watchedVehicles.Contains(vehicle) || BBB.s_Hooks.LeaveAlone(vehicle)) return;
  if (!CarControllerComponent.Cast(vehicle.FindComponent(CarControllerComponent))) return;
  watchedVehicles.Insert(vehicle);
  if (m_Unsticking.Contains(vehicle)) return;

  Physics phys = vehicle.GetPhysics();
  float speed = 0;
  if (phys) speed = phys.GetVelocity().Length();

  ChimeraCharacter driver = BBB.DriverOf(vehicle);
  bool hitRecently = BBB_Transport.VehicleHitRecently(vehicle, 6000);
  bool underFire = hitRecently || KnownEnemiesNear(group, vehicle.GetOrigin(), 300) > 0;

  if (underFire && (!driver || !BBB.IsAlive(driver)) && speed < 2.0)
  {
   if (DismountGroupFromVehicle(group, vehicle, false) > 0)
    PrintFormat("[BBB] DRIVE %1: driver lost under fire, crew bails out", BBB.s_Hooks.NameOf(group));
    BBB_Think.NoteGroup(group, "Vehicle", "driver lost under fire: crew bails out");
   ClearStallState(vehicle);
   return;
  }

  bool eligible = driver != null && BBB.IsAlive(driver) && !BBB.PlayerOf(driver);
  bool wantsToMove = false;
  if (eligible)
  {
   AIWaypoint wp = group.GetCurrentWaypoint();
   if (wp)
   {
    float stopDist = Math.Max(15.0, wp.GetCompletionRadius());
    wantsToMove = vector.DistanceXZ(vehicle.GetOrigin(), wp.GetOrigin()) > stopDist;
   }
  }

  float now = GetGame().GetWorld().GetWorldTime();
  bool hasStall = m_StallSince.Contains(vehicle);
  vector stallPos = m_StallPos.Get(vehicle);
  if (!eligible || !wantsToMove || !hasStall || (speed > 1.0 && vector.DistanceXZ(vehicle.GetOrigin(), stallPos) > 5))
  {
   if (!eligible || !wantsToMove) m_Unsticks.Remove(vehicle);
   m_StallPos.Set(vehicle, vehicle.GetOrigin());
   m_StallSince.Set(vehicle, now);
   return;
  }

  float stalled = (now - m_StallSince.Get(vehicle)) / 1000.0;
  bool fightingVehicle = HasTurret(vehicle);
  bool armed = HasGunner(vehicle);

  if (underFire && !fightingVehicle && stalled >= STALL_SECONDS_HELPLESS)
  {
   if (DismountGroupFromVehicle(group, vehicle, false) > 0)
    PrintFormat("[BBB] DRIVE %1: stopped under fire (unarmed), crew bails out", BBB.s_Hooks.NameOf(group));
    BBB_Think.NoteGroup(group, "Vehicle", "stopped under fire (unarmed): crew bails out");
   ClearStallState(vehicle);
   return;
  }

  float limit = STALL_SECONDS;
  if (underFire || armed) limit = STALL_SECONDS_FIGHTING;
  if (stalled < limit) return;

  int tries = m_Unsticks.Get(vehicle);
  if (tries >= UNSTICK_TRIES) return;
  tries++;
  m_Unsticks.Set(vehicle, tries);
  m_Unsticking.Insert(vehicle);
  float steer = 0.8;
  if (tries % 2 == 0) steer = -0.8;
  if (!m_UnstickListener) m_UnstickListener = new BBB_VehicleHelpersUnstick(this);
  BBB_Unstick.Begin(vehicle, driver, steer, m_UnstickListener);
  PrintFormat("[BBB] DRIVE %1: stuck, backing up (try %2)", BBB.s_Hooks.NameOf(group), tries);
  BBB_Think.NoteGroup(group, "Vehicle", "stuck, backing up (try " + tries + ")");
 }
}

class BBB_VehicleHelpersUnstick : BBB_UnstickListener
{
 protected BBB_VehicleHelpers m_Owner;
 void BBB_VehicleHelpersUnstick(BBB_VehicleHelpers owner) { m_Owner = owner; }
 override void OnUnstickEnded(IEntity vehicle) { if (m_Owner) m_Owner.OnUnstickEnded(vehicle); }
}
