class BBB_AssaultTestHooks : BBB_Hooks
{
 ref array<SCR_AIGroup> m_Groups = {};
 override void GetGroups(notnull array<SCR_AIGroup> groups) { groups.Copy(m_Groups); }
}

// Squad tactic in a real fight (-bbbAssaultTest, BBB #9). One lane per run (two lanes 500 m apart
// saw each other's defenders): a US rifle squad on a search and destroy order against a USSR fire
// team defending 160 m north. Everyone can be hurt and shoots back; every BBB feature runs at its
// defaults. -bbbAssaultLane B: the squad runs no tactic (BBB_Tactic.s_aVanilla), else it does;
// the defenders never do. -bbbAssaultSwap 1: the second site. One fight is noise: the result
// line is for adding up across runs (A vs B, and against older code), not a verdict on its own.
// Judged (fixture validity): the lane spawns, makes contact within 60 s, the tactic ran (A) or
// never did (B), and the fight produces casualties. Reported: closest the squad got, reached (centre within 30 m of
// the objective), cleared (every defender down) with times, men down on each side, seconds spent
// in each tactic, fire and manoeuvre seconds.
class BBB_AssaultLane : Managed
{
 string m_sName;
 vector m_vFrom;
 vector m_vObjective;
 SCR_AIGroup m_Squad;
 SCR_AIGroup m_Foe;
 bool m_bTactic;
 float m_fContactAt = -1;
 float m_fReachedAt = -1;
 float m_fClearedAt = -1;
 float m_fClosest = 1000;
 int m_iFmS;
 ref array<int> m_aTacticS = {0, 0, 0, 0, 0, 0, 0, 0};
 ref array<IEntity> m_aMen = {};
 ref array<IEntity> m_aFoes = {};
}

class BBB_AssaultTest : BBB_AimTest
{
 static ref BBB_AssaultTest s_Assault;
 static const int EXPECTED = 4;
 static const ResourceName SQUAD_US = "{DDF3799FA1387848}Prefabs/Groups/BLUFOR/Group_US_RifleSquad.et";
 static const ResourceName WP_SEARCH = "{B3E7B8DC2BAB8ACC}Prefabs/AI/Waypoints/AIWaypoint_SearchAndDestroy.et";
 static const ResourceName WP_DEFEND = "{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et";
 static const float AHEAD_M = 160;
 static const float REACHED_M = 30;
 static const int FIGHT_S = 170;
 ref BBB_AssaultTestHooks m_Hooks;
 ref BBB_Hooks m_SavedHooks;
 ref array<ref BBB_AssaultLane> m_aAssaultLanes = {};
 int m_Checks;
 int m_Failures;
 int m_Wait;

 static void Start()
 {
  if (!Replication.IsServer() || !System.IsCLIParam("bbbAssaultTest")) return;
  s_Assault = new BBB_AssaultTest();
  GetGame().GetCallqueue().CallLater(s_Assault.Setup, 15000);
 }

 void AssaultCheck(string name, bool ok, string detail = "")
 {
  m_Checks++;
  if (!ok) m_Failures++;
  PrintFormat("[BBB] ASSAULTTEST %1 ok=%2 %3", name, ok, detail);
 }

 void Setup()
 {
  BBB_Tactic.Read();
  m_SavedHooks = BBB.s_Hooks;
  m_Hooks = new BBB_AssaultTestHooks();
  BBB.s_Hooks = m_Hooks;
  bool swap = System.IsCLIParam("bbbAssaultSwap");
  string which;
  bool tactic = !(System.GetCLIParam("bbbAssaultLane", which) && which == "B");
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  bool made = true;
  for (int i = 0; i < 1; i++)
  {
   BBB_AssaultLane lane = new BBB_AssaultLane();
   lane.m_bTactic = tactic;
   lane.m_sName = "B";
   if (lane.m_bTactic) lane.m_sName = "A";
   int site = 0;
   if (swap) site = 1;
   lane.m_vFrom = BBB.Ground(Vector(1340 + site * 500, 0, 2770));
   lane.m_vObjective = BBB.Ground(lane.m_vFrom + Vector(0, 0, AHEAD_M));
   if (observers) observers.InsertObserverSP(22249 + i, lane.m_vFrom[0], lane.m_vFrom[2] + AHEAD_M * 0.5, null);
   lane.m_Squad = SCR_AIGroup.Cast(Spawn(SQUAD_US, lane.m_vFrom, "0 0 1"));
   lane.m_Foe = SCR_AIGroup.Cast(Spawn(TEAM_USSR, lane.m_vObjective, "0 0 -1"));
   made = made && lane.m_Squad && lane.m_Foe;
   m_aAssaultLanes.Insert(lane);
   PrintFormat("[BBB] ASSAULTTEST lane %1 site %2 from %3 objective %4 tactic=%5", lane.m_sName, site, lane.m_vFrom, lane.m_vObjective, lane.m_bTactic);
  }
  AssaultCheck("groups", made);
  if (!made) { FinishAssault(); return; }
  foreach (BBB_AssaultLane each : m_aAssaultLanes)
  {
   m_Hooks.m_Groups.Insert(each.m_Squad);
   m_Hooks.m_Groups.Insert(each.m_Foe);
   BBB_Tactic.s_aVanilla.Insert(each.m_Foe);
   if (!each.m_bTactic) BBB_Tactic.s_aVanilla.Insert(each.m_Squad);
  }
  GetGame().GetCallqueue().CallLater(Prepare, 4000);
 }

