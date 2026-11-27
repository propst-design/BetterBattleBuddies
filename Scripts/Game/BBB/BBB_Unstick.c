// Getting a stuck vehicle free: the game's AI driver often sits against a wall, a tree or another
// vehicle with the throttle down. The host decides when a vehicle is stuck (and what to do after);
// this does the manoeuvre, judged on what is actually around the vehicle (Jacob, 19 Nov: "the timed
// reverse often hinders more than helps"). The old manoeuvre reversed for a fixed 3.5 s with a fixed
// wheel whatever was behind: it rammed the vehicle queued behind, backed into ditches and soldiers,
// and repeated the same move at the same spot.
//  - Look first: rays from the rear bumper (both corners and the middle, two heights) and the ground
//    behind (a drop, a bank or water stops it like a wall); rays from the front corners angled out
//    either side find which way the nose has room to go.
//  - Room behind: reverse with the wheel turned so the nose swings towards the open side, only until
//    that side is open far enough to steer through (ROOM_M), the planned distance is backed, or
//    something comes up behind. Never into anything: the rear is watched while reversing.
//  - No room behind but one front corner open: turn out forward at a walking pace towards it.
//    Boxed in (or reversing but not moving: bogged, hung up) ends at once with that outcome
//    (Hopeless), so the caller hands off (re-route, dismount, hold) instead of trying again.
//  - Again at the same spot: the other side than last time, and further back (BACK_M + BACK_STEP_M a try).
// The caller's wheel is a hint, used when both sides are about as open; a caller's 0 means straight
// back (BackAway: the unarmed vehicle backing away from an armed one ahead, BBB_Traffic).
// A vehicle still rolling forward brakes to a stop first: the gearbox refuses reverse at speed
// (srv:mannersevade, 4 Nov). Inputs are written every frame (BBB.Frame): the vehicle controller drops
// inputs set less often. The driver's horn is released too: an AI driver parked mid-honk keeps it pressed.
// WaitingFor tells "not moving because waiting" (giving way, pulling over, its crew getting in, queued
// behind a stopped vehicle) from stuck; BBB's stall watches ask it before calling anything stuck.

// Told when a manoeuvre is over (finished, driver lost or timed out). LastOutcome says how it went.
class BBB_UnstickListener : Managed
{
 void OnUnstickEnded(IEntity vehicle) {}
}

// What the last manoeuvres at one spot did (escalation across attempts).
class BBB_UnstickMemo : Managed
{
 vector m_vAt;
 float m_fAt;      // world ms
 int m_iTries;     // manoeuvres at this spot
 int m_iSide;      // where the last one swung the nose: 1 right, -1 left, 0 straight
 int m_iOutcome;
}

class BBB_Unstick : Managed
{
 static const int OUTCOME_NONE = 0;
 static const int OUTCOME_ROOM = 1;      // backed out until the nose had room to steer
 static const int OUTCOME_BACKED = 2;    // backed the planned distance (or time)
 static const int OUTCOME_TURNED = 3;    // no room behind: turned out forward towards the open side
 static const int OUTCOME_SHORT = 4;     // stopped short: something came up behind
 static const int OUTCOME_BOXED = 5;     // no room behind and none ahead either side: nothing to do here
 static const int OUTCOME_WHEELS = 6;    // throttle on, not moving (bogged, hung up on something low)
 static const int OUTCOME_LOST = 7;      // driver gone, a player took the wheel, cancelled

