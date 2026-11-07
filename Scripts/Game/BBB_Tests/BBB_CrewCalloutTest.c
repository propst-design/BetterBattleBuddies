// Gunners on a forced move, and the crew call-out (-bbbCrewCalloutTest 1, Test-BBB.ps1 -Preset
// crewcallout; 6 Nov 2026). Jacob's scene: an AC: Actual BMP on a Fast move (a player-level waypoint)
// drove past a squad its crew had reported without firing. Fixture: a crewed LAV-25 (US driver, gunner,
// commander) is given a player-level Move GOAL_M ahead; a USSR fire team (hold fire, so the only cue is
// sight) stands ENEMY_M along a lane with clear line of sight, OFF_NOSE_DEG off the LAV's nose, where the
// driver looks. Four arms, a fresh LAV and fire team each, ARM_S each:
//  gate     - blind gunner, call-out on, -bbbGunnerOrders off (the BMP: the move holds BBB back)
//  callout  - blind gunner, call-out on, -bbbGunnerOrders on
//  see-old  - gunner sees,  call-out off, -bbbGunnerOrders off (vanilla: the move outranks his fire)
//  see-new  - gunner sees,  call-out off, -bbbGunnerOrders on
// The blind gunner (vanilla's perception multiplier at 0: the Hot AI trace's BTR-70 gunner who saw none
// of four riflemen his commander saw) is a controlled input so that only his crewmates can see.
// Everything is counted only while the order holds (BBB_OrderIntent.Held: the waypoint not yet
// reached): after it, vanilla's own fire takes over and is not what this is about.
// Measured per arm (physical): turret rounds fired (magazine counts per weapon), call-out fire series,
// samples where a crewmate (driver, commander) saw an enemy and where the gunner had a target in
// sight, all while held; also first round, fire team down, metres driven, and the gunner's behaviour and
// its level every 5 s.
// Judged: seated; blind arms: the crew saw the enemy on the move and the gunner never did, no call-out
// through the old order rule, call-out fire and rounds on the move with the new one; seeing arms: the
// gunner saw the enemy on the move, fired with the new rule, and more than with the old. One run per
// arm: the rounds are a measurement, not a statistic.
class BBB_CrewCalloutTest : BBB_AimTest
{
 static ref BBB_CrewCalloutTest s_Callout;
 static const ResourceName LAV = "{0FBF8F010F81A4E5}Prefabs/Vehicles/Wheeled/LAV25/LAV25.et";
 static const ResourceName WP_MOVE = "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et";
 static const ResourceName WP_DEFEND = "{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et";
 static const int EXPECTED = 14;
 static const float ENEMY_M = 200;
 static const float GOAL_M = 120;
 static const float ARM_S = 40;
 static const float OFF_NOSE_DEG = 12;
 static const int OBSERVER = 22411;

 protected int m_iChecks;
 protected int m_iFailures;
 protected int m_iArm = -1;
 protected ref array<string> m_aArms;
 protected vector m_vFrom;
 protected vector m_vFacing;
 protected vector m_vEnemyAt;
 // The arm under way.
 protected bool m_bBlind;
 protected SCR_AIGroup m_Crew;
 protected SCR_AIGroup m_Enemy;
 protected IEntity m_Lav;
 protected ref array<IEntity> m_aSeated;
 protected ref array<IEntity> m_aFoes;
 protected vector m_vLavStart;
 protected int m_iSamples, m_iHeld, m_iCrewSaw, m_iGunnerSaw, m_iRounds, m_iCallouts, m_iAllRounds;
 protected int m_iCalloutsSeen;
 protected float m_fFirstRound;
 protected ref map<BaseWeaponComponent, int> m_mAmmo;
 // Per arm, while held.
 protected ref map<string, int> m_mCallouts;
 protected ref map<string, int> m_mRounds;
 protected ref map<string, int> m_mCrewSaw;
 protected ref map<string, int> m_mGunnerSaw;

 static void MaybeStartCallout()
 {
  if (s_Callout || !Replication.IsServer() || !System.IsCLIParam("bbbCrewCalloutTest")) return;
  s_Callout = new BBB_CrewCalloutTest();
  GetGame().GetCallqueue().CallLater(s_Callout.Begin, 12000);
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
  PrintFormat("[BBB] CREWCALLOUTTEST CHECK %1 %2 %3", name, res, detail);
 }

