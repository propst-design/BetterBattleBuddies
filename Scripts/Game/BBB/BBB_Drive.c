// Scripted driver (10 Nov 2026, Jacob: "if we can offer true improvement to the players").
// The game's AI driver plans a vehicle's route well (roads first, a vehicle navmesh off them)
// but drives it badly: it cuts corners into walls, stalls on hills, gives up and puts the crew
// out (one 5 h AC: Actual match: 139 stuck episodes, 62 of them given up). BBB drives the route
// itself instead: the game still plans it (AIBaseMovementComponent.GetCurrentPath, read once the
// move is given), then the driver's AI is parked, the seat unhooked (BBB_PilotHold) and BBB
// steers, accelerates, brakes and shifts every frame:
//  - path following: pure pursuit with a short look-ahead in bends plus a cross-track term, so it
//    keeps the planned line instead of cutting inside a corner;
//  - speed: from the curvature ahead (sideways grip), braking distance to the next bend and to
//    the end, and the guard;
//  - guard: rays along the corridor the planned line sweeps ahead, and along the arc the wheels
//    are turned to; anything in it within braking distance slows or stops the vehicle;
//  - gearbox: shifts on drivetrain rpm, pulls away in first, slips the clutch on a hill start
//    (the same rules as AC: Actual's gunner drive, measured there on 31 % roads);
//  - recovery: stuck against something, it backs out (BBB_Unstick) and carries on; after
//    RECOVER_LIMIT tries the vehicle goes back to the game's driver and the host's own handling.
// Hand-back to the game's driver: contact (hit, or enemies the group knows of near), a player
// or Game Master taking the wheel, a new order elsewhere, the host's hook saying no, arrival.
// Only wheeled vehicles with an AI driver, moves of at least MIN_TRIP. Off by default (-bbbDriver 1).
// Hosts: BBB_Hooks.AllowScriptedDriving(group, vehicle) (AC: Actual's policy in ACR_BBBHooks).
class BBB_DrivePath : Managed
{
 ref array<vector> m_aPts;
 ref array<float> m_aS;      // distance along the path at each point
 ref array<float> m_aV;      // speed limit at each point (m/s), after braking distances
 ref array<float> m_aK;      // curvature at each point (1/m)
 float m_fLength;

 void BBB_DrivePath()
 {
  m_aPts = {};
  m_aS = {};
  m_aV = {};
  m_aK = {};
 }

 // Points every STEP metres along the given polyline.
 void Build(notnull array<vector> raw, float step)
 {
  m_aPts.Clear();
  m_aS.Clear();
  if (raw.Count() < 2) return;
  float s = 0;
  m_aPts.Insert(raw[0]);
  m_aS.Insert(0);
  for (int i = 1; i < raw.Count(); i++)
  {
   vector a = raw[i - 1];
   vector b = raw[i];
   float len = vector.DistanceXZ(a, b);
   if (len < 0.01) continue;
   int n = Math.Max(1, Math.Ceil(len / step));
   float nf = n;
   for (int k = 1; k <= n; k++)
   {
    vector p = a + (b - a) * (k / nf);
    s += len / nf;
    m_aPts.Insert(p);
    m_aS.Insert(s);
   }
  }
  m_fLength = s;
 }

 // Curvature from points ~SPAN m either side, a speed limit from grip, then braking distances
 // backwards from every bend (and to a stop at the end).
 void Profile(float topSpeed, float sideAccel, float brakeAccel)
 {
  int count = m_aPts.Count();
  m_aK.Clear();
  m_aV.Clear();
  for (int i = 0; i < count; i++)
  {
   int a = Math.Max(0, i - 3);
   int b = Math.Min(count - 1, i + 3);
   float k = 0;
   if (b - a >= 2)
   {
    vector u = m_aPts[i] - m_aPts[a];
    vector w = m_aPts[b] - m_aPts[i];
    float turn = Math.AbsFloat(Math.Atan2(u[0] * w[2] - u[2] * w[0], u[0] * w[0] + u[2] * w[2]));
    float span = (m_aS[b] - m_aS[a]) * 0.5;
    if (span > 0.5) k = turn / span;
   }
   m_aK.Insert(k);
   float v = topSpeed;
   if (k > 0.0005) v = Math.Min(v, Math.Sqrt(sideAccel / k));
   m_aV.Insert(Math.Max(2.5, v));
  }
  if (count == 0) return;
  m_aV[count - 1] = 0;
  for (int j = count - 2; j >= 0; j--)
  {
   float d = m_aS[j + 1] - m_aS[j];
   float reach = Math.Sqrt(m_aV[j + 1] * m_aV[j + 1] + 2 * brakeAccel * d);
   if (reach < m_aV[j]) m_aV[j] = reach;
  }
 }

 int Count() { return m_aPts.Count(); }

 // Nearest point to p, searched from 'from' back a little and forward 'ahead' points.
 int Nearest(vector p, int from, int ahead)
 {
  int count = m_aPts.Count();
  int first = Math.Max(0, from - 3);
  int last = Math.Min(count - 1, from + ahead);
  int best = from;
  float bestD = float.MAX;
  for (int i = first; i <= last; i++)
  {
   float d = vector.DistanceXZ(p, m_aPts[i]);
   if (d < bestD) { bestD = d; best = i; }
  }
  return best;
 }

