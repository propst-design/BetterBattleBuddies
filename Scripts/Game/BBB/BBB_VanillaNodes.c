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
  // Null too: a port holding the deleted waypoint reads back null, and vanilla then raised the error.
  if (!SCR_DefendWaypoint.Cast(waypoint)) return ENodeResult.FAIL;
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

// Marking a spotted enemy on the map reads the target's faction without checking it
// (SCR_AIEnemyMarkingSystem.MarkTarget -> GetIdentitySymbol(notnull Faction)). A target identified
// with no faction threw a "NULL pointer to instance. Variable 'targetFaction'" VM exception
// (AC: Actual night soak on Kolguyev, 26 Oct, once in 2 h). Without a faction there is no symbol
// to draw: the target just isn't marked.
modded class SCR_AIEnemyMarkingSystem
{
 override void MarkTarget(notnull SCR_AITargetInfo targetInfo, notnull SCR_AIGroup spottingGroup)
 {
  if (!targetInfo.m_Faction) return;
  super.MarkTarget(targetInfo, spottingGroup);
 }
}

// A gunner keeps his group's gun while it defends (AC: Actual, 19 Nov 2026: a machine-gun team holding
// a roadblock swapped men on its gun every 2.5-3.5 min all match, the gun empty a few seconds each time;
// srv:rbgunner reproduced it, 150 s apart, no enemy near). Vanilla's Defend re-plans now and then: its
// clear step orders every turret occupant out (m_bKeepTurrets off) and its allocation hands each of the
// group's guns to a random member. Now a man already on one of the group's own Defend guns is left on it,
// and if the draw gives that gun to someone else, the two swap: the gunner stays, the other man takes
// whatever the gunner would have got (a post or a sector).
modded class SCR_AIClearActionsForDefendActivity
{
 protected float m_fBBBSaidAt = -1; // one DEFEND line per group every LOG_MS (the step runs several times a re-plan)

 override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
 {
  IEntity waypoint;
  GetVariableIn(PORT_WAYPOINT, waypoint);
  if (m_bKeepTurrets || !m_groupOwner || !BBB_DefendGuns.AllOnDefendGuns(m_groupOwner, SCR_DefendWaypoint.Cast(waypoint))) return super.EOnTaskSimulate(owner, dt);
  float now = GetGame().GetWorld().GetWorldTime();
  if (m_fBBBSaidAt < 0 || now - m_fBBBSaidAt >= BBB_DefendGuns.LOG_MS)
  {
   m_fBBBSaidAt = now;
   PrintFormat("[BBB] DEFEND %1: re-planning the defence, its gunners stay on their guns", BBB.s_Hooks.NameOf(m_groupOwner));
  }
  m_bKeepTurrets = true;
  ENodeResult result = super.EOnTaskSimulate(owner, dt);
  m_bKeepTurrets = false;
  return result;
 }
}

modded class SCR_AIAllocateActionsForDefendActivity
{
 // The gunner on each of the group's guns, and who took his place in the draw (this run only).
 protected ref map<BaseCompartmentSlot, AIAgent> m_mBBBGunners;

 override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
 {
  if (!m_groupOwner || !m_RelatedActivity) return super.EOnTaskSimulate(owner, dt);
  m_mBBBGunners = new map<BaseCompartmentSlot, AIAgent>();
  BBB_DefendGuns.Gunners(m_groupOwner, m_mBBBGunners);
  m_RelatedActivity.m_mBBBStandIn = new map<AIAgent, AIAgent>();
  ENodeResult result = super.EOnTaskSimulate(owner, dt);
  m_RelatedActivity.m_mBBBStandIn = null;
  m_mBBBGunners = null;
  return result;
 }

 override protected bool OccupyTurret(AIAgent who, BaseCompartmentSlot slot, bool teleport)
 {
  AIAgent gunner;
  if (m_mBBBGunners && m_mBBBGunners.Find(slot, gunner) && gunner)
  {
   // He keeps it; the man drawn for it stands in for him in the rest of the draw.
   if (who != gunner && m_RelatedActivity && m_RelatedActivity.m_mBBBStandIn) m_RelatedActivity.m_mBBBStandIn.Set(gunner, who);
   BBB_DefendGuns.s_iKept++;
   return true;
  }
  return super.OccupyTurret(BBB_DefendGuns.StandIn(m_RelatedActivity, who), slot, teleport);
 }

 override protected bool OccupySA(AIAgent who, AISmartActionComponent smartAction, bool teleport)
 {
  return super.OccupySA(BBB_DefendGuns.StandIn(m_RelatedActivity, who), smartAction, teleport);
 }
}

modded class SCR_AIDefendActivity
{
 // While the allocation runs: a gunner kept on his gun -> the man who takes his other job.
 ref map<AIAgent, AIAgent> m_mBBBStandIn;

 override void AddAgentToRadialCover(AIAgent agent)
 {
  super.AddAgentToRadialCover(BBB_DefendGuns.StandIn(this, agent));
 }
}

class BBB_DefendGuns
{
 static int s_iKept; // tests
 static const float LOG_MS = 300000;

 // Each of the group's Defend guns (its allocated turret seats) with an able member of the group on it.
 static void Gunners(SCR_AIGroup group, notnull map<BaseCompartmentSlot, AIAgent> gunners)
 {
  gunners.Clear();
  if (!group) return;
  array<BaseCompartmentSlot> seats = {};
  group.GetAllocatedCompartments(seats);
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  foreach (BaseCompartmentSlot seat : seats)
  {
   if (!TurretCompartmentSlot.Cast(seat)) continue;
   IEntity occupant = seat.GetOccupant();
   if (!occupant || !BBB_VehicleHelpers.IsAble(occupant) || BBB.PlayerOf(occupant)) continue;
   foreach (AIAgent agent : agents)
    if (agent && agent.GetControlledEntity() == occupant) gunners.Set(seat, agent);
  }
 }

 // Margin past the Defend waypoint's radius within which a gun counts as the defence's own.
 static const float GUN_MARGIN_M = 30;

 // The clear step runs before the re-plan allocates the group's guns again (GetAllocatedCompartments is
 // empty then), so it goes by place: there are AI gunners of the group, every one of them able and on a
 // gun inside the Defend waypoint's area. A man in any other turret (a vehicle parked elsewhere) still
 // gets vanilla's get-out.
 static bool AllOnDefendGuns(SCR_AIGroup group, SCR_DefendWaypoint waypoint)
 {
  if (!group || !waypoint) return false;
  float reach = waypoint.GetCompletionRadius() + GUN_MARGIN_M;
  int kept;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   IEntity body = agent.GetControlledEntity();
   if (!body || BBB_Transport.SeatOf(body) != ECompartmentType.TURRET) continue;
   IEntity gun = BBB_Transport.VehicleOf(body);
   if (!BBB_VehicleHelpers.IsAble(body) || BBB.PlayerOf(body) || !gun || vector.DistanceXZ(gun.GetOrigin(), waypoint.GetOrigin()) > reach) return false;
   kept++;
  }
  if (kept == 0) return false;
  s_iKept++;
  return true;
 }

 static AIAgent StandIn(SCR_AIDefendActivity activity, AIAgent agent)
 {
  if (!activity || !activity.m_mBBBStandIn || !agent) return agent;
  int steps = activity.m_mBBBStandIn.Count();
  AIAgent cur = agent;
  for (int i = 0; i < steps; i++)
  {
   AIAgent next;
   if (!activity.m_mBBBStandIn.Find(cur, next) || !next || next == cur) break;
   cur = next;
   if (cur == agent) break;
  }
  return cur;
 }
}
