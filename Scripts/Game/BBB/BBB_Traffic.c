// Traffic (3 Nov 2026, the user's pn34 playtest): "when two vehicles come from opposite directions on a
// road one vehicle should go to the right and the other to the left, they should be able to communicate
// this with each other, this communication would also fix the issue of them driving into each other when
// turning around at bases. Also when getting out of a vehicle they should try to move the vehicle off the
// road or to the side of the road ..., and they tend to forget the handbrake ... The drivers should not
// swerve out of the way when it's an enemy vehicle, especially when one of them is armed: the one not
// armed should try to get away."
//  - Give way (-bbbGiveWay): every 0.25 s, AI-driven land vehicles of sides that aren't enemies, within
//    RANGE_M of each other, are checked for a collision course (the closest approach of their motion
//    within CONFLICT_S, nearer than MISS_M). Who gives way is settled once for the pair and both act on
//    it, the "talk": a vehicle a player drives never gives way; then an armed vehicle keeps the right of
//    way over an unarmed one, a heavier one over a lighter one, and the more westerly one breaks a tie.
//    Head-on (moving more than 135 deg apart): the one giving way pulls over onto its right verge and
//    waits until the other is past, which slows to PASS_KMH and passes on its own right. Any other
//    course (crossing, reversing or turning round at a base): the one giving way brakes and waits.
//  - Handbrake (-bbbHandbrake): a land vehicle its AI driver has left (seat empty, or the driver down),
//    standing still, gets its handbrake on. Vanilla's own get-out sets it, but direct get-outs (the
//    host's), a driver killed or bailing don't. Under fire (hit in the last HIT_S) he forgets it
//    FORGET_UNDER_FIRE of the time (a roll seeded by the vehicle and the moment).
//  - Pull over (BBB_PullOver.Begin): a manoeuvre the host asks for before its crew gets out on a road:
//    forward onto the verge (VergeSpot), brake, handbrake on, then the listener is told.
//  - Enemy vehicles are never given way to. An unarmed AI vehicle whose crew sees an armed enemy vehicle
//    within EVADE_M asks the host to take it away (BBB_Hooks.Evade), once per EVADE_EVERY_S; with the
//    threat ahead within 200 m it first backs away for a few seconds (BBB_Unstick's reverse).
// Each is an option, off by default until it has been played (Vehicles and crews in the settings tab).
// All server only; vehicles the host leaves alone (BBB_Hooks.LeaveAlone) are skipped.
class BBB_Traffic
{
 static bool s_bGiveWay;
 static bool s_bHandbrake;
 static bool s_bEvade;
 static int s_iRead;
 static int s_iGiveWays;      // pairs settled (tests and the log)
 static int s_iHandbrakes;    // handbrakes set
 static int s_iEvades;        // evasions asked for
 protected static bool s_bLoop;
 protected static int s_iTicks;
 static const float TICK_MS = 250;
 static const float RANGE_M = 90;
 static const float CONFLICT_S = 6;
 static const float MISS_M = 4.5;
 static const float HEAD_ON_DOT = -0.7;   // cos 135 deg
 static const float PASS_KMH = 15;
 static const float HIT_S = 10;
 static const float FORGET_UNDER_FIRE = 0.4;
 static const float EVADE_M = 350;
 static const float EVADE_EVERY_S = 40;
 static const float SEEN_S = 5;
 // Filled on first use (no static initializer: the shared static-init budget).
 protected static ref array<ref BBB_GiveWay> s_aPairs;
 protected static ref map<IEntity, float> s_mDriven;     // vehicle -> world time (ms) an AI last drove it
 protected static ref map<IEntity, float> s_mEvadeAt;    // vehicle -> world time (ms) of its last evasion
 protected static ref array<IEntity> s_aParked;          // handled since its driver left

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  // Options, off by default until they have been played.
  s_bGiveWay = BBB_Settings.Get("bbbGiveWay", value) && value == "1";
  s_bHandbrake = BBB_Settings.Get("bbbHandbrake", value) && value == "1";
  s_bEvade = BBB_Settings.Get("bbbEvadeArmour", value) && value == "1";
  PrintFormat("[BBB] TRAFFIC giveWay=%1 handbrake=%2 evade=%3", s_bGiveWay, s_bHandbrake, s_bEvade);
  if (!s_bLoop && Replication.IsServer())
  {
   s_bLoop = true;
   GetGame().GetCallqueue().CallLater(Tick, TICK_MS, true);
  }
 }

 // The world time (ms).
 static float Now()
 {
  return GetGame().GetWorld().GetWorldTime();
 }

 static bool IsLand(IEntity vehicle)
 {
  return Vehicle.Cast(vehicle) && vehicle.FindComponent(CarControllerComponent);
 }

 // A turret seat with someone alive in it and a real gun.
 static bool Armed(IEntity vehicle)
 {
  BaseCompartmentManagerComponent compartments = BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(BaseCompartmentManagerComponent));
  if (!compartments) return false;
  array<BaseCompartmentSlot> slots = {};
  compartments.GetCompartments(slots);
  foreach (BaseCompartmentSlot slot : slots)
  {
   if (!TurretCompartmentSlot.Cast(slot)) continue;
   IEntity gunner = slot.GetOccupant();
   if (!gunner || !BBB.IsAlive(gunner)) continue;
   TurretControllerComponent controller = TurretControllerComponent.Cast(slot.GetController());
   if (!controller || !controller.GetWeaponManager()) continue;
   BaseWeaponComponent weapon = controller.GetWeaponManager().GetCurrentWeapon();
   if (weapon && weapon.GetWeaponType() != EWeaponType.WT_NONE) return true;
  }
  return false;
 }

 static Faction FactionOf(IEntity man)
 {
  if (!man) return null;
  FactionAffiliationComponent affiliation = FactionAffiliationComponent.Cast(man.FindComponent(FactionAffiliationComponent));
  if (!affiliation) return null;
  return affiliation.GetAffiliatedFaction();
 }

 static bool AIDriver(ChimeraCharacter driver)
 {
  if (!driver || !BBB.IsAlive(driver) || BBB.PlayerOf(driver)) return false;
  CharacterControllerComponent controller = driver.GetCharacterController();
  if (controller && controller.IsUnconscious()) return false;
  return true;
 }

 static vector Velocity(IEntity vehicle)
 {
  Physics physics = vehicle.GetPhysics();
  if (!physics) return vector.Zero;
  vector v = physics.GetVelocity();
  v[1] = 0;
  return v;
 }

 static float Mass(IEntity vehicle)
 {
  Physics physics = vehicle.GetPhysics();
  if (!physics) return 0;
  return physics.GetMass();
 }

 static void Tick()
 {
  Read();
  if (!s_bGiveWay && !s_bHandbrake && !s_bEvade) return; // every option off: nothing to do
  if (!BBB.s_Hooks || !GetGame().GetWorld()) return;
  if (!s_aPairs) s_aPairs = {};
  if (!s_mDriven) s_mDriven = new map<IEntity, float>();
  if (!s_mEvadeAt) s_mEvadeAt = new map<IEntity, float>();
  if (!s_aParked) s_aParked = {};
  float now = Now();
  s_iTicks++;
  // The land vehicles with a driver in them, from the groups' drivers.
  array<IEntity> vehicles = {};
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  array<AIAgent> agents = {};
  foreach (SCR_AIGroup group : groups)
  {
   if (!group) continue;
   agents.Clear();
   group.GetAgents(agents);
   foreach (AIAgent agent : agents)
   {
    if (!agent) continue;
    IEntity vehicle = BBB_Transport.VehicleOf(agent.GetControlledEntity());
    if (!vehicle || vehicles.Contains(vehicle) || !IsLand(vehicle) || BBB.s_Hooks.LeaveAlone(vehicle)) continue;
    ChimeraCharacter driver = BBB.DriverOf(vehicle);
    if (!driver) continue;
    vehicles.Insert(vehicle);
    if (AIDriver(driver))
    {
     s_mDriven.Set(vehicle, now);
     s_aParked.RemoveItem(vehicle);
     if (s_bEvade) EvadeCheck(group, vehicle, driver, now);
    }
   }
  }
  if (s_bGiveWay) GiveWayTick(vehicles, now);
  if (s_bHandbrake && (s_iTicks % 4) == 0) HandbrakeTick(now);
 }

 // ---------------------------------------------------------------- give way

 protected static void GiveWayTick(array<IEntity> vehicles, float now)
 {
  for (int i = 0; i < vehicles.Count(); i++)
  {
   IEntity a = vehicles[i];
   for (int j = i + 1; j < vehicles.Count(); j++)
   {
    IEntity b = vehicles[j];
    vector d = b.GetOrigin() - a.GetOrigin();
    d[1] = 0;
    if (d.Length() > RANGE_M) continue;
    ChimeraCharacter da = BBB.DriverOf(a);
    ChimeraCharacter db = BBB.DriverOf(b);
    if (!AIDriver(da) && !AIDriver(db)) continue; // two players sort it out themselves
    if (BBB_Combat.IsHostile(FactionOf(da), FactionOf(db))) continue; // never give way to the enemy
    vector va = Velocity(a);
    vector vb = Velocity(b);
    if (va.Length() < 1 && vb.Length() < 1) continue;
    BBB_GiveWay pair = Pair(a, b);
    if (pair) continue; // being settled: its own tick ends it
    vector dv = vb - va;
    float dv2 = vector.Dot(dv, dv);
    if (dv2 < 1) continue;
    float t = -vector.Dot(d, dv) / dv2;
    if (t < 0 || t > CONFLICT_S) continue;
    vector miss = d + dv * t;
    if (miss.Length() > MISS_M) continue;
    bool headOn = false;
    if (va.Length() > 2 && vb.Length() > 2) headOn = vector.Dot(va.Normalized(), vb.Normalized()) < HEAD_ON_DOT;
    Settle(a, da, b, db, headOn, t, now);
   }
  }
  for (int p = s_aPairs.Count() - 1; p >= 0; p--)
   if (!s_aPairs[p].Tick(now)) s_aPairs.RemoveOrdered(p);
 }

 protected static BBB_GiveWay Pair(IEntity a, IEntity b)
 {
  foreach (BBB_GiveWay pair : s_aPairs)
   if ((pair.m_Yield == a && pair.m_Keep == b) || (pair.m_Yield == b && pair.m_Keep == a)) return pair;
  return null;
 }

 static bool Busy(IEntity vehicle)
 {
  if (!s_aPairs) return false;
  foreach (BBB_GiveWay pair : s_aPairs)
   if (pair.m_Yield == vehicle) return true;
  return false;
 }

 // Who keeps the right of way: the same answer from either side.
 static bool Keeps(IEntity a, ChimeraCharacter da, IEntity b, ChimeraCharacter db)
 {
  bool playerA = !AIDriver(da);
  bool playerB = !AIDriver(db);
  if (playerA != playerB) return playerA;
  bool armedA = Armed(a);
  bool armedB = Armed(b);
  if (armedA != armedB) return armedA;
  float massA = Mass(a);
  float massB = Mass(b);
  if (Math.AbsFloat(massA - massB) > 500) return massA > massB;
  vector pa = a.GetOrigin();
  vector pb = b.GetOrigin();
  if (pa[0] != pb[0]) return pa[0] < pb[0];
  return pa[2] < pb[2];
 }

 protected static void Settle(IEntity a, ChimeraCharacter da, IEntity b, ChimeraCharacter db, bool headOn, float t, float now)
 {
  BBB_GiveWay pair = new BBB_GiveWay();
  if (Keeps(a, da, b, db))
  {
   pair.m_Keep = a;
   pair.m_Yield = b;
   pair.m_YieldDriver = db;
  }
  else
  {
   pair.m_Keep = b;
   pair.m_Yield = a;
   pair.m_YieldDriver = da;
  }
  if (Busy(pair.m_Yield) || BBB_PullOver.Active(pair.m_Yield)) return;
  pair.m_bHeadOn = headOn;
  pair.m_fStart = now;
  if (!pair.Begin(now)) return;
  s_aPairs.Insert(pair);
  s_iGiveWays++;
  string how = "brakes and waits";
  if (headOn) how = "pulls over to its right and waits";
  PrintFormat("[BBB] TRAFFIC give way: %1 %2 for %3 (collision course in %4 s)", BBB_Traffic.Name(pair.m_Yield), how, BBB_Traffic.Name(pair.m_Keep), Math.Round(t * 10) / 10);
 }

 static string Name(IEntity vehicle)
 {
  if (!vehicle || !vehicle.GetPrefabData()) return "vehicle";
  return FilePath.StripExtension(FilePath.StripPath(vehicle.GetPrefabData().GetPrefabName()));
 }

 // ---------------------------------------------------------------- handbrake

 protected static void HandbrakeTick(float now)
 {
  for (int i = s_mDriven.Count() - 1; i >= 0; i--)
  {
   IEntity vehicle = s_mDriven.GetKey(i);
   if (!vehicle || now - s_mDriven.GetElement(i) > 600000)
   {
    s_mDriven.RemoveElement(i);
    continue;
   }
   if (s_aParked.Contains(vehicle) || BBB_PullOver.Active(vehicle)) continue;
   ChimeraCharacter driver = BBB.DriverOf(vehicle);
   if (driver && BBB.IsAlive(driver) && !(driver.GetCharacterController() && driver.GetCharacterController().IsUnconscious())) continue;
   if (Velocity(vehicle).Length() > 2) continue;
   CarControllerComponent car = CarControllerComponent.Cast(vehicle.FindComponent(CarControllerComponent));
   if (!car) continue;
   s_aParked.Insert(vehicle);
   if (car.GetPersistentHandBrake()) continue;
   if (BBB_Transport.VehicleHitRecently(vehicle, HIT_S * 1000))
   {
    vector p = vehicle.GetOrigin();
    float roll = Math.AbsFloat(Math.Sin(p[0] * 12.9898 + p[2] * 78.233 + now * 0.0007));
    if (roll < FORGET_UNDER_FIRE)
    {
     PrintFormat("[BBB] TRAFFIC %1 left under fire without its handbrake", Name(vehicle));
     continue;
    }
   }
   car.SetPersistentHandBrake(true);
   s_iHandbrakes++;
   PrintFormat("[BBB] TRAFFIC %1: its driver left, handbrake on", Name(vehicle));
  }
 }

 // ---------------------------------------------------------------- enemy vehicles

 // An unarmed vehicle whose crew sees an armed enemy vehicle: ask the host to take it away.
 protected static void EvadeCheck(SCR_AIGroup group, IEntity vehicle, ChimeraCharacter driver, float now)
 {
  float last;
  if (s_mEvadeAt.Find(vehicle, last) && now - last < EVADE_EVERY_S * 1000) return;
  if (Armed(vehicle)) return;
  vector threat;
  if (!ArmedEnemySeen(vehicle, threat)) return;
  s_mEvadeAt.Set(vehicle, now);
  if (!BBB.s_Hooks.Evade(group, vehicle, threat)) return;
  s_iEvades++;
  // The threat ahead and close: back away first (BBB_Unstick's reverse), not turn round towards it.
  vector mat[4];
  vehicle.GetWorldTransform(mat);
  vector toThreat = threat - vehicle.GetOrigin();
  toThreat[1] = 0;
  float range = toThreat.Length();
  toThreat.Normalize();
  vector forward = mat[2];
  forward[1] = 0;
  forward.Normalize();
  if (range < 200 && vector.Dot(forward, toThreat) > 0.5)
  {
   AIControlComponent ai = AIControlComponent.Cast(driver.FindComponent(AIControlComponent));
   if (ai && ai.IsAIActivated() && !BBB_PullOver.Active(vehicle)) BBB_Unstick.Begin(vehicle, driver, 0, null);
  }
  PrintFormat("[BBB] TRAFFIC %1 is unarmed and sees an armed enemy vehicle %2 m away: gets away", Name(vehicle), Math.Round(vector.DistanceXZ(vehicle.GetOrigin(), threat)));
 }

 // An enemy vehicle with a gun in it that anyone in this vehicle has seen lately, within EVADE_M.
 static bool ArmedEnemySeen(IEntity vehicle, out vector threat)
 {
  BaseCompartmentManagerComponent compartments = BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(BaseCompartmentManagerComponent));
  if (!compartments) return false;
  array<BaseCompartmentSlot> slots = {};
  compartments.GetCompartments(slots);
  array<BaseTarget> targets = {};
  float best = EVADE_M;
  bool found;
  foreach (BaseCompartmentSlot slot : slots)
  {
   IEntity man = slot.GetOccupant();
   if (!man || !BBB.IsAlive(man) || BBB.PlayerOf(man)) continue;
   PerceptionComponent eyes = PerceptionComponent.Cast(man.FindComponent(PerceptionComponent));
   if (!eyes) continue;
   targets.Clear();
   eyes.GetTargetsList(targets, ETargetCategory.ENEMY);
   foreach (BaseTarget target : targets)
   {
    if (!target || target.GetTimeSinceSeen() > SEEN_S) continue;
    IEntity seen = target.GetTargetEntity();
    IEntity enemyVehicle = seen;
    if (!Vehicle.Cast(enemyVehicle)) enemyVehicle = BBB_Transport.VehicleOf(seen);
    if (!enemyVehicle || enemyVehicle == vehicle || !Armed(enemyVehicle)) continue;
    float distance = vector.DistanceXZ(vehicle.GetOrigin(), target.GetLastSeenPosition());
    if (distance >= best) continue;
    best = distance;
    threat = target.GetLastSeenPosition();
    found = true;
   }
  }
  return found;
 }
}

