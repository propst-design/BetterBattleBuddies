// Held guns: gunners stay on the static guns their group holds where it stands (a checkpoint, a
// dug-in position: the host lists them, BBB_Hooks.GetHeldGuns; by default none). Vanilla gets a
// gunner with no target out of a static gun the moment any enemy he knows of (seen up to 100 s
// ago), or just the direction gunfire came from, is outside the gun's arc
// (SCR_AICombatComponent.EvaluateDismountTurret): in an AC: Actual playtest a checkpoint MG
// stood empty every few minutes, often with no enemy near. On a held gun he stays unless an
// enemy he saw in the last FRESH_S is within CLOSE_M (a real flank), and a held gun that stands
// empty for REMOUNT_S is manned again by the group's nearest soldier on foot within
// REMOUNT_REACH. BBB's own static crewing doesn't let a held gun's gunner go when its sector
// goes quiet. Vehicles keep their rules. -bbbHoldGuns 0 restores vanilla.
// Winchester (ACA #154, -bbbGunWinchester 0 off): vanilla never gets an AI gunner off a static gun
// that is out of ammo, so held gunners sat on dead guns instead of fighting with their rifles. A
// held gun with no ammo is not held (no hold, no re-man), and an AI gunner on any static gun of a
// BBB group (held or not, ACA's or a Game Master's) that has been dry for WINCHESTER_S gets off
// with vanilla's get-out and fights on foot. Left alone: players, mortars, LeaveAlone guns, BBB's own
// static crewing (it releases its gunner on "out of ammo" itself), player/GM-priority orders and a
// group on a boarding waypoint (a GM's "get in" would only put him back on).
class BBB_HeldGuns
{
 static const float FRESH_S = 10;
 static const float CLOSE_M = 60;
 static const float REMOUNT_S = 8;
 static const float REMOUNT_REACH = 40;
 static const float WINCHESTER_S = 3;
 static const int WINCHESTER_EVERY = 2; // groups looked at for dry gunners every 2nd tick (staggered)
 static bool s_bOn = true;
 static bool s_bWinchester = true;
 static int s_iRead;
 static int s_iKept;          // tests read these
 static int s_iRemounted;
 static int s_iWinchester;
 protected static int s_iTicks;
 protected static ref map<IEntity, float> s_mEmptySince = new map<IEntity, float>();
 protected static ref map<IEntity, float> s_mLogged = new map<IEntity, float>();
 protected static ref map<IEntity, float> s_mDrySince = new map<IEntity, float>(); // gunner -> first seen on a dry gun

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  if (BBB_Settings.Get("bbbHoldGuns", value)) s_bOn = value != "0";
  if (BBB_Settings.Get("bbbGunWinchester", value)) s_bWinchester = value != "0";
  PrintFormat("[BBB] HELD GUNS enabled=%1 close=%2m fresh=%3s remount=%4s winchester=%5", s_bOn, CLOSE_M, FRESH_S, REMOUNT_S, s_bWinchester);
 }

 static void Stop()
 {
  s_mEmptySince.Clear();
  s_mLogged.Clear();
  s_mDrySince.Clear();
  s_iKept = 0;
  s_iRemounted = 0;
  s_iWinchester = 0;
 }

 static SCR_AIGroup GroupOf(IEntity soldier)
 {
  if (!soldier) return null;
  AIControlComponent control = AIControlComponent.Cast(soldier.FindComponent(AIControlComponent));
  if (!control || !control.GetControlAIAgent()) return null;
  return SCR_AIGroup.Cast(control.GetControlAIAgent().GetParentGroup());
 }

 // This soldier's group holds this static gun (never a vehicle).
 static bool Holds(IEntity soldier, IEntity gun)
 {
  Read();
  if (!s_bOn || !gun || Vehicle.Cast(gun)) return false;
  SCR_AIGroup group = GroupOf(soldier);
  if (!group || BBB.HasPlayer(group)) return false;
  array<IEntity> guns = {};
  BBB.s_Hooks.GetHeldGuns(group, guns);
  return guns.Contains(gun);
 }

 // An enemy this soldier saw in the last FRESH_S within CLOSE_M.
 static bool FreshClose(IEntity soldier)
 {
  SCR_AIUtilityComponent utility = BBB_StaticWeapons.UtilityOf(soldier);
  if (!utility || !utility.m_PerceptionComponent) return false;
  BaseTarget target = utility.m_PerceptionComponent.GetClosestTarget(ETargetCategory.ENEMY, FRESH_S, FRESH_S);
  return target && vector.Distance(target.GetLastSeenPosition(), soldier.GetOrigin()) <= CLOSE_M;
 }

 // Vanilla wants this gunner off this gun: true when he stays.
 static bool Keep(IEntity gunner, IEntity gun)
 {
  if (!gunner || BBB.PlayerOf(gunner) != 0 || !Holds(gunner, gun) || FreshClose(gunner)) return false;
  // A dry gun isn't held: he gets off it (Winchester) rather than sitting on it.
  if (s_bWinchester && Dry(GunSeat(gun))) return false;
  s_iKept++;
  float now = GetGame().GetWorld().GetWorldTime();
  float logged;
  if (!s_mLogged.Find(gunner, logged) || now - logged > 30000)
  {
   s_mLogged.Set(gunner, now);
   PrintFormat("[BBB] HELD GUN %1: gunner kept on his gun (no enemy seen within %2 m in the last %3 s)", BBB.s_Hooks.NameOf(GroupOf(gunner)), CLOSE_M, FRESH_S);
   BBB_Think.Note(gunner, "Static", "stays on the held gun: no enemy close");
  }
  return true;
 }

 // A static gun's gunner seat, manned or not (never a vehicle's).
 static BaseCompartmentSlot GunSeat(IEntity gun)
 {
  if (!gun || Vehicle.Cast(gun) || BBB.Health(gun) <= 0) return null;
  array<BaseCompartmentSlot> seats = {};
  BBB_VehicleHelpers.GetVehicleCompartments(gun, seats, ECompartmentType.TURRET);
  if (seats.IsEmpty()) return null;
  return seats[0];
 }

 // Once a second (BBB's tick): a held gun that stands empty for REMOUNT_S is manned again.
 static void Tick()
 {
  Read();
  if (!s_bOn && !s_bWinchester) return;
  float now = GetGame().GetWorld().GetWorldTime();
  s_iTicks++;
  if (s_iTicks % 60 == 0) PruneDry();
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  array<IEntity> guns = {};
  foreach (int index, SCR_AIGroup group : groups)
  {
   if (!group || BBB.HasPlayer(group)) continue;
   if (s_bWinchester && (index + s_iTicks) % WINCHESTER_EVERY == 0) Winchester(group, now);
   if (!s_bOn) continue;
   BBB.s_Hooks.GetHeldGuns(group, guns);
   foreach (IEntity gun : guns)
   {
    BaseCompartmentSlot seat = GunSeat(gun);
    if (!seat) continue;
    // Manned, or BBB's static crewing has a soldier on his way to it. A dead or unconscious
    // gunner stays in the seat: that's the gun this exists to man again, not a manned one.
    IEntity occupant = seat.GetOccupant();
    if ((occupant && (BBB.PlayerOf(occupant) != 0 || BBB_VehicleHelpers.IsAble(occupant))) || BBB_StaticWeapons.IsClaimed(gun))
    {
     s_mEmptySince.Remove(gun);
     continue;
    }
    float since;
    if (!s_mEmptySince.Find(gun, since))
    {
     s_mEmptySince.Set(gun, now);
     continue;
    }
    if (now - since < REMOUNT_S * 1000 || !BBB_VehicleHelpers.TurretHasAmmo(seat)) continue;
    s_mEmptySince.Set(gun, now); // the next try, if this one doesn't take
    ChimeraCharacter man = NearestOnFoot(group, gun);
    if (!man || FreshClose(man)) continue;
    SCR_AIUtilityComponent utility = BBB_StaticWeapons.UtilityOf(man);
    if (!utility || BBB_OrderIntent.Blocks(utility) || utility.FindActionOfType(SCR_AIGetInVehicle)) continue;
    // The fallen gunner is set down beside the gun so the seat is free (as static crewing does).
    ChimeraCharacter fallen = ChimeraCharacter.Cast(occupant);
    if (fallen && fallen.GetCompartmentAccessComponent())
     fallen.GetCompartmentAccessComponent().GetOutVehicle(EGetOutType.TELEPORT, -1, ECloseDoorAfterActions.LEAVE_OPEN, false);
    utility.AddAction(new SCR_AIGetInVehicle(utility, null, gun, seat, EAICompartmentType.Turret));
    s_iRemounted++;
    PrintFormat("[BBB] HELD GUN %1: gun empty %2 s, a soldier %3 m away mans it again", BBB.s_Hooks.NameOf(group), REMOUNT_S, Math.Round(vector.Distance(man.GetOrigin(), gun.GetOrigin())));
    BBB_Think.Note(man, "Static", "mans the held gun again");
   }
  }
 }

 // A gun's seat with no round loaded or stowed. TurretHasAmmo is also false for a locked seat (a
 // host's locked gun is not a dry one) and for a mortar (shells are loaded by hand from the crew's
 // kit, the tube never holds a magazine): neither counts.
 static bool Dry(BaseCompartmentSlot seat)
 {
  if (!seat || seat.IsGetInLocked()) return false;
  TurretControllerComponent controller = BBB_VehicleHelpers.TurretControllerOf(seat);
  if (!controller || !controller.GetWeaponManager()) return false;
  array<BaseWeaponComponent> weapons = {};
  controller.GetWeaponManager().GetWeapons(weapons);
  if (weapons.IsEmpty()) return false;
  array<BaseMuzzleComponent> muzzles = {};
  foreach (BaseWeaponComponent weapon : weapons)
  {
   weapon.GetMuzzlesList(muzzles);
   foreach (BaseMuzzleComponent muzzle : muzzles)
    if (SCR_MortarMuzzleComponent.Cast(muzzle)) return false;
  }
  return !BBB_VehicleHelpers.TurretHasAmmo(seat);
 }

 // The static gun (never a vehicle) this soldier sits in as gunner, and its seat.
 static IEntity StaticGunOf(ChimeraCharacter man, out BaseCompartmentSlot seat)
 {
  seat = null;
  if (!man || !man.IsInVehicle()) return null;
  CompartmentAccessComponent access = man.GetCompartmentAccessComponent();
  if (!access) return null;
  BaseCompartmentSlot slot = access.GetCompartment();
  if (!slot || slot.GetType() != ECompartmentType.TURRET) return null;
  IEntity gun = slot.GetVehicle();
  if (!gun) gun = slot.GetOwner();
  if (!gun || Vehicle.Cast(gun) || Vehicle.Cast(slot.GetOwner())) return null;
  seat = slot;
  return gun;
 }

 // Winchester: this group's AI gunners on static guns dry for WINCHESTER_S get off and fight on foot.
 static void Winchester(SCR_AIGroup group, float now)
 {
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(group, agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   ChimeraCharacter man = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!man) continue;
   BaseCompartmentSlot seat;
   IEntity gun = StaticGunOf(man, seat);
   if (!gun || !BBB.IsAlive(man) || BBB.PlayerOf(man) != 0 || !Dry(seat))
   {
    s_mDrySince.Remove(man);
    continue;
   }
   CompartmentAccessComponent access = man.GetCompartmentAccessComponent();
   if (access.IsGettingIn() || access.IsGettingOut() || !BBB_VehicleHelpers.IsAble(man)) continue;
   if (BBB.s_Hooks.LeaveAlone(gun) || BBB_StaticWeapons.IsClaimed(gun)) continue;
   float since;
   if (!s_mDrySince.Find(man, since))
   {
    s_mDrySince.Set(man, now);
    continue;
   }
   if (now - since < WINCHESTER_S * 1000) continue;
   SCR_AIUtilityComponent utility = BBB_StaticWeapons.UtilityOf(man);
   if (!utility || utility.FindActionOfType(SCR_AIGetOutVehicle)) continue;
   if (SCR_BoardingWaypoint.Cast(group.GetCurrentWaypoint()) || BBB_OrderIntent.Blocks(utility)) continue;
   utility.AddAction(new SCR_AIGetOutVehicle(utility, null, gun, 0,
    SCR_AIActionBase.PRIORITY_BEHAVIOR_GET_OUT_VEHICLE_HIGH_PRIORITY, SCR_AIActionBase.PRIORITY_LEVEL_NORMAL));
   s_mDrySince.Remove(man);
   s_iWinchester++;
   PrintFormat("[BBB] HELD GUN %1: gun out of ammo (Winchester), the gunner gets off and fights on foot", BBB.s_Hooks.NameOf(group));
   BBB_Think.Note(man, "Static", "gun out of ammo: gets off, fights on foot");
  }
 }

 // Deleted soldiers leave null keys behind (pruned by index).
 protected static void PruneDry()
 {
  for (int i = s_mDrySince.Count() - 1; i >= 0; i--)
   if (!s_mDrySince.GetKey(i)) s_mDrySince.RemoveElement(i);
 }

 protected static ChimeraCharacter NearestOnFoot(SCR_AIGroup group, IEntity gun)
 {
  ChimeraCharacter best;
  float bestDistance = REMOUNT_REACH;
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(group, agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   ChimeraCharacter man = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!man || !BBB.IsAlive(man) || !BBB_VehicleHelpers.IsAble(man) || man.IsInVehicle() || BBB.PlayerOf(man) != 0) continue;
   // Same busy filter as the turret gunner swap: not mid-drag, on a medic run or committed elsewhere.
   if (BBB_Drag.Busy(man) || BBB_Medic.s_mGoingTo.Contains(man) || BBB_Claims.Committed(man)) continue;
   float d = vector.Distance(man.GetOrigin(), gun.GetOrigin());
   if (d >= bestDistance) continue;
   bestDistance = d;
   best = man;
  }
  return best;
 }
}

// The hold, in vanilla's own dismount decision (see BBB_HeldGuns).
modded class SCR_AICombatComponent
{
 override bool DismountTurretCondition(inout vector targetPos, bool targetPosProvided, out float threatPriority)
 {
  bool dismount = super.DismountTurretCondition(targetPos, targetPosProvided, threatPriority);
  if (!dismount || !m_Utility) return dismount;
  IEntity gun = m_CurrentVehicle;
  if (!gun && m_CurrentCompartmentSlot) gun = m_CurrentCompartmentSlot.GetOwner();
  if (!gun) return dismount;
  return !BBB_HeldGuns.Keep(m_Utility.m_OwnerEntity, gun);
 }
}
