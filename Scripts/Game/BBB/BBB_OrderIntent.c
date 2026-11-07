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

 // Gunners fire on the move (6 Nov 2026). A move order says where the vehicle goes, not whether its
 // gunner may shoot on the way. AC: Actual raises every Fast move to the player level, so a BMP driving
 // to its attack's staging point drove past a squad its crew had reported: the group's player-level
 // move outranked the gunner's fire (modded classes at the end of this file), and BBB's gunner help (Hot
 // AI fire control, crew call-out, BBB_Gunner drill) asked Blocks and stood back. Now, for a gunner on
 // such a move (GunnerOnTheMove), his fire runs at the move's level and GunnerBlocks holds BBB back only
 // for a player-level behaviour of his own besides the group's move. Unchanged: Game Master waypoints,
 // groups with a player, static guns, the host's LeaveAlone, hold fire (the order not to shoot).
 //  -bbbGunnerOrders 0   the old rule: the move outranks his fire, BBB's gunner help stands back
 static bool s_bGunnerOrders;
 protected static int s_iGunnerRead;

 static bool GunnerOrdersOn()
 {
  if (BBB_Settings.Due(s_iGunnerRead))
  {
   string value;
   s_bGunnerOrders = !(BBB_Settings.Get("bbbGunnerOrders", value) && value == "0");
   PrintFormat("[BBB] GUNNER ORDERS move orders leave the gunner free to fire=%1", s_bGunnerOrders);
  }
  return s_bGunnerOrders;
 }

 static bool GunnerBlocks(SCR_AIUtilityComponent utility)
 {
  if (!GunnerOnTheMove(utility)) return Blocks(utility);
  if (!GunnerOwnOrder(utility)) return false;
  BBB_Think.NoteUtility(utility, "Orders", "player/GM order on the gunner: BBB stands back");
  return true;
 }

 // A turret gunner (with or without a gun) of an AI group whose current waypoint is a player-level
 // order that is not a Game Master's, in a vehicle the host doesn't keep for itself. Measured 6 Nov
 // (BBB_CrewCalloutTest, LAV-25 on a player-level Move): vanilla gives every crewman the group's move
 // as SCR_AIMoveInFormationBehavior at the player level (his current behaviour in 74-77 of 80
 // samples), which is what Blocks read as an order on him. Cached a second per soldier.
 static bool GunnerOnTheMove(SCR_AIUtilityComponent utility)
 {
  if (!utility || !utility.m_AIInfo || !utility.m_AIInfo.HasUnitState(EUnitState.IN_TURRET)) return false;
  BaseWorld world = GetGame().GetWorld();
  if (!world) return false;
  float now = world.GetWorldTime();
  if (now < utility.m_fBBBOnMoveUntil) return utility.m_bBBBOnMove;
  utility.m_fBBBOnMoveUntil = now + 1000;
  utility.m_bBBBOnMove = OnTheMoveNow(utility);
  return utility.m_bBBBOnMove;
 }

 protected static bool OnTheMoveNow(SCR_AIUtilityComponent utility)
 {
  if (!GunnerOrdersOn()) return false;
  IEntity man = utility.m_OwnerEntity;
  if (!man || BBB.PlayerOf(man)) return false;
  AIAgent agent = utility.GetAIAgent();
  if (!agent) return false;
  SCR_AIGroup group = SCR_AIGroup.Cast(agent.GetParentGroup());
  if (!group || BBB.HasPlayer(group)) return false;
  SCR_AIWaypoint waypoint = SCR_AIWaypoint.Cast(group.GetCurrentWaypoint());
  if (!waypoint || waypoint.GetPriorityLevel() < SCR_AIActionBase.PRIORITY_LEVEL_PLAYER) return false;
  if (GameMasterOrder(group)) return false;
  // Vehicles only: a man on a static gun (a Turret entity) leaves it with his group on a move.
  IEntity vehicle = BBB_Transport.VehicleOf(man);
  if (!Vehicle.Cast(vehicle) || (BBB.s_Hooks && BBB.s_Hooks.LeaveAlone(vehicle))) return false;
  return true;
 }

 // A player-level behaviour of his own other than the group's move (he sits in a turret: that move
 // is the vehicle's) and his own fire.
 protected static bool GunnerOwnOrder(SCR_AIUtilityComponent utility)
 {
  SCR_AIBehaviorBase current = utility.GetCurrentBehavior();
  if (current && current.EvaluatePriorityLevel() >= SCR_AIActionBase.PRIORITY_LEVEL_PLAYER && !GunnerMoveOrFire(current)) return true;
  if (!utility.m_BBBOrderActions) utility.m_BBBOrderActions = {};
  array<ref AIActionBase> actions = utility.m_BBBOrderActions;
  actions.Clear();
  bool held;
  utility.FindActionsOfInheritedType(SCR_AIBehaviorBase, actions);
  foreach (AIActionBase action : actions)
  {
   if (action.GetRemoveAction() || action.GetActionState() == EAIActionState.FAILED || action.GetActionState() == EAIActionState.COMPLETED) continue;
   if (action.EvaluatePriorityLevel() < SCR_AIActionBase.PRIORITY_LEVEL_PLAYER || GunnerMoveOrFire(action)) continue;
   held = true;
   break;
  }
  actions.Clear();
  return held;
 }

 protected static bool GunnerMoveOrFire(AIActionBase action)
 {
  return SCR_AIMoveInFormationBehavior.Cast(action) != null || SCR_AIMoveBehaviorBase.Cast(action) != null || SCR_AIAttackBehavior.Cast(action) != null || SCR_AISuppressBehavior.Cast(action) != null;
 }

 // The group's current waypoint is a Game Master's: placed in the editor (editable waypoint
 // prefab), or raised to GM priority. Editor waypoints keep priority 0, so the priority checks
 // never saw them. For decisions that would override where the GM sent the group.
 static bool GameMasterOrder(AIGroup group)
 {
  if (!group) return false;
  AIWaypoint waypoint = group.GetCurrentWaypoint();
  if (!waypoint) return false;
  SCR_AIWaypoint scripted = SCR_AIWaypoint.Cast(waypoint);
  if (scripted && scripted.GetPriorityLevel() >= SCR_AIActionBase.PRIORITY_LEVEL_GAMEMASTER) return true;
  return waypoint.FindComponent(SCR_EditableWaypointComponent) != null;
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
  return QueuedOrder(utility);
 }

 // A player-level behaviour of his own: running now or queued.
 static bool OwnOrder(SCR_AIUtilityComponent utility)
 {
  if (!utility) return false;
  SCR_AIBehaviorBase current = utility.GetCurrentBehavior();
  if (current && current.EvaluatePriorityLevel() >= SCR_AIActionBase.PRIORITY_LEVEL_PLAYER) return true;
  return QueuedOrder(utility);
 }

 protected static bool QueuedOrder(SCR_AIUtilityComponent utility)
 {
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

// BBB #15: one gate for squad tactics and fire and manoeuvre, from the host's BBB_Hooks.Intent,
// and the leash it sets. A squad allowed to plan is allowed to move under the same leash.
//  -bbbHostIntent 0   the older separate gates: AllowSquadTactic, AllowFireManeuver, HostLeash
class BBB_HostIntent
{
 static bool s_bOn = true;
 protected static int s_iRead;

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbHostIntent", value) && value == "0");
  PrintFormat("[BBB] HOST INTENT enabled=%1", s_bOn);
 }

 static BBB_EIntent Of(SCR_AIGroup group, out vector centre, out float radius)
 {
  Read();
  radius = 0;
  if (!group) return BBB_EIntent.NONE;
  if (s_bOn) return BBB.s_Hooks.Intent(group, centre, radius);
  if (!BBB.s_Hooks.HostLeash(group, centre, radius)) radius = 0;
  IEntity anchor = BBB.s_Hooks.AnchorOf(group);
  if (anchor && BBB_Tactic.Advancing(group, anchor.GetOrigin())) return BBB_EIntent.PUSH;
  return BBB_EIntent.HOLD;
 }

 // The host's leash for this squad, if it sets one.
 static bool Leash(SCR_AIGroup group, out vector centre, out float radius)
 {
  Of(group, centre, radius);
  return radius > 0;
 }

 static bool Fights(BBB_EIntent intent) { return intent == BBB_EIntent.PUSH || intent == BBB_EIntent.HOLD; }

 static bool TacticAllowed(SCR_AIGroup group, out BBB_EIntent intent)
 {
  vector centre;
  float radius;
  intent = Of(group, centre, radius);
  if (!s_bOn) return BBB.s_Hooks.AllowSquadTactic(group);
  return Fights(intent) && BBB.s_Hooks.AllowSquadTactic(group);
 }

 static bool FireAllowed(SCR_AIGroup group)
 {
  if (!group) return false;
  Read();
  if (!s_bOn) return BBB.s_Hooks.AllowFireManeuver(group);
  vector centre;
  float radius;
  return Fights(Of(group, centre, radius));
 }
}

