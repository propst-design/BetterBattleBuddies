// Standalone native-state comparisons, excluded with all other BBB tests.
// turnout: disabled baseline -> real quiet interval -> observed turnout -> hit
// -> observed button-up -> no early reopening. alignment: native perception,
// limited-arc fixture, disabled baseline -> enabled correction -> move veto.
class BBB_VehiclePostureTest : BBB_AimTest
{
 static ref BBB_VehiclePostureTest s_PostureTest;
 protected SCR_AIGroup m_Crew;
 protected SCR_AIGroup m_Enemy;
 protected IEntity m_Vehicle;
 protected ChimeraCharacter m_Driver;
 protected ChimeraCharacter m_Gunner;
 protected BaseCompartmentSlot m_Pilot;
 protected BaseCompartmentSlot m_GunSeat;
 protected AIWaypoint m_Move;
 protected float m_fStart;
 protected int m_iPhase;
 protected bool m_bTurnout;
 protected bool m_bCompletion;
 protected int m_iChecks;
 protected int m_iFailures;
 protected bool m_bObservedOut;
 protected bool m_bObservedIn;
 protected bool m_bOrderIn;
 protected float m_fShift;
 protected int m_iInBefore;
 protected ref BBB_OrderIntentTestHold m_Hold;
 protected vector m_Original;
 protected vector m_Nose;
 protected float m_fOffTravel;
 protected float m_fOnTravel;
 protected int m_iStartsBefore;
 protected float m_fBestExcess = 1000;
 protected float m_fFirstExcess;
 protected float m_fEligibleAt;
 protected float m_fSeatDeadline;
 protected bool m_bSawEligible;

 static void Start()
 {
  if (!Replication.IsServer() || !System.IsCLIParam("bbbVehiclePostureTest")) return;
  s_PostureTest = new BBB_VehiclePostureTest();
  string testCase;
  System.GetCLIParam("bbbVehiclePostureCase", testCase);
  s_PostureTest.m_bTurnout = testCase == "turnout";
  s_PostureTest.m_bCompletion = testCase == "alignmentcomplete";
  GetGame().GetCallqueue().CallLater(s_PostureTest.BeginPosture, 15000);
 }

 static bool HoldsDriver(IEntity body)
 {
  return s_PostureTest && !s_PostureTest.m_bTurnout && s_PostureTest.m_Driver == body && !s_PostureTest.m_Move;
 }

 protected void Check(string label, bool passed)
 {
  m_iChecks++;
  if (!passed) m_iFailures++;
  PrintFormat("[BBB] POSTURECHECK %1 ok=%2", label, passed);
 }

