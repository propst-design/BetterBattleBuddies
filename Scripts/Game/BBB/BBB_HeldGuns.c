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
class BBB_HeldGuns
{
 static const float FRESH_S = 10;
 static const float CLOSE_M = 60;
 static const float REMOUNT_S = 8;
 static const float REMOUNT_REACH = 40;
 static bool s_bOn = true;
 static int s_iRead;
 static int s_iKept;          // tests read these
 static int s_iRemounted;
 protected static ref map<IEntity, float> s_mEmptySince = new map<IEntity, float>();
 protected static ref map<IEntity, float> s_mLogged = new map<IEntity, float>();

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  if (BBB_Settings.Get("bbbHoldGuns", value)) s_bOn = value != "0";
  PrintFormat("[BBB] HELD GUNS enabled=%1 close=%2m fresh=%3s remount=%4s", s_bOn, CLOSE_M, FRESH_S, REMOUNT_S);
 }

 static void Stop()
 {
  s_mEmptySince.Clear();
  s_mLogged.Clear();
  s_iKept = 0;
  s_iRemounted = 0;
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
  if (!s_bOn) return;
  float now = GetGame().GetWorld().GetWorldTime();
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  array<IEntity> guns = {};
  foreach (SCR_AIGroup group : groups)
  {
   if (!group || BBB.HasPlayer(group)) continue;
   BBB.s_Hooks.GetHeldGuns(group, guns);
   foreach (IEntity gun : guns)
   {
    BaseCompartmentSlot seat = GunSeat(gun);
    if (!seat) continue;
    // Manned, or BBB's static crewing has a soldier on his way to it.
    if (seat.GetOccupant() || BBB_StaticWeapons.IsClaimed(gun))
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
    utility.AddAction(new SCR_AIGetInVehicle(utility, null, gun, seat, EAICompartmentType.Turret));
    s_iRemounted++;
    PrintFormat("[BBB] HELD GUN %1: gun empty %2 s, a soldier %3 m away mans it again", BBB.s_Hooks.NameOf(group), REMOUNT_S, Math.Round(vector.Distance(man.GetOrigin(), gun.GetOrigin())));
    BBB_Think.Note(man, "Static", "mans the held gun again");
   }
  }
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
