// Base-game AI node fixes (found under AC: Actual, 25 Oct 2026 playtests; they hit any host or
// Game Master that replaces a group's waypoints). Each node now fails quietly where vanilla raised
// a node error or a VM exception; the result is the same FAIL, so the group's next evaluation
// goes on as before.

// A Defend activity still running after a new order replaced its waypoint reads the group's
// current waypoint, now the new one ("Wrong class of provided Waypoint!"), or its own, already
// deleted ("Defend Waypoint not provided to the node!"). The activity ends on the group's next
// evaluation anyway. Like vanilla's own SCR_AIGetWaypointParameters for a deleted waypoint ("we
// didnt react on it, yet"), these nodes now just fail.
modded class SCR_AIGetDefendWaypointParameters
{
 override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
 {
  IEntity waypoint;
  if (!GetVariableIn(PORT_WAYPOINT_IN, waypoint))
  {
   AIGroup group = AIGroup.Cast(owner);
   if (group) waypoint = group.GetCurrentWaypoint();
  }
  if (waypoint && !SCR_DefendWaypoint.Cast(waypoint)) return ENodeResult.FAIL;
  return super.EOnTaskSimulate(owner, dt);
 }
}

modded class SCR_AIAllocateActionsForDefendActivity
{
 override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
 {
  IEntity waypoint;
  GetVariableIn(PORT_WAYPOINT, waypoint);
  if (!SCR_DefendWaypoint.Cast(waypoint)) return ENodeResult.FAIL;
  return super.EOnTaskSimulate(owner, dt);
 }
}

// A group's failed move is processed by reading its leader's position without checking that it
// has one (SCR_AIProcessFailedMovementResult, m_Group.GetLeaderEntity().GetOrigin()). A group with
// no leader entity just then (its leader dead, despawned or between members) threw a "NULL pointer
// to instance" VM exception on each failed move: 4 in one 90-minute session. With no leader there
// is no move to process: the node fails, as it would have after the exception.
modded class SCR_AIProcessFailedMovementResult
{
 override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
 {
  if (m_Group && !m_Group.GetLeaderEntity()) return ENodeResult.FAIL;
  return super.EOnTaskSimulate(owner, dt);
 }
}
