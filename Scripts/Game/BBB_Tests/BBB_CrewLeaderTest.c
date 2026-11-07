// Crew leader (-bbbCrewLeaderTest 1, Test-BBB.ps1 -Preset crewleader; 7 Nov 2026). Jacob: the commander
// should tell the gunner where the enemy is and to shoot. Fixture: a crewed LAV-25 (US driver, gunner,
// commander) parked on a clear lane, no orders. A lone USSR rifleman (the decoy: damage off, so killing
// him never ends the distraction) stands DECOY_M out on the nose and shoots at the LAV (harmless to its
// armoured crew); a USSR rifle squad (the group, holding fire) stands GROUP_M out, about 15 degrees
// right. Vanilla's picker scores a man by range, plus a bonus for one endangering him, so the gunner
// takes the decoy and keeps shooting him: "ten men at one o'clock" ignored for one man plinking.
// (Runs 2-4 had the decoy hold fire too: whether vanilla took him depended on whether the gunner
// happened to see him, decoy samples 40 / 4 of 90.)
// Arms, a fresh LAV and enemies each, ARM_S each:
//  off   - crew leader off (0.1.9: the call-out only acts when the gunner sees nothing)
//  on    - crew leader on
//  blind - crew leader on, the gunner blind (vanilla perception multiplier 0): the leader's target from
//          the crew's eyes alone
// Measured per arm (physical): samples the gunner's selected target was the decoy / a group man; turret
// rounds, and those fired with the gun pointing within AT_DEG of the group (its bearing from the LAV,
// nearer the group than the decoy); group men down at the end (logged: coax fire at 230 m rarely downs
// anyone in 45 s, first run 0/4 in every arm); the leader's orders, switches, overrides, blind fire series, call-outs.
// Judged: seated (3); off: the decoy takes a real share of the gunner's time (the fixture's premise) and
// the leader does nothing; on: the leader's target was the group, he switched the gunner or laid him on
// it, the gunner spent less time on the decoy and fired a larger share of his rounds at the group; blind: fire series
// on the leader's target and rounds fired. One run per arm: a measurement, not a statistic.
class BBB_CrewLeaderTest : BBB_AimTest
{
 static ref BBB_CrewLeaderTest s_Leader;
 static const ResourceName LAV = "{0FBF8F010F81A4E5}Prefabs/Vehicles/Wheeled/LAV25/LAV25.et";
 static const ResourceName RU_RIFLE = "{DCB41B3746FDD1BE}Prefabs/Characters/Factions/OPFOR/USSR_Army/Character_USSR_Rifleman.et";
 static const ResourceName SQUAD_USSR = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const ResourceName WP_DEFEND = "{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et";
 static const int EXPECTED = 11;
 static const float DECOY_M = 70;
 static const float DECOY_SIDE_M = 0;
 static const float GROUP_M = 230;
 static const float GROUP_SIDE_M = 60;
 static const float ARM_S = 45;
 static const float AT_DEG = 7; // a round counts as fired at the group when the gun points this close to it
 static const int OBSERVER = 22412;

 protected int m_iChecks;
 protected int m_iFailures;
 protected int m_iArm = -1;
 protected ref array<string> m_aArms;
 protected vector m_vFrom;
 protected vector m_vDir;
 protected vector m_vDecoyAt;
 protected vector m_vGroupAt;
 // The arm under way.
 protected bool m_bBlind;
 protected SCR_AIGroup m_Crew;
 protected SCR_AIGroup m_DecoyTeam;
 protected SCR_AIGroup m_Group;
 protected IEntity m_Lav;
 protected IEntity m_Decoy;
 protected ref array<IEntity> m_aSeated;
 protected ref array<IEntity> m_aGroup;
 protected ref map<BaseWeaponComponent, int> m_mAmmo;
 protected int m_iSamples, m_iOnDecoy, m_iOnGroup, m_iLeaderOnGroup, m_iRounds, m_iAtGroup;
 protected int m_iOrders0, m_iSwitches0, m_iOverrides0, m_iBlind0, m_iSaid0;
 // Per arm.
 protected ref map<string, int> m_mOnDecoy;
 protected ref map<string, int> m_mOnGroup;
 protected ref map<string, int> m_mLeaderOnGroup;
 protected ref map<string, int> m_mRounds;
 protected ref map<string, int> m_mAtGroup;
 protected ref map<string, int> m_mDown;
 protected ref map<string, int> m_mCommands;
 protected ref map<string, int> m_mBlindFires;