 override protected void Begin()
 {
  m_aArms = {"gate", "callout", "see-old", "see-new"};
  m_aSeated = {};
  m_aFoes = {};
  m_mAmmo = new map<BaseWeaponComponent, int>();
  m_mCallouts = new map<string, int>();
  m_mRounds = new map<string, int>();
  m_mCrewSaw = new map<string, int>();
  m_mGunnerSaw = new map<string, int>();
  m_aCheckDistances.Clear();
  m_aCheckDistances.Insert(ENEMY_M * 0.5);
  m_aCheckDistances.Insert(ENEMY_M);
  m_vFrom = BBB.Ground(Vector(1340, 0, 2770));
  vector dir = "0 0 1";
  if (FindSite())
  {
   m_vFrom = BBB.Ground(m_aSiteFrom[0]);
   dir = m_aSiteDir[0];
   LoadLane();
  }
  m_vEnemyAt = BBB.Ground(m_vFrom + dir * ENEMY_M);
  // The LAV faces OFF_NOSE_DEG left of the lane: the fire team is just right of its nose.
  float turn = -OFF_NOSE_DEG * Math.DEG2RAD;
  m_vFacing = Vector(dir[0] * Math.Cos(turn) - dir[2] * Math.Sin(turn), 0, dir[0] * Math.Sin(turn) + dir[2] * Math.Cos(turn));
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers)
  {
   vector mid = m_vFrom + dir * (ENEMY_M * 0.5);
   observers.InsertObserverSP(OBSERVER, mid[0], mid[2], null);
  }
  BBB_CrewCallout.On();
  BBB_OrderIntent.GunnerOrdersOn();
  BBB_HotAI.Read();
  PrintFormat("[BBB] CREWCALLOUTTEST lane at %1, fire team at %2, LAV facing %3, hot vehicles %4", m_vFrom, m_vEnemyAt, m_vFacing, BBB_HotAI.s_bVehicles);
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
  m_bBlind = arm == "gate" || arm == "callout";
  BBB_CrewCallout.s_bOn = m_bBlind;
  BBB_CrewLeader.On();
  BBB_CrewLeader.s_bOn = false; // the call-out on its own (BBB_CrewLeaderTest covers the leader)
  BBB_OrderIntent.s_bGunnerOrders = arm == "callout" || arm == "see-new";
  m_aSeated.Clear();
  m_aFoes.Clear();
  m_mAmmo.Clear();
  m_iSamples = 0; m_iHeld = 0; m_iCrewSaw = 0; m_iGunnerSaw = 0; m_iRounds = 0; m_iCallouts = 0; m_iAllRounds = 0;
  m_fFirstRound = -1;
  m_Crew = SCR_AIGroup.Cast(Spawn(TEAM_US, m_vFrom - m_vFacing * 12, m_vFacing));
  m_Lav = Spawn(LAV, m_vFrom, m_vFacing);
  m_Enemy = SCR_AIGroup.Cast(Spawn(TEAM_USSR, m_vEnemyAt, m_vFacing * -1));
  PrintFormat("[BBB] CREWCALLOUTTEST arm %1: call-out %2, gunners fire on the move %3, gunner blind %4", arm, BBB_CrewCallout.s_bOn, BBB_OrderIntent.s_bGunnerOrders, m_bBlind);
  GetGame().GetCallqueue().CallLater(Seat, 4000);
 }

 protected bool SeatCrew()
 {
  if (!m_Crew || !m_Lav || !m_Enemy) return false;
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
    // Crew only: a hull passenger would be one more pair of eyes.
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
  // The fire team holds fire and stays: sight is the only cue.
  m_Enemy.ActivateAI();
  m_Enemy.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.HOLD_FIRE);
  AIWaypoint stay = AIWaypoint.Cast(Spawn(WP_DEFEND, m_vEnemyAt, "0 0 1"));
  if (stay) m_Enemy.AddWaypoint(stay);
  array<AIAgent> foes = {};
  m_Enemy.GetAgents(foes);
  foreach (AIAgent foe : foes)
  {
   PinDetail(foe);
   if (foe.GetControlledEntity()) m_aFoes.Insert(foe.GetControlledEntity());
  }
  GetGame().GetCallqueue().CallLater(Order, 3000);
 }

 // The blind arms: the gunner recognises nothing (vanilla's perception multiplier at 0).
 protected void Blind()
 {
  if (!m_bBlind || m_aSeated.Count() < 2) return;
  SCR_AIUtilityComponent utility = UtilityOf(m_aSeated[1]);
  if (utility && utility.m_CombatComponent) utility.m_CombatComponent.SetPerceptionFactor(0);
 }

 protected void Order()
 {
  m_Crew.ActivateAI();
  m_Crew.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.FIRE_AT_WILL);
  m_Crew.GetGroupUtilityComponent().EvaluateCombatMode();
  // What AC: Actual does with a Fast move: the waypoint raised to the player level.
  SCR_AIWaypoint move = SCR_AIWaypoint.Cast(Spawn(WP_MOVE, BBB.Ground(m_vFrom + m_vFacing * GOAL_M), m_vFacing));
  if (move)
  {
   move.SetPriorityLevel(SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
   m_Crew.AddWaypoint(move);
  }
  m_vLavStart = m_Lav.GetOrigin();
  m_iCalloutsSeen = BBB_CrewCallout.s_iCallouts;
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
  // A crewmate (driver, commander) recognised an enemy within the last second.
  bool crewSaw;
  array<BaseTarget> targets = {};
  for (int i = 0; i < m_aSeated.Count(); i++)
  {
   if (i == 1 || !m_aSeated[i]) continue;
   PerceptionComponent eyes = PerceptionComponent.Cast(m_aSeated[i].FindComponent(PerceptionComponent));
   if (!eyes) continue;
   targets.Clear();
   eyes.GetTargetsList(targets, ETargetCategory.ENEMY);
   foreach (BaseTarget target : targets)
    if (target && target.GetTimeSinceSeen() < 1) crewSaw = true;
  }
  SCR_AIUtilityComponent utility = UtilityOf(m_aSeated[1]);
  if (utility)
  {
   bool held = BBB_OrderIntent.Held(utility);
   if (held) m_iHeld++;
   if (held && crewSaw) m_iCrewSaw++;
   SCR_AICombatComponent combat = utility.m_CombatComponent;
   bool gunnerSees = combat && combat.GetCurrentTarget() && combat.IsTargetVisible(combat.GetCurrentTarget());
   if (gunnerSees && held) m_iGunnerSaw++;
   int callouts = BBB_CrewCallout.s_iCallouts;
   if (held) m_iCallouts += callouts - m_iCalloutsSeen;
   m_iCalloutsSeen = callouts;
   // Rounds: per weapon, so a switch between the cannon and the coaxial gun isn't counted as fire.
   BaseWeaponComponent weapon;
   if (combat) weapon = combat.GetCurrentWeapon();
   if (weapon && weapon.GetCurrentMagazine())
   {
    int ammo = weapon.GetCurrentMagazine().GetAmmoCount();
    if (m_mAmmo.Contains(weapon) && ammo < m_mAmmo.Get(weapon))
    {
     int fired = m_mAmmo.Get(weapon) - ammo;
     m_iAllRounds += fired;
     if (held) m_iRounds += fired;
     if (m_fFirstRound < 0) m_fFirstRound = t;
    }
    m_mAmmo.Set(weapon, ammo);
   }
   if ((m_iSamples % 10) == 0)
   {
    SCR_AIBehaviorBase doing = utility.GetCurrentBehavior();
    string what = "none";
    if (doing) what = doing.Type().ToString() + " priority " + doing.GetPriority().ToString() + " level " + doing.EvaluatePriorityLevel().ToString();
    PrintFormat("[BBB] CREWCALLOUTTEST t=%1 gunner doing %2; held %3, crew sees %4, gunner sees %5, rounds %6, LAV moved %7 m", t, what, held, crewSaw, gunnerSees, m_iAllRounds, Math.Round(vector.DistanceXZ(m_vLavStart, m_Lav.GetOrigin())));
   }
  }
  if (t < ARM_S) return;
  GetGame().GetCallqueue().Remove(Sample);
  EndArm();
 }

 protected void EndArm()
 {
  string arm = m_aArms[m_iArm];
  int down;
  foreach (IEntity foe : m_aFoes)
   if (Down(foe)) down++;
  float moved = -1;
  if (m_Lav) moved = vector.DistanceXZ(m_vLavStart, m_Lav.GetOrigin());
  m_mCallouts.Set(arm, m_iCallouts);
  m_mRounds.Set(arm, m_iRounds);
  m_mCrewSaw.Set(arm, m_iCrewSaw);
  m_mGunnerSaw.Set(arm, m_iGunnerSaw);
  PrintFormat("[BBB] CREWCALLOUTTEST RESULT %1 on the move (%2 of %3 samples held): call-out fire %4, rounds %5, crew saw %6, gunner saw %7", arm, m_iHeld, m_iSamples, m_iCallouts, m_iRounds, m_iCrewSaw, m_iGunnerSaw);
  PrintFormat("[BBB] CREWCALLOUTTEST RESULT %1 whole arm: rounds %2, first round %3 s, fire team down %4/%5, LAV moved %6 m", arm, m_iAllRounds, m_fFirstRound, down, m_aFoes.Count(), Math.Round(moved));
  // A fresh LAV and fire team for the next arm.
  foreach (IEntity man : m_aSeated)
   if (man) SCR_EntityHelper.DeleteEntityAndChildren(man);
  foreach (IEntity foe : m_aFoes)
   if (foe) SCR_EntityHelper.DeleteEntityAndChildren(foe);
  if (m_Lav) SCR_EntityHelper.DeleteEntityAndChildren(m_Lav);
  if (m_Crew) SCR_EntityHelper.DeleteEntityAndChildren(m_Crew);
  if (m_Enemy) SCR_EntityHelper.DeleteEntityAndChildren(m_Enemy);
  GetGame().GetCallqueue().CallLater(NextArm, 2000);
 }

 protected int Got(map<string, int> values, string arm)
 {
  if (!values.Contains(arm)) return -1;
  return values.Get(arm);
 }

 protected void Judge()
 {
  // The call-out on the move: only the crew can see.
  Check("gate-crew-saw", Got(m_mCrewSaw, "gate") > 0, Got(m_mCrewSaw, "gate").ToString());
  Check("callout-crew-saw", Got(m_mCrewSaw, "callout") > 0, Got(m_mCrewSaw, "callout").ToString());
  Check("blind-gunner-never-saw", Got(m_mGunnerSaw, "gate") == 0 && Got(m_mGunnerSaw, "callout") == 0, string.Format("gate %1, callout %2", Got(m_mGunnerSaw, "gate"), Got(m_mGunnerSaw, "callout")));
  Check("gate-holds-callout", Got(m_mCallouts, "gate") == 0, Got(m_mCallouts, "gate").ToString());
  Check("callout-fire", Got(m_mCallouts, "callout") >= 1, Got(m_mCallouts, "callout").ToString());
  Check("callout-rounds", Got(m_mRounds, "callout") > 0, string.Format("callout %1, gate %2", Got(m_mRounds, "callout"), Got(m_mRounds, "gate")));
  // The gunner's own fire on the move.
  Check("see-old-gunner-saw", Got(m_mGunnerSaw, "see-old") > 0, Got(m_mGunnerSaw, "see-old").ToString());
  Check("see-new-gunner-saw", Got(m_mGunnerSaw, "see-new") > 0, Got(m_mGunnerSaw, "see-new").ToString());
  Check("see-new-rounds", Got(m_mRounds, "see-new") > 0, Got(m_mRounds, "see-new").ToString());
  Check("see-new-more-than-old", Got(m_mRounds, "see-new") > Got(m_mRounds, "see-old"), string.Format("new %1, old %2", Got(m_mRounds, "see-new"), Got(m_mRounds, "see-old")));
  Finish();
 }

 override protected void Finish()
 {
  string verdict = "FAIL";
  if (m_iFailures == 0 && m_iChecks == EXPECTED) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbcrewcallout passed=%2 failed=%3 expected=%4", verdict, m_iChecks - m_iFailures, m_iFailures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_CrewCalloutTest.MaybeStartCallout();
 }
}