 static const float REVERSE_MS = 3500;     // BackAway: straight back this long
 static const float REVERSE_MAX_MS = 7000; // an unstick reverses at most this long
 static const float TAIL_MS = 800;         // braking after it
 static const float ROLLING_SPEED = 1.5;   // m/s forward: brake to a stop before reversing
 static const float BRAKE_MAX_MS = 6000;   // at most this long braking first
 static const float PROBE_M = 10;          // how far behind (and out from the front corners) it looks
 static const float REAR_MIN_M = 1.6;      // less room than this behind: no reverse
 static const float REAR_STOP_M = 0.9;     // something this close behind mid-reverse: stop
 static const float MIN_BACK_M = 2.5;      // backed at least this far before "room to steer" counts
 static const float ROOM_M = 7;            // the nose's side open this far...
 static const float ROOM_AHEAD_M = 3.5;    // ...and straight ahead this far: room to steer
 static const float BACK_M = 4;            // planned back-out, first try at a spot...
 static const float BACK_STEP_M = 2.5;     // ...this much further each try after
 static const float BACK_MAX_M = 9;
 static const float SIDE_ANGLE = 35;       // degrees out from straight ahead, the front corner rays
 static const float SIDE_EVEN_M = 1.5;     // the two sides within this: the caller's hint picks
 static const float TURN_MS = 2500;        // turning out forward: at most this long...
 static const float TURN_M = 3;            // ...or this far
 static const float TURN_MIN_M = 4;        // a front corner open this far allows it
 static const float WHEELS_MS = 2500;      // reverse throttle this long without moving: bogged
 static const float PROBE_EVERY_MS = 200;
 static const float STEER = 0.7;           // short of full lock: a heavy 4x4 binds and crawls there (BBB_PullOver)
 static const float SAME_SPOT_M = 8;
 static const float MEMO_MS = 120000;
 static const float QUEUE_M = 12;          // a stopped vehicle this close ahead: queueing
 static const float BOARD_M = 15;          // its own men on foot this close: getting in
 static const int WAIT_NONE = 0;
 static const int WAIT_HELD = 1;
 static const int WAIT_CREW = 2;
 static const int WAIT_QUEUE = 3;

 protected static const int PHASE_BRAKE = 0;
 protected static const int PHASE_REVERSE = 1;
 protected static const int PHASE_TURN = 2;
 protected static const int PHASE_TAIL = 3;

 protected static ref array<ref BBB_Unstick> s_Active;
 protected static ref map<IEntity, ref BBB_UnstickMemo> s_Memo;
 protected static IEntity s_TraceSelf;
 static int s_iStarted;    // tests and the log
 static int s_iRefused;    // boxed in or bogged: ended without a move
 static int s_iLastNose;   // the last plan's nose side (1 right, -1 left, 0 straight) and...
 static float s_fLastGoal; // ...metres to back or turn out (tests and the log)
 // Called by BBB_StaticInit at game start (static initializer budget: tools/check_static_init.py).
 static void BBB_InitStatics()
 {
  s_Active = {};
  s_Memo = new map<IEntity, ref BBB_UnstickMemo>();
 }

 IEntity m_Vehicle;
 ChimeraCharacter m_Driver;
 protected float m_fStart;
 protected float m_fSteer;         // the caller's hint
 protected bool m_bBackAway;       // straight back for REVERSE_MS, rear watched (BBB_Traffic)
 protected int m_iPhase = PHASE_BRAKE;
 protected float m_fPhaseAt;
 protected float m_fWheel;         // the wheel this manoeuvre drives with
 protected int m_iNose;            // where the nose is meant to go: 1 right, -1 left, 0 straight
 protected float m_fGoal;          // metres to back (or turn out)
 protected vector m_vFrom;         // where the move started
 protected vector m_vMovedAt;      // last place it was seen moving...
 protected float m_fMovedAt;       // ...and when
 protected float m_fNextProbe;
 protected int m_iOutcome = OUTCOME_NONE;
 protected bool m_bParkedAI; // only hand back an AI this parked (something else may have parked it)
 protected BaseVehicleControllerComponent m_PilotHeld; // driver seat unhooked while we drive (BBB_PilotHold)
 protected ref BBB_UnstickListener m_Listener;
 protected string m_sLook;         // what it saw (the log)

 // Get this vehicle free; steer is the caller's wheel hint (-1..1, used when both sides are as open).
 static void Begin(IEntity vehicle, ChimeraCharacter driver, float steer, BBB_UnstickListener listener)
 {
  Start(vehicle, driver, steer, listener, false);
 }

 // Back straight away (from an armed vehicle ahead) for a few seconds, never into anything.
 static void BackAway(IEntity vehicle, ChimeraCharacter driver, BBB_UnstickListener listener)
 {
  Start(vehicle, driver, 0, listener, true);
 }