 protected void BeginPosture()
 {
  BBB_CrewTurnout.Read();
  BBB_DriverAlignment.Read();
  BBB_CrewTurnout.s_bOn = false;
  BBB_DriverAlignment.s_bOn = false;
  BBB_FireManeuver.s_bOn = false; // bounding infantry shifts the target geometry mid-check
  ResourceName vehiclePrefab = "{C012BB3488BEA0C2}Prefabs/Vehicles/Wheeled/BTR70/BTR70.et";
  if (!m_bTurnout) vehiclePrefab = "{BBB0A11900000002}Prefabs/BBB_Tests/BBB_AlignmentVehicle.et";
  vector at = BBB.Ground(Vector(2880, 0, 1580));
  m_Vehicle = Spawn(vehiclePrefab, at, Vector(0, 0, 1));
  if (!m_Vehicle) { Check("vehicle-spawn", false); FinishPosture(); return; }
  // Find open flat land for an adjustment; the production guard is intentionally
  // stricter than the broad rifle lane finder. Moving an empty test fixture only.
  if (!m_bTurnout)
  {
   bool found = false;
   for (int x = 2400; x <= 3200 && !found; x += 25)
   {
    for (int z = 1200; z <= 1900 && !found; z += 25)
    {
     m_Vehicle.SetOrigin(BBB.Ground(Vector(x, 0, z)));
     found = BBB_DriverAlignment.OpenGround(m_Vehicle);
     vector laneEnd = BBB.Ground(m_Vehicle.GetOrigin() + BBB_DriverAlignment.Rotated(Vector(0, 0, 1), 17) * 150) + Vector(0, 1, 0);
     if (found) found = BBB_DriverAlignment.ClearLine(m_Vehicle, null, m_Vehicle.GetOrigin() + Vector(0, 1.8, 0), laneEnd);
    }
   }
   Check("open-ground-fixture", found);
   if (!found) { FinishPosture(); return; }
   vector chosen = m_Vehicle.GetOrigin();
   SCR_EntityHelper.DeleteEntityAndChildren(m_Vehicle);
   m_Vehicle = Spawn(vehiclePrefab, chosen, Vector(0, 0, 1));
  }
  at = m_Vehicle.GetOrigin();
  NavmeshWorldComponent navmesh = Navmesh();
  if (navmesh) navmesh.LoadTileIn(at);
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers) observers.InsertObserverSP(22119, at[0], at[2], null);
  m_Crew = SCR_AIGroup.Cast(Spawn(TEAM_USSR, at + Vector(5, 0, 0), Vector(0, 0, 1)));
  GetGame().GetCallqueue().CallLater(SeatCrew, 3000);
 }

 protected void SeatCrew()
 {
  array<AIAgent> agents = {};
  if (m_Crew) m_Crew.GetAgents(agents);
  array<BaseCompartmentSlot> pilots = {};
  array<BaseCompartmentSlot> turrets = {};
  BBB_VehicleHelpers.GetVehicleCompartments(m_Vehicle, pilots, ECompartmentType.PILOT);
  BBB_VehicleHelpers.GetVehicleCompartments(m_Vehicle, turrets, ECompartmentType.TURRET);
  foreach (BaseCompartmentSlot seat : turrets)
   if ((m_bTurnout && BBB_CrewTurnout.Supported(seat)) || (!m_bTurnout && BBB_VehicleHelpers.TurretHasAmmo(seat))) { m_GunSeat = seat; break; }
  Check("crew-and-seats", agents.Count() >= 2 && !pilots.IsEmpty() && m_GunSeat != null);
  if (agents.Count() < 2 || pilots.IsEmpty() || !m_GunSeat) { FinishPosture(); return; }
  m_Pilot = pilots[0];
  m_Driver = ChimeraCharacter.Cast(agents[0].GetControlledEntity());
  m_Gunner = ChimeraCharacter.Cast(agents[1].GetControlledEntity());
  PinDetail(m_Crew);
  foreach (AIAgent agent : agents)
  {
   PinDetail(agent);
   SCR_AIUtilityComponent crewUtility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   if (!m_bTurnout && crewUtility && crewUtility.m_CombatComponent) crewUtility.m_CombatComponent.SetPerceptionFactor(10);
  }
  for (int i = agents.Count() - 1; i >= 2; i--)
   SCR_EntityHelper.DeleteEntityAndChildren(agents[i].GetControlledEntity());
  m_Driver.GetDamageManager().EnableDamageHandling(false);
  m_Gunner.GetDamageManager().EnableDamageHandling(false);
  bool driverAccepted = m_Driver.GetCompartmentAccessComponent().GetInVehicle(m_Pilot.GetOwner(), m_Pilot, true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false);
  bool gunnerAccepted = m_Gunner.GetCompartmentAccessComponent().GetInVehicle(m_GunSeat.GetOwner(), m_GunSeat, true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false);
  PrintFormat("[BBB] POSTUREBOARD driver=%1 gunner=%2 vehicle=%3", driverAccepted, gunnerAccepted, m_Vehicle.GetOrigin());
  PrintFormat("[BBB] POSTUREBOARD owners driver=%1 gunner=%2 root=%3", m_Pilot.GetVehicle(), m_GunSeat.GetVehicle(), m_Vehicle);
  BBB_VehicleHelpers.s_mGunnerReplaceNext.Set(m_Vehicle, GetGame().GetWorld().GetWorldTime() + 180000);
  m_fSeatDeadline = GetGame().GetWorld().GetWorldTime() + 20000;
  GetGame().GetCallqueue().CallLater(Ready, 5000);
 }

 protected void Ready()
 {
  if ((m_Pilot.GetOccupant() != m_Driver || m_GunSeat.GetOccupant() != m_Gunner) && GetGame().GetWorld().GetWorldTime() < m_fSeatDeadline)
  {
   GetGame().GetCallqueue().CallLater(Ready, 250);
   return;
  }
  PrintFormat("[BBB] POSTURESEATS driver=%1 driverSeat=%2 gunner=%3 gunSeat=%4", m_Driver, m_Pilot.GetOccupant(), m_Gunner, m_GunSeat.GetOccupant());
  Check("actual-occupants", m_Pilot.GetOccupant() == m_Driver && m_GunSeat.GetOccupant() == m_Gunner);
  if (m_iFailures > 0) { FinishPosture(); return; }
  RplComponent vehicleRpl = RplComponent.Cast(m_Vehicle.FindComponent(RplComponent));
  RplComponent crewRpl = RplComponent.Cast(m_Gunner.FindComponent(RplComponent));
  Check("vehicle-replication-node", vehicleRpl && vehicleRpl.Id().IsValid() && Replication.FindItem(vehicleRpl.Id()) == vehicleRpl);
  Check("crew-replication-node", crewRpl && crewRpl.Id().IsValid() && Replication.FindItem(crewRpl.Id()) == crewRpl);
  m_Original = m_Vehicle.GetOrigin();
  m_Nose = m_Vehicle.GetTransformAxis(2);
  m_fStart = GetGame().GetWorld().GetWorldTime();
  if (m_bTurnout)
  {
   Check("supported-armored-commander-hatch", BBB_CrewTurnout.Supported(m_GunSeat));
   Check("armored-vehicle-classification", BBB_VehiclePosture.Armored(m_Vehicle));
   Check("unsupported-driver-seat-skipped", !BBB_CrewTurnout.Supported(m_Pilot));
   if (!BBB_CrewTurnout.Supported(m_GunSeat)) { FinishPosture(); return; }
  }
  else
  {
   CarControllerComponent car = CarControllerComponent.Cast(m_Vehicle.FindComponent(CarControllerComponent));
   car.ForceStartEngine();
   car.SetPersistentHandBrake(true);
   // A stable native contact 17 degrees to the right of a +/-10-degree turret.
   vector enemyAt = m_Original + BBB_DriverAlignment.Rotated(m_Nose, 17) * 150;
   NavmeshWorldComponent navmesh = Navmesh();
   if (navmesh) navmesh.LoadTileIn(enemyAt);
   SCR_AIGroup.IgnoreSpawning(true);
   m_Enemy = SCR_AIGroup.Cast(Spawn(GROUP_US, enemyAt, m_Nose * -1));
   SCR_AIGroup.IgnoreSpawning(false);
   if (m_Enemy)
   {
    if (!m_Enemy.m_aUnitPrefabSlots) m_Enemy.m_aUnitPrefabSlots = {};
    m_Enemy.m_aUnitPrefabSlots.Clear();
    m_Enemy.m_aUnitPrefabSlots.Insert(RIFLE_US);
    IEntity enemyBody = Spawn(RIFLE_US, enemyAt, m_Nose * -1);
    if (enemyBody)
    {
     m_Enemy.AddAIEntityToGroup(enemyBody);
     // This fixture measures an uninterrupted turn, not killing the contact.
     ChimeraCharacter stableEnemy = ChimeraCharacter.Cast(enemyBody);
     if (stableEnemy) stableEnemy.GetDamageManager().EnableDamageHandling(false);
    }
   }
   Check("enemy-fixture", m_Enemy != null);
   Check("signed-bearing", Math.AbsFloat(BBB_DriverAlignment.Bearing(m_Nose, BBB_DriverAlignment.Rotated(m_Nose, 17)) - 17) < 0.1);
   TurretControllerComponent controller = BBB_VehicleHelpers.TurretControllerOf(m_GunSeat);
   vector outside = controller.GetTurretComponent().GetAimingAngleExcess(BBB.Ground(enemyAt) + Vector(0, 1, 0));
   Check("native-limited-arc", outside[0] > 5 && outside[0] < 20);
   BBB_DriverAlignmentState emptyState = new BBB_DriverAlignmentState();
   float emptyExcess;
   Check("missing-target-rejected", !BBB_DriverAlignment.Engagement(emptyState, enemyAt, emptyExcess));
   m_iStartsBefore = BBB_DriverAlignment.s_iStarted;
  }
  GetGame().GetCallqueue().CallLater(Sample, 250, true);
 }

 protected void QuietCrew()
 {
  // The quiet fixture has no enemy. Do not mask production threats in the
  // actual danger phase. Neutralize only transient spawn/equipment noise.
  array<AIAgent> agents = {};
  m_Crew.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   if (utility && utility.m_ThreatSystem) utility.m_ThreatSystem.SetThreatValues(0, 0, 0, 0);
  }
 }

 protected void Sample()
 {
  float elapsed = GetGame().GetWorld().GetWorldTime() - m_fStart;
  if (m_bTurnout) SampleTurnout(elapsed);
  else SampleAlignment(elapsed);
 }

 protected void SampleTurnout(float elapsed)
 {
  if (m_iPhase == 0 && elapsed >= 8000)
  {
   Check("off-baseline-buttoned-up", !m_GunSeat.IsOccupantTurnedOut() && BBB_CrewTurnout.s_iRequestsOut == 0);
   QuietCrew();
   BBB_CrewTurnout.s_bOn = true;
   m_iPhase = 1;
  }
  if (m_iPhase == 1)
  {
   if (elapsed < 37000 && m_GunSeat.IsOccupantTurnedOut()) { Check("quiet-delay-no-early-opening", false); FinishPosture(); return; }
   if (m_GunSeat.IsOccupantTurnedOut()) m_bObservedOut = true;
   if (elapsed >= 48000)
   {
    Check("on-observed-native-turnout", m_bObservedOut);
    Check("door-helper-preserves-open-hatch", m_GunSeat.GetManager().IsDoorOpen(m_GunSeat.GetTurnOutDoorIndex()));
    // A player-level order arrives while BBB has him out: BBB turns him back in first (#7).
    m_Hold = new BBB_OrderIntentTestHold(BBB_VehiclePosture.Utility(m_Gunner), null);
    BBB_VehiclePosture.Utility(m_Gunner).AddAction(m_Hold);
    m_iPhase = 10;
   }
  }
  if (m_iPhase == 10)
  {
   if (!m_GunSeat.IsOccupantTurnedOut()) m_bOrderIn = true;
   if (elapsed >= 56000)
   {
    Check("player-order-buttons-up", m_bOrderIn && !m_GunSeat.IsOccupantTurnedOut());
    Check("player-order-hatch-closed", !m_GunSeat.GetManager().IsDoorOpen(m_GunSeat.GetTurnOutDoorIndex()));
    m_Hold.Fail();
    m_Hold = null;
    QuietCrew();
    m_bObservedOut = false;
    m_iPhase = 11;
   }
  }
  if (m_iPhase == 11)
  {
   if (m_GunSeat.IsOccupantTurnedOut())
   {
    // Turned out again once the order ended and it stayed quiet: the hit phase starts from here.
    Check("turnout-resumes-after-order", elapsed >= 86000);
    m_fShift = elapsed - 48000;
    m_iInBefore = BBB_CrewTurnout.s_iRequestsIn;
    BBB_Transport.OnVehicleHit(m_Vehicle);
    m_iPhase = 2;
   }
   else if (elapsed >= 115000) { Check("turnout-resumes-after-order", false); FinishPosture(); return; }
  }
  if (m_iPhase == 2)
  {
   if (!m_GunSeat.IsOccupantTurnedOut()) m_bObservedIn = true;
   if (elapsed >= 56000 + m_fShift)
   {
    Check("hit-buttons-up", m_bObservedIn && BBB_CrewTurnout.s_iRequestsIn > m_iInBefore);
    Check("hit-hatch-closed", !m_GunSeat.GetManager().IsDoorOpen(m_GunSeat.GetTurnOutDoorIndex()));
    m_iPhase = 3;
   }
  }
  if (m_iPhase == 3 && m_GunSeat.IsOccupantTurnedOut()) { Check("danger-no-hatch-cycling", false); FinishPosture(); return; }
  if (m_iPhase == 3 && elapsed >= 70000 + m_fShift)
  {
   Check("no-early-reopening", !m_GunSeat.IsOccupantTurnedOut());
   SCR_AIUtilityComponent utility = BBB_VehiclePosture.Utility(m_Gunner);
   float suppression, shots, injury, endangered;
   utility.m_ThreatSystem.GetThreatValues(suppression, shots, injury, endangered);
   utility.m_ThreatSystem.SetThreatValues(0.2, 0, 0, 0);
   Check("native-suppression-veto", BBB_VehiclePosture.UnderFire(utility));
   utility.m_ThreatSystem.SetThreatValues(suppression, shots, injury, endangered);
   BBB_OrderIntentTestHold hold = new BBB_OrderIntentTestHold(utility, null);
   utility.AddAction(hold);
   Check("queued-player-order-veto", !BBB_VehiclePosture.CrewAllows(m_Vehicle));
   hold.Fail();
   BBB_CrewTurnout.Stop();
   Check("turnout-lifecycle-cleared", BBB_CrewTurnout.s_States.IsEmpty());
   FinishPosture();
  }
 }

 protected void SampleAlignment(float elapsed)
 {
  if (m_Enemy)
  {
   m_Enemy.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.HOLD_FIRE);
   m_Enemy.GetGroupUtilityComponent().EvaluateCombatMode();
   PinDetail(m_Enemy);
   array<AIAgent> enemies = {};
   m_Enemy.GetAgents(enemies);
   foreach (AIAgent enemy : enemies)
   {
    PinDetail(enemy);
    ChimeraCharacter body = ChimeraCharacter.Cast(enemy.GetControlledEntity());
    SCR_AIUtilityComponent passiveUtility = SCR_AIUtilityComponent.Cast(enemy.FindComponent(SCR_AIUtilityComponent));
    if (passiveUtility && passiveUtility.m_CombatComponent) passiveUtility.m_CombatComponent.SetPerceptionFactor(0);
    if (body)
    {
     body.GetCharacterController().OverrideMaxSpeed(0);
    }
   }
  }
  BBB_DriverAlignmentState state = BBB_DriverAlignment.State(m_Vehicle);
  BBB_DriverAlignmentState probe = new BBB_DriverAlignmentState();
  probe.m_Group = m_Crew;
  probe.m_Vehicle = m_Vehicle;
  probe.m_Driver = m_Driver;
  probe.m_Gunner = m_Gunner;
  probe.m_Seat = m_GunSeat;
  SCR_AIUtilityComponent gunner = BBB_VehiclePosture.Utility(m_Gunner);
  if (gunner && gunner.m_CombatComponent) probe.m_Target = BBB_DriverAlignment.TargetOf(gunner);
  vector aim;
  float excess;
  bool eligible = BBB_DriverAlignment.Engagement(probe, aim, excess);
  if (eligible)
  {
   if (!m_bSawEligible) { m_fEligibleAt = elapsed; m_fFirstExcess = Math.AbsFloat(excess); }
   m_bSawEligible = true;
   m_fBestExcess = Math.Min(m_fBestExcess, Math.AbsFloat(excess));
  }
  if (m_iPhase == 0 && elapsed >= 25000)
  {
   m_fOffTravel = vector.DistanceXZ(m_Original, m_Vehicle.GetOrigin());
   Check("stationary-off-baseline", m_fOffTravel < 0.5);
   Check("off-baseline-no-alignment", BBB_DriverAlignment.s_iStarted == m_iStartsBefore);
   Check("own-visible-viable-native-contact", m_bSawEligible);
   BBB_DriverAlignmentState timing = new BBB_DriverAlignmentState();
   float now = GetGame().GetWorld().GetWorldTime();
   bool first = !BBB_DriverAlignment.Stable(timing, probe.m_Target, now);
   bool early = !BBB_DriverAlignment.Stable(timing, probe.m_Target, now + 1000);
   bool ready = BBB_DriverAlignment.Stable(timing, probe.m_Target, now + 3001);
   timing.m_fCooldownUntil = now + 20000;
   bool cooldown = !BBB_DriverAlignment.Stable(timing, probe.m_Target, now + 5000);
   bool lost = !BBB_DriverAlignment.Stable(timing, null, now + 21000);
   Check("stable-target-delay-cooldown-loss", first && early && ready && cooldown && lost);
   PrintFormat("[BBB] ALIGNDIAG eligibility=%1 orders=%2 protection=%3 open=%4 target=%5 excess=%6", eligible, BBB_DriverAlignment.OrdersAllow(m_Crew, m_Vehicle, m_Driver), BBB_DriverAlignment.Protection(probe, 7), BBB_DriverAlignment.OpenGround(m_Vehicle), probe.m_Target, excess);
   array<AIAgent> crewAgents = {};
   m_Crew.GetAgents(crewAgents);
   foreach (AIAgent crewAgent : crewAgents)
   {
    SCR_AIUtilityComponent crewInfo = SCR_AIUtilityComponent.Cast(crewAgent.FindComponent(SCR_AIUtilityComponent));
    float su, sh, inj, en;
    crewInfo.m_ThreatSystem.GetThreatValues(su, sh, inj, en);
    PrintFormat("[BBB] ALIGNTHREAT actor=%1 suppression=%2 shots=%3 injury=%4 endangered=%5 events=%6", crewAgent.GetControlledEntity(), su, sh, inj, en, crewAgent.GetDangerEventsCount());
   }
   if (probe.m_Target) PrintFormat("[BBB] ALIGNCONTACT seen=%1 trace=%2 exposure=%3 range=%4 category=%5", probe.m_Target.GetTimeSinceSeen(), probe.m_Target.GetTraceFraction(), probe.m_Target.GetExposure(), probe.m_Target.GetDistance(), probe.m_Target.GetTargetCategory());
   PrintFormat("[BBB] ALIGNCOUNTS unknown=%1 detected=%2 friendly=%3 factionless=%4 lod=%5", gunner.m_PerceptionComponent.GetTargetCount(ETargetCategory.UNKNOWN), gunner.m_PerceptionComponent.GetTargetCount(ETargetCategory.DETECTED), gunner.m_PerceptionComponent.GetTargetCount(ETargetCategory.FRIENDLY), gunner.m_PerceptionComponent.GetTargetCount(ETargetCategory.FACTIONLESS), gunner.GetAIAgent().GetLOD());
   CarControllerComponent car = CarControllerComponent.Cast(m_Vehicle.FindComponent(CarControllerComponent));
   SCR_AIUtilityComponent driverUtility = BBB_VehiclePosture.Utility(m_Driver);
   PrintFormat("[BBB] ALIGNDRIVER engine=%1 behavior=%2 waypoint=%3", car.IsEngineOn(), driverUtility.GetCurrentBehavior(), m_Crew.GetCurrentWaypoint());
   TurretComponent diagnosticTurret = BBB_VehicleHelpers.TurretControllerOf(m_GunSeat).GetTurretComponent();
   PrintFormat("[BBB] ALIGNGEOMETRY hullYaw=%1 baseRotation=%2 moveableBase=%3", BBB_DriverAlignment.Bearing(m_Nose, m_Vehicle.GetTransformAxis(2)), diagnosticTurret.GetBaseRotation(), diagnosticTurret.HasMoveableBase());
   array<BaseTarget> contacts = {};
   gunner.m_PerceptionComponent.GetTargetsList(contacts, ETargetCategory.ENEMY);
   PrintFormat("[BBB] ALIGNCONTACTS enemies=%1", contacts.Count());
   PrintFormat("[BBB] ALIGNGUNNER seated=%1 inTurret=%2 behavior=%3 nativeWeaponTypes=%4", m_GunSeat.GetOccupant() == m_Gunner, gunner.m_AIInfo.HasUnitState(EUnitState.IN_TURRET), gunner.GetCurrentBehavior(), gunner.m_CombatComponent.m_WeaponTargetSelector.GetUnitTypesCanAttack());
   if (probe.m_WeaponProbe)
   {
    BaseWeaponComponent probedWeapon;
    BaseMagazineComponent probedMagazine;
    int probedMuzzle;
    probe.m_WeaponProbe.GetSelectedWeapon(probedWeapon, probedMuzzle, probedMagazine);
    PrintFormat("[BBB] ALIGNWEAPON selected=%1 magazine=%2 muzzle=%3", probedWeapon, probedMagazine, probedMuzzle);
   }
   foreach (BaseTarget contact : contacts) PrintFormat("[BBB] ALIGNCONTACT seen=%1 trace=%2 exposure=%3 position=%4", contact.GetTimeSinceSeen(), contact.GetTraceFraction(), contact.GetExposure(), contact.GetLastSeenPosition());
   PrintFormat("[BBB] ALIGNBASELINEPERF %1", BBB_Perf.Take());
   if (m_bCompletion)
   {
    // The 25 s off baseline can consume a belt before the measured turn.
    // Refill once here, never during the turn; production ammo veto stays active.
    BaseWeaponComponent fixtureGun = BBB_VehicleHelpers.TurretControllerOf(m_GunSeat).GetWeaponManager().GetCurrentWeapon();
    BaseMagazineComponent fixtureBelt;
    if (fixtureGun && fixtureGun.GetCurrentMuzzle()) fixtureBelt = fixtureGun.GetCurrentMuzzle().GetMagazine();
    if (fixtureBelt) fixtureBelt.SetAmmoCount(fixtureBelt.GetMaxAmmoCount());
    Check("completion-loaded-belt-fixture", fixtureBelt && fixtureBelt.GetAmmoCount() > 0 && fixtureBelt.GetAmmoCount() == fixtureBelt.GetMaxAmmoCount());
    if (!fixtureBelt) { FinishPosture(); return; }
   }
   BBB_DriverAlignment.s_bOn = true;
   state.m_fStableSince = GetGame().GetWorld().GetWorldTime();
   m_iPhase = 1;
  }
  if (m_iPhase == 1 && m_bCompletion)
  {
   if ((BBB_DriverAlignment.s_iStarted > m_iStartsBefore && !state.m_bActive) || elapsed >= 45000)
   {
    Check("completion-native-alignment-started", BBB_DriverAlignment.s_iStarted > m_iStartsBefore);
    Check("completion-reaches-native-firing-arc", m_fBestExcess <= 0.5);
    Check("completion-bounded-travel", state.m_fLastTravel <= BBB_DriverAlignment.MAX_TRAVEL_M + 0.1);
    Check("completion-bounded-duration", state.m_fLastRunMs <= BBB_DriverAlignment.MAX_RUN_MS + 100);
    AIControlComponent restoredControl = AIControlComponent.Cast(m_Driver.FindComponent(AIControlComponent));
    Check("completion-driver-ai-restored", restoredControl && restoredControl.IsAIActivated());
    PrintFormat("[BBB] ALIGNCOMPLETION firstExcess=%1 bestExcess=%2 runMs=%3 travel=%4", m_fFirstExcess, m_fBestExcess, state.m_fLastRunMs, state.m_fLastTravel);
    PrintFormat("[BBB] ALIGNEND reason=%1 speed=%2", state.m_sEndReason, m_Vehicle.GetPhysics().GetVelocity().Length());
    if (state.m_Target)
    {
     vector targetExcess = BBB_VehicleHelpers.TurretControllerOf(m_GunSeat).GetTurretComponent().GetAimingAngleExcess(state.m_Target.GetLastSeenPosition() + Vector(0, 1, 0));
     PrintFormat("[BBB] ALIGNFAILDETAIL alive=%1 disarmed=%2 seen=%3 trace=%4 exposure=%5 excess=%6", BBB.IsAlive(state.m_Target.GetTargetEntity()), state.m_Target.IsDisarmed(), state.m_Target.GetTimeSinceSeen(), state.m_Target.GetTraceFraction(), state.m_Target.GetExposure(), targetExcess);
    }
    FinishPosture();
   }
   return;
  }
  if (m_iPhase == 1 && ((state.m_bActive && vector.DistanceXZ(m_Original, m_Vehicle.GetOrigin()) > 0.06) || elapsed >= 45000))
  {
   m_fOnTravel = vector.DistanceXZ(m_Original, m_Vehicle.GetOrigin());
   Check("on-native-alignment-started", BBB_DriverAlignment.s_iStarted > m_iStartsBefore);
   Check("on-improves-native-turret-excess", m_fBestExcess + 0.1 < m_fFirstExcess);
   Check("bounded-adjustment", m_fOnTravel <= BBB_DriverAlignment.MAX_TRAVEL_M + 0.2);
   Check("move-arrives-during-active-alignment", state.m_bActive);
   m_Move = AIWaypoint.Cast(Spawn("{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et", m_Original + Vector(0, 0, 60), m_Nose));
   if (m_Move) m_Crew.AddWaypoint(m_Move);
   Check("move-order-veto", !BBB_DriverAlignment.OrdersAllow(m_Crew, m_Vehicle, m_Driver));
   BBB_DriverAlignment.Frame();
   Check("move-cancels-alignment", !state.m_bActive);
   AIControlComponent control = AIControlComponent.Cast(m_Driver.FindComponent(AIControlComponent));
   Check("driver-ai-restored", control && control.IsAIActivated());
   Check("move-waypoint-preserved", m_Crew.GetCurrentWaypoint() == m_Move);
   BBB_DriverAlignment.Stop();
   Check("alignment-lifecycle-cleared", BBB_DriverAlignment.s_States.IsEmpty());
   PrintFormat("[BBB] ALIGNRESULT offTravel=%1 onTravel=%2 firstExcess=%3 bestExcess=%4 eligibleAt=%5", m_fOffTravel, m_fOnTravel, m_fFirstExcess, m_fBestExcess, m_fEligibleAt);
   PrintFormat("[BBB] ALIGNACTUATION started=%1 lastRunMs=%2 lastTravel=%3 reason=%4", BBB_DriverAlignment.s_iStarted - m_iStartsBefore, state.m_fLastRunMs, state.m_fLastTravel, state.m_sEndReason);
   FinishPosture();
  }
 }

 protected void FinishPosture()
 {
  GetGame().GetCallqueue().Remove(Sample);
  PrintFormat("[BBB] POSTUREPERF %1", BBB_Perf.Take());
  BBB_CrewTurnout.s_bOn = false;
  BBB_CrewTurnout.Stop();
  BBB_DriverAlignment.s_bOn = false;
  BBB_DriverAlignment.Stop();
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers) observers.RemoveObserverSP(22119);
  if (m_Crew) SCR_EntityHelper.DeleteEntityAndChildren(m_Crew);
  if (m_Enemy) SCR_EntityHelper.DeleteEntityAndChildren(m_Enemy);
  if (m_Vehicle) SCR_EntityHelper.DeleteEntityAndChildren(m_Vehicle);
  if (m_Move) SCR_EntityHelper.DeleteEntityAndChildren(m_Move);
  string verdict = "FAIL";
  if (m_iFailures == 0 && m_iChecks >= 8) verdict = "PASS";
  string label = "driveralign";
  if (m_bTurnout) label = "crewturnout";
  PrintFormat("[ACR] VERDICT %1 test=%2 checks=%3 failed=%4", verdict, label, m_iChecks, m_iFailures);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_VehiclePostureTest.Start();
 }
}

// Isolate a genuinely idle driver: vanilla's idle-driver behavior normally
// performs its own combat repositioning, which is a separate movement system.
// Perception, gunner aiming and all production alignment guards stay native.
modded class SCR_AIIdleBehavior_Driver
{
 override float CustomEvaluate()
 {
  float priority = super.CustomEvaluate();
  if (m_Utility && BBB_VehiclePostureTest.HoldsDriver(m_Utility.m_OwnerEntity)) m_bUseCombatMove = false;
  return priority;
 }
}
