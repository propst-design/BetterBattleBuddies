// Rollover checks for BBB_PullOver (20 Nov 2026, play-test: "Dismount pull over needs checks against
// rollovers. Seen two on the same stretch of Everon road."). Two causes in the old pull-over: the verge
// test let through a rise or drop of 0.25 m every half metre (a 27 degree bank) and only on a line straight
// out from the road's middle, not the line the vehicle actually drives; and the hand-driving swung the wheel
// to 0.6 at once at up to 40 km/h (play logs, 19 Nov: trucks from 40 km/h, steer 0.47 at t=0), so a tall
// truck tipped on the turn-in or rolled off the bank.
//
// Limits per vehicle (BBB_PullOverLimits.For): a tall or heavy vehicle (trucks, APCs: 2.6 m or more high, 7 t
// or more, or narrow for its height) gets stricter ground limits, a slower turn-in and less wheel than a car.
// The spot test (BBB_PullOverSafety.SpotSafe) samples the ground every metre along the straight line from the
// vehicle to the spot (roughly where it drives: the steering aims at the spot) and at the spot's footprint:
// slope across (roll) and along (pitch), steps (a ditch or a kerb), water; and the height between the road's
// edge and the spot. BBB_PullOver stops at once when the hull rolls or pitches past the abort angle.
class BBB_PullOverLimits : Managed
{
 bool m_bHeavy;
 string m_sKind;          // "car" or "heavy" (the log)
 float m_fMaxRollDeg;     // ground across the vehicle's path or footprint
 float m_fMaxPitchDeg;    // ground along it
 float m_fMaxStepM;       // change of height change from one metre to the next along the path (a ditch's or a bank's edge)
 float m_fMaxDropM;       // the spot against the road's edge beside it, up or down
 float m_fTurnSpeed;      // m/s: the pace it pulls off at
 float m_fBrakeAbove;     // m/s: brakes above this
 float m_fSteerMax;       // wheel at the turn-in pace (less above it)
 float m_fSteerRate;      // wheel travel per second
 float m_fAbortRollDeg;   // the hull rolls past this: stop at once
 float m_fAbortPitchDeg;  // or pitches past this (nosing into a ditch)
 float m_fAheadPerSide;   // metres on along the road per metre to the side (a gentle line)
 float m_fDecel;          // m/s2 it is assumed to brake at (where the turn-in can start)
 float m_fHalfWidth;
 float m_fHalfLength;

 static BBB_PullOverLimits For(IEntity vehicle)
 {
  BBB_PullOverLimits l = new BBB_PullOverLimits();
  float width = 2.2;
  float length = 4.8;
  float height = 2;
  float mass = 2500;
  if (vehicle)
  {
   vector mins, maxs;
   vehicle.GetBounds(mins, maxs);
   if (maxs[0] - mins[0] > 0.5) width = maxs[0] - mins[0];
   if (maxs[2] - mins[2] > 1) length = maxs[2] - mins[2];
   if (maxs[1] - mins[1] > 0.5) height = maxs[1] - mins[1];
   Physics physics = vehicle.GetPhysics();
   if (physics && physics.GetMass() > 0) mass = physics.GetMass();
  }
  l.m_fHalfWidth = Math.Clamp(width * 0.5, 0.8, 1.7);
  l.m_fHalfLength = Math.Clamp(length * 0.5, 1.5, 5);
  // Narrow for its height: the half track against a centre of mass at about half the height.
  float stability = l.m_fHalfWidth / Math.Max(0.5, height * 0.5);
  l.m_bHeavy = height >= 2.6 || mass >= 7000 || stability < 0.95;
  if (l.m_bHeavy)
  {
   l.m_sKind = "heavy";
   l.m_fMaxRollDeg = 6;
   l.m_fMaxPitchDeg = 10;
   l.m_fMaxStepM = 0.15;
   l.m_fMaxDropM = 0.45;
   l.m_fTurnSpeed = 2.5;
   l.m_fBrakeAbove = 3.2;
   l.m_fSteerMax = 0.45;
   l.m_fSteerRate = 0.5;
   l.m_fAbortRollDeg = 12;
   l.m_fAbortPitchDeg = 16;
   l.m_fAheadPerSide = 3;
   l.m_fDecel = 2.5;
  }
  else
  {
   l.m_sKind = "car";
   l.m_fMaxRollDeg = 10;
   l.m_fMaxPitchDeg = 14;
   l.m_fMaxStepM = 0.25;
   l.m_fMaxDropM = 0.7;
   l.m_fTurnSpeed = 3.5;
   l.m_fBrakeAbove = 4.5;
   l.m_fSteerMax = 0.6;
   l.m_fSteerRate = 0.8;
   l.m_fAbortRollDeg = 18;
   l.m_fAbortPitchDeg = 22;
   l.m_fAheadPerSide = 2.2;
   l.m_fDecel = 3.5;
  }
  return l;
 }

 // How far on the spot must be at this speed: braked down to the turn-in pace first, then a few metres to turn.
 float AheadFor(float speed)
 {
  float fast = Math.Max(0, speed * speed - m_fTurnSpeed * m_fTurnSpeed);
  return Math.Min(45, fast / (2 * m_fDecel) + 8);
 }

 // The wheel allowed at this speed: full (m_fSteerMax) at the turn-in pace, less the faster it goes.
 float SteerCap(float speed)
 {
  if (speed <= m_fTurnSpeed) return m_fSteerMax;
  return m_fSteerMax * Math.Clamp(m_fTurnSpeed / speed, 0.2, 1);
 }
}

