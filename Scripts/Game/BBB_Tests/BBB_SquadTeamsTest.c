// Squad leader stage 4, teams (-bbbSquadTeamsTest 1, Test-BBB.ps1 -Preset squadteams; 7 Nov 2026). Fixture:
// the squad leader test's lane, but the US rifle squad is on a search and destroy order toward the front
// team (FRONT_M up the lane), so its tactic assaults and fire and manoeuvre bounds; a second USSR team
// stands FLANK_M out at the first visible spot 45-65 degrees off, so the leader splits the squad.
// Everyone invulnerable, the USSR teams hold fire.
// Arms, a fresh squad each, ARM_S each: off (-bbbSquadTeams off: roster-order halves, the second team in
// the bounds) and on.
// Measured every 0.5 s: the men bounding (a fire and manoeuvre job's moving half while it moves, or a
// flank/search drill runner: the first run's squad flanked and never assaulted); how many of them are on
// the leader's second team; how spread out the bounding men are (mean distance from their centre,
// physical); an assistant bounding without his gunner (BBB_Teams).
// Judged: spawned (2); fire and manoeuvre ran in both arms and the squad split in both (the scene);
// off: second-team men were in the bounds (the conflict this fixes); on: none were. The bounding men's
// spread is logged only (see Judge). One run per arm: a measurement, not a statistic.
class BBB_SquadTeamsTest : BBB_AimTest
{
 static ref BBB_SquadTeamsTest s_Teams;
 static const ResourceName SQUAD_US = "{DDF3799FA1387848}Prefabs/Groups/BLUFOR/Group_US_RifleSquad.et";
 static const ResourceName WP_DEFEND = "{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et";
 static const ResourceName WP_SEARCH = "{B3E7B8DC2BAB8ACC}Prefabs/AI/Waypoints/AIWaypoint_SearchAndDestroy.et";
 static const int EXPECTED = 5;
 static const float FRONT_M = 170;
 static const float FLANK_M = 130;
 static const float ARM_S = 70;
 static const int OBSERVER = 22415;

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
 protected ref array<IEntity> m_aFoes;
 protected int m_iSamples, m_iJobSamples, m_iSecondInJob, m_iMovingSamples, m_iPairSplit, m_iSplitSamples;
 protected float m_fSpreadSum;
 protected int m_iSplits0, m_iHalves0;
 protected ref map<string, int> m_mJob;
 protected ref map<string, int> m_mSecondInJob;
 protected ref map<string, float> m_mSpread;
 protected ref map<string, int> m_mMoving;
 protected ref map<string, int> m_mSplit;

 static void MaybeStartTeams()
 {
  if (s_Teams || !Replication.IsServer() || !System.IsCLIParam("bbbSquadTeamsTest")) return;
  s_Teams = new BBB_SquadTeamsTest();
  GetGame().GetCallqueue().CallLater(s_Teams.Begin, 12000);
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
  PrintFormat("[BBB] SQUADTEAMSTEST CHECK %1 %2 %3", name, res, detail);
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
  m_aFoes = {};
  m_mJob = new map<string, int>();
  m_mSecondInJob = new map<string, int>();
  m_mSpread = new map<string, float>();
  m_mMoving = new map<string, int>();
  m_mSplit = new map<string, int>();
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
  m_vFlankAt = BBB.Ground(m_vFrom + Turned(m_vDir, 50) * FLANK_M);
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
  PrintFormat("[BBB] SQUADTEAMSTEST lane at %1 heading %2, front team at %3 (sight %4), flank team at %5 (sight %6)", m_vFrom, m_vDir, m_vFrontAt, Visible(m_vFrom, m_vFrontAt), m_vFlankAt, Visible(m_vFrom, m_vFlankAt));
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
  BBB_SquadLeader.s_bTeams = arm == "on";
  m_aMen.Clear();
  m_aFoes.Clear();
  m_iSamples = 0; m_iJobSamples = 0; m_iSecondInJob = 0; m_iMovingSamples = 0; m_iPairSplit = 0; m_iSplitSamples = 0;
  m_fSpreadSum = 0;
  m_Squad = SCR_AIGroup.Cast(Spawn(SQUAD_US, m_vFrom, m_vDir));
  m_Front = SCR_AIGroup.Cast(Spawn(TEAM_USSR, m_vFrontAt, m_vDir * -1));
  m_Flank = SCR_AIGroup.Cast(Spawn(TEAM_USSR, m_vFlankAt, (m_vFrom - m_vFlankAt).Normalized()));
  PrintFormat("[BBB] SQUADTEAMSTEST arm %1: leader's teams %2", arm, BBB_SquadLeader.s_bTeams);
  GetGame().GetCallqueue().CallLater(Ready, 4000);
 }