// One settled pair: the vehicle giving way and the one keeping the right of way.
class BBB_GiveWay : Managed
{
 IEntity m_Yield;
 ChimeraCharacter m_YieldDriver;
 IEntity m_Keep;
 bool m_bHeadOn;
 float m_fStart;
 bool m_bSlowed;
 static const float MAX_MS = 20000;
 static const float CLEAR_M = 14;

 bool Begin(float now)
 {
  int mode = BBB_PullOver.MODE_HOLD;
  if (m_bHeadOn) mode = BBB_PullOver.MODE_YIELD;
  if (!BBB_PullOver.Begin(m_Yield, m_YieldDriver, mode, 1, m_Keep, null)) return false;
  // The one keeping the way passes slowly (an AI driver only).
  ChimeraCharacter keeper = BBB.DriverOf(m_Keep);
  if (BBB_Traffic.AIDriver(keeper))
  {
   AICarMovementComponent movement = AICarMovementComponent.Cast(m_Keep.FindComponent(AICarMovementComponent));
   if (movement)
   {
    movement.SetCruiseSpeed(BBB_Traffic.PASS_KMH);
    m_bSlowed = true;
   }
  }
  return true;
 }

 // False once it is over: they are past each other (or one is gone).
 bool Tick(float now)
 {
  bool over = !m_Yield || !m_Keep || now - m_fStart > MAX_MS;
  if (!over)
  {
   vector d = m_Keep.GetOrigin() - m_Yield.GetOrigin();
   d[1] = 0;
   vector dv = BBB_Traffic.Velocity(m_Keep) - BBB_Traffic.Velocity(m_Yield);
   bool parting = vector.Dot(d, dv) > 0;
   over = parting && d.Length() > CLEAR_M;
  }
  if (!over) return true;
  BBB_PullOver.Release(m_Yield);
  if (m_bSlowed && m_Keep)
  {
   AICarMovementComponent movement = AICarMovementComponent.Cast(m_Keep.FindComponent(AICarMovementComponent));
   if (movement) movement.ResetCruiseSpeed();
  }
  return false;
 }
}

