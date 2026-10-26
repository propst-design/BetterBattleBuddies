// Base-game Defend node fixes (-bbbVanillaNodesTest 1, Test-BBB.ps1 -Preset vanillanodes, about
// 1.5 min): a US team is ordered to defend a spot; once its group's Defend activity runs, the
// Defend waypoint is taken off the group and a Move waypoint given in the same frame, the old one
// left in the world a second longer (as a Game Master's waypoints stay when a group gets another),
// and back, CYCLES times; the runner fails a run on any script error. Under AC: Actual the same
// swap raised "Wrong class of provided Waypoint!" without BBB_VanillaNodes (its srv:defendswap);
// with BBB alone it hasn't so far, so here it guards the fix rather than reproducing the error.
// (The failed-movement guard has no test: a failed move in the moment a group has no leader can't
// be set up on cue, and that node can't be run outside a behaviour tree.)
class BBB_VanillaNodesTest : BBB_AimTest
{
 static ref BBB_VanillaNodesTest s_Nodes;
 static const int CYCLES = 12;
 static const int EXPECTED = 2;
 static const ResourceName WP_DEFEND = "{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et";
 static const ResourceName WP_MOVE = "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et";
 SCR_AIGroup m_Team;
 vector m_vHome;
 AIWaypoint m_Order;
 int m_Checks;
 int m_Failures;
 int m_Step;
 int m_Waited;
 int m_Cycle;
 int m_Swaps;

 static void Start()
 {
  if (s_Nodes || !Replication.IsServer() || !System.IsCLIParam("bbbVanillaNodesTest")) return;
  s_Nodes = new BBB_VanillaNodesTest();
  GetGame().GetCallqueue().CallLater(s_Nodes.Setup, 15000);
 }

 void NodeCheck(string name, bool ok, string detail = "")
 {
  m_Checks++;
  if (!ok) m_Failures++;
  PrintFormat("[BBB] NODETEST %1 ok=%2 %3", name, ok, detail);
 }

 void Setup()
 {
  m_vHome = BBB.Ground("1340 0 2770");
  m_Team = SCR_AIGroup.Cast(Spawn(TEAM_US, m_vHome, "0 0 1"));
  NodeCheck("group", m_Team != null);
  if (!m_Team)
  {
   FinishNodes();
   return;
  }
  GetGame().GetCallqueue().CallLater(Prepare, 4000);
 }

 void Prepare()
 {
  PinDetail(m_Team);
  m_Team.ActivateAI();
  array<AIAgent> agents = {};
  m_Team.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   PinDetail(agent);
   agent.ActivateAI();
  }
  GetGame().GetCallqueue().CallLater(Poll, 500, true);
 }

 // The group's waypoints replaced by one new one of 'prefab' at 'at', the old ones off the group
 // at once and deleted a second later.
 protected void Order(ResourceName prefab, vector at)
 {
  array<AIWaypoint> old = {};
  m_Team.GetWaypoints(old);
  foreach (AIWaypoint waypoint : old)
  {
   m_Team.RemoveWaypoint(waypoint);
   GetGame().GetCallqueue().CallLater(DeleteLater, 1000, false, waypoint);
  }
  m_Order = AIWaypoint.Cast(Spawn(prefab, at, "0 0 1"));
  if (!m_Order) return;
  m_Order.SetCompletionRadius(15);
  m_Team.AddWaypoint(m_Order);
 }

 protected void DeleteLater(AIWaypoint waypoint)
 {
  if (waypoint) SCR_EntityHelper.DeleteEntityAndChildren(waypoint);
 }

 protected bool Defending()
 {
  SCR_AIGroupUtilityComponent utility = m_Team.GetGroupUtilityComponent();
  return utility && SCR_AIDefendActivity.Cast(utility.GetCurrentAction()) != null;
 }

 void Poll()
 {
  if (!m_Team)
  {
   NodeCheck("defend-swap", false, "the team is gone");
   FinishNodes();
   return;
  }
  m_Waited++;
  if (m_Step == 0)
  {
   Order(WP_DEFEND, m_vHome + Vector(15, 0, 0));
   m_Step = 1;
   m_Waited = 0;
   return;
  }
  if (m_Step == 1)
  {
   // The group's Defend activity running: the new order now (or after 10 s without one).
   if (!Defending() && m_Waited < 20) return;
   if (Defending()) m_Swaps++;
   Order(WP_MOVE, m_vHome + Vector(0, 0, 20));
   m_Cycle++;
   m_Step = 2;
   m_Waited = 0;
   return;
  }
  if (m_Waited < 4) return;
  if (m_Cycle < CYCLES)
  {
   m_Step = 0;
   return;
  }
  NodeCheck("defend-swap", m_Swaps >= CYCLES / 2, string.Format("%1 of %2 cycles swapped from a running Defend", m_Swaps, CYCLES));
  FinishNodes();
 }

 void FinishNodes()
 {
  GetGame().GetCallqueue().Remove(Poll);
  string verdict = "FAIL";
  if (m_Failures == 0 && m_Checks == EXPECTED) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbvanillanodes passed=%2 failed=%3 expected=%4", verdict, m_Checks - m_Failures, m_Failures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 override protected void OnGameStart()
 {
  super.OnGameStart();
  BBB_VanillaNodesTest.Start();
 }
}
