// Game Master waypoints and waypoint kinds (-bbbGMWaypointTest 1, Test-BBB.ps1 -Preset gmwaypoint, or
// via -ExtraArgs beside another preset; about 15 s, done before other tests set up at 15 s).
// Rulings (Jacob, 15 Nov): the order leash and BBB_Tactic.Advancing read a waypoint by kind
// (BBB_WaypointKind), so a GM's editable waypoint and Conflict's variants are orders both agree on;
// a GM waypoint is seen (GameMasterOrder) but isn't a player order (Held), so soldiers' own
// reactions keep running. Real vanilla prefabs are spawned: the editable E_AIWaypoint_* a GM places,
// Conflict's Defend_* variants, and the base ones. A US team far from other fixtures gets a GM move,
// a GM defend and a Conflict defend, and the leash captures each order (Capture: before BBB starts
// the hooks list no groups). Not covered: the morale gate (a break under a GM waypoint), which needs
// a squad broken on cue.
class BBB_GMWaypointTest : BBB_AimTest
{
 static ref BBB_GMWaypointTest s_GM;
 static const int EXPECTED = 26;
 static const ResourceName E_MOVE = "{FFF9518F73279473}PrefabsEditable/Auto/AI/Waypoints/E_AIWaypoint_Move.et";
 static const ResourceName E_DEFEND = "{D9C14ECEC9772CC6}PrefabsEditable/Auto/AI/Waypoints/E_AIWaypoint_Defend.et";
 static const ResourceName WP_MOVE = "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et";
 static const ResourceName WP_PATROL = "{22A875E30470BD4F}Prefabs/AI/Waypoints/AIWaypoint_Patrol.et";
 static const ResourceName WP_SEARCH = "{B3E7B8DC2BAB8ACC}Prefabs/AI/Waypoints/AIWaypoint_SearchAndDestroy.et";
 static const ResourceName WP_DEFEND_CP = "{2A81753527971941}Prefabs/AI/Waypoints/AIWaypoint_Defend_CP.et";
 static const ResourceName WP_DEFEND_LARGE = "{FAD1D789EE291964}Prefabs/AI/Waypoints/AIWaypoint_Defend_Large.et";
 static const ResourceName WP_DEFEND_BASE_PATROL = "{06B1B14B6DE3C983}Prefabs/AI/Waypoints/AIWaypoint_Defend_ConflictBaseTeamPatrol.et";
 static const ResourceName WP_DEFEND_SMALL = "{2FCBE5C76E285A7B}Prefabs/AI/Waypoints/AIWaypoint_DefendSmall.et";
 static const ResourceName WP_FORCED = "{06E1B6EBD480C6E0}Prefabs/AI/Waypoints/AIWaypoint_ForcedMove.et";
 static const ResourceName WP_GETIN = "{712F4795CF8B91C7}Prefabs/AI/Waypoints/AIWaypoint_GetIn.et";
 SCR_AIGroup m_Team;
 vector m_vHome;
 ref array<IEntity> m_Spawned;
 int m_Checks;
 int m_Failures;

 static void Start()
 {
  if (s_GM || !Replication.IsServer() || !System.IsCLIParam("bbbGMWaypointTest")) return;
  s_GM = new BBB_GMWaypointTest();
  GetGame().GetCallqueue().CallLater(s_GM.Setup, 3000);
 }

 void GMCheck(string name, bool ok, string detail = "")
 {
  m_Checks++;
  if (!ok) m_Failures++;
  PrintFormat("[BBB] GMWAYPOINT %1 ok=%2 %3", name, ok, detail);
 }

 protected AIWaypoint Waypoint(ResourceName prefab, vector at)
 {
  AIWaypoint waypoint = AIWaypoint.Cast(Spawn(prefab, at, "0 0 1"));
  if (waypoint) m_Spawned.Insert(waypoint);
  return waypoint;
 }

 protected void Kind(string name, ResourceName prefab, int expected)
 {
  AIWaypoint waypoint = Waypoint(prefab, m_vHome + Vector(0, 0, -60));
  int kind = -1;
  string type = "none";
  if (waypoint)
  {
   kind = BBB_WaypointKind.Of(waypoint);
   type = waypoint.ClassName();
  }
  GMCheck("kind-" + name, waypoint && kind == expected, string.Format("kind=%1 want=%2 class=%3", kind, expected, type));
 }

 void Setup()
 {
  m_Spawned = {};
  m_vHome = BBB.Ground("1340 0 2770");
  // Kinds of the real prefabs: a GM's, Conflict's and the base ones.
  Kind("gm-move", E_MOVE, BBB_WaypointKind.MOVE);
  Kind("gm-defend", E_DEFEND, BBB_WaypointKind.DEFEND);
  Kind("move", WP_MOVE, BBB_WaypointKind.MOVE);
  Kind("patrol", WP_PATROL, BBB_WaypointKind.PATROL);
  Kind("search", WP_SEARCH, BBB_WaypointKind.SEARCH);
  Kind("defend-cp", WP_DEFEND_CP, BBB_WaypointKind.DEFEND);
  Kind("defend-large", WP_DEFEND_LARGE, BBB_WaypointKind.DEFEND);
  Kind("defend-conflict-base", WP_DEFEND_BASE_PATROL, BBB_WaypointKind.DEFEND);
  Kind("defend-small", WP_DEFEND_SMALL, BBB_WaypointKind.DEFEND);
  Kind("forced-move-no-order", WP_FORCED, BBB_WaypointKind.NONE);
  Kind("get-in-no-order", WP_GETIN, BBB_WaypointKind.NONE);
  m_Team = SCR_AIGroup.Cast(Spawn(TEAM_US, m_vHome, "0 0 1"));
  GMCheck("group", m_Team != null);
  if (!m_Team)
  {
   FinishGM();
   return;
  }
  GetGame().GetCallqueue().CallLater(GameMasterMove, 4000);
 }