// Told when a pull-over is done (parked on the verge, or given up).
class BBB_PullOverListener : Managed
{
 void OnPullOverEnded(IEntity vehicle, bool parked) {}
}

// The driver's AI is parked and the car driven by hand, every frame (BBB.Frame), as BBB_Unstick does:
//  MODE_PARK  forward onto the verge, stop, handbrake on (before a crew gets out);
//  MODE_YIELD forward onto the verge on its right, stop, wait until released (giving way head-on);
//  MODE_HOLD  brake where it is until released (giving way anywhere else).
class BBB_PullOver : Managed
{
 static const int MODE_PARK = 0;
 static const int MODE_YIELD = 1;
 static const int MODE_HOLD = 2;
 static const float DRIVE_MAX_MS = 16000;
 static const float STEER_MAX = 0.6;    // at full lock a heavy 4x4 binds and crawls (srv:mannersgetout, 4 Nov: 1.8 km/h at 70% throttle)
 static const float WAIT_MAX_MS = 18000;
 static const float SPOT_AHEAD_M = 11;
 static const float VERGE_M = 2.5;     // past the road's edge
 static const float SHOULDER_M = 1;    // past the edge, when the verge is a ditch or a bank
 static const float EDGE_IN_M = 1.2;   // inside the edge: the road's own right side, when nothing off it is drivable
 static const float STEP_M = 0.5;      // the ground is sampled every half metre out from the road's middle...
 static const float STEP_RISE_M = 0.25; // ...and a rise or drop of more than this between two samples isn't drivable
 static string s_sWhere;               // where the last VergeSpot put it: "verge", "shoulder" or "road edge" (the host logs it)
 static int s_iParked;                // tests and the log
 static string s_sWhyNot;             // why the last Begin or VergeSpot refused (the host logs it)
 protected static ref array<ref BBB_PullOver> s_aActive;