 protected static void Start(IEntity vehicle, ChimeraCharacter driver, float steer, BBB_UnstickListener listener, bool backAway)
 {
  if (!vehicle || !driver) return;
  if (BBB_Claims.Busy(vehicle, BBB_Claims.JOB_UNSTICK) || BBB_PullOver.Active(vehicle)) return;
  BBB_Unstick unstick = new BBB_Unstick();
  unstick.m_Vehicle = vehicle;
  unstick.m_Driver = driver;
  unstick.m_fStart = GetGame().GetWorld().GetWorldTime();
  unstick.m_fPhaseAt = unstick.m_fStart;
  unstick.m_fSteer = steer;
  unstick.m_bBackAway = backAway;
  unstick.m_Listener = listener;
  AIControlComponent ai = AIControlComponent.Cast(driver.FindComponent(AIControlComponent));
  if (ai && ai.IsAIActivated())
  {
   ai.DeactivateAI();
   unstick.m_bParkedAI = true;
  }
  ReleaseHorn(driver);
  s_Active.Insert(unstick);
  s_iStarted++;
  BBB_Claims.Mark(vehicle, BBB_Claims.JOB_UNSTICK);
 }

 // How the last manoeuvre on this vehicle ended (OUTCOME_NONE if none lately).
 static int LastOutcome(IEntity vehicle)
 {
  if (!vehicle || !s_Memo) return OUTCOME_NONE;
  BBB_UnstickMemo memo = s_Memo.Get(vehicle);
  if (!memo) return OUTCOME_NONE;
  return memo.m_iOutcome;
 }

 // Manoeuvres made at the spot the vehicle stands on now, lately.
 static int TriesHere(IEntity vehicle)
 {
  BBB_UnstickMemo memo = MemoHere(vehicle);
  if (!memo) return 0;
  return memo.m_iTries;
 }

 // Nothing more a manoeuvre can do from here: the caller should hand off, not try again.
 static bool Hopeless(int outcome)
 {
  return outcome == OUTCOME_BOXED || outcome == OUTCOME_WHEELS;
 }

 static string OutcomeName(int outcome)
 {
  switch (outcome)
  {
   case OUTCOME_ROOM: return "room to steer";
   case OUTCOME_BACKED: return "backed out";
   case OUTCOME_TURNED: return "turned out forward";
   case OUTCOME_SHORT: return "stopped short, something behind";
   case OUTCOME_BOXED: return "boxed in";
   case OUTCOME_WHEELS: return "wheels not gripping";
   case OUTCOME_LOST: return "driver lost";
  }
  return "none";
 }

 protected static BBB_UnstickMemo MemoHere(IEntity vehicle)
 {
  if (!vehicle || !s_Memo) return null;
  BBB_UnstickMemo memo = s_Memo.Get(vehicle);
  if (!memo) return null;
  float now = GetGame().GetWorld().GetWorldTime();
  if (now - memo.m_fAt > MEMO_MS || vector.DistanceXZ(memo.m_vAt, vehicle.GetOrigin()) > SAME_SPOT_M) return null;
  return memo;
 }

 // Throttle, wheel and brake for a parked driver's vehicle (written every frame: the vehicle controller
 // drops inputs set less often).
 static void Drive(ChimeraCharacter driver, VehicleWheeledSimulation sim, float throttle, float steer, float brake)
 {
  sim.SetThrottle(throttle);
  sim.SetSteering(steer);
  sim.SetBreak(brake, false);
 }

 // Let go of the horn (an AI parked mid-honk never releases it itself).
 static void ReleaseHorn(ChimeraCharacter character)
 {
  if (!character) return;
  CharacterControllerComponent controller = character.GetCharacterController();
  if (!controller) return;
  CharacterInputContext input = controller.GetInputContext();
  if (input && input.GetVehicleHorn() != 0) input.SetVehicleHorn(0);
 }

 // Drop a manoeuvre without telling its owner (it is gone). Inputs are still released and a
 // parked driver gets his AI back: dropping it bare left him frozen in his seat for good.
 static void Cancel(BBB_UnstickListener listener)
 {
  for (int i = s_Active.Count() - 1; i >= 0; i--)
  {
   BBB_Unstick unstick = s_Active[i];
   if (unstick.m_Listener != listener) continue;
   unstick.m_Listener = null;
   CarControllerComponent car;
   if (unstick.m_Vehicle) car = CarControllerComponent.Cast(unstick.m_Vehicle.FindComponent(CarControllerComponent));
   if (unstick.m_iOutcome == OUTCOME_NONE) unstick.m_iOutcome = OUTCOME_LOST;
   unstick.End(car);
   BBB_Claims.Unmark(unstick.m_Vehicle, BBB_Claims.JOB_UNSTICK);
   s_Active.RemoveOrdered(i);
  }
 }

