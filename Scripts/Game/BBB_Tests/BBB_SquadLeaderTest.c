// Squad leader, stages 1-2 (-bbbSquadLeaderTest 1, Test-BBB.ps1 -Preset squadleader; 7 Nov 2026). Fixture:
// a US rifle squad holds a spot on a clear lane (Defend waypoint), facing up the lane. A USSR fire team
// (front) stands FRONT_M up the lane; another (flank) stands FLANK_M out, FLANK_DEG to the side. Everyone
// is invulnerable and both USSR teams hold fire, so the fight never changes shape: only where the
// squad looks and shoots does.
// Arms, a fresh squad and enemies each, ARM_S each: off (cue and fire split off) and on.
// Measured per arm (physical), every 0.5 s: members whose selected target is a front / flank man;
// samples with at least two men on each ("both engaged"); rounds fired by each man (magazine counts
// per weapon) with his gun pointing within AT_DEG of the front or the flank team; when the first man
// saw a flank man and when half the squad had (the cue's job); the leader's cues, splits, switches.
// Judged: spawned (2); off: the leader does nothing; on: he split the squad and cued looks, both teams
// were engaged in more samples than with him off, the squad's rounds were more evenly shared between
// the two teams (the smaller share of the two, on > off), and half the squad knew the flank team no
// later than with him off. One run per arm: a measurement, not a statistic.
class BBB_SquadLeaderTest : BBB_AimTest
{
 static ref BBB_SquadLeaderTest s_SquadLeader;
 static const ResourceName SQUAD_US = "{DDF3799FA1387848}Prefabs/Groups/BLUFOR/Group_US_RifleSquad.et";
 static const ResourceName WP_DEFEND = "{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et";
 static const int EXPECTED = 8;
 static const float FRONT_M = 180;
 static const float FLANK_M = 130;
 static const float FLANK_DEG = 50;
 static const float ARM_S = 50;
 static const float AT_DEG = 10;
 static const int OBSERVER = 22413;

 protected int m_iChecks;
 protected int m_iFailures;
 protected int m_iArm = -1;
 protected ref array<string> m_aArms;
 protected vector m_vFrom;
 protected vector m_vDir;
 protected vector m_vFrontAt;
 protected vector m_vFlankAt;
 protected SCR_AIGroup m_Squad;
 protected SCR_AIGroup m_Front;
 protected SCR_AIGroup m_Flank;
 protected ref array<IEntity> m_aMen;
 protected ref array<IEntity> m_aFront;
 protected ref array<IEntity> m_aFlank;
 protected ref map<BaseWeaponComponent, int> m_mAmmo;
 protected int m_iSamples, m_iOnFront, m_iOnFlank, m_iBoth, m_iRounds, m_iAtFront, m_iAtFlank;
 protected float m_fFirstSawFlank, m_fHalfKnewFlank;
 protected int m_iCues0, m_iSplits0, m_iSwitches0, m_iLaid0;
 protected ref map<string, int> m_mBoth;
 protected ref map<string, float> m_mBalance;
 protected ref map<string, float> m_mKnowDelay;
 protected ref map<string, int> m_mCues;
 protected ref map<string, int> m_mSplits;
 protected ref map<string, int> m_mSwitches;

 static void MaybeStartSquadLeader()
 {
  if (s_SquadLeader || !Replication.IsServer() || !System.IsCLIParam("bbbSquadLeaderTest")) return;
  s_SquadLeader = new BBB_SquadLeaderTest();
  GetGame().GetCallqueue().CallLater(s_SquadLeader.Begin, 12000);
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
  PrintFormat("[BBB] SQUADLEADERTEST CHECK %1 %2 %3", name, res, detail);
 }

 protected vector Turned(vector dir, float degrees)
 {
  float a = degrees * Math.DEG2RAD;
  return Vector(dir[0] * Math.Cos(a) + dir[2] * Math.Sin(a), 0, -dir[0] * Math.Sin(a) + dir[2] * Math.Cos(a));
 }

