// Reactive defence uses observed missile motion/damage, never a shooter's live position.
class BBB_ATProjectile : Managed
{
 IEntity m_Projectile;
 IEntity m_Shooter;
 vector m_Last;
 float m_Expires;
}

class BBB_ArmorEscape : Managed
{
 IEntity m_Vehicle;
 ChimeraCharacter m_Driver;
 SCR_AIGroup m_Group;
 ref BBB_OrderLease m_Lease;
 SCR_AIUtilityComponent m_Utility;
 AIControlComponent m_Control;
 vector m_Start;
 vector m_Line;
 vector m_Direction;
 vector m_Forward;
 float m_Steer;
 float m_Started;
 float m_CheckAt;
 bool m_bParked;
 int m_Gear;
 bool m_Handbrake;
 float m_Clutch;
}

class BBB_ArmorReaction
{
 static int s_iRead;
 static bool s_bOn;
 static int s_iMissiles;
 static int s_iReactions;
 static int s_iReverses;
 static int s_iSmoke;
 static ref array<ref BBB_ATProjectile> s_Projectiles = {};
 static ref array<ref BBB_ArmorEscape> s_Escapes = {};
 protected static ref array<IEntity> s_Vehicles = {};
 protected static ref map<IEntity, float> s_Retry = new map<IEntity, float>();
 protected static IEntity s_TraceVehicle;
 protected static ref array<TurretControllerComponent> s_SmokePulses = {};

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbArmorReaction", value) && value == "0");
  PrintFormat("[BBB] ARMOR REACTION enabled=%1", s_bOn);
 }

 static ChimeraCharacter Driver(IEntity vehicle)
 {
  if (!vehicle) return null;
  BaseVehicleControllerComponent controller = BaseVehicleControllerComponent.Cast(vehicle.FindComponent(BaseVehicleControllerComponent));
  if (!controller || !controller.GetPilotCompartmentSlot()) return null;
  return ChimeraCharacter.Cast(controller.GetPilotCompartmentSlot().GetOccupant());
 }

 static bool PlayerAboard(IEntity vehicle)
 {
  // Its seats, not every connected player (escapes ask this each frame).
  array<BaseCompartmentSlot> seats = {};
  BBB_VehicleHelpers.GetVehicleCompartments(vehicle, seats);
  foreach (BaseCompartmentSlot seat : seats)
   if (BBB.PlayerOf(seat.GetOccupant())) return true;
  return false;
 }

 static bool Allowed(IEntity vehicle, SCR_AIGroup group, SCR_AIUtilityComponent utility)
 {
  if (!s_bOn || !BBB.IsStarted() || !vehicle || !group || !utility || BBB.Health(vehicle) <= 0) return false;
  if (BBB_Unstick.ArmorBusy(vehicle) || BBB_Armour.Aircraft(vehicle) || PlayerAboard(vehicle) || !BBB_OrderLeash.Allows(group, vehicle.GetOrigin())) return false;
  if (BBB.s_Hooks.LeaveAlone(vehicle) || !BBB_OrderLeash.DrivingAllows(group) || !BBB.s_Hooks.AllowArmorReaction(group)) return false;
  ChimeraCharacter driver = Driver(vehicle);
  if (!driver || !BBB_VehicleHelpers.IsAble(driver) || driver != utility.m_OwnerEntity || BBB_OrderIntent.Blocks(utility)) return false;
  if (group.GetCurrentWaypoint())
  {
   Physics motion = vehicle.GetPhysics();
   if (!motion || motion.GetVelocity().Length() > 3) return false;
   AICarMovementComponent movement = AICarMovementComponent.Cast(vehicle.FindComponent(AICarMovementComponent));
   if (movement && !movement.HasCompletedRequest(false)) return false;
   if (SCR_AIMoveIndividuallyBehavior.Cast(utility.GetCurrentBehavior()) || SCR_AIMoveInFormationBehavior.Cast(utility.GetCurrentBehavior())) return false;
  }
  CharacterControllerComponent cc = driver.GetCharacterController();
  return cc && !cc.IsPlayerControlled();
 }

 static bool Hostile(IEntity shooter, SCR_AIGroup group)
 {
  if (!shooter || !group || !group.GetFaction()) return false;
  Faction faction = SCR_AIFactionHandling.GetEntityPerceivedFaction(shooter);
  return faction && group.GetFaction().IsFactionEnemy(faction);
 }

 static float SegmentDistance(vector point, vector start, vector finish)
 {
  vector line = finish - start;
  float len = line.LengthSq();
  if (len < 0.001) return vector.Distance(point, start);
  float fraction = Math.Clamp(vector.Dot(point - start, line) / len, 0, 1);
  return vector.Distance(point, start + line * fraction);
 }

 static float LineDistance(vector point, vector origin, vector direction)
 {
  vector delta = point - origin;
  delta[1] = 0;
  direction[1] = 0;
  direction.Normalize();
  return (delta - direction * vector.Dot(delta, direction)).Length();
 }

 // Called only for actual rocket-launcher shots; duplicate muzzle effect callbacks
 // for the same projectile are collapsed. No tracking of bullets or hand grenades.
 static void Fired(BaseMuzzleComponent muzzle, IEntity projectile)
 {
  if (!s_bOn || !BBB.IsStarted() || !Replication.IsServer() || !muzzle || !projectile) return;
  IEntity weaponEntity = muzzle.GetOwner();
  BaseWeaponComponent weapon;
  IEntity cursor = weaponEntity;
  while (cursor && !weapon)
  {
   weapon = BaseWeaponComponent.Cast(cursor.FindComponent(BaseWeaponComponent));
   if (!weapon) cursor = cursor.GetParent();
  }
  if (!weapon || weapon.GetWeaponType() != EWeaponType.WT_ROCKETLAUNCHER) return;
  foreach (BBB_ATProjectile prior : s_Projectiles)
   if (prior.m_Projectile == projectile) return;
  if (s_Projectiles.Count() >= 32) return;
  BBB_ATProjectile shot = new BBB_ATProjectile();
  shot.m_Projectile = projectile;
  shot.m_Shooter = weaponEntity.GetRootParent();
  shot.m_Last = projectile.GetOrigin();
  shot.m_Expires = GetGame().GetWorld().GetWorldTime() + 12000;
  s_Projectiles.Insert(shot);
  s_iMissiles++;
 }

 static void Hit(IEntity vehicle, BaseDamageContext context)
 {
  if (!s_bOn || !BBB.IsStarted() || !Replication.IsServer() || !vehicle || !context || !context.damageSource) return;
  bool missile = context.damageSource.FindComponent(MissileMoveComponent) != null;
  foreach (BBB_ATProjectile shot : s_Projectiles)
   if (shot.m_Projectile == context.damageSource) missile = true;
  if (!missile) return;
  vector incoming = context.impactVelocity;
  if (incoming.LengthSq() < 1)
  {
   // Explosion damage may have no projectile velocity. A recorded flight segment
   // can still establish the incoming bearing; otherwise smoke alone is allowed.
   foreach (BBB_ATProjectile observed : s_Projectiles)
    if (observed.m_Projectile == context.damageSource) incoming = context.hitPosition - observed.m_Last;
  }
  if (incoming.LengthSq() > 0.01) incoming.Normalize();
  else incoming = vector.Zero;
  IEntity shooter;
  if (context.instigator) shooter = context.instigator.GetInstigatorEntity();
  React(vehicle, shooter, context.hitPosition, incoming);
 }

 protected static bool IgnoreVehicle(notnull IEntity entity)
 {
  return entity != s_TraceVehicle && entity.GetRootParent() != s_TraceVehicle && BBB_Transport.VehicleOf(entity) != s_TraceVehicle;
 }

 // A conservative swept envelope, including turning clearance at both sides.
 // Reject water, drops/steps, slopes and all solid obstructions, including people.
 static bool Clearance(IEntity vehicle)
 {
  if (!vehicle) return false;
  vector matrix[4];
  vehicle.GetWorldTransform(matrix);
  vector mins, maxs;
  vehicle.GetBounds(mins, maxs);
  float halfWidth = Math.Max(Math.AbsFloat(mins[0]), Math.AbsFloat(maxs[0])) + 1.5;
  float length = Math.Max(Math.AbsFloat(mins[2]), Math.AbsFloat(maxs[2])) + 8;
  if (halfWidth > 4.5 || length > 14) return false;
  vector origin = vehicle.GetOrigin();
  BaseWorld world = GetGame().GetWorld();
  float ground = world.GetSurfaceY(origin[0], origin[2]);
  if (Math.AbsFloat(origin[1] - ground) > 2) return false;
  s_TraceVehicle = vehicle;
  // World-aligned box enclosing every allowed yaw (turning envelope).
  float hullRadius = Math.Sqrt(halfWidth * halfWidth + (length - 8) * (length - 8));
  TraceBox sweep = new TraceBox();
  sweep.Mins = Vector(-hullRadius, 0.4, -hullRadius);
  sweep.Maxs = Vector(hullRadius, Math.Max(1.5, maxs[1]), hullRadius);
  sweep.Start = origin;
  sweep.Start[1] = ground;
  sweep.End = sweep.Start - matrix[2] * 8;
  sweep.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  if (world.TraceMove(sweep, IgnoreVehicle) < 1) return false;
  for (int side = -1; side <= 1; side++)
  {
   vector start = origin + matrix[0] * (halfWidth * side);
   for (int step = 0; step <= 5; step++)
   {
    vector sample = start - matrix[2] * (length * step / 5.0);
    float height = world.GetSurfaceY(sample[0], sample[2]);
    if (height < 0.5 || Math.AbsFloat(height - ground) > 0.8) return false;
   }
   for (int level = 0; level <= 1; level++)
   {
    TraceParam trace = new TraceParam();
    trace.Start = start;
    trace.Start[1] = ground + 0.5 + level * 1.2;
    trace.End = trace.Start - matrix[2] * length;
    trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
    if (world.TraceMove(trace, IgnoreVehicle) < 1) return false;
   }
  }
  return true;
 }

 // Use only an already selected, dedicated native smoke weapon. Never switch a
 // gunner away from his gun or create smoke ammunition for unequipped vehicles.
 static bool DeploySmoke(IEntity vehicle)
 {
  array<BaseCompartmentSlot> slots = {};
  BBB_VehicleHelpers.GetVehicleCompartments(vehicle, slots, ECompartmentType.TURRET);
  foreach (BaseCompartmentSlot slot : slots)
  {
   if (!slot.GetOccupant() || !BBB_VehicleHelpers.IsAble(slot.GetOccupant())) continue;
   SCR_AIUtilityComponent crewUtility = BBB_StaticWeapons.UtilityOf(slot.GetOccupant());
   if (!crewUtility || BBB_OrderIntent.Blocks(crewUtility)) continue;
   TurretControllerComponent turret = TurretControllerComponent.Cast(slot.GetController());
   if (!turret || !turret.GetWeaponManager() || BBB.PlayerOf(slot.GetOccupant())) continue;
   BaseWeaponComponent weapon = turret.GetWeaponManager().GetCurrentWeapon();
   if (!weapon) continue;
   array<BaseMuzzleComponent> muzzles = {};
   weapon.GetMuzzlesList(muzzles);
   if (muzzles.IsEmpty()) continue;
   bool smokeOnly = true;
   foreach (BaseMuzzleComponent muzzle : muzzles)
   {
    RocketEjectorMuzzleComponent ejector = RocketEjectorMuzzleComponent.Cast(muzzle);
    if (!ejector || !EjectorInterfaceSmokeLauncher.Cast(ejector.GetEjectorInterface())) smokeOnly = false;
   }
   if (!smokeOnly) continue;
   RocketEjectorMuzzleComponent selected = RocketEjectorMuzzleComponent.Cast(weapon.GetCurrentMuzzle());
   if (!selected) continue;
   // Ejectors carry projectile entities in barrels, rather than magazine rounds.
   array<IEntity> loaded = {};
   selected.GetLoadedEntities(loaded);
   bool hasSmoke;
   foreach (IEntity roundEntity : loaded)
    if (roundEntity) hasSmoke = true;
   if (!hasSmoke) continue;
   turret.SetFireWeaponWanted(true);
   s_SmokePulses.Insert(turret);
   GetGame().GetCallqueue().CallLater(ReleaseSmoke, 150, false, turret);
   s_iSmoke++;
   return true;
  }
  return false;
 }

 protected static void ReleaseSmoke(TurretControllerComponent turret)
 {
  if (!s_SmokePulses.Contains(turret)) return;
  if (turret)
  {
   BaseCompartmentSlot slot = turret.GetCompartmentSlot();
   if (!slot || !BBB.PlayerOf(slot.GetOccupant())) turret.SetFireWeaponWanted(false);
  }
  s_SmokePulses.RemoveItem(turret);
 }

 static bool React(IEntity vehicle, IEntity shooter, vector line, vector direction)
 {
  float now = GetGame().GetWorld().GetWorldTime();
  if (now < s_Retry.Get(vehicle)) return false;
  ChimeraCharacter driver = Driver(vehicle);
  SCR_AIUtilityComponent utility = BBB_StaticWeapons.UtilityOf(driver);
  SCR_AIGroup group = BBB_Buildings.GroupOf(utility);
  if (!Allowed(vehicle, group, utility) || !Hostile(shooter, group)) return false;
  array<SCR_AIGroup> roster = {};
  BBB.s_Hooks.GetGroups(roster);
  if (!roster.Contains(group)) return false;
  AIControlComponent control = AIControlComponent.Cast(driver.FindComponent(AIControlComponent));
  if (!control || !control.IsAIActivated()) return false;
  s_Retry.Set(vehicle, now + 30000);
  s_iReactions++;
  DeploySmoke(vehicle);
  // Input ownership is deliberately limited to supported native wheeled physics.
  CarControllerComponent car = CarControllerComponent.Cast(vehicle.FindComponent(CarControllerComponent));
  if (!car || !car.GetSimulation() || !car.IsEngineOn() || !control || !control.IsAIActivated()) return true;
  Physics physics = vehicle.GetPhysics();
  if (!physics || physics.GetVelocity().Length() > 3 || !Clearance(vehicle)) return true;
  vector matrix[4];
  vehicle.GetWorldTransform(matrix);
  // Do not back toward the observed incoming threat, or chase shots from behind.
  vector incoming = direction;
  incoming[1] = 0;
  if (incoming.LengthSq() < 0.1) return true;
  incoming.Normalize();
  if (vector.Dot(matrix[2], incoming) > -0.35) return true;
  BBB_ArmorEscape escape = new BBB_ArmorEscape();
  escape.m_Vehicle = vehicle;
  escape.m_Driver = driver;
  escape.m_Group = group;
  escape.m_Lease = BBB_OrderLeash.Capture(group);
  escape.m_Utility = utility;
  escape.m_Control = control;
  escape.m_Start = vehicle.GetOrigin();
  escape.m_Line = line;
  escape.m_Direction = incoming;
  escape.m_Forward = matrix[2];
  escape.m_Started = now;
  escape.m_Steer = 0.65;
  if (LineDistance(escape.m_Start + matrix[0] * 3, line, incoming) > LineDistance(escape.m_Start - matrix[0] * 3, line, incoming)) escape.m_Steer = -0.65;
  escape.m_Gear = car.GetSimulation().GetGear();
  escape.m_Handbrake = car.GetPersistentHandBrake();
  escape.m_Clutch = car.GetSimulation().GetClutch();
  control.DeactivateAI();
  CharacterInputContext input = driver.GetCharacterController().GetInputContext();
  if (input) input.SetVehicleHorn(0);
  escape.m_bParked = true;
  s_Escapes.Insert(escape);
  s_iReverses++;
  return true;
 }

 static void End(BBB_ArmorEscape escape)
 {
  CarControllerComponent car;
  if (escape.m_Vehicle) car = CarControllerComponent.Cast(escape.m_Vehicle.FindComponent(CarControllerComponent));
  // Clear our last inputs before returning control; never write after takeover.
  bool player = escape.m_Vehicle && PlayerAboard(escape.m_Vehicle);
  if (car && car.GetSimulation() && !player)
  {
   car.GetSimulation().SetThrottle(0);
   car.GetSimulation().SetSteering(0);
   car.GetSimulation().SetBreak(0, false);
   car.GetSimulation().SetGear(escape.m_Gear);
   car.GetSimulation().SetClutch(escape.m_Clutch);
   car.SetPersistentHandBrake(escape.m_Handbrake);
  }
  if (escape.m_bParked && escape.m_Control && escape.m_Driver && BBB.IsAlive(escape.m_Driver) && !BBB.PlayerOf(escape.m_Driver) && !escape.m_Driver.GetCharacterController().IsPlayerControlled() && !escape.m_Control.IsAIActivated()) escape.m_Control.ActivateAI();
  escape.m_bParked = false;
 }

 static void Stop()
 {
  if (GetGame()) GetGame().GetCallqueue().Remove(ReleaseSmoke);
  foreach (BBB_ArmorEscape escape : s_Escapes) End(escape);
  s_Escapes.Clear();
  s_Projectiles.Clear();
  s_Vehicles.Clear();
  s_Retry.Clear();
  for (int i = s_SmokePulses.Count() - 1; i >= 0; i--) ReleaseSmoke(s_SmokePulses[i]);
 }

 static void Tick()
 {
  Read();
  if (!s_bOn) { Stop(); return; }
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  s_Vehicles.Clear();
  foreach (SCR_AIGroup group : groups)
  {
   IEntity vehicle = BBB_Transport.CrewedVehicle(group);
   if (vehicle && !BBB_Armour.Aircraft(vehicle) && s_Vehicles.Count() < 64 && !s_Vehicles.Contains(vehicle)) s_Vehicles.Insert(vehicle);
  }
  float now = GetGame().GetWorld().GetWorldTime();
  for (int r = s_Retry.Count() - 1; r >= 0; r--)
   if (!s_Retry.GetKey(r) || !s_Vehicles.Contains(s_Retry.GetKey(r)) || now >= s_Retry.GetElement(r)) s_Retry.RemoveElement(r);
  for (int j = s_Escapes.Count() - 1; j >= 0; j--)
   if (!groups.Contains(s_Escapes[j].m_Group)) { End(s_Escapes[j]); s_Escapes.RemoveOrdered(j); }
 }

 static void Frame()
 {
  if (!s_bOn || !Replication.IsServer()) return;
  float now = GetGame().GetWorld().GetWorldTime();
  // Only the capped set of live launcher projectiles, against cached crewed vehicles.
  for (int p = s_Projectiles.Count() - 1; p >= 0; p--)
  {
   BBB_ATProjectile shot = s_Projectiles[p];
   if (!shot.m_Projectile || now >= shot.m_Expires) { s_Projectiles.RemoveOrdered(p); continue; }
   vector here = shot.m_Projectile.GetOrigin();
   vector motion = here - shot.m_Last;
   if (motion.LengthSq() > 0.01)
   {
    foreach (IEntity vehicle : s_Vehicles)
     if (vehicle && SegmentDistance(vehicle.GetOrigin() + Vector(0, 1, 0), shot.m_Last, here) <= 8) React(vehicle, shot.m_Shooter, shot.m_Last, motion.Normalized());
   }
   shot.m_Last = here;
  }
  for (int i = s_Escapes.Count() - 1; i >= 0; i--)
  {
   BBB_ArmorEscape escape = s_Escapes[i];
   bool valid = escape.m_Vehicle && BBB_OrderLeash.Allows(escape.m_Group, escape.m_Vehicle.GetOrigin(), escape.m_Lease) && Allowed(escape.m_Vehicle, escape.m_Group, escape.m_Utility) && Driver(escape.m_Vehicle) == escape.m_Driver;
   if (valid) valid = escape.m_Control && !escape.m_Control.IsAIActivated();
   if (valid) valid = now - escape.m_Started < 6500 && vector.Distance(escape.m_Start, escape.m_Vehicle.GetOrigin()) < 7;
   if (valid)
   {
    vector pose[4];
    escape.m_Vehicle.GetWorldTransform(pose);
    if (vector.Dot(pose[2], escape.m_Forward) < 0.9063 || vector.Dot(pose[2], escape.m_Direction) > -0.35) valid = false;
   }
   if (valid && now >= escape.m_CheckAt)
   {
    escape.m_CheckAt = now + 200;
    valid = Clearance(escape.m_Vehicle);
    if (escape.m_Vehicle.GetPhysics().GetVelocity().Length() > 3) valid = false;
   }
   if (!valid) { End(escape); s_Escapes.RemoveOrdered(i); continue; }
   CarControllerComponent car = CarControllerComponent.Cast(escape.m_Vehicle.FindComponent(CarControllerComponent));
   VehicleWheeledSimulation sim = car.GetSimulation();
   int neutral = sim.GearboxGearsCount() - sim.GearboxForwardGearsCount() - 1;
   Physics physics = escape.m_Vehicle.GetPhysics();
   if (physics && !physics.IsActive()) physics.SetActive(ActiveState.ACTIVE);
   car.SetPersistentHandBrake(false);
   sim.SetClutch(1);
   sim.SetGear(neutral - 1);
   sim.SetBreak(0, false);
   sim.SetThrottle(0.35);
   sim.SetSteering(escape.m_Steer);
  }
 }
}

modded class SCR_MuzzleEffectComponent
{
 override void OnFired(IEntity effectEntity, BaseMuzzleComponent muzzle, IEntity projectileEntity)
 {
  super.OnFired(effectEntity, muzzle, projectileEntity);
  BBB_ArmorReaction.Fired(muzzle, projectileEntity);
 }
}

modded class BBB_Unstick
{
 static bool ArmorBusy(IEntity vehicle)
 {
  foreach (BBB_Unstick job : s_Active)
   if (job.m_Vehicle == vehicle) return true;
  return false;
 }
}

modded class SCR_OptimizedMuzzleEffectComponent
{
 override void OnFired(IEntity effectEntity, BaseMuzzleComponent muzzle, IEntity projectileEntity)
 {
  super.OnFired(effectEntity, muzzle, projectileEntity);
  BBB_ArmorReaction.Fired(muzzle, projectileEntity);
 }
}