 // BBB is stopping: finish every manoeuvre (inputs released, parked AI handed back), tell no one.
 static void StopAll()
 {
  foreach (BBB_Unstick unstick : s_Active)
  {
   unstick.m_Listener = null;
   CarControllerComponent car;
   if (unstick.m_Vehicle) car = CarControllerComponent.Cast(unstick.m_Vehicle.FindComponent(CarControllerComponent));
   unstick.End(car);
  }
  s_Active.Clear();
  s_Memo.Clear();
  BBB_Claims.UnmarkAll(BBB_Claims.JOB_UNSTICK);
 }

 static void FrameAll()
 {
  for (int i = s_Active.Count() - 1; i >= 0; i--)
  {
   if (s_Active[i].Frame()) continue;
   BBB_Claims.Unmark(s_Active[i].m_Vehicle, BBB_Claims.JOB_UNSTICK);
   s_Active.RemoveOrdered(i);
  }
  // Forget vehicles that are gone.
  for (int m = s_Memo.Count() - 1; m >= 0; m--) if (!s_Memo.GetKey(m)) s_Memo.RemoveElement(m);
 }

 // ---------------------------------------------------------------- waiting, not stuck

 // Why this vehicle is standing still on purpose (WAIT_NONE when nothing explains it), and in words:
 //  WAIT_HELD   another BBB manoeuvre has it (giving way, pulling over, backing away, an escape, the
 //              scripted driver) or something else drives its driver by hand: it ends on its own;
 //  WAIT_CREW   its own men are getting in or out, or walking up to it;
 //  WAIT_QUEUE  queued behind a vehicle (anyone's) that is stopped too.
 // The caller decides how long a crew or queue wait may last (the one ahead may never move).
 static int WaitingFor(IEntity vehicle, ChimeraCharacter driver, SCR_AIGroup group, out string why)
 {
  why = "";
  if (!vehicle) return WAIT_NONE;
  if (BBB_Claims.Busy(vehicle, BBB_Claims.JOB_GIVE_WAY)) why = "giving way";
  else if (BBB_Claims.Busy(vehicle, BBB_Claims.JOB_PULL_OVER)) why = "pulling over";
  else if (BBB_Claims.Busy(vehicle, BBB_Claims.JOB_UNSTICK)) why = "manoeuvring";
  else if (BBB_PilotHold.Driving(vehicle)) why = "driven by BBB";
  else if (driver && !BBB.PlayerOf(driver))
  {
   AIControlComponent ai = AIControlComponent.Cast(driver.FindComponent(AIControlComponent));
   if (ai && !ai.IsAIActivated()) why = "its driver is driven by hand";
  }
  if (!why.IsEmpty()) return WAIT_HELD;
  if (group && CrewBoarding(group, vehicle))
  {
   why = "its crew getting in or out";
   return WAIT_CREW;
  }
  if (QueuedBehind(vehicle))
  {
   why = "queued behind a stopped vehicle";
   return WAIT_QUEUE;
  }
  return WAIT_NONE;
 }

