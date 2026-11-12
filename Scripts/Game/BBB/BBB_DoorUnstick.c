// Soldiers jammed in a doorway: a door leaf left half-open across the path, or
// swung back into the squad. The engine walks infantry through house doors itself
// and gives script no hook, so this watches for the symptom instead. Once a second,
// a foot soldier who has a path to follow but has not moved 0.4 m in 3 s gets the
// house door next to him (within 1.6 m) swung fully open, or over to its other side
// if it already is. Positions only: the door search runs for stalled soldiers alone.
// Never closes a door. -bbbDoorUnstick 0 turns it off.
// A man the swings don't free isn't jammed by the leaf: after ROUNDS_BEFORE_STEP rounds he is nudged
// STEP_M along his own path (a little further each time, on the navmesh); after MAX_STEPS he and the
// door are left alone for GIVE_UP_MS (an AC: Actual playtest: one door swung 237 times in 55 minutes
// for one man). The nudge is BBB's one teleport of a soldier on foot, a narrow exception to the
// no-teleport rule (Jacob, 14 Nov): short, only along a path he is walking and past its next metre,
// and never a man at a post: in cover (sandbags), on a Defend waypoint, indoors on a Buildings job or
// committed to a gun, rescue or ammo run (OnPost).

class BBB_DoorStall : Managed
{
 vector m_Pos;
 float m_Since;     // world ms when he stopped making progress
 float m_Next;      // no new swing for him before this
 BaseDoorComponent m_Door;    // the door last swung for him (until he moves again)
 int m_Tries;
 int m_Rounds;      // rounds of MAX_TRIES swings that freed nobody
 int m_Steps;       // times set further along his path
 vector m_StepAt;   // where the last one put him
 int m_Seen;        // tick stamp
}

class BBB_DoorUnstick
{
 static const float STALL_MS = 3000;
 static const float MOVED_M = 0.4;
 static const float DOOR_RADIUS = 1.6;
 static const float HOUSE_DOOR_MAX = 2.5; // bounds diagonal (XZ); gates are bigger
 static const float RETRY_MS = 3000;
 static const int MAX_TRIES = 3;
 static const float BACKOFF_MS = 30000;
 static const int ROUNDS_BEFORE_STEP = 2;
 static const float STEP_M = 1.5;         // along his path, times the step's number (was 3 m: Jacob, 14 Nov)
 static const float STEP_BEYOND_M = 1;    // his path must go on at least this far past the nudge
 static const int MAX_STEPS = 3;
 static const float STEP_CLEAR_M = 15;    // this far from the last step: that one worked
 static const float GIVE_UP_MS = 300000;