 override protected void Begin()
 {
  m_aArms = {"off", "on"};
  m_aMen = {};
  m_aFront = {};
  m_aFlank = {};
  m_mAmmo = new map<BaseWeaponComponent, int>();
  m_mBoth = new map<string, int>();
  m_mBalance = new map<string, float>();
  m_mKnowDelay = new map<string, float>();
  m_mCues = new map<string, int>();
  m_mSplits = new map<string, int>();
  m_mSwitches = new map<string, int>();
  m_aCheckDistances.Clear();
  m_aCheckDistances.Insert(FLANK_M);
  m_aCheckDistances.Insert(FRONT_M);
  m_vFrom = BBB.Ground(Vector(1340, 0, 2770));
  m_vDir = "0 0 1";
  if (FindSite())
  {
   m_vFrom = BBB.Ground(m_aSiteFrom[0]);
   m_vDir = m_aSiteDir[0];
   LoadLane();
  }
  m_vFrontAt = BBB.Ground(m_vFrom + m_vDir * FRONT_M);
  // The flank spot: the first one the squad can see, 45-65 degrees off either side (run 1: the fixed
  // spot sat in a hollow; run 2, 75 degrees on a hill: at most 2 of 7 ever noticed a silent team that
  // far to the side of a squad busy with the front).
  m_vFlankAt = BBB.Ground(m_vFrom + Turned(m_vDir, FLANK_DEG) * FLANK_M);
  array<float> tries = {50, -50, 55, -55, 45, -45, 60, -60, 65, -65};
  foreach (float angle : tries)
  {
   vector spot = BBB.Ground(m_vFrom + Turned(m_vDir, angle) * FLANK_M);
   if (!Visible(m_vFrom, spot) || !Visible(m_vFrom + Turned(m_vDir, 90) * 4, spot)) continue;
   m_vFlankAt = spot;
   break;
  }
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers) observers.InsertObserverSP(OBSERVER, m_vFrom[0], m_vFrom[2], null);
  BBB_SquadLeader.Read();
  PrintFormat("[BBB] SQUADLEADERTEST lane at %1 heading %2, front team at %3 (sight %4), flank team at %5 (sight %6)", m_vFrom, m_vDir, m_vFrontAt, Visible(m_vFrom, m_vFrontAt), m_vFlankAt, Visible(m_vFrom, m_vFlankAt));
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
  bool on = arm == "on";
  BBB_SquadLeader.s_bCue = on;
  BBB_SquadLeader.s_bFire = on;
  m_aMen.Clear();
  m_aFront.Clear();
  m_aFlank.Clear();
  m_mAmmo.Clear();
  m_iSamples = 0; m_iOnFront = 0; m_iOnFlank = 0; m_iBoth = 0; m_iRounds = 0; m_iAtFront = 0; m_iAtFlank = 0;
  m_fFirstSawFlank = -1;
  m_fHalfKnewFlank = -1;
  m_Squad = SCR_AIGroup.Cast(Spawn(SQUAD_US, m_vFrom, m_vDir));
  m_Front = SCR_AIGroup.Cast(Spawn(TEAM_USSR, m_vFrontAt, m_vDir * -1));
  m_Flank = SCR_AIGroup.Cast(Spawn(TEAM_USSR, m_vFlankAt, (m_vFrom - m_vFlankAt).Normalized()));
  PrintFormat("[BBB] SQUADLEADERTEST arm %1: cue %2, fire split %3", arm, BBB_SquadLeader.s_bCue, BBB_SquadLeader.s_bFire);
  GetGame().GetCallqueue().CallLater(Ready, 4000);
 }

 // Everyone invulnerable; the USSR teams hold fire and stay; the squad holds its spot, fire at will.
 protected void Ready()
 {
  bool spawned = Collect(m_Squad, m_aMen) >= 6 && Collect(m_Front, m_aFront) >= 3 && Collect(m_Flank, m_aFlank) >= 3;
  Check(m_aArms[m_iArm] + "-spawned", spawned, string.Format("squad %1, front %2, flank %3", m_aMen.Count(), m_aFront.Count(), m_aFlank.Count()));
  if (!spawned)
  {
   EndArm();
   return;
  }
  Stay(m_Front, m_vFrontAt, EAIGroupCombatMode.HOLD_FIRE);
  Stay(m_Flank, m_vFlankAt, EAIGroupCombatMode.HOLD_FIRE);
  Stay(m_Squad, m_vFrom, EAIGroupCombatMode.FIRE_AT_WILL);
  m_iCues0 = BBB_SquadLeader.s_iCues;
  m_iSplits0 = BBB_SquadLeader.s_iSplits;
  m_iSwitches0 = BBB_SquadLeader.s_iSwitches;
  m_iLaid0 = BBB_SquadLeader.s_iLaid;
  GetGame().GetCallqueue().CallLater(Sample, 500, true);
 }

 protected int Collect(SCR_AIGroup group, array<IEntity> into)
 {
  if (!group) return 0;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!body) continue;
   PinDetail(agent);
   body.GetDamageManager().EnableDamageHandling(false);
   into.Insert(body);
  }
  return into.Count();
 }

 protected void Stay(SCR_AIGroup group, vector at, EAIGroupCombatMode mode)
 {
  group.ActivateAI();
  group.GetGroupUtilityComponent().SetCombatMode(mode);
  AIWaypoint stay = AIWaypoint.Cast(Spawn(WP_DEFEND, at, "0 0 1"));
  if (stay) group.AddWaypoint(stay);
 }

 protected SCR_AIUtilityComponent UtilityOf(IEntity man)
 {
  if (!man) return null;
  AIControlComponent control = AIControlComponent.Cast(man.FindComponent(AIControlComponent));
  if (!control || !control.GetControlAIAgent()) return null;
  return SCR_AIUtilityComponent.Cast(control.GetControlAIAgent().FindComponent(SCR_AIUtilityComponent));
 }

 // Degrees between a weapon's forward axis and the bearing from it to a spot, flat.
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

 protected void Sample()
 {
  m_iSamples++;
  float t = 0.5 * m_iSamples;
  int onFront, onFlank, knowFlank, sawFlank;
  string probe = "";
  foreach (IEntity man : m_aMen)
  {
   SCR_AIUtilityComponent utility = UtilityOf(man);
   if (!utility || !utility.m_CombatComponent) continue;
   SCR_AICombatComponent combat = utility.m_CombatComponent;
   BaseTarget current = combat.GetCurrentTarget();
   IEntity on;
   if (current) on = current.GetTargetEntity();
   if (on && m_aFront.Contains(on)) onFront++;
   if (on && m_aFlank.Contains(on)) onFlank++;
   // What he knows of the flank team.
   if (utility.m_PerceptionComponent)
   {
    array<BaseTarget> targets = {};
    utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
    bool knows;
    foreach (BaseTarget target : targets)
    {
     if (!target || !m_aFlank.Contains(target.GetTargetEntity())) continue;
     if (target.GetTimeSinceSeen() < 1) sawFlank++;
     if (target.GetTimeSinceSeen() < 4 || target.GetTimeSinceDetected() < 4) knows = true; // detected alone doesn't follow a man in sight
    }
    if (knows) knowFlank++;
   }
   // Rounds, and where his gun pointed when they went.
   BaseWeaponComponent weapon = combat.GetCurrentWeapon();
   if (weapon && weapon.GetCurrentMagazine())
   {
    int ammo = weapon.GetCurrentMagazine().GetAmmoCount();
    if (m_mAmmo.Contains(weapon) && ammo < m_mAmmo.Get(weapon))
    {
     int fired = m_mAmmo.Get(weapon) - ammo;
     m_iRounds += fired;
     float offFront = GunOff(weapon, m_vFrontAt);
     float offFlank = GunOff(weapon, m_vFlankAt);
     if (offFront <= AT_DEG && offFront < offFlank) m_iAtFront += fired;
     if (offFlank <= AT_DEG && offFlank < offFront) m_iAtFlank += fired;
    }
    m_mAmmo.Set(weapon, ammo);
    if (probe.IsEmpty() && current && combat.IsTargetVisible(current))
    {
     string side = "other";
     if (m_aFront.Contains(on)) side = "front";
     else if (m_aFlank.Contains(on)) side = "flank";
     probe = string.Format("a man on the %1 team: gun %2 deg off the front, %3 off the flank", side, Math.Round(GunOff(weapon, m_vFrontAt)), Math.Round(GunOff(weapon, m_vFlankAt)));
    }
   }
  }
  m_iOnFront += onFront;
  m_iOnFlank += onFlank;
  if (onFront >= 2 && onFlank >= 2) m_iBoth++;
  if (sawFlank > 0 && m_fFirstSawFlank < 0) m_fFirstSawFlank = t;
  if (m_fHalfKnewFlank < 0 && knowFlank * 2 >= m_aMen.Count()) m_fHalfKnewFlank = t;
  if ((m_iSamples % 10) == 0)
   PrintFormat("[BBB] SQUADLEADERTEST t=%1 on front %2, on flank %3, know flank %4/%5; rounds %6 (front %7, flank %8); %9", t, onFront, onFlank, knowFlank, m_aMen.Count(), m_iRounds, m_iAtFront, m_iAtFlank, probe);
  if (t < ARM_S) return;
  GetGame().GetCallqueue().Remove(Sample);
  EndArm();
 }

 protected void EndArm()
 {
  string arm = m_aArms[m_iArm];
  int aimed = m_iAtFront + m_iAtFlank;
  float balance = 0;
  if (aimed > 0)
  {
   float smaller = Math.Min(m_iAtFront, m_iAtFlank);
   float all = aimed;
   balance = smaller / all;
  }
  float delay = 999;
  if (m_fFirstSawFlank >= 0 && m_fHalfKnewFlank >= 0) delay = Math.Max(m_fHalfKnewFlank - m_fFirstSawFlank, 0);
  m_mBoth.Set(arm, m_iBoth);
  m_mBalance.Set(arm, balance);
  m_mKnowDelay.Set(arm, delay);
  m_mCues.Set(arm, BBB_SquadLeader.s_iCues - m_iCues0);
  m_mSplits.Set(arm, BBB_SquadLeader.s_iSplits - m_iSplits0);
  m_mSwitches.Set(arm, BBB_SquadLeader.s_iSwitches - m_iSwitches0);
  PrintFormat("[BBB] SQUADLEADERTEST RESULT %1 (%2 samples): target samples front %3, flank %4; both engaged %5; rounds %6, at the front %7, at the flank %8 (smaller share %9)", arm, m_iSamples, m_iOnFront, m_iOnFlank, m_iBoth, m_iRounds, m_iAtFront, m_iAtFlank, balance);
  PrintFormat("[BBB] SQUADLEADERTEST RESULT %1 flank first seen %2 s, half the squad knew it %3 s; leader: cues %4, splits %5, switches %6, laid bursts %7", arm, m_fFirstSawFlank, m_fHalfKnewFlank, BBB_SquadLeader.s_iCues - m_iCues0, BBB_SquadLeader.s_iSplits - m_iSplits0, BBB_SquadLeader.s_iSwitches - m_iSwitches0, BBB_SquadLeader.s_iLaid - m_iLaid0);
  foreach (IEntity man : m_aMen)
   if (man) SCR_EntityHelper.DeleteEntityAndChildren(man);
  foreach (IEntity foe : m_aFront)
   if (foe) SCR_EntityHelper.DeleteEntityAndChildren(foe);
  foreach (IEntity flanker : m_aFlank)
   if (flanker) SCR_EntityHelper.DeleteEntityAndChildren(flanker);
  if (m_Squad) SCR_EntityHelper.DeleteEntityAndChildren(m_Squad);
  if (m_Front) SCR_EntityHelper.DeleteEntityAndChildren(m_Front);
  if (m_Flank) SCR_EntityHelper.DeleteEntityAndChildren(m_Flank);
  GetGame().GetCallqueue().CallLater(NextArm, 3000);
 }

 protected int GotInt(map<string, int> values, string arm)
 {
  if (!values.Contains(arm)) return -1;
  return values.Get(arm);
 }

 protected float GotFloat(map<string, float> values, string arm)
 {
  if (!values.Contains(arm)) return -1;
  return values.Get(arm);
 }

 protected void Judge()
 {
  Check("off-leader-silent", GotInt(m_mCues, "off") == 0 && GotInt(m_mSplits, "off") == 0 && GotInt(m_mSwitches, "off") == 0, string.Format("cues %1, splits %2, switches %3", GotInt(m_mCues, "off"), GotInt(m_mSplits, "off"), GotInt(m_mSwitches, "off")));
  Check("on-split", GotInt(m_mSplits, "on") >= 1, GotInt(m_mSplits, "on").ToString());
  Check("on-cued", GotInt(m_mCues, "on") >= 1, GotInt(m_mCues, "on").ToString());
  Check("on-both-engaged-more", GotInt(m_mBoth, "on") > GotInt(m_mBoth, "off"), string.Format("on %1, off %2 samples", GotInt(m_mBoth, "on"), GotInt(m_mBoth, "off")));
  Check("on-fire-shared", GotFloat(m_mBalance, "on") > GotFloat(m_mBalance, "off"), string.Format("smaller share on %1, off %2", GotFloat(m_mBalance, "on"), GotFloat(m_mBalance, "off")));
  // Within a second (two samples): when the flank team is in plain view vanilla needs 1-1.5 s too.
  Check("on-knows-flank-no-later", GotFloat(m_mKnowDelay, "on") <= GotFloat(m_mKnowDelay, "off") + 1, string.Format("on %1 s, off %2 s (999: never)", GotFloat(m_mKnowDelay, "on"), GotFloat(m_mKnowDelay, "off")));
  Finish();
 }

 override protected void Finish()
 {
  string verdict = "FAIL";
  if (m_iFailures == 0 && m_iChecks == EXPECTED) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbsquadleader passed=%2 failed=%3 expected=%4", verdict, m_iChecks - m_iFailures, m_iFailures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_SquadLeaderTest.MaybeStartSquadLeader();
 }
}