 // One of the group's men is getting in or out, or is on foot near the vehicle.
 static bool CrewBoarding(SCR_AIGroup group, IEntity vehicle)
 {
  array<AIAgent> agents = {};
  BBB.Members(group, agents);
  foreach (AIAgent agent : agents)
  {
   ChimeraCharacter man = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!man || !BBB.IsAlive(man)) continue;
   CharacterControllerComponent controller = man.GetCharacterController();
   if (controller && controller.IsUnconscious()) continue;
   CompartmentAccessComponent access = man.GetCompartmentAccessComponent();
   if (access && (access.IsGettingIn() || access.IsGettingOut())) return true;
   if (!BBB_Transport.VehicleOf(man) && vector.DistanceXZ(man.GetOrigin(), vehicle.GetOrigin()) < BOARD_M) return true;
  }
  return false;
 }

 // A vehicle (any side's, a civilian's too) nearly stopped right ahead in its lane.
 static bool QueuedBehind(IEntity vehicle)
 {
  vector mins, maxs;
  vehicle.GetBounds(mins, maxs);
  float half = Math.Max(Math.AbsFloat(mins[0]), Math.AbsFloat(maxs[0])) - 0.2;
  float height = mins[1] + 1.1;
  array<float> offsets = {-half, 0, half};
  foreach (float x : offsets)
  {
   IEntity hit;
   float free = RayLocal(vehicle, Vector(x, height, maxs[2] + 0.1), Vector(0, 0, 1), QUEUE_M, hit);
   if (free >= QUEUE_M || !hit) continue;
   IEntity root = hit.GetRootParent();
   if (!root) root = hit;
   if (!Vehicle.Cast(root)) continue;
   Physics physics = root.GetPhysics();
   if (!physics || physics.GetVelocity().Length() < 1.5) return true;
  }
  return false;
 }

 // ---------------------------------------------------------------- looking around

 // Free distance along a ray given in the vehicle's own frame (metres; 'range' when clear).
 protected static float RayLocal(IEntity vehicle, vector from, vector dir, float range, out IEntity hit)
 {
  BaseWorld world = GetGame().GetWorld();
  TraceParam trace = new TraceParam();
  trace.Start = vehicle.CoordToParent(from);
  trace.End = vehicle.CoordToParent(from + dir * range);
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  trace.Exclude = vehicle;
  s_TraceSelf = vehicle;
  float f = world.TraceMove(trace, Solid);
  s_TraceSelf = null;
  hit = trace.TraceEnt;
  if (f >= 1) return range;
  return f * range;
 }

 // What a vehicle can't push through: not bushes, nor its own parts.
 protected static bool Solid(notnull IEntity entity)
 {
  IEntity root = entity.GetRootParent();
  if (s_TraceSelf && (entity == s_TraceSelf || root == s_TraceSelf)) return false;
  EntityPrefabData data = entity.GetPrefabData();
  if (data)
  {
   string prefab = data.GetPrefabName();
   if (prefab.Contains("Vegetation/Bush")) return false;
  }
  return true;
 }

 // Room behind the rear bumper: rays from both corners and the middle at two heights, and the
 // ground behind (a drop, a bank or water is as good as a wall). Up to 'range'.
 static float RoomBehind(IEntity vehicle, float range)
 {
  vector mins, maxs;
  vehicle.GetBounds(mins, maxs);
  float half = Math.Max(Math.AbsFloat(mins[0]), Math.AbsFloat(maxs[0])) - 0.15;
  float z = mins[2] - 0.1;
  float room = range;
  array<float> offsets = {-half, 0, half};
  array<float> heights = {mins[1] + 0.55, mins[1] + 1.3};
  IEntity hit;
  foreach (float x : offsets)
   foreach (float y : heights)
    room = Math.Min(room, RayLocal(vehicle, Vector(x, y, z), Vector(0, 0, -1), range, hit));
  return Math.Min(room, GroundBehind(vehicle, z, range));
 }

 // How far back the ground stays drivable: no step up or down of more than a metre between
 // samples 1.5 m apart, no water.
 protected static float GroundBehind(IEntity vehicle, float z, float range)
 {
  BaseWorld world = GetGame().GetWorld();
  vector start = vehicle.CoordToParent(Vector(0, 0, z));
  float last = world.GetSurfaceY(start[0], start[2]);
  for (float d = 1.5; d <= range; d += 1.5)
  {
   vector p = vehicle.CoordToParent(Vector(0, 0, z - d));
   float y = world.GetSurfaceY(p[0], p[2]);
   if (y < 0.5 || BBB_Terrain.WaterDepthAt(p) > 0 || Math.AbsFloat(y - last) > 1.0) return Math.Max(0, d - 1.5);
   last = y;
  }
  return range;
 }

 // How far the nose can go out to one side (side 1 right, -1 left): a ray from that front corner,
 // SIDE_ANGLE out from straight ahead.
 static float RoomAhead(IEntity vehicle, int side, float range)
 {
  vector mins, maxs;
  vehicle.GetBounds(mins, maxs);
  float half = Math.Max(Math.AbsFloat(mins[0]), Math.AbsFloat(maxs[0])) - 0.15;
  float a = SIDE_ANGLE * Math.DEG2RAD;
  vector dir = Vector(Math.Sin(a) * side, 0, Math.Cos(a));
  IEntity hit;
  float low = RayLocal(vehicle, Vector(half * side, mins[1] + 0.7, maxs[2] + 0.1), dir, range, hit);
  float high = RayLocal(vehicle, Vector(half * side, mins[1] + 1.3, maxs[2] + 0.1), dir, range, hit);
  return Math.Min(low, high);
 }

 // Room straight ahead of the front bumper (both corners and the middle, bumper height).
 static float RoomStraight(IEntity vehicle, float range)
 {
  vector mins, maxs;
  vehicle.GetBounds(mins, maxs);
  float half = Math.Max(Math.AbsFloat(mins[0]), Math.AbsFloat(maxs[0])) - 0.15;
  float room = range;
  array<float> offsets = {-half, 0, half};
  IEntity hit;
  foreach (float x : offsets) room = Math.Min(room, RayLocal(vehicle, Vector(x, mins[1] + 0.9, maxs[2] + 0.1), Vector(0, 0, 1), range, hit));
  return room;
 }

 // ---------------------------------------------------------------- the manoeuvre

 // Returns false when finished.
 protected bool Frame()
 {
  float now = GetGame().GetWorld().GetWorldTime();
  IEntity vehicle = m_Vehicle;
  // A player who takes the driver over (a Game Master possessing him) or a driver who changed
  // seats ends it: inputs written every frame would steer the car under the player.
  bool alive = vehicle != null && m_Driver != null && BBB.IsAlive(m_Driver) && BBB.IsConscious(m_Driver) && BBB_Transport.VehicleOf(m_Driver) == vehicle && !BBB.PlayerOf(m_Driver) && BBB.DriverOf(vehicle) == m_Driver;
  // The car even when it ends that way: End lets go of the throttle, wheel and reverse gear once,
  // or a player taking the wheel would find it still backing out.
  CarControllerComponent car;
  if (vehicle) car = CarControllerComponent.Cast(vehicle.FindComponent(CarControllerComponent));
  if (!alive || !car || !car.GetSimulation())
  {
   if (m_iOutcome == OUTCOME_NONE) m_iOutcome = OUTCOME_LOST;
   End(car);
   return false;
  }
  VehicleWheeledSimulation sim = car.GetSimulation();
  Physics physics = vehicle.GetPhysics();
  if (physics && !physics.IsActive()) physics.SetActive(ActiveState.ACTIVE);
  if (!car.IsEngineOn()) car.ForceStartEngine();
  if (!m_PilotHeld && BBB_PilotHold.Take(car)) m_PilotHeld = car;
  car.SetPersistentHandBrake(false);
  ReleaseHorn(m_Driver);
  int neutral = sim.GearboxGearsCount() - sim.GearboxForwardGearsCount() - 1;
  sim.SetClutch(1);
  float elapsed = now - m_fPhaseAt;

  if (m_iPhase == PHASE_BRAKE)
  {
   // Rolling forward: brake to a stop first; the move is planned once it is slow.
   float forward;
   if (physics) forward = vector.Dot(physics.GetVelocity(), vehicle.GetTransformAxis(2));
   if (forward > ROLLING_SPEED && elapsed < BRAKE_MAX_MS)
   {
    Drive(m_Driver, sim, 0, 0, 1);
    return true;
   }
   Plan(now);
   if (m_iPhase == PHASE_TAIL) return Tail(sim, now - m_fPhaseAt, car);
   elapsed = 0;
  }

  if (m_iPhase == PHASE_REVERSE)
  {
   float backed = vector.DistanceXZ(vehicle.GetOrigin(), m_vFrom);
   NoteMoving(vehicle, now);
   int done = OUTCOME_NONE;
   float cap = REVERSE_MAX_MS;
   if (m_bBackAway) cap = REVERSE_MS;
   if (elapsed > cap) done = OUTCOME_BACKED;
   else if (!m_bBackAway && backed >= m_fGoal) done = OUTCOME_BACKED;
   else if (now - m_fMovedAt > WHEELS_MS) done = OUTCOME_WHEELS;
   else if (now >= m_fNextProbe)
   {
    m_fNextProbe = now + PROBE_EVERY_MS;
    if (RoomBehind(vehicle, 3) < REAR_STOP_M) done = OUTCOME_SHORT;
    else if (!m_bBackAway && m_iNose != 0 && backed >= MIN_BACK_M && RoomStraight(vehicle, ROOM_AHEAD_M) >= ROOM_AHEAD_M && RoomAhead(vehicle, m_iNose, ROOM_M) >= ROOM_M) done = OUTCOME_ROOM;
   }
   if (done != OUTCOME_NONE)
   {
    m_iOutcome = done;
    m_iPhase = PHASE_TAIL;
    m_fPhaseAt = now;
    return Tail(sim, 0, car);
   }
   if (sim.GetGear() >= neutral) sim.SetGear(neutral - 1);
   Drive(m_Driver, sim, 0.7, m_fWheel, 0);
   return true;
  }

  if (m_iPhase == PHASE_TURN)
  {
   float turned = vector.DistanceXZ(vehicle.GetOrigin(), m_vFrom);
   NoteMoving(vehicle, now);
   int stop = OUTCOME_NONE;
   if (turned >= m_fGoal || elapsed > TURN_MS) stop = OUTCOME_TURNED;
   else if (now - m_fMovedAt > WHEELS_MS) stop = OUTCOME_WHEELS;
   else if (now >= m_fNextProbe)
   {
    m_fNextProbe = now + PROBE_EVERY_MS;
    if (RoomAhead(vehicle, m_iNose, 2.5) < 1.2) stop = OUTCOME_TURNED;
   }
   if (stop != OUTCOME_NONE)
   {
    m_iOutcome = stop;
    m_iPhase = PHASE_TAIL;
    m_fPhaseAt = now;
    return Tail(sim, 0, car);
   }
   if (sim.GetGear() <= neutral) sim.SetGear(neutral + 1);
   float speed = 0;
   if (physics) speed = physics.GetVelocity().Length();
   float throttle = 0.6;
   if (speed > 2.5) throttle = 0;
   Drive(m_Driver, sim, throttle, m_fWheel, 0);
   return true;
  }

  return Tail(sim, elapsed, car);
 }

 // Seen moving lately (the bogged check).
 protected void NoteMoving(IEntity vehicle, float now)
 {
  if (vector.DistanceXZ(vehicle.GetOrigin(), m_vMovedAt) < 0.3) return;
  m_vMovedAt = vehicle.GetOrigin();
  m_fMovedAt = now;
 }

 // Braking after the move, then the driver gets his AI back.
 protected bool Tail(VehicleWheeledSimulation sim, float elapsed, CarControllerComponent car)
 {
  if (elapsed <= TAIL_MS)
  {
   Drive(m_Driver, sim, 0, 0, 1);
   return true;
  }
  End(car);
  return false;
 }

 // Stopped: look around and choose the move.
 protected void Plan(float now)
 {
  IEntity vehicle = m_Vehicle;
  m_fPhaseAt = now;
  m_fNextProbe = now + PROBE_EVERY_MS;
  m_vFrom = vehicle.GetOrigin();
  m_vMovedAt = m_vFrom;
  m_fMovedAt = now;
  float behind = RoomBehind(vehicle, PROBE_M);
  if (m_bBackAway)
  {
   m_iNose = 0;
   m_fWheel = 0;
   m_fGoal = PROBE_M;
   m_sLook = string.Format("backing away, %1 m clear behind", Math.Round(behind * 10) / 10);
   if (behind < REAR_MIN_M)
   {
    m_iOutcome = OUTCOME_BOXED;
    m_iPhase = PHASE_TAIL;
    s_iRefused++;
    Print(string.Format("[BBB] UNSTICK %1: %2: no room to back away", BBB_Traffic.Name(vehicle), m_sLook));
    return;
   }
   m_iPhase = PHASE_REVERSE;
   return;
  }
  BBB_UnstickMemo memo = MemoHere(vehicle);
  int tries = 1;
  int lastSide = 0;
  if (memo)
  {
   tries = memo.m_iTries + 1;
   lastSide = memo.m_iSide;
  }
  float right = RoomAhead(vehicle, 1, PROBE_M);
  float left = RoomAhead(vehicle, -1, PROBE_M);
  // Which way the nose should go: the more open side; even, the caller's hint (its wheel turns the
  // nose the other way when reversing); a repeat at the same spot takes the side not tried last.
  int side = 0;
  if (right - left > SIDE_EVEN_M) side = 1;
  else if (left - right > SIDE_EVEN_M) side = -1;
  else if (m_fSteer > 0.05) side = -1;
  else if (m_fSteer < -0.05) side = 1;
  else if (right >= left) side = 1;
  else side = -1;
  if (tries > 1 && lastSide != 0 && side == lastSide && (memo.m_iOutcome != OUTCOME_ROOM || Math.AbsFloat(right - left) <= SIDE_EVEN_M * 2)) side = -lastSide;
  m_iNose = side;
  m_sLook = string.Format("try %1 here, %2 m clear behind, nose room left %3 m right %4 m", tries, Math.Round(behind * 10) / 10, Math.Round(left * 10) / 10, Math.Round(right * 10) / 10);
  if (behind >= REAR_MIN_M)
  {
   // Reversing with the wheel turned swings the nose the other way.
   m_fWheel = -STEER * side;
   m_fGoal = Math.Min(BACK_M + BACK_STEP_M * (tries - 1), BACK_MAX_M);
   m_fGoal = Math.Min(m_fGoal, behind - REAR_STOP_M);
   m_iPhase = PHASE_REVERSE;
  }
  else
  {
   // Nothing behind to use: turn out forward towards an open front corner, if there is one.
   float open = right;
   if (side < 0) open = left;
   if (open < TURN_MIN_M && Math.Max(left, right) >= TURN_MIN_M)
   {
    side = 1;
    if (left > right) side = -1;
    open = Math.Max(left, right);
    m_iNose = side;
   }
   if (open >= TURN_MIN_M)
   {
    m_fWheel = STEER * side;
    m_fGoal = TURN_M;
    m_iPhase = PHASE_TURN;
   }
   else
   {
    m_iOutcome = OUTCOME_BOXED;
    m_iPhase = PHASE_TAIL;
    s_iRefused++;
   }
  }
  if (!memo)
  {
   memo = new BBB_UnstickMemo();
   s_Memo.Set(vehicle, memo);
   memo.m_vAt = vehicle.GetOrigin();
   memo.m_iTries = 0;
  }
  memo.m_iTries = tries;
  memo.m_iSide = m_iNose;
  s_iLastNose = m_iNose;
  s_fLastGoal = m_fGoal;
  memo.m_fAt = now;
  string move = "boxed in, no move";
  if (m_iPhase == PHASE_REVERSE) move = string.Format("reversing up to %1 m, nose to the %2", Math.Round(m_fGoal * 10) / 10, Side(m_iNose));
  else if (m_iPhase == PHASE_TURN) move = string.Format("no room behind, turning out forward to the %1", Side(m_iNose));
  Print(string.Format("[BBB] UNSTICK %1: %2: %3", BBB_Traffic.Name(vehicle), m_sLook, move));
 }

 protected static string Side(int side)
 {
  if (side > 0) return "right";
  if (side < 0) return "left";
  return "rear";
 }

 protected void End(CarControllerComponent car)
 {
  bool leaveAlone = (m_Driver && BBB.PlayerOf(m_Driver)) || (BBB.s_Hooks && BBB.s_Hooks.LeaveAlone(m_Vehicle));
  if (!leaveAlone && car && car.GetSimulation())
  {
   VehicleWheeledSimulation sim = car.GetSimulation();
   Drive(m_Driver, sim, 0, 0, 0);
   int neutral = sim.GearboxGearsCount() - sim.GearboxForwardGearsCount() - 1;
   sim.SetGear(neutral + 1);
  }
  if (m_PilotHeld) BBB_PilotHold.Give(m_PilotHeld);
  m_PilotHeld = null;
  ReleaseHorn(m_Driver);
  if (m_bParkedAI && !leaveAlone && m_Driver && BBB.IsAlive(m_Driver))
  {
   AIControlComponent ai = AIControlComponent.Cast(m_Driver.FindComponent(AIControlComponent));
   if (ai && !ai.IsAIActivated()) ai.ActivateAI();
  }
  if (m_Vehicle && !m_bBackAway && s_Memo)
  {
   BBB_UnstickMemo memo = s_Memo.Get(m_Vehicle);
   if (memo) memo.m_iOutcome = m_iOutcome;
  }
  if (m_Vehicle && m_iOutcome != OUTCOME_NONE && m_iOutcome != OUTCOME_LOST && m_iPhase != PHASE_BRAKE)
   Print(string.Format("[BBB] UNSTICK %1: done, %2 (%3 m from where it started)", BBB_Traffic.Name(m_Vehicle), OutcomeName(m_iOutcome), Math.Round(vector.DistanceXZ(m_Vehicle.GetOrigin(), m_vFrom) * 10) / 10));
  if (m_Listener) m_Listener.OnUnstickEnded(m_Vehicle);
 }
}