 // Index of the point 'dist' metres along from index i.
 int Ahead(int i, float dist)
 {
  int count = m_aPts.Count();
  float target = m_aS[i] + dist;
  int j = i;
  while (j + 1 < count && m_aS[j] < target) j++;
  return j;
 }

 // Largest curvature over the next 'dist' metres.
 float MaxCurvature(int i, float dist)
 {
  int j = Ahead(i, dist);
  float k = 0;
  for (int n = i; n <= j; n++) k = Math.Max(k, m_aK[n]);
  return k;
 }
}

class BBB_DriveUnstick : BBB_UnstickListener
{
 BBB_Drive m_Drive;
 void BBB_DriveUnstick(BBB_Drive drive) { m_Drive = drive; }
 override void OnUnstickEnded(IEntity vehicle) { if (m_Drive) m_Drive.OnRecovered(); }
}

class BBB_Drive : Managed
{
 static bool s_bOn;
 static const float MIN_TRIP = 300;          // shorter moves stay with the game's driver
 static const float PLAN_WAIT_MS = 6000;     // the game's plan must arrive by then (else roads)
 static const float STEP = 3;                // path point spacing (m)
 static const float TOP_SPEED = 17;          // m/s (61 km/h); the vehicle's own top speed caps it too
 static const float SIDE_ACCEL = 2.8;        // m/s2 sideways in a bend (2.2 ran 15 % slower than the game's driver)
 static const float BRAKE_ACCEL = 3.0;       // m/s2 planned braking
 static const float GUARD_DECEL = 3.5;       // m/s2 when something is in the way
 static const float PROBE_MS = 200;
 static const float STALL_MS = 3500;         // throttle on, not moving: stuck
 static const float BLOCKED_MS = 8000;       // something in the way that doesn't clear
 static const int RECOVER_LIMIT = 3;         // back-outs within RECOVER_WINDOW_MS, then hand back
 static const float RECOVER_WINDOW_MS = 120000;
 static const float CONTACT_REST_MS = 30000; // after a hand-back for contact, wait before driving again
 static const float ENEMY_RANGE = 300;
 static const float ARRIVE_M = 15;
 static const float SHIFT_COOLDOWN_MS = 1500;

 protected static ref array<ref BBB_Drive> s_Active;
 protected static ref map<SCR_AIGroup, float> s_Pending;  // move seen at (world ms): waiting for the game's plan
 protected static ref map<SCR_AIGroup, float> s_Rest;     // not before (world ms)
 static int s_iStarted;
 static int s_iArrived;
 static int s_iHandedBack;
 static int s_iRecoveries;
 static int s_iFrames;
 static int s_iFrameMs;
 static int s_iGuardStops;
 static string s_sLastEnd;

 static void BBB_InitStatics()
 {
  s_Active = {};
  s_Pending = new map<SCR_AIGroup, float>();
  s_Rest = new map<SCR_AIGroup, float>();
 }

 static void Read()
 {
  string value;
  s_bOn = false;
  if (BBB_Settings.Get("bbbDriver", value)) s_bOn = value == "1";
  s_bGameMaster = true;
  if (BBB_Settings.Get("bbbDriverGM", value)) s_bGameMaster = value != "0";
  PrintFormat("[BBB] DRIVER on=%1", s_bOn);
 }

 // ---------------------------------------------------------------- the session

 SCR_AIGroup m_Group;
 IEntity m_Vehicle;
 ChimeraCharacter m_Driver;
 AIWaypoint m_Waypoint;
 vector m_vGoal;
 ref BBB_DrivePath m_Path;
 string m_sSource;
 int m_iIdx;
 protected bool m_bParkedAI;
 protected BaseVehicleControllerComponent m_PilotHeld;
 protected float m_fSteer;
 protected float m_fThrottle;
 protected float m_fLastFrame = -1;
 protected float m_fLastShift;
 protected float m_fGuardCap = 99;
 protected float m_fNextProbe;
 protected float m_fStallSince = -1;
 protected float m_fBlockedSince = -1;
 protected bool m_bRecovering;
 protected ref BBB_DriveUnstick m_Listener;
 protected ref array<float> m_aRecoveries;
 protected float m_fGain;          // curvature per unit of steering, learnt while driving
 protected float m_fHalfWidth = 1.3;
 protected float m_fFront = 3;
 float m_fMaxXte;
 float m_fSumXte;
 protected float m_fLastHeading;
 int m_iXteN;
 float m_fStarted;
 vector m_vStart;

 // ---------------------------------------------------------------- manager

 static bool Driving(IEntity vehicle)
 {
  if (!vehicle || !s_Active) return false;
  foreach (BBB_Drive d : s_Active) if (d.m_Vehicle == vehicle) return true;
  return false;
 }

 static BBB_Drive Of(IEntity vehicle)
 {
  if (!vehicle || !s_Active) return null;
  foreach (BBB_Drive d : s_Active) if (d.m_Vehicle == vehicle) return d;
  return null;
 }