 static int s_iRead;
 static bool s_bOn;
 static int s_iStalls;  // stalled soldiers found next to a house door
 static int s_iSwings;  // doors swung
 static int s_iFreed;   // soldiers who moved again after a swing
 static int s_iSteps;   // soldiers set further along their path
 static int s_iGaveUp;  // soldiers left alone with their door
 protected static int s_iTick;
 protected static ref map<IEntity, ref BBB_DoorStall> s_Stalls;
 protected static ref array<SCR_AIGroup> s_Groups;
 protected static ref array<AIAgent> s_Agents;
 protected static ref array<vector> s_Path;
 // Called by BBB_StaticInit at game start (static initializer budget: tools/check_static_init.py).
 static void BBB_InitStatics()
 {
  s_Stalls = new map<IEntity, ref BBB_DoorStall>();
  s_Groups = {};
  s_Agents = {};
  s_Path = {};
 }
 protected static vector s_vQuery;
 protected static BaseDoorComponent s_Best;
 protected static float s_fBest;

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbDoorUnstick", value) && value == "0");
  PrintFormat("[BBB] DOORUNSTICK enabled=%1", s_bOn);
 }

 static void Stop()
 {
  s_Stalls.Clear();
 }

 static void Tick()
 {
  Read();
  if (!s_bOn) { s_Stalls.Clear(); return; }
  s_iTick++;
  float now = GetGame().GetWorld().GetWorldTime();
  BBB.Groups(s_Groups);
  foreach (SCR_AIGroup group : s_Groups)
  {
   if (!group || !BBB.s_Hooks.AllowDoorUnstick(group)) continue;
   BBB.Members(group, s_Agents);
   foreach (AIAgent agent : s_Agents)
    if (agent) Watch(agent, now);
  }
  for (int i = s_Stalls.Count() - 1; i >= 0; i--)
  {
   BBB_DoorStall stale = s_Stalls.GetElement(i);
   if (!s_Stalls.GetKey(i) || !stale || stale.m_Seen != s_iTick) s_Stalls.RemoveElement(i);
  }
 }

 // A soldier on foot under AI control, with a path he is meant to be walking.
 static bool Eligible(AIAgent agent, ChimeraCharacter body)
 {
  if (!body || !BBB.IsAlive(body) || body.IsInVehicle() || BBB_Drag.Busy(body)) return false;
  CharacterControllerComponent cc = body.GetCharacterController();
  if (!cc || cc.IsUnconscious() || cc.IsPlayerControlled() || cc.IsClimbing() || cc.IsSwimming()) return false;
  AIControlComponent control = AIControlComponent.Cast(body.FindComponent(AIControlComponent));
  if (!control || !control.IsAIActivated()) return false;
  return HasRequest(agent);
 }

 // Cheap, every tick: a move he hasn't finished. The path itself (copied out) is only read
 // once he has stood still long enough to count as stalled (WantsToMove).
 static bool HasRequest(AIAgent agent)
 {
  AIBaseMovementComponent move = agent.GetMovementComponent();
  return move && !move.HasCompletedRequest(false);
 }

 static bool WantsToMove(AIAgent agent)
 {
  AIBaseMovementComponent move = agent.GetMovementComponent();
  if (!move || move.HasCompletedRequest(false)) return false;
  s_Path.Clear();
  move.GetCurrentPath(s_Path);
  return s_Path.Count() > 0;
 }

 protected static void Watch(AIAgent agent, float now)
 {
  ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
  if (!body) return;
  BBB_DoorStall stall = s_Stalls.Get(body);
  if (!Eligible(agent, body))
  {
   if (stall) s_Stalls.Remove(body);
   return;
  }
  vector pos = body.GetOrigin();
  if (!stall)
  {
   stall = new BBB_DoorStall();
   stall.m_Pos = pos;
   stall.m_Since = now;
   s_Stalls.Insert(body, stall);
  }
  stall.m_Seen = s_iTick;
  if (vector.DistanceXZ(pos, stall.m_Pos) >= MOVED_M)
  {
   if (stall.m_Door)
   {
    s_iFreed++;
    PrintFormat("[BBB] DOORUNSTICK freed %1 after %2 swing(s)", body, stall.m_Tries);
   }
   stall.m_Pos = pos;
   stall.m_Since = now;
   stall.m_Door = null;
   stall.m_Tries = 0;
   stall.m_Rounds = 0;
   if (stall.m_Steps > 0 && vector.DistanceXZ(pos, stall.m_StepAt) >= STEP_CLEAR_M) stall.m_Steps = 0;
   return;
  }
  if (now - stall.m_Since < STALL_MS || now < stall.m_Next) return;
  if (!WantsToMove(agent)) { stall.m_Since = now; return; } // no path to walk: not jammed
  BaseDoorComponent door = HouseDoorNear(pos);
  if (!door) { stall.m_Next = now + RETRY_MS; return; }
  if (stall.m_Tries >= MAX_TRIES)
  {
   RoundFailed(agent, body, stall, pos, now);
   return;
  }
  if (stall.m_Tries == 0) s_iStalls++;
  Swing(door);
  stall.m_Door = door;
  stall.m_Tries++;
  stall.m_Next = now + RETRY_MS;
  s_iSwings++;
  PrintFormat("[BBB] DOORUNSTICK swing %1 for %2 try=%3 state=%4", stall.m_Door, body, stall.m_Tries, door.GetControlValue());
 }

 // MAX_TRIES swings freed nobody: not the door's fault (or not one we can fix). Leave him to the
 // engine for a while; after ROUNDS_BEFORE_STEP rounds set him along his path (WantsToMove has just
 // read it); after MAX_STEPS leave him and the door alone.
 static void RoundFailed(AIAgent agent, ChimeraCharacter body, BBB_DoorStall stall, vector pos, float now)
 {
  stall.m_Tries = 0;
  stall.m_Rounds++;
  stall.m_Next = now + BACKOFF_MS;
  if (stall.m_Rounds < ROUNDS_BEFORE_STEP) return;
  stall.m_Rounds = 0;
  if (stall.m_Steps >= MAX_STEPS)
  {
   stall.m_Steps = 0;
   stall.m_Door = null;
   stall.m_Next = now + GIVE_UP_MS;
   s_iGaveUp++;
   PrintFormat("[BBB] DOORUNSTICK gave up on %1 at %2: not a door he can get past", body, pos);
   return;
  }
  vector onward;
  if (OnPost(agent, body)) return; // a man at his post is never moved off it: the engine keeps him
  if (!StepTarget(agent, pos, STEP_M * (stall.m_Steps + 1), onward)) return;
  stall.m_Steps++;
  stall.m_StepAt = onward;
  stall.m_Pos = onward;
  stall.m_Since = now;
  stall.m_Door = null;
  vector transform[4];
  body.GetWorldTransform(transform);
  transform[3] = onward;
  body.Teleport(transform);
  s_iSteps++;
  PrintFormat("[BBB] DOORUNSTICK %1 set %2 m along his path, step %3", body, vector.Distance(pos, onward), stall.m_Steps);
 }

 // A man holding a post: in cover (behind sandbags or a wall: vanilla's flag), his squad on a Defend
 // waypoint, indoors on a Buildings job, or committed to a static gun, rescue or ammo run.
 static bool OnPost(AIAgent agent, ChimeraCharacter body)
 {
  int role = BBB_Claims.Role(body);
  if (role == BBB_Claims.BUILDINGS || BBB_Claims.Committed(body)) return true;
  SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
  if (utility && utility.m_CombatMoveState && utility.m_CombatMoveState.m_bInCover) return true;
  AIGroup group = agent.GetParentGroup();
  return group && SCR_DefendWaypoint.Cast(group.GetCurrentWaypoint()) != null;
 }

 // The point 'distance' m along the path he was just given (WantsToMove read it), on the navmesh,
 // only if the path goes on past it (STEP_BEYOND_M): a man walking the last metre to his spot
 // isn't jammed in a doorway.
 static bool StepTarget(AIAgent agent, vector pos, float distance, out vector onward)
 {
  float left = distance;
  vector from = pos;
  vector target = pos;
  foreach (vector point : s_Path)
  {
   float leg = vector.DistanceXZ(from, point);
   if (leg <= 0.01) continue;
   if (leg >= left)
   {
    target = from + (point - from) * (left / leg);
    left = 0;
    break;
   }
   left -= leg;
   from = point;
   target = point;
  }
  if (left > 0 || vector.DistanceXZ(pos, target) < 1) return false;
  if (!PathGoesOn(target, STEP_BEYOND_M)) return false;
  AIPathfindingComponent pathing = AIPathfindingComponent.Cast(agent.FindComponent(AIPathfindingComponent));
  if (!pathing) return false;
  return pathing.GetClosestPositionOnNavmesh(target, Vector(2, 3, 2), onward);
 }

 // The path read by WantsToMove reaches at least 'beyond' m (XZ) further than 'from'.
 static bool PathGoesOn(vector from, float beyond)
 {
  if (s_Path.IsEmpty()) return false;
  return vector.DistanceXZ(from, s_Path[s_Path.Count() - 1]) >= beyond;
 }

 // Fully open the way it was already going; if it is fully open, over to the other side.
 static void Swing(BaseDoorComponent door)
 {
  float value = door.GetControlValue();
  float target = 1;
  if (Math.AbsFloat(value) < 0.9)
  {
   if (value < 0) target = -1;
  }
  else if (value > 0) target = -1;
  door.SetControlValue(target, RplId.Invalid());
 }

 // The nearest house-sized door within DOOR_RADIUS of a point (hinge or leaf centre).
 static BaseDoorComponent HouseDoorNear(vector pos)
 {
  s_vQuery = pos;
  s_Best = null;
  s_fBest = DOOR_RADIUS;
  GetGame().GetWorld().QueryEntitiesBySphere(pos, DOOR_RADIUS + 1.0, ConsiderDoor);
  BaseDoorComponent best = s_Best;
  s_Best = null;
  return best;
 }

 protected static bool ConsiderDoor(IEntity entity)
 {
  BaseDoorComponent door = BaseDoorComponent.Cast(entity.FindComponent(BaseDoorComponent));
  if (!door) return true;
  vector mins, maxs;
  entity.GetBounds(mins, maxs);
  vector size = maxs - mins;
  size[1] = 0;
  if (size.Length() > HOUSE_DOOR_MAX) return true;
  float distance = Math.Min(vector.DistanceXZ(s_vQuery, door.GetDoorPivotPointWS()), vector.DistanceXZ(s_vQuery, SCR_EntityHelper.GetEntityCenterWorld(entity)));
  if (distance < s_fBest)
  {
   s_fBest = distance;
   s_Best = door;
  }
  return true;
 }
}