 IEntity m_Vehicle;
 ChimeraCharacter m_Driver;
 IEntity m_Other;
 int m_iMode;
 vector m_vSpot;
 protected bool m_bHasSpot;
 protected float m_fStart;
 protected bool m_bStopped;
 protected bool m_bReleased;
 protected bool m_bParkedAI;
 protected ref BBB_PullOverListener m_Listener;
 protected float m_fLogAt;

 static bool Active(IEntity vehicle)
 {
  if (!s_aActive || !vehicle) return false;
  foreach (BBB_PullOver pull : s_aActive)
   if (pull.m_Vehicle == vehicle) return true;
  return false;
 }

 // side: 1 the verge on the vehicle's right, -1 its left (MODE_HOLD ignores it).
 static bool Begin(IEntity vehicle, ChimeraCharacter driver, int mode, int side, IEntity other, BBB_PullOverListener listener, float ahead = 0)
 {
  s_sWhyNot = "";
  if (!vehicle || !driver || !BBB_Traffic.AIDriver(driver)) { s_sWhyNot = "no AI driver"; return false; }
  if (Active(vehicle)) { s_sWhyNot = "already pulling over"; return false; }
  if (!s_aActive) s_aActive = {};
  AIControlComponent ai = AIControlComponent.Cast(driver.FindComponent(AIControlComponent));
  if (!ai || !ai.IsAIActivated()) { s_sWhyNot = "its driver is driven by hand already"; return false; } // an unstick, an escape
  BBB_PullOver pull = new BBB_PullOver();
  pull.m_Vehicle = vehicle;
  pull.m_Driver = driver;
  pull.m_iMode = mode;
  pull.m_Other = other;
  pull.m_Listener = listener;
  pull.m_fStart = GetGame().GetWorld().GetWorldTime();
  if (mode != MODE_HOLD) pull.m_bHasSpot = VergeSpot(vehicle, side, pull.m_vSpot, ahead);
  if (mode == MODE_PARK && !pull.m_bHasSpot) return false; // nowhere to pull over to: the host gets out where it is
  ai.DeactivateAI();
  pull.m_bParkedAI = true;
  BBB_Unstick.ReleaseHorn(driver);
  s_aActive.Insert(pull);
  return true;
 }