 // Once a second: start sessions for fresh long moves, end those that should go back.
 static void Tick()
 {
  if (!s_bOn || !s_Active) return;
  float now = GetGame().GetWorld().GetWorldTime();
  for (int i = s_Active.Count() - 1; i >= 0; i--)
  {
   BBB_Drive d = s_Active[i];
   string why = d.Why(now);
   if (!why.IsEmpty()) d.End(why, now);
   if (!d.m_Path) s_Active.Remove(i);
  }
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  foreach (SCR_AIGroup group : groups) Consider(group, now);
  for (int p = s_Pending.Count() - 1; p >= 0; p--) if (!s_Pending.GetKey(p)) s_Pending.RemoveElement(p);
  for (int r = s_Rest.Count() - 1; r >= 0; r--) if (!s_Rest.GetKey(r) || s_Rest.GetElement(r) < now) s_Rest.RemoveElement(r);
 }

 // A group whose own vehicle has a long move: once the game has planned it, BBB drives it.
 protected static void Consider(SCR_AIGroup group, float now)
 {
  if (!group) return;
  IEntity vehicle = BBB_Transport.CrewedVehicle(group);
  if (!vehicle || Driving(vehicle)) { s_Pending.Remove(group); return; }
  float rest;
  if (s_Rest.Find(group, rest) && rest > now) return;
  AIWaypoint wp = group.GetCurrentWaypoint();
  string why = WhyNot(group, vehicle, wp);
  if (!why.IsEmpty()) { s_Pending.Remove(group); return; }
  float since;
  if (!s_Pending.Find(group, since)) { s_Pending.Set(group, now); return; }
  // The game's own plan for this move, once it reaches the waypoint.
  array<vector> plan = {};
  AIBaseMovementComponent move = AIBaseMovementComponent.Cast(vehicle.FindComponent(AICarMovementComponent));
  if (move) move.GetCurrentPath(plan);
  vector goal = wp.GetOrigin();
  string source = "game plan";
  bool complete = plan.Count() >= 2 && vector.DistanceXZ(plan[plan.Count() - 1], goal) < Math.Max(40, wp.GetCompletionRadius() + 15);
  if (!complete)
  {
   if (now - since < PLAN_WAIT_MS) return;
   // No full plan in time (long trips on big maps): BBB's own road route, from here to the goal.
   plan.Clear();
   int nodes, millis;
   if (!BBB_Roads.Route(vehicle.GetOrigin(), goal, plan, nodes, millis) || plan.IsEmpty())
   {
    s_Pending.Remove(group);
    s_Rest.Set(group, now + 60000); // no route of ours either: the game's driver keeps it
    return;
   }
   plan.InsertAt(vehicle.GetOrigin(), 0);
   if (vector.DistanceXZ(plan[plan.Count() - 1], goal) > 15) plan.Insert(goal);
   source = string.Format("roads (%1 nodes, %2 ms)", nodes, millis);
  }
  // Take over once the game's driver has it rolling along the line: pulling out of a yard or a parking
  // spot onto the road is its job (BBB grabbed trucks standing crosswise, 6-23 m off the line).
  if (!Rolling(vehicle, plan)) { if (now - since > 60000) s_Pending.Remove(group); return; }
  s_Pending.Remove(group);
  BBB_Drive d = new BBB_Drive();
  if (!d.Begin(group, vehicle, wp, plan, source, now)) { s_Rest.Set(group, now + 30000); return; }
  s_Active.Insert(d);
 }

 // Moving at least ROLL_SPEED, within ROLL_LINE_M of the planned line and heading along it.
 static const float ROLL_SPEED = 3;
 static const float ROLL_LINE_M = 4;
 static bool Rolling(IEntity vehicle, notnull array<vector> plan)
 {
  Physics phys = vehicle.GetPhysics();
  if (!phys || phys.GetVelocity().Length() < ROLL_SPEED) return false;
  vector pos = vehicle.GetOrigin();
  float best = float.MAX;
  int at = -1;
  for (int i = 0; i + 1 < plan.Count() && i < 400; i++)
  {
   float d = vector.DistanceXZ(pos, plan[i]);
   if (d < best) { best = d; at = i; }
  }
  if (at < 0 || best > ROLL_LINE_M + 3) return false; // plan points are a few metres apart
  vector dir = plan[at + 1] - plan[at];
  dir[1] = 0;
  vector fwd = vehicle.GetTransformAxis(2);
  fwd[1] = 0;
  if (dir.Length() < 0.1) return true;
  dir.Normalize();
  fwd.Normalize();
  return vector.Dot(dir, fwd) > 0.85;
 }