 void PrepareGroup(SCR_AIGroup group, vector order, ResourceName waypoint, notnull array<IEntity> men)
 {
  PinDetail(group);
  group.ActivateAI();
  group.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.FIRE_AT_WILL);
  group.GetGroupUtilityComponent().EvaluateCombatMode();
  AIWaypoint wp = AIWaypoint.Cast(Spawn(waypoint, order, "0 0 1"));
  if (wp) group.AddWaypoint(wp);
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   PinDetail(agent);
   agent.ActivateAI();
   IEntity man = agent.GetControlledEntity();
   if (man) men.Insert(man);
  }
 }

 void Prepare()
 {
  foreach (BBB_AssaultLane lane : m_aAssaultLanes)
  {
   PrepareGroup(lane.m_Squad, lane.m_vObjective, WP_SEARCH, lane.m_aMen);
   PrepareGroup(lane.m_Foe, lane.m_vObjective, WP_DEFEND, lane.m_aFoes);
   PrintFormat("[BBB] ASSAULTTEST lane %1: %2 attackers, %3 defenders", lane.m_sName, lane.m_aMen.Count(), lane.m_aFoes.Count());
  }
  GetGame().GetCallqueue().CallLater(Watch, 1000, true);
 }

 int DownCount(notnull array<IEntity> men)
 {
  int count;
  foreach (IEntity man : men)
   if (Down(man)) count++;
  return count;
 }

 bool SquadCentre(notnull array<IEntity> men, out vector centre)
 {
  centre = vector.Zero;
  int count;
  foreach (IEntity man : men)
  {
   if (Down(man)) continue;
   centre = centre + man.GetOrigin();
   count++;
  }
  if (count == 0) return false;
  centre = centre * (1.0 / count);
  return true;
 }

 void Watch()
 {
  m_Wait++;
  bool over = true;
  foreach (BBB_AssaultLane lane : m_aAssaultLanes)
  {
   vector seen;
   if (lane.m_fContactAt < 0 && BBB_Squad.Threat(lane.m_Squad, BBB_Squad.RECENT_S, seen)) lane.m_fContactAt = m_Wait;
   vector centre;
   bool up = SquadCentre(lane.m_aMen, centre);
   if (up)
   {
    float range = vector.DistanceXZ(centre, lane.m_vObjective);
    lane.m_fClosest = Math.Min(lane.m_fClosest, range);
    if (lane.m_fReachedAt < 0 && range <= REACHED_M) lane.m_fReachedAt = m_Wait;
   }
   if (lane.m_fClearedAt < 0 && DownCount(lane.m_aFoes) == lane.m_aFoes.Count()) lane.m_fClearedAt = m_Wait;
   int tactic = BBB_Tactic.TacticOf(lane.m_Squad);
   if (tactic >= 0 && tactic < lane.m_aTacticS.Count()) lane.m_aTacticS[tactic] = lane.m_aTacticS[tactic] + 1;
   if (BBB_FireManeuver.Job(lane.m_Squad)) lane.m_iFmS++;
   if (up && lane.m_fClearedAt < 0) over = false;
   if (m_Wait % 10 == 0) PrintFormat("[BBB] ASSAULTTEST t=%1 lane %2 tactic %3 fm %4 range %5 down us %6 ru %7", m_Wait, lane.m_sName, BBB_Tactic.Name(tactic), BBB_FireManeuver.Job(lane.m_Squad) != null, Math.Round(vector.DistanceXZ(centre, lane.m_vObjective)), DownCount(lane.m_aMen), DownCount(lane.m_aFoes));
  }
  if (!over && m_Wait < FIGHT_S) return;
  GetGame().GetCallqueue().Remove(Watch);
  int casualties;
  foreach (BBB_AssaultLane done : m_aAssaultLanes)
  {
   int usDown = DownCount(done.m_aMen);
   int ruDown = DownCount(done.m_aFoes);
   casualties += usDown + ruDown;
   string times = "";
   for (int t = 1; t < done.m_aTacticS.Count(); t++)
    if (done.m_aTacticS[t] > 0) times += string.Format(" %1=%2", BBB_Tactic.Name(t), done.m_aTacticS[t]);
   string counts = string.Format("usDown=%1/%2 ruDown=%3/%4 fmS=%5 t=%6", usDown, done.m_aMen.Count(), ruDown, done.m_aFoes.Count(), done.m_iFmS, m_Wait);
   PrintFormat("[BBB] ASSAULTTEST RESULT lane=%1 tactic=%2 contact=%3 closest=%4 reached=%5 cleared=%6 %7 tacticS:%8", done.m_sName, done.m_bTactic, done.m_fContactAt, Math.Round(done.m_fClosest), done.m_fReachedAt, done.m_fClearedAt, counts, times);
  }
  BBB_AssaultLane only = m_aAssaultLanes[0];
  AssaultCheck("contact", only.m_fContactAt >= 0 && only.m_fContactAt <= 60, string.Format("t=%1", only.m_fContactAt));
  bool ran = only.m_aTacticS[BBB_Tactic.NONE] < m_Wait;
  AssaultCheck("tactic-as-set", ran == only.m_bTactic, string.Format("tactic %1, ran %2", only.m_bTactic, ran));
  AssaultCheck("casualties", casualties > 0, string.Format("%1 men down", casualties));
  FinishAssault();
 }

 void FinishAssault()
 {
  PrintFormat("[BBB] ASSAULTTEST changes %1", BBB_Tactic.s_iChanges);
  if (m_SavedHooks) BBB.s_Hooks = m_SavedHooks;
  string verdict = "FAIL";
  if (m_Failures == 0 && m_Checks == EXPECTED) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbassault passed=%2 failed=%3 expected=%4", verdict, m_Checks - m_Failures, m_Failures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 override protected void OnGameStart()
 {
  super.OnGameStart();
  BBB_AssaultTest.Start();
 }
}