 protected void Ready()
 {
  int squad = Collect(m_Squad, m_aMen);
  int front = Collect(m_Front, m_aFoes);
  bool spawned = squad >= 6 && Collect(m_Flank, m_aFoes) - front >= 3 && front >= 3;
  Check(m_aArms[m_iArm] + "-spawned", spawned, string.Format("squad %1, foes %2", m_aMen.Count(), m_aFoes.Count()));
  if (!spawned)
  {
   EndArm();
   return;
  }
  Stay(m_Front, m_vFrontAt);
  Stay(m_Flank, m_vFlankAt);
  m_Squad.ActivateAI();
  m_Squad.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.FIRE_AT_WILL);
  AIWaypoint search = AIWaypoint.Cast(Spawn(WP_SEARCH, m_vFrontAt, "0 0 1"));
  if (search) m_Squad.AddWaypoint(search);
  m_iSplits0 = BBB_SquadLeader.s_iSplits;
  m_iHalves0 = BBB_SquadLeader.s_iHalves;
  GetGame().GetCallqueue().CallLater(Sample, 500, true);
 }

 protected int Collect(SCR_AIGroup group, array<IEntity> into)
 {
  if (!group) return into.Count();
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

 protected void Stay(SCR_AIGroup group, vector at)
 {
  group.ActivateAI();
  group.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.HOLD_FIRE);
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

 // Bounding now: in a fire and manoeuvre job's moving half while it moves, or a flank/search drill runner.
 protected bool Bounding(SCR_AIUtilityComponent utility, BBB_FireTeamJob job)
 {
  if (job && job.m_bMoving && job.m_Members.Contains(utility) && utility.m_iBBBFireTeam != job.m_iCoverTeam) return true;
  return utility.m_bBBBTacticRunner && BBB_Morale.Running(utility.m_BBBWithdrawRun);
 }

 protected void Sample()
 {
  m_iSamples++;
  float t = 0.5 * m_iSamples;
  bool split;
  BBB_FireTeamJob job = BBB_FireManeuver.Job(m_Squad);
  vector centre;
  int bounding, second;
  array<IEntity> bounders = {};
  foreach (IEntity man : m_aMen)
  {
   SCR_AIUtilityComponent utility = UtilityOf(man);
   if (!utility) continue;
   AIAgent agent = utility.GetAIAgent();
   bool onSecond = BBB_SquadLeader.OnSecond(m_Squad, agent);
   if (onSecond) split = true;
   if (!Bounding(utility, job)) continue;
   bounders.Insert(man);
   centre = centre + man.GetOrigin();
   if (onSecond) second++;
   BBB_Pair pair = BBB_Teams.PairOf(man);
   if (pair && pair.m_Assistant == man && pair.m_Gunner && !bounders.Contains(pair.m_Gunner))
   {
    SCR_AIUtilityComponent gunner = UtilityOf(pair.m_Gunner);
    if (gunner && !Bounding(gunner, job)) m_iPairSplit++;
   }
  }
  if (split) m_iSplitSamples++;
  m_iSecondInJob += second;
  float spread = 0;
  if (!bounders.IsEmpty()) m_iJobSamples++;
  if (bounders.Count() >= 2)
  {
   centre = centre * (1.0 / bounders.Count());
   foreach (IEntity mover : bounders) spread += vector.DistanceXZ(mover.GetOrigin(), centre);
   spread = spread / bounders.Count();
   m_fSpreadSum += spread;
   m_iMovingSamples++;
  }
  if ((m_iSamples % 10) == 0)
  {
   string tactic = "none";
   BBB_TacticState state = BBB_Tactic.StateOf(m_Squad);
   if (state) tactic = state.m_iTactic.ToString();
   PrintFormat("[BBB] SQUADTEAMSTEST t=%1 split %2, tactic %3, bound job %4; bounding %5 (%6 of the second team), spread %7 m", t, split, tactic, job != null, bounders.Count(), second, Math.Round(spread * 10) / 10);
  }
  if (t < ARM_S) return;
  GetGame().GetCallqueue().Remove(Sample);
  EndArm();
 }

 protected void EndArm()
 {
  string arm = m_aArms[m_iArm];
  float spread = -1;
  if (m_iMovingSamples > 0) spread = m_fSpreadSum / m_iMovingSamples;
  m_mJob.Set(arm, m_iJobSamples);
  m_mSecondInJob.Set(arm, m_iSecondInJob);
  m_mSpread.Set(arm, spread);
  m_mMoving.Set(arm, m_iMovingSamples);
  m_mSplit.Set(arm, m_iSplitSamples);
  PrintFormat("[BBB] SQUADTEAMSTEST RESULT %1 (%2 samples): split %3 samples; bounding %4 samples, second-team man-samples bounding %5; 2+ bounding %6 samples, mean spread of the bounding men %7 m; pair split %8 man-samples; leader halves drawn %9", arm, m_iSamples, m_iSplitSamples, m_iJobSamples, m_iSecondInJob, m_iMovingSamples, spread, m_iPairSplit, BBB_SquadLeader.s_iHalves - m_iHalves0);
  foreach (IEntity man : m_aMen)
   if (man) SCR_EntityHelper.DeleteEntityAndChildren(man);
  foreach (IEntity foe : m_aFoes)
   if (foe) SCR_EntityHelper.DeleteEntityAndChildren(foe);
  if (m_Squad) SCR_EntityHelper.DeleteEntityAndChildren(m_Squad);
  if (m_Front) SCR_EntityHelper.DeleteEntityAndChildren(m_Front);
  if (m_Flank) SCR_EntityHelper.DeleteEntityAndChildren(m_Flank);
  GetGame().GetCallqueue().CallLater(NextArm, 3000);
 }

 protected int GotI(map<string, int> values, string arm)
 {
  if (!values.Contains(arm)) return -1;
  return values.Get(arm);
 }

 protected float GotF(map<string, float> values, string arm)
 {
  if (!values.Contains(arm)) return -1;
  return values.Get(arm);
 }

 protected void Judge()
 {
  Check("bounds-and-split-both-arms", GotI(m_mJob, "off") >= 10 && GotI(m_mJob, "on") >= 10 && GotI(m_mSplit, "off") > 0 && GotI(m_mSplit, "on") > 0, string.Format("bounding samples off %1 on %2, split samples off %3 on %4", GotI(m_mJob, "off"), GotI(m_mJob, "on"), GotI(m_mSplit, "off"), GotI(m_mSplit, "on")));
  Check("off-second-team-in-bounds", GotI(m_mSecondInJob, "off") > 0, GotI(m_mSecondInJob, "off").ToString());
  Check("on-second-team-kept-out", GotI(m_mSecondInJob, "on") == 0, GotI(m_mSecondInJob, "on").ToString());
  // Logged, not judged: the arms often run different movers (one flanks, one assaults), and with the
  // second team out the leader's bounds have fewer men, so spread isn't like for like (runs 2-3: drill
  // vs drill 8.3 vs 8.1 m; assault vs assault 6.6 vs 8.1 m with 5 men against 9).
  PrintFormat("[BBB] SQUADTEAMSTEST spread of the bounding men (not judged): on %1 m (%2 samples), off %3 m (%4 samples)", GotF(m_mSpread, "on"), GotI(m_mMoving, "on"), GotF(m_mSpread, "off"), GotI(m_mMoving, "off"));
  Finish();
 }

 override protected void Finish()
 {
  string verdict = "FAIL";
  if (m_iFailures == 0 && m_iChecks == EXPECTED) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbsquadteams passed=%2 failed=%3 expected=%4", verdict, m_iChecks - m_iFailures, m_iFailures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_SquadTeamsTest.MaybeStartTeams();
 }
}
