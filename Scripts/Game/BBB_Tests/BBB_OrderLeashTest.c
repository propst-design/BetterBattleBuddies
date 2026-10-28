// Uses the established Arland native-cover fixture. Fixture placement belongs
// to BBB_ReactionTest; every measured deviation/return here is native movement.
class BBB_LeashHooks : BBB_ReactionHooks
{
 bool m_Binding;
 override bool IsBinding(SCR_AIGroup group) { return m_Binding; }
}

class BBB_OrderLeashTest : BBB_ReactionTest
{
 static ref BBB_OrderLeashTest s_LeashTest;
 ref BBB_LeashHooks m_LeashHooks;
 AIWaypoint m_Intent;
 ref BBB_OrderLease m_Snapshot;
 ref SCR_AIBehaviorBase m_OwnedAction;
 vector m_ReturnStart;
 float m_ReturnDistance;
 bool m_Entered;

 static void StartLeash()
 {
  if (!Replication.IsServer() || (!System.IsCLIParam("bbbOrderLeashTest") && !System.IsCLIParam("acrBbbOrderLeashTest"))) return;
  s_LeashTest = new BBB_OrderLeashTest();
  GetGame().GetCallqueue().CallLater(s_LeashTest.Setup, 15000);
 }

 override void Setup()
 {
  super.Setup();
  m_LeashHooks = new BBB_LeashHooks();
  m_LeashHooks.m_Groups = m_Hooks.m_Groups;
  m_Hooks = m_LeashHooks;
  BBB.s_Hooks = m_LeashHooks;
 }

 override void BuildingFixtureReady()
 {
  m_Intent = AIWaypoint.Cast(Spawn("{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et", m_InfantryStart + Vector(0, 0, 500), "0 0 1"));
  Assert("ordinary-move-fixture", m_Intent != null);
  if (!m_Intent) { FinishLeash(); return; }
  // Headless worlds have no player streaming the destination/route tiles.
  NavmeshWorldComponent routeMesh = Navmesh();
  Assert("route-navmesh-fixture", routeMesh != null);
  if (!routeMesh) { FinishLeash(); return; }
  for (int tileStep = 0; tileStep <= 11; tileStep++)
   for (int routeSide = -1; routeSide <= 1; routeSide++)
    routeMesh.LoadTileIn(m_InfantryStart + Vector(routeSide * 50, 0, tileStep * 50));
  m_Infantry.AddWaypoint(m_Intent);
  BBB_OrderLeash.Tick();
  m_Snapshot = BBB_OrderLeash.Lease(m_Infantry);
  Assert("route-snapshot", m_Snapshot && m_Snapshot.m_Waypoint == m_Intent);
  BBB_OrderLeash.s_bOn = false;
  Assert("off-retains-waypoint-veto", !BBB_Buildings.Begin(m_InfantryUtility, m_InfantryStart + Vector(40, 0, 30)));
  BBB_OrderLeash.s_bOn = true;
  super.BuildingFixtureReady();
 }

