// Soldiers jammed in a doorway: a door leaf left half-open across the path, or
// swung back into the squad. The engine walks infantry through house doors itself
// and gives script no hook, so this watches for the symptom instead. Once a second,
// a foot soldier who has a path to follow but has not moved 0.4 m in 3 s gets the
// house door next to him (within 1.6 m) swung fully open, or over to its other side
// if it already is. Positions only: the door search runs for stalled soldiers alone.
// Never closes a door. -bbbDoorUnstick 0 turns it off.

class BBB_DoorStall : Managed
{
 vector m_Pos;
 float m_Since;     // world ms when he stopped making progress
 float m_Next;      // no new swing for him before this
 BaseDoorComponent m_Door;    // the door last swung for him (until he moves again)
 int m_Tries;
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

 static int s_iRead;
 static bool s_bOn;
 static int s_iStalls;  // stalled soldiers found next to a house door
 static int s_iSwings;  // doors swung
 static int s_iFreed;   // soldiers who moved again after a swing
 protected static int s_iTick;
 protected static ref map<IEntity, ref BBB_DoorStall> s_Stalls = new map<IEntity, ref BBB_DoorStall>();
 protected static ref array<SCR_AIGroup> s_Groups = {};
 protected static ref array<AIAgent> s_Agents = {};
 protected static ref array<vector> s_Path = {};
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
  BBB.s_Hooks.GetGroups(s_Groups);
  foreach (SCR_AIGroup group : s_Groups)
  {
   if (!group || !BBB.s_Hooks.AllowDoorUnstick(group)) continue;
   BBB.s_Hooks.GetMembers(group, s_Agents);
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
  return WantsToMove(agent);
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
   return;
  }
  if (now - stall.m_Since < STALL_MS || now < stall.m_Next) return;
  BaseDoorComponent door = HouseDoorNear(pos);
  if (!door) { stall.m_Next = now + RETRY_MS; return; }
  if (stall.m_Tries >= MAX_TRIES)
  {
   // Not the door's fault (or not one we can fix): leave him to the engine for a while.
   stall.m_Tries = 0;
   stall.m_Next = now + BACKOFF_MS;
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