 // Why BBB won't drive this group's vehicle for this waypoint ("" = it will).
 static string WhyNot(SCR_AIGroup group, IEntity vehicle, AIWaypoint wp)
 {
  if (!wp) return "no order";
  if (SCR_EntityWaypoint.Cast(wp) || SCR_BoardingWaypoint.Cast(wp)) return "not a move";
  CarControllerComponent car = CarControllerComponent.Cast(vehicle.FindComponent(CarControllerComponent));
  if (!car || !car.GetSimulation()) return "not a wheeled vehicle";
  ChimeraCharacter driver = BBB.DriverOf(vehicle);
  if (!driver || !BBB.IsAlive(driver) || BBB.PlayerOf(driver)) return "no AI driver";
  if (BBB_Transport.VehicleOf(driver) != vehicle) return "driver not aboard";
  AIControlComponent ai = AIControlComponent.Cast(driver.FindComponent(AIControlComponent));
  if (!ai || !ai.IsAIActivated()) return "driver driven by hand already"; // an unstick, an escape, a gunner drive
  if (BBB_PilotHold.Driving(vehicle)) return "driven by hand already";
  if (BBB.s_Hooks.LeaveAlone(vehicle)) return "host: leave alone";
  if (BBB_OrderIntent.GameMasterOrder(group) && !s_bGameMaster) return "Game Master order";
  if (vector.DistanceXZ(vehicle.GetOrigin(), wp.GetOrigin()) < MIN_TRIP) return "short move";
  if (BBB_Transport.VehicleHitRecently(vehicle, 6000)) return "under fire";
  if (BBB_VehicleHelpers.KnownEnemiesNear(group, vehicle.GetOrigin(), ENEMY_RANGE) > 0) return "enemies near";
  if (BBB.HasPlayer(group)) return "a player's group";
  if (!BBB.s_Hooks.AllowScriptedDriving(group, vehicle)) return "host: no";
  return "";
 }
 static bool s_bGameMaster; // Game Master moves too (the universal driver); -bbbDriverGM 0 for not

 static void FrameAll()
 {
  if (!s_Active || s_Active.IsEmpty()) return;
  int started = System.GetTickCount();
  float now = GetGame().GetWorld().GetWorldTime();
  foreach (BBB_Drive d : s_Active) if (d.m_Path) d.Frame(now);
  s_iFrames++;
  s_iFrameMs += System.GetTickCount(started);
 }

 static void StopAll()
 {
  if (!s_Active) return;
  float now = GetGame().GetWorld().GetWorldTime();
  foreach (BBB_Drive d : s_Active) if (d.m_Path) d.End("BBB stopping", now);
  s_Active.Clear();
  s_Pending.Clear();
 }

 // ---------------------------------------------------------------- one vehicle

 bool Begin(SCR_AIGroup group, IEntity vehicle, AIWaypoint wp, notnull array<vector> plan, string source, float now)
 {
  CarControllerComponent car = CarControllerComponent.Cast(vehicle.FindComponent(CarControllerComponent));
  ChimeraCharacter driver = BBB.DriverOf(vehicle);
  if (!car || !car.GetSimulation() || !driver) return false;
  m_Path = new BBB_DrivePath();
  m_Path.Build(plan, STEP);
  if (m_Path.Count() < 3) { m_Path = null; return false; }
  m_Path.Profile(TOP_SPEED, SIDE_ACCEL, BRAKE_ACCEL);
  AIControlComponent ai = AIControlComponent.Cast(driver.FindComponent(AIControlComponent));
  if (!ai || !ai.IsAIActivated()) { m_Path = null; return false; }
  if (!BBB_PilotHold.Take(car)) { m_Path = null; return false; }
  m_PilotHeld = car;
  ai.DeactivateAI();
  m_bParkedAI = true;
  BBB_Unstick.ReleaseHorn(driver);
  m_Group = group;
  m_Vehicle = vehicle;
  m_Driver = driver;
  m_Waypoint = wp;
  m_vGoal = wp.GetOrigin();
  m_sSource = source;
  m_fStarted = now;
  m_vStart = vehicle.GetOrigin();
  m_fLastShift = now - SHIFT_COOLDOWN_MS;
  vector f0 = vehicle.GetTransformAxis(2);
  m_fLastHeading = Math.Atan2(f0[0], f0[2]) * Math.RAD2DEG;
  m_aRecoveries = {};
  m_iIdx = m_Path.Nearest(vehicle.GetOrigin(), 0, 30);
  Measure();
  s_iStarted++;
  PrintFormat("[BBB] DRIVER %1: driving %2 m (%3, %4 points), steer gain %5", BBB.s_Hooks.NameOf(group), (int)m_Path.m_fLength, source, m_Path.Count(), m_fGain);
  BBB_Think.NoteGroup(group, "Vehicle", "BBB drives the vehicle on the planned route");
  return true;
 }

 // Width, length to the front bumper, and a first steering gain from the wheelbase (refined while driving).
 protected void Measure()
 {
  vector mins, maxs;
  m_Vehicle.GetBounds(mins, maxs);
  m_fHalfWidth = Math.Clamp((maxs[0] - mins[0]) * 0.5, 0.8, 2.2);
  m_fFront = Math.Clamp(maxs[2], 1.5, 6);
  float wheelbase = 3.5;
  CarControllerComponent car = CarControllerComponent.Cast(m_Vehicle.FindComponent(CarControllerComponent));
  VehicleWheeledSimulation sim = car.GetSimulation();
  int wheels = sim.WheelCount();
  if (wheels >= 4)
  {
   float front = -1000;
   float rear = 1000;
   for (int i = 0; i < wheels; i++)
   {
    vector wpos = sim.WheelGetPosition(i);
    front = Math.Max(front, wpos[2]);
    rear = Math.Min(rear, wpos[2]);
   }
   if (front - rear > 1.5) wheelbase = front - rear;
  }
  // Full lock taken as 32 degrees until driving shows otherwise.
  m_fGain = Math.Tan(32 * Math.DEG2RAD) / wheelbase;
 }

