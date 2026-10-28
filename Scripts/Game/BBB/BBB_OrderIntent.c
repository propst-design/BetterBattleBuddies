// Native priority levels identify explicit orders independently of their provider.
// A temporary danger/attack behavior must not hide an unfinished player order.
class BBB_OrderIntent
{
 // A BBB feature asks before acting (noted for the BBB_Think panel when it holds back).
 static bool Blocks(SCR_AIUtilityComponent utility)
 {
  if (!Held(utility)) return false;
  BBB_Think.NoteUtility(utility, "Orders", "player/GM order active: BBB stands back");
  return true;
 }

 static bool Held(SCR_AIUtilityComponent utility)
 {
  if (!utility) return false;
  SCR_AIBehaviorBase current = utility.GetCurrentBehavior();
  if (current && current.EvaluatePriorityLevel() >= SCR_AIActionBase.PRIORITY_LEVEL_PLAYER) return true;
  AIAgent agent = utility.GetAIAgent();
  AIGroup group;
  if (agent) group = agent.GetParentGroup();
  SCR_AIWaypoint waypoint;
  if (group) waypoint = SCR_AIWaypoint.Cast(group.GetCurrentWaypoint());
  if (waypoint && waypoint.GetPriorityLevel() >= SCR_AIActionBase.PRIORITY_LEVEL_PLAYER) return true;
  // Reuse storage per controller; native decisions can ask many times a frame.
  // Clear after the scan so this buffer never retains finished actions.
  if (!utility.m_BBBOrderActions) utility.m_BBBOrderActions = {};
  array<ref AIActionBase> actions = utility.m_BBBOrderActions;
  actions.Clear();
  bool queued;
  utility.FindActionsOfInheritedType(SCR_AIBehaviorBase, actions);
  foreach (AIActionBase action : actions)
  {
   if (action.GetRemoveAction() || action.GetActionState() == EAIActionState.FAILED || action.GetActionState() == EAIActionState.COMPLETED) continue;
   if (action.EvaluatePriorityLevel() >= SCR_AIActionBase.PRIORITY_LEVEL_PLAYER) { queued = true; break; }
  }
  actions.Clear();
  return queued;
 }
}

// A route is never removed or replaced. BBB owns only its temporary actions;
// releasing them lets the engine continue the original waypoint.
class BBB_OrderLease : Managed
{
 AIWaypoint m_Waypoint;
 vector m_Start, m_Destination;
 float m_LastSeen = -1000000;
 bool m_Blocked;
 bool m_HadWaypoint;
}

class BBB_OrderLeash
{
 static bool s_bOn = true;
 static const float ROUTE_RADIUS = 150;
 static const float DEFEND_RADIUS = 35;
 static const float QUIET_MS = 75000;
 protected static ref map<SCR_AIGroup, ref BBB_OrderLease> s_Leases = new map<SCR_AIGroup, ref BBB_OrderLease>();

 static void Read()
 {
  string value;
  s_bOn = !(BBB_Settings.Get("bbbOrderLeash", value) && value == "0");
 }

 static BBB_OrderLease Lease(SCR_AIGroup group) { return s_Leases.Get(group); }

 static bool Defensive(AIWaypoint waypoint)
 {
  if (!waypoint || !waypoint.GetPrefabData()) return false;
  string path = waypoint.GetPrefabData().GetPrefabName();
  return path.Contains("Prefabs/AI/Waypoints/AIWaypoint_Defend.et") || path.Contains("Prefabs/AI/Waypoints/AIWaypoint_Hold.et");
 }

 // Unknown/scripted specialist orders fail closed. Explicit native priority wins
 // independently of the prefab, host or currently selected combat behavior.
 static bool Intent(AIWaypoint waypoint)
 {
  SCR_AIWaypoint scripted = SCR_AIWaypoint.Cast(waypoint);
  if (!scripted || scripted.GetPriorityLevel() >= SCR_AIActionBase.PRIORITY_LEVEL_PLAYER || !waypoint.GetPrefabData()) return false;
  string path = waypoint.GetPrefabData().GetPrefabName();
  return path.Contains("Prefabs/AI/Waypoints/AIWaypoint_Move.et") || path.Contains("Prefabs/AI/Waypoints/AIWaypoint_Patrol.et") || path.Contains("Prefabs/AI/Waypoints/AIWaypoint_SearchAndDestroy.et") || Defensive(waypoint);
 }

 static bool SameOrder(SCR_AIGroup group, BBB_OrderLease lease)
 {
  if (!group || !lease) return false;
  AIWaypoint waypoint = group.GetCurrentWaypoint();
  if ((waypoint != null) != lease.m_HadWaypoint || waypoint != lease.m_Waypoint) return false;
  return !waypoint || vector.DistanceXZ(waypoint.GetOrigin(), lease.m_Destination) <= 1;
 }