 // The giving way is over, or the host no longer wants it parked (its order changed): drive on.
 static void Release(IEntity vehicle)
 {
  if (!s_aActive) return;
  foreach (BBB_PullOver pull : s_aActive)
   if (pull.m_Vehicle == vehicle) pull.m_bReleased = true;
 }

 static void FrameAll()
 {
  if (!s_aActive) return;
  for (int i = s_aActive.Count() - 1; i >= 0; i--)
   if (!s_aActive[i].Frame()) s_aActive.RemoveOrdered(i);
 }

 // A spot on the verge SPOT_AHEAD_M ahead of the vehicle, VERGE_M past the edge of the road it is on,
 // on the side asked, on dry, fairly level ground with nothing solid in the way.
 // A spot to pull over to, 'ahead' m on along the road (SPOT_AHEAD_M when 0 or less): the verge, else the
 // shoulder, else the road's own right edge, whichever is first drivable from the road's middle (no ditch or
 // bank on the way, nothing solid there or on the way). False: not on a road, or even the road's edge is blocked.
 static bool VergeSpot(IEntity vehicle, int side, out vector spot, float ahead = 0)
 {
  vector at = vehicle.GetOrigin();
  vector point, dir;
  float width;
  if (!Snap(at, 6, point, dir, width)) { s_sWhyNot = "not on a road"; return false; }
  vector mat[4];
  vehicle.GetWorldTransform(mat);
  vector forward = mat[2];
  forward[1] = 0;
  forward.Normalize();
  if (vector.Dot(dir, forward) < 0) dir = -dir;
  vector right = Vector(dir[2], 0, -dir[0]);
  if (side < 0) right = -right;
  if (ahead <= 0) ahead = SPOT_AHEAD_M;
  vector middle = point + dir * ahead;
  string why;
  for (int option = 0; option < 3; option++)
  {
   float offset = width * 0.5 + VERGE_M;
   string where = "verge";
   if (option == 1) { offset = width * 0.5 + SHOULDER_M; where = "shoulder"; }
   if (option == 2) { offset = Math.Max(0.5, width * 0.5 - EDGE_IN_M); where = "road edge"; }
   if (!SpotClear(vehicle, at, middle, right, offset, option < 2, spot, why)) continue;
   s_sWhere = where;
   return true;
  }
  s_sWhyNot = why;
  return false;
 }