 protected SCR_AIUtilityComponent LeaderUtility()
 {
  IEntity leader = m_Team.GetLeaderEntity();
  if (!leader) return null;
  AIControlComponent control = AIControlComponent.Cast(leader.FindComponent(AIControlComponent));
  if (!control || !control.GetAIAgent()) return null;
  return SCR_AIUtilityComponent.Cast(control.GetAIAgent().FindComponent(SCR_AIUtilityComponent));
 }

 protected void Give(AIWaypoint waypoint)
 {
  array<AIWaypoint> old = {};
  m_Team.GetWaypoints(old);
  foreach (AIWaypoint previous : old) m_Team.RemoveWaypoint(previous);
  if (waypoint) m_Team.AddWaypoint(waypoint);
 }

 // A GM's move 300 m off: an order to both the leash and Tactic, seen as a GM's, not a player's.
 void GameMasterMove()
 {
  IEntity anchor = m_Team.GetLeaderEntity();
  AIWaypoint move = Waypoint(E_MOVE, m_vHome + Vector(0, 0, 300));
  Give(move);
  bool current = move && anchor && m_Team.GetCurrentWaypoint() == move;
  GMCheck("gm-move-current", current, string.Format("leader=%1 waypoint=%2", anchor, m_Team.GetCurrentWaypoint()));
  if (!current)
  {
   FinishGM();
   return;
  }
  GMCheck("gm-move-is-gm-order", BBB_OrderIntent.GameMasterOrder(m_Team));
  SCR_AIUtilityComponent utility = LeaderUtility();
  GMCheck("gm-move-not-player-order", utility && !BBB_OrderIntent.Held(utility), "soldiers' reactions keep running");
  GMCheck("gm-move-leash-order", BBB_OrderLeash.Intent(move));
  GMCheck("gm-move-tactic-push", BBB_Tactic.Advancing(m_Team, anchor.GetOrigin()));
  vector centre;
  float radius;
  BBB_EIntent intent = BBB_HostIntent.Of(m_Team, centre, radius);
  GMCheck("gm-move-intent-push", intent == BBB_EIntent.PUSH, string.Format("intent=%1", intent));
  BBB_OrderLease lease = BBB_OrderLeash.Capture(m_Team); // as its Tick does for an order it reads
  GMCheck("gm-move-lease-captured", lease && lease.m_Waypoint == move && vector.DistanceXZ(lease.m_Destination, move.GetOrigin()) < 1);
  GameMasterDefend();
 }

 // A GM's defend where the team stands: a local leash, no ground to take.
 void GameMasterDefend()
 {
  IEntity anchor = m_Team.GetLeaderEntity();
  AIWaypoint defend = Waypoint(E_DEFEND, anchor.GetOrigin());
  Give(defend);
  GMCheck("gm-defend-is-gm-order", defend && BBB_OrderIntent.GameMasterOrder(m_Team));
  GMCheck("gm-defend-leash-defensive", BBB_OrderLeash.Intent(defend) && BBB_OrderLeash.Defensive(defend));
  GMCheck("gm-defend-tactic-holds", !BBB_Tactic.Advancing(m_Team, anchor.GetOrigin()));
  BBB_OrderLease lease = BBB_OrderLeash.Capture(m_Team); // as its Tick does for an order it reads
  vector spot = defend.GetOrigin();
  GMCheck("gm-defend-local-leash", lease && lease.m_Waypoint == defend && BBB_OrderLeash.Inside(lease, spot + Vector(34, 0, 0)) && !BBB_OrderLeash.Inside(lease, spot + Vector(36, 0, 0)));
  ConflictDefend();
 }

 // Conflict's defend: not a GM's, an order the leash bounds locally (before 15 Nov: no order at all).
 void ConflictDefend()
 {
  IEntity anchor = m_Team.GetLeaderEntity();
  AIWaypoint defend = Waypoint(WP_DEFEND_CP, anchor.GetOrigin());
  Give(defend);
  GMCheck("conflict-defend-not-gm", defend && !BBB_OrderIntent.GameMasterOrder(m_Team));
  GMCheck("conflict-defend-leash-defensive", BBB_OrderLeash.Intent(defend) && BBB_OrderLeash.Defensive(defend));
  GMCheck("conflict-defend-tactic-holds", !BBB_Tactic.Advancing(m_Team, anchor.GetOrigin()));
  FinishGM();
 }

 void FinishGM()
 {
  if (m_Team)
  {
   Give(null);
   array<AIAgent> agents = {};
   m_Team.GetAgents(agents);
   foreach (AIAgent agent : agents)
    if (agent && agent.GetControlledEntity()) SCR_EntityHelper.DeleteEntityAndChildren(agent.GetControlledEntity());
   SCR_EntityHelper.DeleteEntityAndChildren(m_Team);
  }
  if (m_Spawned)
  {
   foreach (IEntity spawned : m_Spawned)
    if (spawned) SCR_EntityHelper.DeleteEntityAndChildren(spawned);
  }
  BBB_OrderLeash.Stop();
  string verdict = "PASS";
  if (m_Failures > 0 || m_Checks != EXPECTED) verdict = "FAIL";
  PrintFormat("[ACR] VERDICT %1 test=bbb-gm-waypoint checks=%2 failed=%3 expected=%4", verdict, m_Checks, m_Failures, EXPECTED);
  // Alone it ends the run; beside the reactions preset, that test ends it.
  if (!System.IsCLIParam("bbbReactionTest")) Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 override protected void OnGameStart()
 {
  super.OnGameStart();
  BBB_GMWaypointTest.Start();
 }
}