 static BBB_OrderLease Capture(SCR_AIGroup group)
 {
  if (!s_bOn || !group) return null;
  BBB_OrderLease lease = Lease(group);
  if (SameOrder(group, lease)) return lease;
  IEntity anchor = BBB.s_Hooks.AnchorOf(group);
  if (!anchor) return null;
  lease = new BBB_OrderLease();
  lease.m_Waypoint = group.GetCurrentWaypoint();
  lease.m_HadWaypoint = lease.m_Waypoint != null;
  lease.m_Start = anchor.GetOrigin();
  if (lease.m_Waypoint) lease.m_Destination = lease.m_Waypoint.GetOrigin();
  s_Leases.Set(group, lease);
  return lease;
 }

 static float RouteDistance(vector point, vector start, vector finish)
 {
  point[1] = 0; start[1] = 0; finish[1] = 0;
  vector line = finish - start;
  float fraction;
  if (line.LengthSq() > 0.01) fraction = Math.Clamp(vector.Dot(point - start, line) / line.LengthSq(), 0, 1);
  return vector.DistanceXZ(point, start + line * fraction);
 }

 static bool Inside(BBB_OrderLease lease, vector point)
 {
  if (!lease) return false;
  if (Defensive(lease.m_Waypoint)) return vector.DistanceXZ(point, lease.m_Destination) <= DEFEND_RADIUS;
  return RouteDistance(point, lease.m_Start, lease.m_Destination) <= ROUTE_RADIUS;
 }

 static bool Allows(SCR_AIGroup group, vector point, BBB_OrderLease expected = null, bool legacyMove = false)
 {
  if (!group || BBB.s_Hooks.IsBinding(group)) return false;
  if (expected && (!SameOrder(group, expected) || Lease(group) != expected)) return false;
  // The host's leash, when it sets one, is the area (BBB_Hooks.HostLeash).
  vector centre;
  float radius;
  if (s_bOn && BBB.s_Hooks.HostLeash(group, centre, radius)) return vector.DistanceXZ(point, centre) <= radius;
  if (!group.GetCurrentWaypoint()) return true;
  // FireManeuver retains its own pre-leash waypoint filter in the off case.
  if (!s_bOn) return legacyMove;
  if (!Intent(group.GetCurrentWaypoint())) return false;
  BBB_OrderLease lease = Lease(group);
  if (!SameOrder(group, lease) || lease.m_Blocked || GetGame().GetWorld().GetWorldTime() - lease.m_LastSeen >= QUIET_MS) return false;
  return Inside(lease, point);
 }

 // An established host still owns travel/unstick. Only a host explicitly declaring
 // its current order non-binding may permit these small stationary reactions.
 static bool DrivingAllows(SCR_AIGroup group)
 {
  if (!group || BBB.s_Hooks.IsBinding(group)) return false;
  return !BBB.s_Hooks.ManagesVehicleDriving(group) || s_bOn;
 }

 static void Tick()
 {
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  for (int old = s_Leases.Count() - 1; old >= 0; old--)
   if (!s_Leases.GetKey(old) || !groups.Contains(s_Leases.GetKey(old))) s_Leases.RemoveElement(old);
  if (!s_bOn) { s_Leases.Clear(); return; }
  float now = GetGame().GetWorld().GetWorldTime();
  foreach (SCR_AIGroup group : groups)
  {
   if (!group || !Intent(group.GetCurrentWaypoint()) || BBB.s_Hooks.IsBinding(group)) continue;
   BBB_OrderLease lease = Capture(group);
   if (!lease) continue;
   lease.m_Blocked = false;
   array<AIAgent> agents = {};
   BBB.s_Hooks.GetMembers(group, agents);
   foreach (AIAgent agent : agents)
   {
    if (!agent) continue;
    ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (!body) continue;
    if (BBB.PlayerOf(body) || (body.GetCharacterController() && body.GetCharacterController().IsPlayerControlled())) lease.m_Blocked = true;
    SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
    if (BBB_OrderIntent.Held(utility)) lease.m_Blocked = true;
    if (!BBB.IsConscious(body) || !utility || !utility.m_PerceptionComponent) continue;
    array<BaseTarget> targets = {};
    utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
    foreach (BaseTarget target : targets)
    {
     if (!target || target.IsDisarmed()) continue;
     float age = target.GetTimeSinceSeen();
     if (age < 0 || age > 10 || vector.DistanceXZ(body.GetOrigin(), target.GetLastSeenPosition()) > 350) continue;
     lease.m_LastSeen = Math.Max(lease.m_LastSeen, now - age * 1000);
    }
   }
  }
 }

 static void Stop() { s_Leases.Clear(); }
}

modded class SCR_AIUtilityComponent
{
 ref array<ref AIActionBase> m_BBBOrderActions;
}
