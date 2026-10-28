// Optional limited-traverse alignment. No waypoints are created/removed and no
// gun is aimed or fired here. Native perception and weapon selection remain in charge.
class BBB_DriverAlignmentState : Managed
{
 IEntity m_Vehicle;
 SCR_AIGroup m_Group;
 ref BBB_OrderLease m_Lease;
 ChimeraCharacter m_Driver;
 ChimeraCharacter m_Gunner;
 BaseCompartmentSlot m_Seat;
 ref BaseTarget m_Target;
 ref AIWeaponTargetSelector m_WeaponProbe;
 IEntity m_ProbeOwner;
 float m_fStableSince;
 float m_fCooldownUntil;
 float m_fStarted;
 float m_fNextCheck;
 float m_fDirection;
 float m_fSeenAt;
 float m_fLastRunMs;
 float m_fLastTravel;
 string m_sEndReason;
 string m_sEngagementReason;
 vector m_Start;
 vector m_Forward;
 bool m_bActive;
 bool m_bParkedAI;
 bool m_bHandbrake;
 int m_iGear;
}

class BBB_DriverAlignment
{
 static int s_iRead;
 static bool s_bOn;
 static const float STABLE_MS = 3000;
 static const float COOLDOWN_MS = 15000;
 static const float DEADBAND_DEG = 5;
 static const float MAX_TURN_DEG = 20;
 static const float MAX_TRAVEL_M = 2.5;
 static const float MAX_RUN_MS = 8000;
 static ref array<ref BBB_DriverAlignmentState> s_States = {};
 static int s_iStarted;
 static int s_iCancelled;
 protected static IEntity s_TraceVehicle;
 protected static IEntity s_TraceTarget;
 protected static bool s_bCoverFound;
 protected static float s_fCoverGround;

