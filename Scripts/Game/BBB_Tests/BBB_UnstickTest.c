// Getting a stuck vehicle free (-bbbUnstickTest 1, Test-BBB.ps1 -Preset unstick). Jacob, 19 Nov: "the
// timed reverse often hinders more than helps". A real Ural with an AI driver on open Arland ground:
//  1. rear blocked: an empty Ural parked 1 m behind it. The old manoeuvre (replayed here frame by
//     frame: AI parked, reverse gear, 0.7 throttle, wheel 0.8 for 3.5 s) rams it; BBB_Unstick sees
//     the truck behind, doesn't reverse, turns out forward and never touches it.
//  2. a soldier standing 1.2 m behind it: no reverse, he is never reached.
//  3. a truck right in front, open behind: it reverses, swings its nose, stops once it has room or
//     has backed its planned distance (not a fixed time), and a second try at the same spot
//     backs further on the other side.
//  4. waiting is not stuck: a stopped truck ahead is a queue; a pull-over in hand is held.
// Not covered: the stall watches' timing in a real match (srv:regress covers ACA's), tracked vehicles,
// slopes and ditches (the ground check is read, not driven here).
class BBB_UnstickTest : BBB_AimTest
{
 static ref BBB_UnstickTest s_UnstickTest;
 static const ResourceName URAL = "{16C1F16C9B053801}Prefabs/Vehicles/Wheeled/Ural4320/Ural4320_transport.et";
 static const int EXPECTED = 15;
 protected int m_iChecks;
 protected int m_iFailures;
 protected vector m_vSite;
 protected vector m_vFwd = "0 0 1";
 protected IEntity m_Truck;
 protected IEntity m_Other;
 protected IEntity m_Man;
 protected ChimeraCharacter m_Driver;
 protected SCR_AIGroup m_Group;
 protected vector m_vOtherStart;
 protected vector m_vTruckStart;
 protected float m_fMinGap;
 protected float m_fLegacyUntil;
 protected float m_fManMin;
 protected int m_iStep;

 static void MaybeStartUnstick()
 {
  if (s_UnstickTest || !Replication.IsServer() || !System.IsCLIParam("bbbUnstickTest")) return;
  s_UnstickTest = new BBB_UnstickTest();
  GetGame().GetCallqueue().CallLater(s_UnstickTest.Begin, 15000);
 }

 override protected void Check(string name, bool ok, string detail)
 {
  m_iChecks++;
  string res = "PASS";
  if (!ok)
  {
   res = "FAIL";
   m_iFailures++;
  }
  PrintFormat("[BBB] UNSTICKTEST CHECK %1 %2 %3", name, res, detail);
 }

 // Truck A's half-length to its rear bumper / nose (local z), from its bounds.
 protected float Rear(IEntity v) { vector mins, maxs; v.GetBounds(mins, maxs); return mins[2]; }
 protected float Nose(IEntity v) { vector mins, maxs; v.GetBounds(mins, maxs); return maxs[2]; }

 protected void Put(IEntity v, vector at)
 {
  vector m[4];
  vector angles = m_vFwd.VectorToAngles();
  Math3D.AnglesToMatrix(Vector(angles[0], 0, 0), m);
  m[3] = BBB.Ground(at);
  v.SetWorldTransform(m);
  v.Update();
  Physics p = v.GetPhysics();
  if (p)
  {
   p.SetVelocity(vector.Zero);
   p.SetAngularVelocity(vector.Zero);
  }
 }

 // Gap between the truck's rear bumper and the other truck's nose, along the truck's axis.
 protected float GapBehind()
 {
  vector otherNose = m_Other.CoordToParent(Vector(0, 0, Nose(m_Other)));
  return Rear(m_Truck) - m_Truck.CoordToLocal(otherNose)[2];
 }