 // The spot 'offset' m to the side of the road's middle: drivable out to it (when 'offRoad', the ground
 // sampled every STEP_M), not water, nothing solid there or on the way.
 protected static bool SpotClear(IEntity vehicle, vector at, vector middle, vector right, float offset, bool offRoad, out vector spot, out string why)
 {
  BaseWorld world = GetGame().GetWorld();
  spot = middle + right * offset;
  spot[1] = world.GetSurfaceY(spot[0], spot[2]);
  if (BBB_Terrain.IsWaterAt(spot)) { why = "the verge is water"; return false; }
  if (offRoad)
  {
   float last = world.GetSurfaceY(middle[0], middle[2]);
   for (float s = STEP_M; s <= offset + 0.01; s += STEP_M)
   {
    vector p = middle + right * s;
    float y = world.GetSurfaceY(p[0], p[2]);
    if (Math.AbsFloat(y - last) > STEP_RISE_M) { why = "a ditch or a bank on the way to the verge"; return false; }
    last = y;
   }
  }
  BBB_PullOverObstacles obstacles = new BBB_PullOverObstacles();
  obstacles.m_Self = vehicle;
  GetGame().GetWorld().QueryEntitiesBySphere(spot + Vector(0, 1, 0), 3, obstacles.Add, null, EQueryEntitiesFlags.STATIC);
  if (obstacles.m_bBlocked) { why = "something solid on the verge: " + obstacles.m_sWhat; return false; }
  // From outside the hull: its crew, doors and wheels are entities of their own the trace would hit.
  vector toSpot = spot - at;
  toSpot[1] = 0;
  toSpot.Normalize();
  TraceParam trace = new TraceParam();
  trace.Start = at + toSpot * 3.5 + Vector(0, 1, 0);
  trace.End = spot + Vector(0, 1, 0);
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  trace.Exclude = vehicle;
  float fraction = GetGame().GetWorld().TraceMove(trace, null);
  if (fraction < 0.98) { why = "the way to the verge is blocked (" + BBB_Traffic.Name(trace.TraceEnt) + ")"; return false; }
  return true;
 }