 // Why this session should end now ("" = carry on). Checked once a second.
 string Why(float now)
 {
  if (!m_Path) return "";
  if (!m_Vehicle || !m_Driver || !BBB.IsAlive(m_Driver)) return "vehicle or driver lost";
  if (BBB.PlayerOf(m_Driver)) return "a player took the driver";
  if (BBB_Transport.VehicleOf(m_Driver) != m_Vehicle) return "driver got out";
  if (!m_Group) return "group gone";
  AIWaypoint wp = m_Group.GetCurrentWaypoint();
  if (wp != m_Waypoint)
  {
   // The host re-issuing the same move (a dropped waypoint, a retry) isn't a new order.
   if (wp && vector.DistanceXZ(wp.GetOrigin(), m_vGoal) < 30) m_Waypoint = wp;
   else return "new order";
  }
  if (BBB_Transport.VehicleHitRecently(m_Vehicle, 6000)) return "contact: hit";
  if (BBB_VehicleHelpers.KnownEnemiesNear(m_Group, m_Vehicle.GetOrigin(), ENEMY_RANGE) > 0) return "contact: enemies near";
  if (BBB.HasPlayer(m_Group)) return "a player joined the group";
  if (BBB.s_Hooks.LeaveAlone(m_Vehicle) || !BBB.s_Hooks.AllowScriptedDriving(m_Group, m_Vehicle)) return "host: hand back";
  return "";
 }

 void End(string why, float now)
 {
  if (!m_Path) return;
  bool arrived = why == "arrived";
  CarControllerComponent car;
  if (m_Vehicle) car = CarControllerComponent.Cast(m_Vehicle.FindComponent(CarControllerComponent));
  if (m_bRecovering && m_Listener) { m_Listener.m_Drive = null; BBB_Unstick.Cancel(m_Listener); }
  m_bRecovering = false;
  if (car && car.GetSimulation())
  {
   VehicleWheeledSimulation sim = car.GetSimulation();
   sim.SetThrottle(0);
   sim.SetSteering(0);
   float brake = 0;
   if (arrived) brake = 1;
   sim.SetBreak(brake, false);
   sim.SetClutch(1);
  }
  if (m_PilotHeld) BBB_PilotHold.Give(m_PilotHeld);
  m_PilotHeld = null;
  if (m_bParkedAI && m_Driver && BBB.IsAlive(m_Driver) && !BBB.PlayerOf(m_Driver))
  {
   AIControlComponent ai = AIControlComponent.Cast(m_Driver.FindComponent(AIControlComponent));
   if (ai && !ai.IsAIActivated()) ai.ActivateAI();
  }
  m_bParkedAI = false;
  float driven = 0;
  if (m_Vehicle) driven = m_Path.m_aS[m_iIdx];
  float meanXte = 0;
  if (m_iXteN > 0) meanXte = m_fSumXte / m_iXteN;
  if (arrived) s_iArrived++;
  else s_iHandedBack++;
  s_sLastEnd = why;
  PrintFormat("[BBB] DRIVER %1: %2 after %3 s, %4 of %5 m, line error mean %6 max %7 m, back-outs %8", BBB.s_Hooks.NameOf(m_Group), why, (int)((now - m_fStarted) / 1000), (int)driven, (int)m_Path.m_fLength, meanXte, m_fMaxXte, m_aRecoveries.Count());
  if (m_Group && why.Contains("contact")) s_Rest.Set(m_Group, now + CONTACT_REST_MS);
  else if (m_Group && !arrived) s_Rest.Set(m_Group, now + 60000); // the game's driver (and the host) take this move from here
  m_Path = null;
 }