 static bool CollectCover(IEntity entity)
 {
  if (!TraceOther(entity) || !entity.GetPhysics()) return true;
  vector mins, maxs;
  entity.GetWorldBounds(mins, maxs);
  if (maxs[1] > s_fCoverGround + 0.4 && maxs[1] - mins[1] > 0.5)
  {
   s_bCoverFound = true;
   return false;
  }
  return true;
 }

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbDriverAlign", value) && value == "0");
 }

 static bool TraceOther(notnull IEntity entity)
 {
  if (entity == s_TraceVehicle || entity.GetRootParent() == s_TraceVehicle) return false;
  if (entity == s_TraceTarget || entity.GetRootParent() == s_TraceTarget) return false;
  ChimeraCharacter body = ChimeraCharacter.Cast(entity);
  if (body && BBB_Transport.VehicleOf(body) == s_TraceVehicle) return false;
  return true;
 }

 static bool ClearLine(IEntity vehicle, IEntity target, vector start, vector end)
 {
  TraceParam trace = new TraceParam();
  trace.Start = start;
  trace.End = end;
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  s_TraceVehicle = vehicle;
  s_TraceTarget = target;
  return GetGame().GetWorld().TraceMove(trace, TraceOther) >= 0.999;
 }

 static float Bearing(vector forward, vector direction)
 {
  forward[1] = 0;
  direction[1] = 0;
  forward.Normalize();
  direction.Normalize();
  return Math.Atan2(forward[2] * direction[0] - forward[0] * direction[2], vector.Dot(forward, direction)) * Math.RAD2DEG;
 }

 static vector Rotated(vector forward, float degrees)
 {
  float radians = degrees * Math.DEG2RAD;
  return Vector(forward[0] * Math.Cos(radians) + forward[2] * Math.Sin(radians), 0, forward[2] * Math.Cos(radians) - forward[0] * Math.Sin(radians));
 }

 // Conservative open-ground test: no roof, walls, trees or terrain screen in
 // the whole adjustment envelope. Sheltered/defensive positions are left alone.
 static bool OpenGround(IEntity vehicle)
 {
  if (!vehicle) return false;
  vector at = vehicle.GetOrigin();
  BaseWorld world = GetGame().GetWorld();
  float ground = world.GetSurfaceY(at[0], at[2]);
  if (Math.AbsFloat(at[1] - ground) > 2 || ground < 1) return false;
  vector center = Vector(at[0], ground + 0.6, at[2]);
  s_TraceVehicle = vehicle;
  s_TraceTarget = null;
  s_fCoverGround = ground;
  s_bCoverFound = false;
  world.QueryEntitiesBySphere(center, 12, CollectCover);
  if (s_bCoverFound) return false;
  if (!ClearLine(vehicle, null, center, center + Vector(0, 8, 0))) return false;
  for (int i = 0; i < 16; i++)
  {
   float angle = i * 22.5 * Math.DEG2RAD;
   vector edge = center + Vector(Math.Sin(angle) * 12, 0, Math.Cos(angle) * 12);
   if (Math.AbsFloat(world.GetSurfaceY(edge[0], edge[2]) - ground) > 0.6) return false;
   if (!ClearLine(vehicle, null, center, edge)) return false;
   if (!ClearLine(vehicle, null, center + Vector(0, 1, 0), edge + Vector(0, 1, 0))) return false;
  }
  return true;
 }

 static bool OrdersAllow(SCR_AIGroup group, IEntity vehicle, ChimeraCharacter driver, bool checkCrew = true)
 {
  if (!group || !vehicle || BBB.Health(vehicle) <= 0 || !BBB_VehiclePosture.Controlled(driver) || BBB_Transport.VehicleOf(driver) != vehicle) return false;
  if ((checkCrew && BBB.HasPlayer(group)) || BBB.s_Hooks.LeaveAlone(vehicle) || !BBB_OrderLeash.DrivingAllows(group)) return false;
  if (!BBB_OrderLeash.Allows(group, vehicle.GetOrigin())) return false;
  SCR_AIUtilityComponent utility = BBB_VehiclePosture.Utility(driver);
  if (!utility || BBB_OrderIntent.Blocks(utility)) return false;
  if (SCR_AIMoveIndividuallyBehavior.Cast(utility.GetCurrentBehavior()) || SCR_AIMoveInFormationBehavior.Cast(utility.GetCurrentBehavior())) return false;
  AICarMovementComponent movement = AICarMovementComponent.Cast(vehicle.FindComponent(AICarMovementComponent));
  return !movement || movement.HasCompletedRequest(false);
 }

 // Native target intention may live in the cannot-engage slot when traverse
 // alone blocks a shot. Never search perception lists or choose another enemy.
 static BaseTarget TargetOf(SCR_AIUtilityComponent utility)
 {
  if (!utility || !utility.m_CombatComponent) return null;
  BaseTarget target = utility.m_CombatComponent.GetCurrentTarget();
  if (target) return target;
  if (!utility.m_CombatComponent.m_WeaponTargetSelector) return null;
  float score;
  utility.m_CombatComponent.m_WeaponTargetSelector.GetMostRelevantTargetCantAttack(target, score);
  if (target) return target;
  // A native gunner may select a group suppression goal instead of filling
  // the firing-target slot. Only an unambiguous one-enemy goal is eligible;
  // resolve it through this gunner's own perception, never group positions.
  SCR_AISuppressGroupClusterBehavior suppression = SCR_AISuppressGroupClusterBehavior.Cast(utility.GetCurrentBehavior());
  if (!suppression || !utility.m_PerceptionComponent) return null;
  SCR_AITargetClusterState cluster = suppression.GetClusterState();
  if (!cluster || !cluster.m_Cluster || !cluster.m_Cluster.m_aEntities || cluster.m_Cluster.m_aEntities.Count() != 1) return null;
  return utility.m_PerceptionComponent.GetTargetPerceptionObject(cluster.m_Cluster.m_aEntities[0], ETargetCategory.ENEMY);
 }

 static bool Rejected(BBB_DriverAlignmentState state, string reason)
 {
  if (state) state.m_sEngagementReason = reason;
  return false;
 }

 static bool Engagement(BBB_DriverAlignmentState state, out vector aim, out float excess)
 {
  if (!state || !state.m_Seat || state.m_Seat.GetOccupant() != state.m_Gunner || !BBB_VehiclePosture.Controlled(state.m_Gunner)) return Rejected(state, "seat");
  SCR_AIUtilityComponent utility = BBB_VehiclePosture.Utility(state.m_Gunner);
  if (!utility || !utility.m_CombatComponent || !utility.m_PerceptionComponent || BBB_OrderIntent.Blocks(utility)) return Rejected(state, "utility-order");
  SCR_AICombatComponent combat = utility.m_CombatComponent;
  BaseTarget target = TargetOf(utility);
  if (!target || target != state.m_Target || !target.GetTargetEntity() || target.GetTargetCategory() != ETargetCategory.ENEMY) return Rejected(state, "selected-target");
  DamageManagerComponent targetDamage = target.GetDamageManagerComponent();
  if ((targetDamage && targetDamage.IsDestroyed()) || target.IsDisarmed()) return Rejected(state, "target-disabled");
  // Require this gunner's own perception object and current direct exposure.
  if (utility.m_PerceptionComponent.GetTargetPerceptionObject(target.GetTargetEntity(), ETargetCategory.ENEMY) != target) return Rejected(state, "own-perception");
  if (target.GetTraceFraction() < 0.999 || target.GetExposure() <= 0 || target.GetTimeSinceSeen() > 0.5) return Rejected(state, "sight");
  if (combat.GetCombatMode() == EAIGroupCombatMode.HOLD_FIRE || utility.m_PerceptionComponent.GetFriendlyInLineOfFire()) return Rejected(state, "hold-or-friendly");
  TurretControllerComponent controller = BBB_VehicleHelpers.TurretControllerOf(state.m_Seat);
  if (!controller || BBB.Health(controller.GetOwner()) <= 0 || !controller.GetTurretComponent() || !controller.GetWeaponManager()) return Rejected(state, "turret");
  // Probe abstract type/range with a separate native selector. This does not
  // alter the gunner's selector, weapon, target or fire commands, and ignores
  // direction only so the arc may be the sole missing engagement condition.
  if (!state.m_WeaponProbe || state.m_ProbeOwner != state.m_Gunner)
  {
   state.m_WeaponProbe = new AIWeaponTargetSelector();
   state.m_WeaponProbe.Init(state.m_Gunner);
   state.m_ProbeOwner = state.m_Gunner;
  }
  if (!state.m_WeaponProbe.SelectWeaponAgainstUnitTypeAndDistance(target.GetUnitType(), target.GetDistance(), true, true)) return Rejected(state, "weapon-selection");
  BaseWeaponComponent selected;
  BaseMagazineComponent selectedMagazine;
  int muzzleId;
  state.m_WeaponProbe.GetSelectedWeapon(selected, muzzleId, selectedMagazine);
  if (!selected || selected != controller.GetWeaponManager().GetCurrentWeapon()) return Rejected(state, "loaded-weapon");
  array<BaseMuzzleComponent> muzzles = {};
  selected.GetMuzzlesList(muzzles);
  if (!muzzles.IsIndexValid(muzzleId)) return Rejected(state, "muzzle-index");
  BaseMuzzleComponent muzzle = muzzles[muzzleId];
  if (!muzzle || (muzzle.GetAmmoCount() <= 0 && !muzzle.IsCurrentBarrelChambered())) return Rejected(state, "ammo");
  // Suitability must describe what is loaded now, not a better spare magazine.
  if (selectedMagazine != muzzle.GetMagazine() || BBB_Armour.Useless(target, selectedMagazine)) return Rejected(state, "magazine");
  float minRange, maxRange;
  bool direct;
  state.m_WeaponProbe.GetSelectedWeaponProperties(minRange, maxRange, direct);
  if (!direct || maxRange <= 0 || target.GetDistance() < minRange || target.GetDistance() > maxRange) return Rejected(state, "range");
  aim = target.GetLastSeenPosition() + Vector(0, 1, 0);
  if (!ClearLine(state.m_Vehicle, target.GetTargetEntity(), state.m_Gunner.GetOrigin() + Vector(0, 1.4, 0), aim)) return Rejected(state, "crew-line");
  if (!ClearLine(state.m_Vehicle, target.GetTargetEntity(), selected.GetOwner().GetOrigin(), aim)) return Rejected(state, "weapon-line");
  vector outside = controller.GetTurretComponent().GetAimingAngleExcess(aim);
  if (Math.AbsFloat(outside[1]) > 0.5) return Rejected(state, "elevation");
  excess = outside[0];
  return true;
 }

 // Cheap native-state reads before every input write; no soldier/world scan.
 // A stale cached safety trace must never keep steering toward a lost target.
 static bool ContactStillValid(BBB_DriverAlignmentState state)
 {
  if (!state || !state.m_Target || !state.m_Target.GetTargetEntity()) return false;
  if (state.m_Target.GetTraceFraction() < 0.999 || state.m_Target.GetExposure() <= 0 || state.m_Target.GetTimeSinceSeen() > 0.5) return false;
  SCR_AIUtilityComponent utility = BBB_VehiclePosture.Utility(state.m_Gunner);
  if (!utility || !utility.m_CombatComponent || !utility.m_PerceptionComponent || BBB_OrderIntent.Blocks(utility)) return false;
  return TargetOf(utility) == state.m_Target && utility.m_CombatComponent.GetCombatMode() != EAIGroupCombatMode.HOLD_FIRE && !utility.m_PerceptionComponent.GetFriendlyInLineOfFire();
 }

 static bool Protection(BBB_DriverAlignmentState state, float turn)
 {
  if (!state || !state.m_Vehicle || !BBB_VehiclePosture.CrewAllows(state.m_Vehicle)) return false;
  if (BBB_Transport.VehicleHitRecently(state.m_Vehicle, 15000)) return false;
  vector nose = state.m_Vehicle.GetTransformAxis(2);
  vector nextNose = Rotated(nose, turn);
  array<AIAgent> members = {};
  BBB.s_Hooks.GetMembers(state.m_Group, members);
  foreach (AIAgent member : members)
  {
   if (!member || !BBB.IsAlive(member.GetControlledEntity())) continue;
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(member.FindComponent(SCR_AIUtilityComponent));
   if (!utility || !utility.m_PerceptionComponent || BBB_VehiclePosture.IncomingFire(utility)) return false;
   array<BaseTarget> threats = {};
   utility.m_PerceptionComponent.GetTargetsList(threats, ETargetCategory.ENEMY);
   array<BaseTarget> detected = {};
   utility.m_PerceptionComponent.GetTargetsList(detected, ETargetCategory.DETECTED);
   foreach (BaseTarget unknown : detected) threats.Insert(unknown);
   foreach (BaseTarget known : threats)
   {
    if (!known) continue;
    vector knownPosition = known.GetLastDetectedPosition();
    if (known.GetTargetCategory() == ETargetCategory.ENEMY) knownPosition = known.GetLastSeenPosition();
    vector to = knownPosition - state.m_Vehicle.GetOrigin();
    float before = Math.AbsFloat(Bearing(nose, to));
    float after = Math.AbsFloat(Bearing(nextNose, to));
    // Never rotate a flank/rear toward a known contact or open a second flank.
    if (before > 60 || after > before + 0.1) return false;
   }
  }
  return true;
 }

 static BBB_DriverAlignmentState State(IEntity vehicle)
 {
  foreach (BBB_DriverAlignmentState found : s_States)
   if (found.m_Vehicle == vehicle) return found;
  if (s_States.Count() >= 64) return null;
  BBB_DriverAlignmentState created = new BBB_DriverAlignmentState();
  created.m_Vehicle = vehicle;
  s_States.Insert(created);
  return created;
 }

 static bool Stable(BBB_DriverAlignmentState state, BaseTarget target, float now)
 {
  if (state.m_Target != target)
  {
   state.m_Target = target;
   state.m_fStableSince = now;
   return false;
  }
  return target && now - state.m_fStableSince >= STABLE_MS && now >= state.m_fCooldownUntil;
 }

 static void Tick()
 {
  Read();
  float now = GetGame().GetWorld().GetWorldTime();
  for (int i = s_States.Count() - 1; i >= 0; i--)
   if (!s_States[i].m_Vehicle || (!s_States[i].m_bActive && now - s_States[i].m_fSeenAt > 60000)) { End(s_States[i]); s_States.RemoveOrdered(i); }
  if (!s_bOn) return;
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  array<IEntity> handled = {};
  int activeCount;
  foreach (BBB_DriverAlignmentState running : s_States)
   if (running.m_bActive) activeCount++;
  foreach (SCR_AIGroup group : groups)
  {
   IEntity vehicle = BBB_Transport.CrewedVehicle(group);
   if (!vehicle || handled.Contains(vehicle)) continue;
   handled.Insert(vehicle);
   ChimeraCharacter driver = BBB.DriverOf(vehicle);
   if (!OrdersAllow(group, vehicle, driver)) continue;
   CarControllerComponent car = CarControllerComponent.Cast(vehicle.FindComponent(CarControllerComponent));
   if (!car || !car.GetSimulation() || !car.IsEngineOn()) continue;
   Physics physics = vehicle.GetPhysics();
   if (!physics || physics.GetVelocity().Length() > 0.2) continue;
   BBB_DriverAlignmentState state = State(vehicle);
   if (!state || state.m_bActive) continue;
   state.m_Group = group;
   state.m_Driver = driver;
   state.m_fSeenAt = now;
   array<BaseCompartmentSlot> seats = {};
   BBB_VehicleHelpers.GetVehicleCompartments(vehicle, seats, ECompartmentType.TURRET);
   BaseTarget picked;
   foreach (BaseCompartmentSlot seat : seats)
   {
    SCR_AIUtilityComponent gunnerUtility = BBB_VehiclePosture.Utility(seat.GetOccupant());
    if (!gunnerUtility || !gunnerUtility.m_CombatComponent) continue;
    BaseTarget candidate = TargetOf(gunnerUtility);
    if (!candidate) continue;
    state.m_Seat = seat;
    state.m_Gunner = ChimeraCharacter.Cast(seat.GetOccupant());
    picked = candidate;
    break;
   }
   if (!Stable(state, picked, now)) continue;
   vector aim;
   float excess;
   if (!Engagement(state, aim, excess)) { state.m_fStableSince = now; continue; }
   if (Math.AbsFloat(excess) < DEADBAND_DEG || Math.AbsFloat(excess) > MAX_TURN_DEG || activeCount >= 4) continue;
   float heading = Bearing(vehicle.GetTransformAxis(2), aim - vehicle.GetOrigin());
   if (Math.AbsFloat(heading) > 60 || heading * excess <= 0 || !Protection(state, excess) || !OpenGround(vehicle)) continue;
   AIControlComponent ai = AIControlComponent.Cast(driver.FindComponent(AIControlComponent));
   if (!ai || !ai.IsAIActivated()) continue;
   state.m_Lease = BBB_OrderLeash.Capture(group);
   state.m_Start = vehicle.GetOrigin();
   state.m_Forward = vehicle.GetTransformAxis(2);
   state.m_fStarted = now;
   state.m_fNextCheck = now;
   state.m_fDirection = 1;
   if (excess < 0) state.m_fDirection = -1;
   state.m_bHandbrake = car.GetPersistentHandBrake();
   state.m_iGear = car.GetSimulation().GetGear();
   CharacterInputContext input = driver.GetCharacterController().GetInputContext();
   if (input) input.SetVehicleHorn(0);
   ai.DeactivateAI();
   state.m_bParkedAI = true;
   state.m_bActive = true;
   s_iStarted++;
   activeCount++;
  }
 }

 static void End(BBB_DriverAlignmentState state)
 {
  if (!state || !state.m_bActive) return;
  state.m_fLastRunMs = GetGame().GetWorld().GetWorldTime() - state.m_fStarted;
  if (state.m_Vehicle) state.m_fLastTravel = vector.Distance(state.m_Vehicle.GetOrigin(), state.m_Start);
  // Do not write any input once a player has taken the controls.
  if (state.m_Vehicle && BBB_VehiclePosture.Controlled(state.m_Driver) && BBB_Transport.VehicleOf(state.m_Driver) == state.m_Vehicle)
  {
   CarControllerComponent car = CarControllerComponent.Cast(state.m_Vehicle.FindComponent(CarControllerComponent));
   if (car && car.GetSimulation())
   {
    car.GetSimulation().SetThrottle(0);
    car.GetSimulation().SetSteering(0);
    car.GetSimulation().SetBreak(0, false);
    car.GetSimulation().SetGear(state.m_iGear);
    car.SetPersistentHandBrake(state.m_bHandbrake);
   }
  }
  // Restore only AI we parked, even after that AI leaves the seat. A player
  // taking possession owns its activation state from that point onward.
  if (state.m_Driver && !BBB.PlayerOf(state.m_Driver) && !state.m_Driver.GetCharacterController().IsPlayerControlled())
  {
   AIControlComponent ai = AIControlComponent.Cast(state.m_Driver.FindComponent(AIControlComponent));
   if (state.m_bParkedAI && ai && !ai.IsAIActivated()) ai.ActivateAI();
  }
  state.m_bActive = false;
  state.m_bParkedAI = false;
  state.m_fCooldownUntil = GetGame().GetWorld().GetWorldTime() + COOLDOWN_MS;
  state.m_fStableSince = GetGame().GetWorld().GetWorldTime();
  s_iCancelled++;
 }

 // Only active vehicles are visited per frame. Perception/safety work is bounded
 // to 4 Hz; orders/player takeover are checked before every input write.
 static void Frame()
 {
  if (s_States.IsEmpty()) return;
  int profileStart = System.GetTickCount();
  float now = GetGame().GetWorld().GetWorldTime();
  foreach (BBB_DriverAlignmentState state : s_States)
  {
   if (!state.m_bActive) continue;
   if (!s_bOn || !state.m_Vehicle || !BBB_OrderLeash.Allows(state.m_Group, state.m_Vehicle.GetOrigin(), state.m_Lease) || !OrdersAllow(state.m_Group, state.m_Vehicle, state.m_Driver, false) || !BBB_VehiclePosture.Controlled(state.m_Gunner) || !ContactStillValid(state) || now - state.m_fStarted >= MAX_RUN_MS || vector.Distance(state.m_Vehicle.GetOrigin(), state.m_Start) >= MAX_TRAVEL_M)
   { state.m_sEndReason = "contact-order-or-bound"; End(state); continue; }
   Physics physics = state.m_Vehicle.GetPhysics();
   if (!physics || physics.GetVelocity().Length() > 1.0 || Math.AbsFloat(Bearing(state.m_Forward, state.m_Vehicle.GetTransformAxis(2))) > MAX_TURN_DEG)
   { state.m_sEndReason = "physics-speed-or-turn"; End(state); continue; }
   if (now >= state.m_fNextCheck)
   {
    vector aim;
    float excess;
    bool canEngage = Engagement(state, aim, excess);
    bool protectedPosture = Protection(state, state.m_fDirection * 2);
    bool open = OpenGround(state.m_Vehicle);
    if (!canEngage || Math.AbsFloat(excess) <= 0.1 || excess * state.m_fDirection <= 0 || !protectedPosture || !open)
    { state.m_sEndReason = string.Format("engage=%1 excess=%2 protection=%3 open=%4 gate=%5", canEngage, excess, protectedPosture, open, state.m_sEngagementReason); End(state); continue; }
    state.m_fNextCheck = now + 250;
   }
   CarControllerComponent car = CarControllerComponent.Cast(state.m_Vehicle.FindComponent(CarControllerComponent));
   if (!car || !car.GetSimulation()) { End(state); continue; }
   VehicleWheeledSimulation simulation = car.GetSimulation();
   if (!physics.IsActive()) physics.SetActive(ActiveState.ACTIVE);
   car.SetPersistentHandBrake(false);
   int neutral = simulation.GearboxGearsCount() - simulation.GearboxForwardGearsCount() - 1;
   simulation.SetGear(neutral + 1);
   simulation.SetClutch(1);
   simulation.SetSteering(state.m_fDirection);
   // A tiny forward arc, bounded by distance, duration, speed and hull rotation.
   if (physics.GetVelocity().Length() < 0.7)
   {
    simulation.SetBreak(0, false);
    simulation.SetThrottle(0.15);
   }
   else
   {
    simulation.SetThrottle(0);
    simulation.SetBreak(0.25, false);
   }
  }
  BBB_Perf.Add("ev.driverAlignFrame", System.GetTickCount() - profileStart);
 }

 static void Stop()
 {
  foreach (BBB_DriverAlignmentState state : s_States) End(state);
  s_States.Clear();
 }
}