 override protected void Begin()
 {
  m_vSite = BBB.Ground(Vector(1340, 0, 2830));
  m_Truck = Spawn(URAL, m_vSite, m_vFwd);
  m_Other = Spawn(URAL, m_vSite - m_vFwd * 30, m_vFwd);
  SCR_AIGroup.IgnoreSpawning(true);
  m_Group = SCR_AIGroup.Cast(Spawn(GROUP_US, m_vSite + Vector(6, 0, 0), m_vFwd));
  SCR_AIGroup.IgnoreSpawning(false);
  m_Driver = ChimeraCharacter.Cast(Spawn(RIFLE_US, m_vSite + Vector(5, 0, 0), m_vFwd));
  if (!m_Truck || !m_Other || !m_Group || !m_Driver)
  {
   Check("fixture-spawned", false, "");
   Finish();
   return;
  }
  m_Group.AddAIEntityToGroup(m_Driver);
  array<BaseCompartmentSlot> pilots = {};
  BBB_VehicleHelpers.GetVehicleCompartments(m_Truck, pilots, ECompartmentType.PILOT);
  if (!pilots.IsEmpty()) m_Driver.GetCompartmentAccessComponent().GetInVehicle(m_Truck, pilots[0], true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false);
  GetGame().GetCallqueue().CallLater(RearBlocked, 4000);
 }

 // ---------------------------------------------------------------- 1. rear blocked

 protected void RearBlocked()
 {
  Check("fixture-driver-seated", BBB.DriverOf(m_Truck) == m_Driver, "");
  Put(m_Truck, m_vSite);
  float behind = -Rear(m_Truck) + Nose(m_Other) + 1.0;
  Put(m_Other, m_vSite - m_vFwd * behind);
  m_vOtherStart = m_Other.GetOrigin();
  m_vTruckStart = m_Truck.GetOrigin();
  m_fMinGap = GapBehind();
  // The old manoeuvre, replayed.
  AIControlComponent ai = AIControlComponent.Cast(m_Driver.FindComponent(AIControlComponent));
  if (ai) ai.DeactivateAI();
  m_fLegacyUntil = Now() + 4.3;
  GetGame().GetCallqueue().CallLater(LegacyFrame, 0, true);
 }

 protected void LegacyFrame()
 {
  CarControllerComponent car = CarControllerComponent.Cast(m_Truck.FindComponent(CarControllerComponent));
  if (!car || !car.GetSimulation()) return;
  VehicleWheeledSimulation sim = car.GetSimulation();
  m_fMinGap = Math.Min(m_fMinGap, GapBehind());
  float left = m_fLegacyUntil - Now();
  if (left < 0)
  {
   GetGame().GetCallqueue().Remove(LegacyFrame);
   BBB_Unstick.Drive(m_Driver, sim, 0, 0, 0);
   int n = sim.GearboxGearsCount() - sim.GearboxForwardGearsCount() - 1;
   sim.SetGear(n + 1);
   AIControlComponent ai = AIControlComponent.Cast(m_Driver.FindComponent(AIControlComponent));
   if (ai) ai.ActivateAI();
   float moved = vector.DistanceXZ(m_Other.GetOrigin(), m_vOtherStart);
   Check("legacy-reverse-rams", m_fMinGap < 0.3 || moved > 0.3, string.Format("fixture: the old timed reverse closed the 1 m gap to %1 m, pushed the truck behind %2 m", Math.Round(m_fMinGap * 100) / 100, Math.Round(moved * 100) / 100));
   GetGame().GetCallqueue().CallLater(RearBlockedNew, 2000);
   return;
  }
  if (!car.IsEngineOn()) car.ForceStartEngine();
  car.SetPersistentHandBrake(false);
  sim.SetClutch(1);
  int neutral = sim.GearboxGearsCount() - sim.GearboxForwardGearsCount() - 1;
  if (left > 0.8)
  {
   if (sim.GetGear() >= neutral) sim.SetGear(neutral - 1);
   BBB_Unstick.Drive(m_Driver, sim, 0.7, 0.8, 0);
  }
  else BBB_Unstick.Drive(m_Driver, sim, 0, 0, 1);
 }

 protected void RearBlockedNew()
 {
  Put(m_Truck, m_vSite);
  float behind = -Rear(m_Truck) + Nose(m_Other) + 1.0;
  Put(m_Other, m_vSite - m_vFwd * behind);
  m_vOtherStart = m_Other.GetOrigin();
  m_vTruckStart = m_Truck.GetOrigin();
  m_fMinGap = GapBehind();
  float room = BBB_Unstick.RoomBehind(m_Truck, BBB_Unstick.PROBE_M);
  Check("sees-truck-behind", room < BBB_Unstick.REAR_MIN_M, string.Format("%1 m clear behind", Math.Round(room * 100) / 100));
  BBB_Unstick.Begin(m_Truck, m_Driver, 0.8, null);
  m_iStep = 0;
  GetGame().GetCallqueue().CallLater(WatchGap, 0, true);
 }