 // Every frame: steer, speed, gears, guard, stall watch.
 protected void Frame(float now)
 {
  if (!m_Vehicle || !m_Driver) return;
  if (m_bRecovering) return; // BBB_Unstick has the controls
  CarControllerComponent car = CarControllerComponent.Cast(m_Vehicle.FindComponent(CarControllerComponent));
  if (!car || !car.GetSimulation()) return;
  VehicleWheeledSimulation sim = car.GetSimulation();
  float dt = 0.016;
  if (m_fLastFrame >= 0) dt = Math.Clamp((now - m_fLastFrame) / 1000, 0.001, 0.1);
  m_fLastFrame = now;
  vector pos = m_Vehicle.GetOrigin();
  vector fwd = m_Vehicle.GetTransformAxis(2);
  vector right = m_Vehicle.GetTransformAxis(0);
  Physics phys = m_Vehicle.GetPhysics();
  float speed = 0;
  if (phys)
  {
   speed = vector.Dot(phys.GetVelocity(), fwd);
   if (!phys.IsActive()) phys.SetActive(ActiveState.ACTIVE);
  }
  // Turn rate from the heading (degrees, clockwise from above = to the right).
  float heading = Math.Atan2(fwd[0], fwd[2]) * Math.RAD2DEG;
  float yaw = heading - m_fLastHeading;
  if (yaw > 180) yaw -= 360;
  if (yaw < -180) yaw += 360;
  m_fLastHeading = heading;
  float yawRate = yaw * Math.DEG2RAD / dt;
  if (!car.IsEngineOn()) car.ForceStartEngine();
  car.SetPersistentHandBrake(false);

  // Where on the line.
  m_iIdx = m_Path.Nearest(pos, m_iIdx, 40);
  int count = m_Path.Count();
  float toEnd = m_Path.m_fLength - m_Path.m_aS[m_iIdx] + vector.DistanceXZ(pos, m_Path.m_aPts[m_iIdx]);
  if (m_iIdx >= count - 2 || toEnd < ARRIVE_M)
  {
   float radius = ARRIVE_M;
   if (m_Waypoint) radius = Math.Max(ARRIVE_M, m_Waypoint.GetCompletionRadius());
   if (vector.DistanceXZ(pos, m_vGoal) < radius || m_iIdx >= count - 1)
   {
    End("arrived", now);
    return;
   }
  }
  float xte = SignedXte(pos);
  float axte = Math.AbsFloat(xte);
  m_fMaxXte = Math.Max(m_fMaxXte, axte);
  m_fSumXte += axte;
  m_iXteN++;

  // Look-ahead: shorter in bends, so the pursuit point doesn't drag the nose inside the corner.
  float absSpeed = Math.AbsFloat(speed);
  float look = Math.Clamp(3 + 0.55 * absSpeed, 5, 16);
  float bend = m_Path.MaxCurvature(m_iIdx, look + 10);
  if (bend > 0.05) look = Math.Min(look, 5);
  else if (bend > 0.02) look = Math.Min(look, 8);
  int t = m_Path.Ahead(m_iIdx, look);
  vector to = m_Path.m_aPts[t] - pos;
  float x = to[0] * right[0] + to[2] * right[2];
  float z = to[0] * fwd[0] + to[2] * fwd[2];
  float dist = Math.Max(1, Math.Sqrt(x * x + z * z));
  float kappa = 2 * x / (dist * dist);                 // pure pursuit curvature
  kappa += Math.Atan2(-0.6 * xte, absSpeed + 2) / Math.Max(4, look); // back onto the line (xte > 0: right of it)
  // Learn how hard the wheels turn for a given steering input (mod vehicles differ).
  if (absSpeed > 3 && Math.AbsFloat(m_fSteer) > 0.15)
  {
   float measured = yawRate / speed / m_fSteer; // positive steering turns right (measured, driver spike 10 Nov)
   if (measured > 0.02 && measured < 0.6) m_fGain += (measured - m_fGain) * Math.Min(1, dt * 0.5);
  }
  float steer = Math.Clamp(kappa / Math.Max(0.02, m_fGain), -1, 1);
  float slew = 2.5 * dt;
  m_fSteer += Math.Clamp(steer - m_fSteer, -slew, slew);

  // Speed: the line's profile here, the guard's cap, and the end.
  float target = m_Path.m_aV[m_iIdx];
  int near = m_Path.Ahead(m_iIdx, 12);
  target = Math.Min(target, m_Path.m_aV[near] + 1.5);
  if (axte > 2.5) target = Math.Min(target, 5); // well off the line: slow down to get back on it
  if (now >= m_fNextProbe)
  {
   m_fNextProbe = now + PROBE_MS;
   m_fGuardCap = Guard(pos, fwd, right, absSpeed);
  }
  target = Math.Min(target, m_fGuardCap);
  float throttle = 0;
  float brake = 0;
  float err = target - speed;
  if (err < -0.8) brake = Math.Clamp(-err * 0.35, 0.1, 1);
  else
  {
   float want = Math.Clamp(0.25 + 0.35 * err, 0, 1);
   if (target < 0.5) want = 0;
   m_fThrottle += Math.Clamp(want - m_fThrottle, -3 * dt, 1.2 * dt); // ease in, let off quickly
   throttle = m_fThrottle;
  }
  if (target < 0.5 && absSpeed < 1) brake = 1;
  if (brake > 0) m_fThrottle = 0;
  float clutch = Gearbox(sim, speed, throttle, now);
  sim.SetClutch(clutch);
  sim.SetThrottle(throttle);
  sim.SetBreak(brake, false);
  sim.SetSteering(m_fSteer);

  // Stuck: throttle on and not moving, or blocked and not clearing.
  if (throttle > 0.45 && absSpeed < 0.4)
  {
   if (m_fStallSince < 0) m_fStallSince = now;
  }
  else m_fStallSince = -1;
  if (m_fGuardCap < 0.5 && absSpeed < 0.5)
  {
   if (m_fBlockedSince < 0) m_fBlockedSince = now;
  }
  else m_fBlockedSince = -1;
  if (m_fStallSince >= 0 && now - m_fStallSince > STALL_MS) { m_sStuckWhy = "not moving on throttle"; Recover(now); }
  else if (m_fBlockedSince >= 0 && now - m_fBlockedSince > BLOCKED_MS) { m_sStuckWhy = "blocked ahead"; Recover(now); }
 }