 override void ObserveBuilding()
 {
  // Every success needs native sight, physical displacement and a real indoor
  // arrival while the same ordinary order remains installed.
  foreach (BBB_BuildingJob job : BBB_Buildings.s_Jobs)
  {
   if (job.m_Group != m_Infantry || !job.m_Utility || !job.m_Utility.m_OwnerEntity) continue;
   IEntity body = job.m_Utility.m_OwnerEntity;
   if (job.m_Arrived <= 0 || !m_InfantryOrigins.Contains(body) || vector.DistanceXZ(body.GetOrigin(), m_InfantryOrigins.Get(body)) < 2 || BBB_Buildings.RoofAt(body.GetOrigin()) != job.m_Building) continue;
   m_Entered = true;
   m_OwnedAction = job.m_Action;
   m_InfantryUtility = job.m_Utility;
   break;
  }
  if (!m_Entered && Now() - m_At < 45) return;
  GetGame().GetCallqueue().Remove(ObserveBuilding);
  Assert("native-indoor-deviation-with-move", m_Entered);
  Assert("ordinary-waypoint-preserved", m_Infantry.GetCurrentWaypoint() == m_Intent);
  Assert("contact-lease-live", m_Snapshot && m_Snapshot.m_LastSeen > 0 && BBB_OrderLeash.Allows(m_Infantry, m_InfantryUtility.m_OwnerEntity.GetOrigin()));
  if (!m_Entered || !m_Snapshot) { FinishLeash(); return; }
  SCR_AIGetSuppressionVolumeCenterPosition missingVolumeTask = new SCR_AIGetSuppressionVolumeCenterPosition();
  Assert("missing-suppression-input-fails-task", missingVolumeTask.EOnTaskSimulate(m_InfantryUtility.GetAIAgent(), 0.016) == ENodeResult.FAIL);
  vector routeMid = (m_Snapshot.m_Start + m_Snapshot.m_Destination) * 0.5;
  vector routeDirection = m_Snapshot.m_Destination - m_Snapshot.m_Start;
  routeDirection[1] = 0;
  routeDirection.Normalize();
  vector lateral = Vector(-routeDirection[2], 0, routeDirection[0]);
  Assert("route-inner-edge", BBB_OrderLeash.Allows(m_Infantry, routeMid + lateral * 149));
  Assert("route-outer-edge", !BBB_OrderLeash.Allows(m_Infantry, routeMid + lateral * 151));
  BBB_OrderIntentTestHold queuedHumanOrder = new BBB_OrderIntentTestHold(m_InfantryUtility, null);
  m_InfantryUtility.AddAction(queuedHumanOrder);
  array<ref AIActionBase> queuedActions = {};
  m_InfantryUtility.FindActionsOfInheritedType(SCR_AIBehaviorBase, queuedActions);
  SCR_AIBehaviorBase currentBehavior = m_InfantryUtility.GetCurrentBehavior();
  Assert("queued-player-order-fixture", queuedActions.Contains(queuedHumanOrder) && currentBehavior && currentBehavior != queuedHumanOrder && currentBehavior.EvaluatePriorityLevel() < SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
  BBB_Buildings.Tick();
  bool ownJobRemains;
  foreach (BBB_BuildingJob pendingIndoor : BBB_Buildings.s_Jobs)
   if (pendingIndoor.m_Utility == m_InfantryUtility) ownJobRemains = true;
  // Since 26 Oct an arrived job holds no action (BBB_Buildings drops its move on arrival), so
  // the job itself is what BBB owns; a move still running must have been failed.
  Assert("queued-player-binding-releases-owned-action", !ownJobRemains && (!m_OwnedAction || m_OwnedAction.GetActionState() == EAIActionState.FAILED));
  queuedHumanOrder.Fail();
  queuedActions.Clear();
  m_LeashHooks.m_Binding = true;

  Assert("host-binding-blocks", !BBB_OrderLeash.Allows(m_Infantry, routeMid));
  m_LeashHooks.m_Binding = false;
  SCR_AIWaypoint.Cast(m_Intent).SetPriorityLevel(SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
  BBB_Buildings.Tick();
  Assert("player-waypoint-binding", !BBB_OrderLeash.Allows(m_Infantry, routeMid) && BBB_OrderIntent.Blocks(m_InfantryUtility));
  SCR_AIWaypoint.Cast(m_Intent).SetPriorityLevel(SCR_AIActionBase.PRIORITY_LEVEL_NORMAL);
  // Re-targeting the same object must invalidate the old lease as well.
  vector original = m_Intent.GetOrigin();
  m_Intent.SetOrigin(original + Vector(3, 0, 0));
  Assert("retarget-invalidates-old-lease", !BBB_OrderLeash.Allows(m_Infantry, routeMid, m_Snapshot));
  m_Intent.SetOrigin(original);
  AIWaypoint transport = AIWaypoint.Cast(Spawn("{712F4795CF8B91C7}Prefabs/AI/Waypoints/AIWaypoint_GetIn.et", original, "0 0 1"));
  Assert("transport-fixture-binding", transport && !BBB_OrderLeash.Intent(transport));
  AIWaypoint defend = AIWaypoint.Cast(Spawn("{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et", original, "0 0 1"));
  BBB_OrderLease defence = new BBB_OrderLease();
  defence.m_Waypoint = defend; defence.m_Destination = original;
  Assert("defend-local-limit", defend && BBB_OrderLeash.Inside(defence, original + Vector(34, 0, 0)) && !BBB_OrderLeash.Inside(defence, original + Vector(36, 0, 0)));
  BBB_Buildings.s_bOn = false;
  // Remove contact, keep the route, observe natural return and real quiet expiry.
  array<AIAgent> enemies = {};
  m_Enemy.GetAgents(enemies);
  foreach (AIAgent enemy : enemies)
   if (enemy.GetControlledEntity()) SCR_EntityHelper.DeleteEntityAndChildren(enemy.GetControlledEntity());
  m_ReturnStart = m_InfantryUtility.m_OwnerEntity.GetOrigin();
  m_ReturnDistance = vector.DistanceXZ(m_ReturnStart, m_Intent.GetOrigin());
  m_At = Now();
  GetGame().GetCallqueue().CallLater(ObserveReturn, 1000, true);
 }

 void ObserveReturn()
 {
  if (Now() - m_At < 77) return;
  GetGame().GetCallqueue().Remove(ObserveReturn);
  vector here = m_InfantryUtility.m_OwnerEntity.GetOrigin();
  PrintFormat("[BBB] LEASHRETURN behavior=%1 inCover=%2 order=%3", m_InfantryUtility.GetCurrentBehavior(), m_InfantryUtility.m_CombatMoveState.m_bInCover, m_Infantry.GetCurrentWaypoint());
  Assert("native-route-resumed", vector.DistanceXZ(here, m_ReturnStart) > 5 && vector.DistanceXZ(here, m_Intent.GetOrigin()) < m_ReturnDistance - 5, string.Format("from=%1 to=%2 remaining=%3", m_ReturnStart, here, vector.DistanceXZ(here, m_Intent.GetOrigin())));
  Assert("route-still-current-at-expiry", m_Infantry.GetCurrentWaypoint() == m_Intent);
  Assert("quiet-lease-expires", GetGame().GetWorld().GetWorldTime() - m_Snapshot.m_LastSeen >= BBB_OrderLeash.QUIET_MS && !BBB_OrderLeash.Allows(m_Infantry, here));
  Assert("cleanup-restores-no-owned-jobs", BBB_Buildings.s_Jobs.IsEmpty());
  m_Infantry.RemoveWaypoint(m_Intent);
  SCR_EntityHelper.DeleteEntityAndChildren(m_Intent);
  GetGame().GetCallqueue().CallLater(ObserveDeletedOrder, 250);
 }

 void ObserveDeletedOrder()
 {
  Assert("deleted-waypoint-reference-cleared", m_Snapshot.m_Waypoint == null);
  Assert("deleted-waypoint-invalidates-lease", !BBB_OrderLeash.Allows(m_Infantry, m_InfantryUtility.m_OwnerEntity.GetOrigin(), m_Snapshot));
  FinishLeash();
 }

 override void FinishReaction() { FinishLeash(); }

 override void ArmorChecks()
 {
  Assert("order-leash-fixture-must-reach-indoor-test", false);
  FinishLeash();
 }

 void FinishLeash()
 {
  GetGame().GetCallqueue().Remove(ObserveBuilding);
  GetGame().GetCallqueue().Remove(ObserveReturn);
  GetGame().GetCallqueue().Remove(ObserveDeletedOrder);
  BBB_Buildings.Stop(); BBB_OrderLeash.Stop();
  Assert("lease-cache-cleared", !BBB_OrderLeash.Lease(m_Infantry));
  BBB.s_Hooks = m_SavedHooks;
  string verdict = "PASS";
  if (m_Failures > 0) verdict = "FAIL";
  PrintFormat("[ACR] VERDICT %1 test=bbb-order-leash checks=%2 failed=%3", verdict, m_Checks, m_Failures);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_OrderLeashTest.StartLeash();
 }
}