 protected void WatchGap()
 {
  m_fMinGap = Math.Min(m_fMinGap, GapBehind());
  m_iStep++;
  if (BBB_Claims.Busy(m_Truck, BBB_Claims.JOB_UNSTICK) && m_iStep < 600) return;
  GetGame().GetCallqueue().Remove(WatchGap);
  float moved = vector.DistanceXZ(m_Other.GetOrigin(), m_vOtherStart);
  Check("rear-blocked-no-ram", m_fMinGap > 0.5 && moved < 0.2, string.Format("gap stayed >= %1 m, truck behind moved %2 m", Math.Round(m_fMinGap * 100) / 100, Math.Round(moved * 100) / 100));
  vector local = m_Truck.CoordToLocal(m_vTruckStart);
  int outcome = BBB_Unstick.LastOutcome(m_Truck);
  Check("rear-blocked-turns-out", outcome == BBB_Unstick.OUTCOME_TURNED && -local[2] > 0.5, string.Format("%1, %2 m forward", BBB_Unstick.OutcomeName(outcome), Math.Round(-local[2] * 10) / 10));
  GetGame().GetCallqueue().CallLater(ManBehind, 2500);
 }

 // ---------------------------------------------------------------- 2. a soldier behind

 protected void ManBehind()
 {
  Put(m_Other, m_vSite - m_vFwd * 40);
  Put(m_Truck, m_vSite + Vector(20, 0, 0));
  vector at = m_Truck.CoordToParent(Vector(0, 0, Rear(m_Truck) - 1.2));
  m_Man = Spawn(RIFLE_US, at, m_vFwd);
  GetGame().GetCallqueue().CallLater(ManBehindGo, 1500);
 }

 protected void ManBehindGo()
 {
  float room = BBB_Unstick.RoomBehind(m_Truck, BBB_Unstick.PROBE_M);
  m_fManMin = 99;
  Check("sees-man-behind", m_Man && room < BBB_Unstick.REAR_MIN_M, string.Format("%1 m clear behind", Math.Round(room * 100) / 100));
  BBB_Unstick.Begin(m_Truck, m_Driver, -0.8, null);
  m_iStep = 0;
  GetGame().GetCallqueue().CallLater(WatchMan, 0, true);
 }

 protected void WatchMan()
 {
  if (m_Man) m_fManMin = Math.Min(m_fManMin, Rear(m_Truck) - m_Truck.CoordToLocal(m_Man.GetOrigin())[2]);
  m_iStep++;
  if (BBB_Claims.Busy(m_Truck, BBB_Claims.JOB_UNSTICK) && m_iStep < 600) return;
  GetGame().GetCallqueue().Remove(WatchMan);
  Check("man-behind-not-reached", m_Man && BBB.IsAlive(m_Man) && m_fManMin > 0.6, string.Format("closest %1 m behind the bumper, outcome %2", Math.Round(m_fManMin * 100) / 100, BBB_Unstick.OutcomeName(BBB_Unstick.LastOutcome(m_Truck))));
  if (m_Man) SCR_EntityHelper.DeleteEntityAndChildren(m_Man);
  GetGame().GetCallqueue().CallLater(FrontBlocked, 2500);
 }

 // ---------------------------------------------------------------- 3. front blocked, open behind

 protected void FrontBlocked()
 {
  Put(m_Truck, m_vSite + Vector(-20, 0, 0));
  float ahead = Nose(m_Truck) - Rear(m_Other) + 0.4;
  Put(m_Other, m_vSite + Vector(-20, 0, 0) + m_vFwd * ahead);
  m_vTruckStart = m_Truck.GetOrigin();
  float room = BBB_Unstick.RoomBehind(m_Truck, BBB_Unstick.PROBE_M);
  Check("open-behind", room >= BBB_Unstick.PROBE_M - 0.5, string.Format("%1 m clear behind", Math.Round(room * 10) / 10));
  BBB_Unstick.Begin(m_Truck, m_Driver, 0.8, null);
  m_iStep = 0;
  GetGame().GetCallqueue().CallLater(WatchFront, 0, true);
 }