 // The nearest road's point, direction and width: every road piece round the point, projected onto
 // (the road network's own closest road can be another one: srv:manners, 62 m off for a car on a road).
 static bool Snap(vector near, float maxDistance, out vector point, out vector dir, out float width)
 {
  ChimeraAIWorld aiWorld = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
  if (!aiWorld || !aiWorld.GetRoadNetworkManager()) return false;
  float reach = maxDistance + 25;
  array<BaseRoad> roads = {};
  aiWorld.GetRoadNetworkManager().GetRoadsInAABB(near - Vector(reach, 500, reach), near + Vector(reach, 500, reach), roads);
  float best = float.MAX;
  array<vector> nodes = {};
  foreach (BaseRoad road : roads)
  {
   nodes.Clear();
   road.GetPoints(nodes);
   for (int i = 0; i + 1 < nodes.Count(); i++)
   {
    vector a = nodes[i];
    vector b = nodes[i + 1];
    vector ab = b - a;
    ab[1] = 0;
    float len2 = vector.Dot(ab, ab);
    if (len2 < 0.01) continue;
    vector ap = near - a;
    ap[1] = 0;
    float t = Math.Clamp(vector.Dot(ap, ab) / len2, 0, 1);
    vector closest = a + ab * t;
    float d = vector.DistanceXZ(closest, near);
    if (d >= best) continue;
    best = d;
    point = closest;
    dir = ab;
    width = road.GetWidth();
   }
  }
  if (best > maxDistance) return false;
  dir[1] = 0;
  dir.Normalize();
  point[1] = GetGame().GetWorld().GetSurfaceY(point[0], point[2]);
  if (width <= 0) width = 6;
  return true;
 }