class BBB_OrderLeash
{
 static bool s_bOn = true;
 static const float ROUTE_RADIUS = 150;
 static const float DEFEND_RADIUS = 35;
 static const float QUIET_MS = 75000;
 protected static ref map<SCR_AIGroup, ref BBB_OrderLease> s_Leases;
 // Called by BBB_StaticInit at game start (static initializer budget: tools/check_static_init.py).
 static void BBB_InitStatics()
 {
  s_Leases = new map<SCR_AIGroup, ref BBB_OrderLease>();
 }

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
  // The host's leash, when it sets one, is the area (BBB_Hooks.Intent / HostLeash).
  vector centre;
  float radius;
  if (s_bOn && BBB_HostIntent.Leash(group, centre, radius)) return vector.DistanceXZ(point, centre) <= radius;
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

// Gunners fire on the move: a turret gunner's attack and suppressive fire (vanilla's, and BBB's, which
// extend SCR_AISuppressBehavior) at the level of the forced move he rides on (GunnerOnTheMove). While the
// group's move (SCR_AIMoveInFormationBehavior, player level) is selected, normal-level fire never runs:
// measured 6 Nov, call-out fire started at 12 s and 22 s fired its first round only when the waypoint
// completed (27 s), and a gunner with an enemy in sight in 20-24 samples fired 0-1 rounds on the move.
modded class SCR_AIAttackBehavior
{
 override float EvaluatePriorityLevel()
 {
  float level = super.EvaluatePriorityLevel();
  if (level < PRIORITY_LEVEL_PLAYER && BBB_OrderIntent.GunnerOnTheMove(m_Utility)) return PRIORITY_LEVEL_PLAYER;
  return level;
 }
}

modded class SCR_AISuppressBehavior
{
 override float EvaluatePriorityLevel()
 {
  float level = super.EvaluatePriorityLevel();
  if (level < PRIORITY_LEVEL_PLAYER && BBB_OrderIntent.GunnerOnTheMove(m_Utility)) return PRIORITY_LEVEL_PLAYER;
  return level;
 }
}

modded class SCR_AIUtilityComponent
{
 float m_fBBBOnMoveUntil; // world time (ms) GunnerOnTheMove is cached to
 bool m_bBBBOnMove;
}