class BBB_PullOverSafety
{
 static int s_iRefused; // spots refused by these checks (tests, the log)

 // The hull's roll and pitch in degrees (0 upright and level).
 static float RollDeg(IEntity vehicle)
 {
  vector mat[4];
  vehicle.GetWorldTransform(mat);
  return Math.Asin(Math.Clamp(mat[0][1], -1, 1)) * Math.RAD2DEG;
 }

 static float PitchDeg(IEntity vehicle)
 {
  vector mat[4];
  vehicle.GetWorldTransform(mat);
  return Math.Asin(Math.Clamp(mat[2][1], -1, 1)) * Math.RAD2DEG;
 }

 // The ground's slope in degrees between two points 'span' m apart either side of 'p' along 'axis'.
 protected static float SlopeDeg(BaseWorld world, vector p, vector axis, float half)
 {
  vector a = p + axis * half;
  vector b = p - axis * half;
  float rise = world.GetSurfaceY(a[0], a[2]) - world.GetSurfaceY(b[0], b[2]);
  return Math.AbsFloat(Math.Atan2(rise, 2 * half)) * Math.RAD2DEG;
 }

 // Safe to drive from 'at' to 'spot' and stand there: the line between them, sampled every metre, keeps under
 // the limits across and along and has no step (ditch, bank) or water; the spot's footprint (facing 'dir')
 // is level enough; and, off the road ('edge' set), the spot isn't far below or above the road's edge.
 static bool SpotSafe(IEntity vehicle, vector at, vector spot, vector dir, vector edge, bool offRoad, BBB_PullOverLimits limits, out string why)
 {
  BaseWorld world = GetGame().GetWorld();
  vector right = Vector(dir[2], 0, -dir[0]);
  // The footprint where it will stand.
  float roll = SlopeDeg(world, spot, right, limits.m_fHalfWidth);
  float pitch = SlopeDeg(world, spot, dir, limits.m_fHalfLength);
  if (roll > limits.m_fMaxRollDeg) { why = string.Format("the spot slopes %1 deg across (%2 limit %3)", Math.Round(roll), limits.m_sKind, limits.m_fMaxRollDeg); s_iRefused++; return false; }
  if (offRoad && pitch > limits.m_fMaxPitchDeg) { why = string.Format("the spot slopes %1 deg along (%2 limit %3)", Math.Round(pitch), limits.m_sKind, limits.m_fMaxPitchDeg); s_iRefused++; return false; }
  if (!offRoad) return true;
  float drop = world.GetSurfaceY(spot[0], spot[2]) - world.GetSurfaceY(edge[0], edge[2]);
  if (Math.AbsFloat(drop) > limits.m_fMaxDropM)
  {
   why = string.Format("the verge is %1 m %2 the road's edge (%3 limit %4)", Math.Round(Math.AbsFloat(drop) * 10) / 10, BelowOrAbove(drop), limits.m_sKind, limits.m_fMaxDropM);
   s_iRefused++;
   return false;
  }
  // The way there.
  vector line = spot - at;
  line[1] = 0;
  float length = line.Length();
  if (length < 0.5) return true;
  line.Normalize();
  vector across = Vector(line[2], 0, -line[0]);
  // A step is a change of grade between one metre and the next (the edge of a ditch or a bank); an even
  // climb or fall is the pitch test's.
  float last = world.GetSurfaceY(at[0], at[2]);
  float lastRise = 0;
  for (float s = 1; s <= length + 0.01; s += 1)
  {
   vector p = at + line * Math.Min(s, length);
   float y = world.GetSurfaceY(p[0], p[2]);
   float rise = y - last;
   if (s > 1.5 && Math.AbsFloat(rise - lastRise) > limits.m_fMaxStepM)
   {
    why = string.Format("a ditch or a bank %1 m on the way (%2 m step, %3 limit %4)", Math.Round(s), Math.Round(Math.AbsFloat(rise - lastRise) * 100) / 100, limits.m_sKind, limits.m_fMaxStepM);
    s_iRefused++;
    return false;
   }
   last = y;
   lastRise = rise;
   float r = SlopeDeg(world, p, across, limits.m_fHalfWidth);
   if (r > limits.m_fMaxRollDeg) { why = string.Format("the way slopes %1 deg across %2 m on (%3 limit %4)", Math.Round(r), Math.Round(s), limits.m_sKind, limits.m_fMaxRollDeg); s_iRefused++; return false; }
   float g = SlopeDeg(world, p, line, limits.m_fHalfLength);
   if (g > limits.m_fMaxPitchDeg) { why = string.Format("the way slopes %1 deg along %2 m on (%3 limit %4)", Math.Round(g), Math.Round(s), limits.m_sKind, limits.m_fMaxPitchDeg); s_iRefused++; return false; }
   if (BBB_Terrain.WaterDepthAt(p) > 0 || BBB_Terrain.WaterDepthAt(p + across * limits.m_fHalfWidth) > 0 || BBB_Terrain.WaterDepthAt(p - across * limits.m_fHalfWidth) > 0) { why = string.Format("water %1 m on the way", Math.Round(s)); s_iRefused++; return false; }
  }
  return true;
 }

 protected static string BelowOrAbove(float drop)
 {
  if (drop < 0) return "below";
  return "above";
 }
}