 // Returns false when finished.
 protected bool Frame()
 {
  float elapsed = GetGame().GetWorld().GetWorldTime() - m_fStart;
  IEntity vehicle = m_Vehicle;
  bool alive = vehicle != null && m_Driver != null && BBB.IsAlive(m_Driver) && BBB_Transport.VehicleOf(m_Driver) == vehicle && !BBB.PlayerOf(m_Driver) && BBB.DriverOf(vehicle) == m_Driver;
  CarControllerComponent car;
  if (vehicle) car = CarControllerComponent.Cast(vehicle.FindComponent(CarControllerComponent));
  if (!alive || !car || !car.GetSimulation() || elapsed > DRIVE_MAX_MS + WAIT_MAX_MS)
  {
   End(car, false);
   return false;
  }
  // Shot at while giving way: drive on (the fight comes first).
  if (m_iMode != MODE_PARK && BBB_Transport.VehicleHitRecently(vehicle, 3000)) m_bReleased = true;
  // Parking, but released by the host or hit: stop here, no handbrake, and tell the host it didn't
  // park (a newer order takes the driver at once; under fire the crew gets out where it is).
  if (m_iMode == MODE_PARK && (m_bReleased || BBB_Transport.VehicleHitRecently(vehicle, 3000)))
  {
   End(car, false);
   return false;
  }
  VehicleWheeledSimulation sim = car.GetSimulation();
  Physics physics = vehicle.GetPhysics();
  if (physics && !physics.IsActive()) physics.SetActive(ActiveState.ACTIVE);
  BBB_Unstick.ReleaseHorn(m_Driver);
  float speed = BBB_Traffic.Velocity(vehicle).Length();
  int neutral = sim.GearboxGearsCount() - sim.GearboxForwardGearsCount() - 1;
  if (!m_bStopped && m_bHasSpot && m_iMode != MODE_HOLD)
  {
   vector local = vehicle.CoordToLocal(m_vSpot);
   float distance = Math.Sqrt(local[0] * local[0] + local[2] * local[2]);
   if (distance < 2.5 || local[2] < 0.5 || elapsed > DRIVE_MAX_MS) m_bStopped = true;
   else
   {
    if (!car.IsEngineOn()) car.ForceStartEngine();
    car.SetPersistentHandBrake(false);
    if (sim.GetGear() <= neutral) sim.SetGear(neutral + 1);
    sim.SetClutch(1);
    float steer = Math.Clamp(Math.Atan2(local[0], local[2]) * 1.8, -STEER_MAX, STEER_MAX);
    if (elapsed >= m_fLogAt)
    {
     m_fLogAt = elapsed + 1000;
     PrintFormat("[BBB] TRAFFIC pull-over %1 t=%2 s: spot %3 m right, %4 m ahead; steer %5, %6 km/h, gear %7 (neutral %8)", BBB_Traffic.Name(vehicle), Math.Round(elapsed / 100) / 10, Math.Round(local[0] * 10) / 10, Math.Round(local[2] * 10) / 10, Math.Round(steer * 100) / 100, Math.Round(speed * 36) / 10, sim.GetGear(), neutral);
    }
    // A walking pace onto the verge, the wheel short of full lock (STEER_MAX).
    if (speed > 4.5) BBB_Unstick.Drive(m_Driver, sim, 0, steer, 0.6);
    else if (speed < 2.5) BBB_Unstick.Drive(m_Driver, sim, 0.7, steer, 0);
    else BBB_Unstick.Drive(m_Driver, sim, 0.3, steer, 0);
    return true;
   }
  }
  // Stopped (or holding): brake.
  BBB_Unstick.Drive(m_Driver, sim, 0, 0, 1);
  if (m_iMode == MODE_PARK)
  {
   if (speed > 0.4) return true;
   car.SetPersistentHandBrake(true);
   s_iParked++;
   End(car, true);
   return false;
  }
  if (!m_bReleased && elapsed < DRIVE_MAX_MS + WAIT_MAX_MS) return true;
  End(car, false);
  return false;
 }

 protected void End(CarControllerComponent car, bool parked)
 {
  if (car && car.GetSimulation())
  {
   BBB_Unstick.Drive(m_Driver, car.GetSimulation(), 0, 0, 0);
  }
  BBB_Unstick.ReleaseHorn(m_Driver);
  if (m_bParkedAI && m_Driver && BBB.IsAlive(m_Driver) && !BBB.PlayerOf(m_Driver))
  {
   AIControlComponent ai = AIControlComponent.Cast(m_Driver.FindComponent(AIControlComponent));
   if (ai && !ai.IsAIActivated()) ai.ActivateAI();
  }
  if (parked) PrintFormat("[BBB] TRAFFIC %1 pulled over onto the verge, handbrake on", BBB_Traffic.Name(m_Vehicle));
  if (m_Listener) m_Listener.OnPullOverEnded(m_Vehicle, parked);
 }
}

// Solid things at a verge spot: buildings, walls, fences, trees (bushes are fine).
class BBB_PullOverObstacles : Managed
{
 bool m_bBlocked;
 string m_sWhat;
 IEntity m_Self;

 bool Add(IEntity e)
 {
  if (!e || e == m_Self || (m_Self && e.GetRootParent() == m_Self)) return true;
  if (BaseBuilding.Cast(e) || SCR_DestructibleBuildingEntity.Cast(e) || WallGeneratorEntity.Cast(e) || Vehicle.Cast(e))
  {
   m_bBlocked = true;
   m_sWhat = BBB_Traffic.Name(e);
   return false;
  }
  if (Tree.Cast(e))
  {
   vector mins, maxs;
   e.GetBounds(mins, maxs);
   if (maxs[1] - mins[1] >= 4) { m_bBlocked = true; m_sWhat = "a tree"; return false; }
  }
  return true;
 }
}