 protected void WatchFront()
 {
  m_iStep++;
  if (BBB_Claims.Busy(m_Truck, BBB_Claims.JOB_UNSTICK) && m_iStep < 800) return;
  GetGame().GetCallqueue().Remove(WatchFront);
  int outcome = BBB_Unstick.LastOutcome(m_Truck);
  float backed = vector.DistanceXZ(m_Truck.GetOrigin(), m_vTruckStart);
  vector f = m_Truck.GetTransformAxis(2);
  float turn = Math.Acos(Math.Clamp(vector.Dot(f, m_vFwd), -1, 1)) * Math.RAD2DEG;
  Check("front-blocked-backs-out", (outcome == BBB_Unstick.OUTCOME_ROOM || outcome == BBB_Unstick.OUTCOME_BACKED) && backed >= 2 && backed <= BBB_Unstick.BACK_MAX_M + 1, string.Format("%1 after %2 m", BBB_Unstick.OutcomeName(outcome), Math.Round(backed * 10) / 10));
  Check("front-blocked-nose-swung", turn > 3, string.Format("heading changed %1 deg, nose to %2", Math.Round(turn), BBB_Unstick.s_iLastNose));
  // Back where it was stuck: the second try backs further, the other way.
  m_iStep = BBB_Unstick.s_iLastNose;
  m_fMinGap = BBB_Unstick.s_fLastGoal;
  Put(m_Truck, m_vTruckStart);
  BBB_Unstick.Begin(m_Truck, m_Driver, 0.8, null);
  GetGame().GetCallqueue().CallLater(SecondTry, 300);
 }

 protected void SecondTry()
 {
  bool other = BBB_Unstick.s_iLastNose == -m_iStep && m_iStep != 0;
  Check("second-try-escalates", BBB_Unstick.TriesHere(m_Truck) == 2 && other && BBB_Unstick.s_fLastGoal > m_fMinGap, string.Format("tries %1, nose %2 -> %3, goal %4 -> %5 m", BBB_Unstick.TriesHere(m_Truck), m_iStep, BBB_Unstick.s_iLastNose, m_fMinGap, BBB_Unstick.s_fLastGoal));
  GetGame().GetCallqueue().CallLater(Waiting, 9000);
 }

 // ---------------------------------------------------------------- 4. waiting is not stuck

 protected void Waiting()
 {
  Put(m_Truck, m_vSite + Vector(-40, 0, 0));
  float ahead = Nose(m_Truck) - Rear(m_Other) + 5;
  Put(m_Other, m_vSite + Vector(-40, 0, 0) + m_vFwd * ahead);
  GetGame().GetCallqueue().CallLater(WaitingCheck, 1000);
 }

 protected void WaitingCheck()
 {
  string why;
  int wait = BBB_Unstick.WaitingFor(m_Truck, m_Driver, m_Group, why);
  Check("queue-is-waiting", wait == BBB_Unstick.WAIT_QUEUE, why);
  Put(m_Other, m_vSite - m_vFwd * 60);
  wait = BBB_Unstick.WaitingFor(m_Truck, m_Driver, m_Group, why);
  Check("open-road-not-waiting", wait == BBB_Unstick.WAIT_NONE, why);
  BBB_Claims.Mark(m_Truck, BBB_Claims.JOB_PULL_OVER);
  wait = BBB_Unstick.WaitingFor(m_Truck, m_Driver, m_Group, why);
  BBB_Claims.Unmark(m_Truck, BBB_Claims.JOB_PULL_OVER);
  Check("pull-over-is-held", wait == BBB_Unstick.WAIT_HELD, why);
  Check("hopeless-outcomes", BBB_Unstick.Hopeless(BBB_Unstick.OUTCOME_BOXED) && BBB_Unstick.Hopeless(BBB_Unstick.OUTCOME_WHEELS) && !BBB_Unstick.Hopeless(BBB_Unstick.OUTCOME_ROOM), "");
  Finish();
 }

 override protected void Finish()
 {
  string verdict = "FAIL";
  if (m_iFailures == 0 && m_iChecks == EXPECTED) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbunstick passed=%2 failed=%3 expected=%4", verdict, m_iChecks - m_iFailures, m_iFailures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_UnstickTest.MaybeStartUnstick();
 }
}