 static void MaybeStartLeader()
 {
  if (s_Leader || !Replication.IsServer() || !System.IsCLIParam("bbbCrewLeaderTest")) return;
  s_Leader = new BBB_CrewLeaderTest();
  GetGame().GetCallqueue().CallLater(s_Leader.Begin, 12000);
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
  PrintFormat("[BBB] CREWLEADERTEST CHECK %1 %2 %3", name, res, detail);
 }

 // A point ahead along the lane, side metres to its right.
 protected vector Along(float ahead, float side)
 {
  vector right = Vector(m_vDir[2], 0, -m_vDir[0]);
  return BBB.Ground(m_vFrom + m_vDir * ahead + right * side);
 }

 override protected void Begin()
 {
  m_aArms = {"off", "on", "blind"};
  m_aSeated = {};
  m_aGroup = {};
  m_mAmmo = new map<BaseWeaponComponent, int>();
  m_mOnDecoy = new map<string, int>();
  m_mOnGroup = new map<string, int>();
  m_mLeaderOnGroup = new map<string, int>();
  m_mRounds = new map<string, int>();
  m_mAtGroup = new map<string, int>();
  m_mDown = new map<string, int>();
  m_mCommands = new map<string, int>();
  m_mBlindFires = new map<string, int>();
  m_aCheckDistances.Clear();
  m_aCheckDistances.Insert(DECOY_M);
  m_aCheckDistances.Insert(GROUP_M);
  m_vFrom = BBB.Ground(Vector(1340, 0, 2770));
  m_vDir = "0 0 1";
  if (FindSite())
  {
   m_vFrom = BBB.Ground(m_aSiteFrom[0]);
   m_vDir = m_aSiteDir[0];
   LoadLane();
  }
  m_vDecoyAt = Along(DECOY_M, DECOY_SIDE_M);
  m_vGroupAt = Along(GROUP_M, GROUP_SIDE_M);
  // Mirror the sides if the right-hand spot has no line of sight.
  if (!Visible(m_vFrom, m_vGroupAt) || !Visible(m_vFrom, m_vDecoyAt))
  {
   m_vDecoyAt = Along(DECOY_M, -DECOY_SIDE_M);
   m_vGroupAt = Along(GROUP_M, -GROUP_SIDE_M);
  }
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers)
  {
   vector mid = Along(GROUP_M * 0.5, 0);
   observers.InsertObserverSP(OBSERVER, mid[0], mid[2], null);
  }
  BBB_CrewLeader.On();
  BBB_CrewCallout.On();
  BBB_HotAI.Read();
  PrintFormat("[BBB] CREWLEADERTEST lane at %1 heading %2, decoy at %3 (sight %4), group at %5 (sight %6), hot vehicles %7, call-out %8", m_vFrom, m_vDir, m_vDecoyAt, Visible(m_vFrom, m_vDecoyAt), m_vGroupAt, Visible(m_vFrom, m_vGroupAt), BBB_HotAI.s_bVehicles, BBB_CrewCallout.s_bOn);
  NextArm();
 }

 protected void NextArm()
 {
  m_iArm++;
  if (m_iArm >= m_aArms.Count())
  {
   Judge();
   return;
  }
  string arm = m_aArms[m_iArm];
  m_bBlind = arm == "blind";
  BBB_CrewLeader.s_bOn = arm != "off";
  m_aSeated.Clear();
  m_aGroup.Clear();
  m_mAmmo.Clear();
  m_iSamples = 0; m_iOnDecoy = 0; m_iOnGroup = 0; m_iLeaderOnGroup = 0; m_iRounds = 0; m_iAtGroup = 0;
  // Three riflemen for driver, gunner and commander (no fourth man to delete later).
  SCR_AIGroup.IgnoreSpawning(true);
  m_Crew = SCR_AIGroup.Cast(Spawn(TEAM_US, m_vFrom - m_vDir * 12, m_vDir));
  SCR_AIGroup.IgnoreSpawning(false);
  if (m_Crew)
  {
   m_Crew.m_aUnitPrefabSlots.Clear();
   vector side = Vector(m_vDir[2], 0, -m_vDir[0]);
   for (int c = 0; c < 3; c++)
   {
    m_Crew.m_aUnitPrefabSlots.Insert(RIFLE_US);
    IEntity crewman = Spawn(RIFLE_US, m_vFrom - m_vDir * 12 + side * (c * 2 - 2), m_vDir);
    if (crewman) m_Crew.AddAIEntityToGroup(crewman);
   }
  }
  m_Lav = Spawn(LAV, m_vFrom, m_vDir);
  // The decoy: an empty group with one rifleman (a trimmed team left the crew's group a contact report
  // on a deleted man: vanilla script error, third run).
  SCR_AIGroup.IgnoreSpawning(true);
  m_DecoyTeam = SCR_AIGroup.Cast(Spawn(TEAM_USSR, m_vDecoyAt, m_vDir * -1));
  SCR_AIGroup.IgnoreSpawning(false);
  if (m_DecoyTeam)
  {
   m_DecoyTeam.m_aUnitPrefabSlots.Clear();
   m_DecoyTeam.m_aUnitPrefabSlots.Insert(RU_RIFLE);
   m_Decoy = Spawn(RU_RIFLE, m_vDecoyAt, m_vDir * -1);
   if (m_Decoy) m_DecoyTeam.AddAIEntityToGroup(m_Decoy);
  }
  m_Group = SCR_AIGroup.Cast(Spawn(SQUAD_USSR, m_vGroupAt, m_vDir * -1));
  PrintFormat("[BBB] CREWLEADERTEST arm %1: crew leader %2, gunner blind %3", arm, BBB_CrewLeader.s_bOn, m_bBlind);
  GetGame().GetCallqueue().CallLater(Seat, 4000);
 }

 protected bool SeatCrew()
 {
  if (!m_Crew || !m_Lav || !m_DecoyTeam || !m_Group) return false;
  array<BaseCompartmentSlot> pilots = {};
  BBB_VehicleHelpers.GetVehicleCompartments(m_Lav, pilots, ECompartmentType.PILOT);
  array<BaseCompartmentSlot> turrets = {};
  BBB_VehicleHelpers.GetVehicleCompartments(m_Lav, turrets, ECompartmentType.TURRET);
  array<BaseCompartmentSlot> cargo = {};
  BBB_VehicleHelpers.GetVehicleCompartments(m_Lav, cargo, ECompartmentType.CARGO);
  BaseCompartmentSlot commander;
  foreach (BaseCompartmentSlot slot : cargo)
   if (BBB_Transport.CommanderSeat(slot)) { commander = slot; break; }
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(m_Crew, agents);
  if (pilots.IsEmpty() || turrets.IsEmpty() || !commander || agents.Count() < 3) return false;
  array<BaseCompartmentSlot> seats = {pilots[0], turrets[0], commander};
  bool accepted = true;
  for (int i = 0; i < agents.Count(); i++)
  {
   ChimeraCharacter man = ChimeraCharacter.Cast(agents[i].GetControlledEntity());
   if (!man) return false;
   if (i >= 3)
   {
    // Crew only: a squadmate on foot would be one more pair of eyes for the leader.
    SCR_EntityHelper.DeleteEntityAndChildren(man);
    continue;
   }
   PinDetail(agents[i]);
   m_aSeated.Insert(man);
   if (!man.GetCompartmentAccessComponent().GetInVehicle(m_Lav, seats[i], true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false)) accepted = false;
  }
  SCR_AIGroupUtilityComponent utility = SCR_AIGroupUtilityComponent.Cast(m_Crew.FindComponent(SCR_AIGroupUtilityComponent));
  SCR_AIVehicleUsageComponent usage = SCR_AIVehicleUsageComponent.Cast(m_Lav.FindComponent(SCR_AIVehicleUsageComponent));
  if (utility && usage) utility.AddUsableVehicle(usage);
  return accepted;
 }

 protected void Seat()
 {
  bool seated = SeatCrew();
  Check(m_aArms[m_iArm] + "-seated", seated, "");
  if (!seated)
  {
   EndArm();
   return;
  }
  Blind();
  // The decoy: damage off.
  array<AIAgent> agents = {};
  m_DecoyTeam.GetAgents(agents);
  foreach (AIAgent decoy : agents) PinDetail(decoy);
  ChimeraCharacter body = ChimeraCharacter.Cast(m_Decoy);
  if (body) body.GetDamageManager().EnableDamageHandling(false);
  Hold(m_DecoyTeam, m_vDecoyAt);
  m_DecoyTeam.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.FIRE_AT_WILL);
  agents.Clear();
  m_Group.GetAgents(agents);
  foreach (AIAgent foe : agents)
  {
   PinDetail(foe);
   if (foe.GetControlledEntity()) m_aGroup.Insert(foe.GetControlledEntity());
  }
  Hold(m_Group, m_vGroupAt);
  GetGame().GetCallqueue().CallLater(Start, 3000);
 }

 // Hold fire and stay: sight is the only cue.
 protected void Hold(SCR_AIGroup group, vector at)
 {
  group.ActivateAI();
  group.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.HOLD_FIRE);
  AIWaypoint stay = AIWaypoint.Cast(Spawn(WP_DEFEND, at, "0 0 1"));
  if (stay) group.AddWaypoint(stay);
 }

 protected void Blind()
 {
  if (!m_bBlind || m_aSeated.Count() < 2) return;
  SCR_AIUtilityComponent utility = UtilityOf(m_aSeated[1]);
  if (utility && utility.m_CombatComponent) utility.m_CombatComponent.SetPerceptionFactor(0);
 }

 protected void Start()
 {
  m_Crew.ActivateAI();
  m_Crew.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.FIRE_AT_WILL);
  m_Crew.GetGroupUtilityComponent().EvaluateCombatMode();
  m_iOrders0 = BBB_CrewLeader.s_iOrders;
  m_iSwitches0 = BBB_CrewLeader.s_iSwitches;
  m_iOverrides0 = BBB_CrewLeader.s_iOverrides;
  m_iBlind0 = BBB_CrewLeader.s_iBlindFires;
  m_iSaid0 = BBB_CrewLeader.s_iSaid;
  GetGame().GetCallqueue().CallLater(Sample, 500, true);
 }

 protected SCR_AIUtilityComponent UtilityOf(IEntity man)
 {
  if (!man) return null;
  AIControlComponent control = AIControlComponent.Cast(man.FindComponent(AIControlComponent));
  if (!control || !control.GetControlAIAgent()) return null;
  return SCR_AIUtilityComponent.Cast(control.GetControlAIAgent().FindComponent(SCR_AIUtilityComponent));
 }

 protected void Sample()
 {
  m_iSamples++;
  float t = 0.5 * m_iSamples;
  Blind();
  SCR_AIUtilityComponent utility = UtilityOf(m_aSeated[1]);
  if (utility && utility.m_CombatComponent)
  {
   SCR_AICombatComponent combat = utility.m_CombatComponent;
   BaseTarget current = combat.GetCurrentTarget();
   IEntity on;
   if (current) on = current.GetTargetEntity();
   if (on && on == m_Decoy) m_iOnDecoy++;
   if (on && m_aGroup.Contains(on)) m_iOnGroup++;
   // The leader's target is the group (any of its men in his order).
   foreach (IEntity man : m_aGroup)
   {
    if (!BBB_CrewLeader.OnTarget(m_Lav, man)) continue;
    m_iLeaderOnGroup++;
    break;
   }
   // Rounds: per weapon, so a switch between the cannon and the coaxial gun isn't counted as fire.
   BaseWeaponComponent weapon = combat.GetCurrentWeapon();
   if (weapon && weapon.GetCurrentMagazine())
   {
    int ammo = weapon.GetCurrentMagazine().GetAmmoCount();
    if (m_mAmmo.Contains(weapon) && ammo < m_mAmmo.Get(weapon))
    {
     int fired = m_mAmmo.Get(weapon) - ammo;
     m_iRounds += fired;
     if (GunOff(weapon, m_vGroupAt) <= AT_DEG && GunOff(weapon, m_vGroupAt) < GunOff(weapon, m_vDecoyAt)) m_iAtGroup += fired;
    }
    m_mAmmo.Set(weapon, ammo);
   }
   if ((m_iSamples % 10) == 0)
   {
    SCR_AIBehaviorBase doing = utility.GetCurrentBehavior();
    string what = "none";
    if (doing) what = doing.Type().ToString() + " priority " + doing.GetPriority().ToString();
    string target = "none";
    if (on && on == m_Decoy) target = "decoy";
    else if (on && m_aGroup.Contains(on)) target = "group";
    else if (on) target = "other";
    bool visible = current && combat.IsTargetVisible(current);
    float offGroup = -1;
    float offDecoy = -1;
    if (weapon)
    {
     offGroup = GunOff(weapon, m_vGroupAt);
     offDecoy = GunOff(weapon, m_vDecoyAt);
    }
    PrintFormat("[BBB] CREWLEADERTEST t=%1 gunner doing %2, target %3 (visible %4); gun %5 deg off the group, %6 off the decoy; rounds %7 (%8 at the group), group down %9", t, what, target, visible, Math.Round(offGroup), Math.Round(offDecoy), m_iRounds, m_iAtGroup, GroupDown());
   }
  }
  if (t < ARM_S) return;
  GetGame().GetCallqueue().Remove(Sample);
  EndArm();
 }

 // Degrees between the gun's barrel (its entity's forward axis) and the bearing from the gun to a spot, flat.
 protected float GunOff(BaseWeaponComponent weapon, vector spot)
 {
  IEntity gun = weapon.GetOwner();
  if (!gun) return 180;
  vector m[4];
  gun.GetWorldTransform(m);
  vector to = spot - m[3];
  float aim = Math.Atan2(m[2][0], m[2][2]) * Math.RAD2DEG;
  float want = Math.Atan2(to[0], to[2]) * Math.RAD2DEG;
  float off = Math.AbsFloat(aim - want);
  if (off > 180) off = 360 - off;
  return off;
 }

 protected int GroupDown()
 {
  int down;
  foreach (IEntity foe : m_aGroup)
   if (Down(foe)) down++;
  return down;
 }

 protected void EndArm()
 {
  string arm = m_aArms[m_iArm];
  int down = GroupDown();
  int commands = (BBB_CrewLeader.s_iSwitches - m_iSwitches0) + (BBB_CrewLeader.s_iOverrides - m_iOverrides0);
  int blindFires = BBB_CrewLeader.s_iBlindFires - m_iBlind0;
  m_mOnDecoy.Set(arm, m_iOnDecoy);
  m_mOnGroup.Set(arm, m_iOnGroup);
  m_mLeaderOnGroup.Set(arm, m_iLeaderOnGroup);
  m_mRounds.Set(arm, m_iRounds);
  m_mAtGroup.Set(arm, m_iAtGroup);
  m_mDown.Set(arm, down);
  m_mCommands.Set(arm, commands);
  m_mBlindFires.Set(arm, blindFires);
  PrintFormat("[BBB] CREWLEADERTEST RESULT %1 (%2 samples): gunner on decoy %3, on group %4; leader's target the group %5; rounds %6, at the group %7; group down %8/%9", arm, m_iSamples, m_iOnDecoy, m_iOnGroup, m_iLeaderOnGroup, m_iRounds, m_iAtGroup, down, m_aGroup.Count());
  PrintFormat("[BBB] CREWLEADERTEST RESULT %1 leader: orders %2, switches %3, overrides %4, blind fire %5, call-outs %6", arm, BBB_CrewLeader.s_iOrders - m_iOrders0, BBB_CrewLeader.s_iSwitches - m_iSwitches0, BBB_CrewLeader.s_iOverrides - m_iOverrides0, blindFires, BBB_CrewLeader.s_iSaid - m_iSaid0);
  foreach (IEntity man : m_aSeated)
   if (man) SCR_EntityHelper.DeleteEntityAndChildren(man);
  foreach (IEntity foe : m_aGroup)
   if (foe) SCR_EntityHelper.DeleteEntityAndChildren(foe);
  if (m_Decoy) SCR_EntityHelper.DeleteEntityAndChildren(m_Decoy);
  if (m_Lav) SCR_EntityHelper.DeleteEntityAndChildren(m_Lav);
  if (m_Crew) SCR_EntityHelper.DeleteEntityAndChildren(m_Crew);
  if (m_DecoyTeam) SCR_EntityHelper.DeleteEntityAndChildren(m_DecoyTeam);
  if (m_Group) SCR_EntityHelper.DeleteEntityAndChildren(m_Group);
  GetGame().GetCallqueue().CallLater(NextArm, 2000);
 }

 protected int Got(map<string, int> values, string arm)
 {
  if (!values.Contains(arm)) return -1;
  return values.Get(arm);
 }

 // Share of an arm's rounds fired at the group (0 when it fired none).
 protected float Share(map<string, int> part, map<string, int> whole, string arm)
 {
  float all = Got(whole, arm);
  if (all <= 0) return 0;
  float got = Got(part, arm);
  return got / all;
 }

 protected void Judge()
 {
  // The premise: without the leader the decoy takes a real share of the gunner's time.
  Check("off-distracted", Got(m_mOnDecoy, "off") >= 15, string.Format("decoy %1, group %2", Got(m_mOnDecoy, "off"), Got(m_mOnGroup, "off")));
  Check("off-leader-silent", Got(m_mCommands, "off") == 0 && Got(m_mLeaderOnGroup, "off") == 0, string.Format("commands %1, leader on group %2", Got(m_mCommands, "off"), Got(m_mLeaderOnGroup, "off")));
  // The leader picks the group and gets the gunner on it.
  Check("on-leader-target-group", Got(m_mLeaderOnGroup, "on") > 0, Got(m_mLeaderOnGroup, "on").ToString());
  Check("on-commanded", Got(m_mCommands, "on") >= 1, Got(m_mCommands, "on").ToString());
  Check("on-gunner-less-on-decoy", Got(m_mOnDecoy, "on") < Got(m_mOnDecoy, "off"), string.Format("on %1, off %2 (on the group: on %3, off %4)", Got(m_mOnDecoy, "on"), Got(m_mOnDecoy, "off"), Got(m_mOnGroup, "on"), Got(m_mOnGroup, "off")));
  Check("on-more-rounds-at-group", Share(m_mAtGroup, m_mRounds, "on") > Share(m_mAtGroup, m_mRounds, "off"), string.Format("on %1 of %2, off %3 of %4 (group down: on %5, off %6)", Got(m_mAtGroup, "on"), Got(m_mRounds, "on"), Got(m_mAtGroup, "off"), Got(m_mRounds, "off"), Got(m_mDown, "on"), Got(m_mDown, "off")));
  // The blind gunner: the leader's target from the crew's eyes alone.
  Check("blind-fire", Got(m_mBlindFires, "blind") >= 1, Got(m_mBlindFires, "blind").ToString());
  Check("blind-rounds", Got(m_mRounds, "blind") > 0, Got(m_mRounds, "blind").ToString());
  Finish();
 }

 override protected void Finish()
 {
  string verdict = "FAIL";
  if (m_iFailures == 0 && m_iChecks == EXPECTED) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbcrewleader passed=%2 failed=%3 expected=%4", verdict, m_iChecks - m_iFailures, m_iFailures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_CrewLeaderTest.MaybeStartLeader();
 }
}