 // Signed distance from the line (positive: right of it).
 protected float SignedXte(vector pos)
 {
  int i = m_iIdx;
  int j = Math.Min(m_Path.Count() - 1, i + 1);
  if (j == i) return 0;
  vector a = m_Path.m_aPts[i];
  vector b = m_Path.m_aPts[j];
  vector ab = b - a;
  vector ap = pos - a;
  float len = Math.Max(0.01, Math.Sqrt(ab[0] * ab[0] + ab[2] * ab[2]));
  return (ab[0] * ap[2] - ab[2] * ap[0]) / len * -1;
 }

 // Speed cap from what's in the way: the corridor along the line ahead (both edges) and the arc the
 // wheels point along. Rays follow the ground at bumper height. Every PROBE_MS.
 protected float Guard(vector pos, vector fwd, vector right, float speed)
 {
  float reach = speed * speed / (2 * GUARD_DECEL) + speed * 0.6 + 6;
  reach = Math.Clamp(reach, 8, 60);
  float nearest = 999;
  // Corridor: points along the line from the bumper out to 'reach', edges at half width + 0.4 m.
  float edge = m_fHalfWidth + 0.1; // body edge plus a little (0.4 m caught roadside trees, Arland 10 Nov)
  int from = m_Path.Ahead(m_iIdx, m_fFront);
  int to = m_Path.Ahead(m_iIdx, m_fFront + reach);
  int stepPts = Math.Max(1, 4 / STEP);
  int prev = from;
  for (int i = from + stepPts; i <= to; i += stepPts)
  {
   vector a = m_Path.m_aPts[prev];
   vector b = m_Path.m_aPts[i];
   vector dir = b - a;
   dir[1] = 0;
   float len = dir.Length();
   if (len < 0.1) continue;
   dir = dir * (1 / len);
   vector side = Vector(dir[2], 0, -dir[0]);
   float along = m_Path.m_aS[prev] - m_Path.m_aS[m_iIdx] - m_fFront;
   if (Ray(Lift(a + side * edge), Lift(b + side * edge)) || Ray(Lift(a - side * edge), Lift(b - side * edge)))
   {
    m_sWhere = "corridor";
    nearest = Math.Min(nearest, along);
    break;
   }
   prev = i;
  }
  // Arc: where the wheels are taking it in the next 1.2 s, both front corners.
  float k = m_fSteer * m_fGain;
  vector p = pos + fwd * (m_fFront + 0.3);
  vector h = fwd;
  float travel = Math.Max(4, speed * 1.2);
  for (int n = 0; n < 3; n++)
  {
   float seg = travel / 3;
   vector q = p + h * seg;
   float turn = k * seg;
   h = h * Math.Cos(turn) + right * Math.Sin(turn);
   right = Vector(h[2], 0, -h[0]);
   if (Ray(Lift(p + right * m_fHalfWidth), Lift(q + right * m_fHalfWidth)) || Ray(Lift(p - right * m_fHalfWidth), Lift(q - right * m_fHalfWidth)))
   {
    m_sWhere = "arc";
    nearest = Math.Min(nearest, seg * n);
    break;
   }
   p = q;
  }
  if (nearest >= 998) return 99;
  s_iGuardStops++;
  m_iHits++;
  if (m_iHits <= 6 || m_iHits % 100 == 0) PrintFormat("[BBB] DRIVER %1: guard hit %2 at %3 m ahead (speed %4, %5 hits so far): %6", BBB.s_Hooks.NameOf(m_Group), m_sWhere, (int)nearest, speed, m_iHits, m_sHit);
  if (nearest < 4) return 0;
  return Math.Sqrt(2 * GUARD_DECEL * (nearest - 3));
 }

 protected vector Lift(vector p)
 {
  p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + 0.9;
  return p;
 }

 protected string m_sHit;
 protected string m_sStuckWhy;
 protected string m_sWhere;
 protected int m_iHits;
 // Something solid between a and b. A truck drives through bushes; a gate in the lane that is open,
 // opening or wide enough to open (it is opened now) isn't in the way. Looks past those (twice).
 protected bool Ray(vector a, vector b)
 {
  IEntity skip = m_Vehicle;
  for (int tries = 0; tries < 3; tries++)
  {
   TraceParam trace = new TraceParam();
   trace.Start = a;
   trace.End = b;
   trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
   trace.Exclude = skip;
   float f = GetGame().GetWorld().TraceMove(trace, null);
   if (f >= 1) return false;
   IEntity hit = trace.TraceEnt;
   m_sHit = "terrain or static";
   if (!hit) return true;
   string prefab = "";
   EntityPrefabData pd = hit.GetPrefabData();
   if (pd) prefab = pd.GetPrefabName();
   m_sHit = hit.ClassName() + " " + prefab;
   if (!Passable(hit, prefab)) return true;
   // Carry on from just past it, ignoring it.
   vector dir = b - a;
   float len = dir.Length();
   if (len < 0.5) return false;
   a = a + dir * Math.Min(1, f + 0.3 / len);
   skip = hit;
  }
  return false;
 }

 // Bushes, and gates the vehicle can go through.
 protected bool Passable(IEntity hit, string prefab)
 {
  if (prefab.Contains("/Vegetation/Bush/") || prefab.Contains("/Vegetation/Clutter/")) return true;
  IEntity e = hit;
  for (int up = 0; up < 3 && e; up++)
  {
   BaseDoorComponent door = BaseDoorComponent.Cast(e.FindComponent(BaseDoorComponent));
   if (door)
   {
    if (door.IsOpen() || door.IsOpening()) return true;
    if (BBB_VehicleHelpers.OpeningWidth(e) < m_fHalfWidth * 2 + 0.4) return false;
    door.SetControlValue(1, RplId.Invalid());
    PrintFormat("[BBB] DRIVER %1: opening the gate ahead (%2)", BBB.s_Hooks.NameOf(m_Group), prefab);
    return true;
   }
   e = e.GetParent();
  }
  return false;
 }

 // Forward gears only (backing out is BBB_Unstick's): pull away in first, shift on the drivetrain's
 // rpm, slip the clutch while pulling away uphill. Returns the clutch.
 protected float Gearbox(VehicleWheeledSimulation sim, float speed, float throttle, float now)
 {
  int neutral = sim.GearboxGearsCount() - sim.GearboxForwardGearsCount() - 1;
  int gear = sim.GetGear();
  if (throttle <= 0 && Math.AbsFloat(speed) < 0.5) return 1;
  if (gear <= neutral) { sim.SetGear(neutral + 1); m_fLastShift = now; return 1; }
  if (Math.AbsFloat(speed) < 1 && gear > neutral + 1) { sim.SetGear(neutral + 1); m_fLastShift = now; return 1; }
  float driven = sim.EngineGetRPMFeedback();
  float rpm = sim.EngineGetRPM();
  float peakPower = sim.EngineGetRPMPeakPower();
  float peakTorque = sim.EngineGetRPMPeakTorque();
  float idle = sim.EngineGetRPMIdle();
  bool settled = now - m_fLastShift > SHIFT_COOLDOWN_MS;
  if (settled && throttle > 0 && driven > peakPower * 0.65 && gear < sim.GearboxGearsCount() - 1) { sim.SetGear(gear + 1); m_fLastShift = now; }
  else if (settled && driven < peakTorque * 0.6 && gear > neutral + 1 && Math.AbsFloat(rpm - driven) < 250) { sim.SetGear(gear - 1); m_fLastShift = now; }
  // Pulling away (or lugging in first): let the engine rev into its torque before the clutch bites.
  if (throttle > 0 && gear == neutral + 1 && rpm < peakTorque && peakTorque > idle)
   return Math.Clamp((rpm - idle) / (peakTorque - idle), 0.3, 1);
  return 1;
 }

 // Stuck: back out (BBB_Unstick), wheel opposite to where we were steering; too many: hand back.
 protected void Recover(float now)
 {
  m_fStallSince = -1;
  m_fBlockedSince = -1;
  for (int i = m_aRecoveries.Count() - 1; i >= 0; i--) if (now - m_aRecoveries[i] > RECOVER_WINDOW_MS) m_aRecoveries.Remove(i);
  if (m_aRecoveries.Count() >= RECOVER_LIMIT) { End(string.Format("stuck: handed back after %1 back-outs", RECOVER_LIMIT), now); return; }
  m_aRecoveries.Insert(now);
  s_iRecoveries++;
  float steer = -0.8;
  if (m_fSteer < 0) steer = 0.8;
  if (!m_Listener) m_Listener = new BBB_DriveUnstick(this);
  m_Listener.m_Drive = this;
  m_bRecovering = true;
  PrintFormat("[BBB] DRIVER %1: stuck at %2 m of %3 (%4), backing out (%5); line error %6 m, last guard hit: %7", BBB.s_Hooks.NameOf(m_Group), (int)m_Path.m_aS[m_iIdx], (int)m_Path.m_fLength, m_sStuckWhy, m_aRecoveries.Count(), SignedXte(m_Vehicle.GetOrigin()), m_sHit);
  BBB_Unstick.Begin(m_Vehicle, m_Driver, steer, m_Listener);
 }

 void OnRecovered()
 {
  m_bRecovering = false;
  m_fLastFrame = -1;
  m_fThrottle = 0;
  if (m_Vehicle) { vector f1 = m_Vehicle.GetTransformAxis(2); m_fLastHeading = Math.Atan2(f1[0], f1[2]) * Math.RAD2DEG; }
  m_fSteer = 0;
  if (!m_Path || !m_Vehicle) return;
  // Back on the line from wherever the back-out left it.
  m_iIdx = m_Path.Nearest(m_Vehicle.GetOrigin(), Math.Max(0, m_iIdx - 10), 40);
 }
}
