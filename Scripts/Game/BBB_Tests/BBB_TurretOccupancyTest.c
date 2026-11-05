// Opt-in native transition diagnosis. Fresh actors per trial; 100 ms samples,
// ten seconds after each replacement request. Real enemy magazine expenditure
// establishes fire coverage; a move waypoint alone does not establish motion.
class BBB_TurretOccupancyTest : BBB_AimTest
{
 static ref BBB_TurretOccupancyTest s_SeatTest;
 protected int m_Trial;
 protected bool m_FireOnly;
 protected bool m_IdleOnly;
 protected int m_Mode;
 protected bool m_Downed;
 protected bool m_Pinned;
 protected SCR_AIGroup m_Group;
 protected ref BBB_AimSide m_Enemy = new BBB_AimSide();
 protected IEntity m_Truck;
 protected AIWaypoint m_Move;
 protected BaseCompartmentSlot m_Seat;
 protected ref array<ChimeraCharacter> m_Men = {};
 protected ref array<vector> m_StartPositions = {};
 protected ChimeraCharacter m_Expected;
 protected float m_SetupAt;
 protected float m_RequestAt;
 protected vector m_TruckAt;
 protected bool m_Requested;
 protected bool m_Boarded;
 protected bool m_Departed;
 protected bool m_Different;
 protected float m_FirstBoardMs;
 protected float m_FirstAcceptedMs;
 protected float m_MaxTravel;
 protected float m_MaxSpeed;
 protected bool m_FootMoved;
 protected int m_ShotsBefore;
 protected int m_AcceptedBefore;
 protected int m_SetupFailures;
 protected int m_Empty;
 protected int m_Left;
 protected int m_Stayed;
 protected int m_FireCovered;
 protected int m_MoveCovered;
 protected int m_Observed;

 static void StartSeatDiagnostic()
 {
  if (s_SeatTest || !Replication.IsServer() || !System.IsCLIParam("bbbSeatDiag")) return;
  s_SeatTest = new BBB_TurretOccupancyTest();
  s_SeatTest.m_FireOnly = System.IsCLIParam("bbbSeatDiagFireOnly");
  s_SeatTest.m_IdleOnly = System.IsCLIParam("bbbSeatDiagIdleOnly");
  if (s_SeatTest.m_FireOnly) s_SeatTest.m_Trial = 2;
  GetGame().GetCallqueue().CallLater(s_SeatTest.BeginTrial, 12000);
 }

 protected string ModeName()
 {
  if (m_Mode == 1) return "move-order";
  if (m_Mode == 2) return "live-fire";
  return "idle";
 }

 protected void BeginTrial()
 {
  m_Mode = m_Trial % 3;
  m_Downed = (m_Trial / 3) % 2 == 1;
  m_Pinned = m_Trial >= 6;
  m_Boarded = false;
  m_Departed = false;
  m_Different = false;
  m_MaxTravel = 0;
  m_MaxSpeed = 0;
  m_FootMoved = false;
  m_FirstBoardMs = -1;
  m_FirstAcceptedMs = -1;
  vector position = BBB.Ground(Vector(2880, 0, 1580));
  m_Group = SCR_AIGroup.Cast(Spawn(TEAM_USSR, position, Vector(0, 0, 1)));
  m_Truck = Spawn(GUN_TRUCK, position + Vector(8, 0, 8), Vector(0, 0, 1));
  if (m_Mode == 2) m_Enemy.m_Group = SCR_AIGroup.Cast(Spawn(TEAM_US, position + Vector(1000, 0, 25), Vector(-1, 0, -0.3)));
  PrintFormat("[BBB] SEATDIAG begin trial=%1 mode=%2 downed=%3 pinned=%4 group=%5 truck=%6", m_Trial, ModeName(), m_Downed, m_Pinned, m_Group, m_Truck);
  GetGame().GetCallqueue().CallLater(PrepareTrial, 3000);
 }

 protected void PrepareTrial()
 {
  m_Men.Clear();
  m_StartPositions.Clear();
  array<AIAgent> agents = {};
  if (m_Group) m_Group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   ChimeraCharacter man = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!man) continue;
   m_Men.Insert(man);
   man.GetDamageManager().EnableDamageHandling(false);
   if (m_Pinned) PinDetail(agent);
  }
  if (m_Pinned) PinDetail(m_Group);
  if (m_Enemy.m_Group)
  {
   Hook(m_Enemy, false);
   foreach (IEntity enemy : m_Enemy.m_aMen)
   {
    ChimeraCharacter enemyMan = ChimeraCharacter.Cast(enemy);
    if (enemyMan) enemyMan.GetDamageManager().EnableDamageHandling(false);
   }
  }
  array<BaseCompartmentSlot> pilots = {};
  array<BaseCompartmentSlot> cargo = {};
  array<BaseCompartmentSlot> turrets = {};
  BBB_VehicleHelpers.GetVehicleCompartments(m_Truck, pilots, ECompartmentType.PILOT);
  BBB_VehicleHelpers.GetVehicleCompartments(m_Truck, cargo, ECompartmentType.CARGO);
  BBB_VehicleHelpers.GetVehicleCompartments(m_Truck, turrets, ECompartmentType.TURRET);
  if (!m_Group || !m_Truck || m_Men.Count() < 3 || pilots.IsEmpty() || cargo.Count() < 2 || turrets.IsEmpty())
  {
   m_SetupFailures++;
   PrintFormat("[BBB] SEATDIAG setup-failed trial=%1 reason=entities", m_Trial);
   EndTrial();
   return;
  }
  m_Seat = turrets[0];
  m_Group.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.HOLD_FIRE);
  m_Group.GetGroupUtilityComponent().EvaluateCombatMode();
  // Prevent the normal one-second scanner from racing this explicit request.
  BBB_VehicleHelpers.s_mGunnerReplaceNext.Set(m_Truck, GetGame().GetWorld().GetWorldTime() + 60000);
  bool pilotOK = m_Men[0].GetCompartmentAccessComponent().GetInVehicle(pilots[0].GetOwner(), pilots[0], true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false);
  BaseCompartmentSlot first = cargo[0];
  if (m_Downed) first = m_Seat;
  bool firstOK = m_Men[1].GetCompartmentAccessComponent().GetInVehicle(first.GetOwner(), first, true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false);
  bool secondOK = m_Men[2].GetCompartmentAccessComponent().GetInVehicle(cargo[1].GetOwner(), cargo[1], true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false);
  PrintFormat("[BBB] SEATDIAG initial trial=%1 pilotAccepted=%2 firstAccepted=%3 secondAccepted=%4", m_Trial, pilotOK, firstOK, secondOK);
  m_SetupAt = GetGame().GetWorld().GetWorldTime();
  GetGame().GetCallqueue().CallLater(AwaitSetup, 100);
 }

 protected bool SeatedInTruck(ChimeraCharacter man)
 {
  CompartmentAccessComponent access = man.GetCompartmentAccessComponent();
  return man.IsInVehicle() && access && access.GetCompartment() && access.GetCompartment().GetVehicle() == m_Truck && !access.IsGettingIn() && !access.IsGettingOut();
 }

 protected void AwaitSetup()
 {
  if (m_Enemy.m_Group) m_Enemy.SampleAmmo();
  float elapsed = GetGame().GetWorld().GetWorldTime() - m_SetupAt;
  bool seated = SeatedInTruck(m_Men[0]) && SeatedInTruck(m_Men[1]) && SeatedInTruck(m_Men[2]);
  if (!seated && elapsed < 20000)
  {
   GetGame().GetCallqueue().CallLater(AwaitSetup, 100);
   return;
  }
  PrintFormat("[BBB] SEATDIAG setup trial=%1 elapsedMs=%2 seated=%3 enemyShots=%4", m_Trial, elapsed, seated, m_Enemy.m_iShots);
  if (!seated)
  {
   m_SetupFailures++;
   TraceActors();
   EndTrial();
   return;
  }
  if (m_Downed)
  {
   m_Men[1].GetDamageManager().EnableDamageHandling(true);
   SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.GetDamageManager(m_Men[1]);
   if (dmg) dmg.Kill(Instigator.CreateInstigator(null));
  }
  GetGame().GetCallqueue().CallLater(RequestReplacement, 500);
 }

 protected void RequestReplacement()
 {
  m_Expected = m_Men[1];
  if (m_Downed) m_Expected = m_Men[2];
  m_TruckAt = m_Truck.GetOrigin();
  foreach (ChimeraCharacter man : m_Men) m_StartPositions.Insert(man.GetOrigin());
  if (m_Mode == 1)
  {
   m_Move = AIWaypoint.Cast(Spawn("{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et", m_TruckAt + Vector(80, 0, 20), Vector(1, 0, 0)));
   if (m_Move) m_Group.AddWaypoint(m_Move);
  }
  // Start real fire after seating is established. Otherwise danger can make
  // the crew abandon the initial setup before any replacement is requested.
  foreach (int enemyIndex, IEntity enemy : m_Enemy.m_aMen)
   if (enemy) enemy.SetOrigin(BBB.Ground(m_TruckAt + Vector(40 + enemyIndex * 2, 0, 20)));
  m_ShotsBefore = m_Enemy.m_iShots;
  m_AcceptedBefore = BBB_VehicleHelpers.s_iGunnerReplacements;
  m_RequestAt = GetGame().GetWorld().GetWorldTime();
  m_Requested = BBB_VehicleHelpers.TryReplaceGunner(m_Truck, m_Group);
  PrintFormat("[BBB] SEATDIAG request trial=%1 t=%2 selectedExpected=%3 requested=%4 waypoint=%5", m_Trial, m_RequestAt, m_Expected, m_Requested, m_Group.GetCurrentWaypoint());
  SampleTrial();
 }

 protected void TraceActors()
 {
  foreach (int index, ChimeraCharacter man : m_Men)
  {
   if (!man) continue;
   CompartmentAccessComponent access = man.GetCompartmentAccessComponent();
   AIControlComponent control = AIControlComponent.Cast(man.FindComponent(AIControlComponent));
   AIAgent agent = null;
   if (control) agent = control.GetAIAgent();
   SCR_AIUtilityComponent utility = null;
   int lod = -1;
   if (agent) { lod = agent.GetLOD(); utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent)); }
   string behavior = "none";
   if (utility && utility.GetCurrentBehavior()) behavior = utility.GetCurrentBehavior().Type().ToString();
   BaseCompartmentSlot current = null;
   if (access) current = access.GetCompartment();
   bool entering = false;
   bool exiting = false;
   if (access) { entering = access.IsGettingIn(); exiting = access.IsGettingOut(); }
   PrintFormat("[BBB] SEATDIAG actor trial=%1 t=%2 index=%3 entity=%4 able=%5 inVehicle=%6 entering=%7 exiting=%8", m_Trial, GetGame().GetWorld().GetWorldTime(), index, man, BBB_VehicleHelpers.IsAble(man), man.IsInVehicle(), entering, exiting);
   PrintFormat("[BBB] SEATDIAG state trial=%1 index=%2 lod=%3 behavior=%4 compartment=%5 explicitOrder=%6", m_Trial, index, lod, behavior, current, BBB_OrderIntent.Blocks(utility));
   if (current) PrintFormat("[BBB] SEATDIAG access trial=%1 index=%2 seat=%3 vehicle=%4 owner=%5", m_Trial, index, current.GetCompartmentName(), current.GetVehicle(), current.GetOwner());
   float suppression, shots, incoming, enemyValue;
   if (utility && utility.m_ThreatSystem) utility.m_ThreatSystem.GetThreatValues(suppression, shots, incoming, enemyValue);
   PrintFormat("[BBB] SEATDIAG threat trial=%1 index=%2 sup=%3 shots=%4 incoming=%5 enemy=%6", m_Trial, index, suppression, shots, incoming, enemyValue);
   if (index < m_StartPositions.Count() && !man.IsInVehicle() && BBB.IsAlive(man) && vector.DistanceXZ(man.GetOrigin(), m_StartPositions[index]) > 5) m_FootMoved = true;
  }
 }

 protected void SampleTrial()
 {
  float elapsed = GetGame().GetWorld().GetWorldTime() - m_RequestAt;
  if (m_Enemy.m_Group) m_Enemy.SampleAmmo();
  float speed = 0;
  if (m_Truck.GetPhysics()) speed = m_Truck.GetPhysics().GetVelocity().Length();
  m_MaxSpeed = Math.Max(m_MaxSpeed, speed);
  m_MaxTravel = Math.Max(m_MaxTravel, vector.DistanceXZ(m_TruckAt, m_Truck.GetOrigin()));
  array<BaseCompartmentSlot> fresh = {};
  BBB_VehicleHelpers.GetVehicleCompartments(m_Truck, fresh, ECompartmentType.TURRET);
  IEntity living = null;
  foreach (BaseCompartmentSlot slot : fresh)
  {
   PrintFormat("[BBB] SEATDIAG seat trial=%1 elapsedMs=%2 name=%3 occupant=%4 owner=%5 vehicle=%6", m_Trial, elapsed, slot.GetCompartmentName(), slot.GetOccupant(), slot.GetOwner(), slot.GetVehicle());
   if (BBB_VehicleHelpers.IsAble(slot.GetOccupant())) living = slot.GetOccupant();
  }
  PrintFormat("[BBB] SEATDIAG sample trial=%1 elapsedMs=%2 cachedOccupant=%3 speed=%4 travel=%5 waypoint=%6 nativeAccepted=%7 enemyShots=%8", m_Trial, elapsed, m_Seat.GetOccupant(), speed, m_MaxTravel, m_Group.GetCurrentWaypoint(), BBB_VehicleHelpers.s_iGunnerReplacements - m_AcceptedBefore, m_Enemy.m_iShots - m_ShotsBefore);
  if (m_FirstAcceptedMs < 0 && BBB_VehicleHelpers.s_iGunnerReplacements > m_AcceptedBefore) m_FirstAcceptedMs = elapsed;
  if (living)
  {
   if (!m_Boarded) m_FirstBoardMs = elapsed;
   m_Boarded = true;
   if (living != m_Expected) m_Different = true;
  }
  else if (m_Boarded) m_Departed = true;
  TraceActors();
  float observationEnd = 10000;
  // In the idle confirmation run, a retry accepted near t=9 s needs its own
  // ten-second observation window. Never call that a ten-second native failure
  // after watching it for only the final second of the original request window.
  if (m_IdleOnly && m_FirstAcceptedMs >= 0) observationEnd = Math.Max(observationEnd, m_FirstAcceptedMs + 10000);
  if (elapsed < observationEnd && elapsed < 30000)
  {
   GetGame().GetCallqueue().CallLater(SampleTrial, 100);
   return;
  }
  m_Observed++;
  if (!m_Boarded) m_Empty++;
  else if (m_Departed) m_Left++;
  else m_Stayed++;
  int fireShots = m_Enemy.m_iShots - m_ShotsBefore;
  if (m_Mode == 2 && fireShots > 0) m_FireCovered++;
  if (m_Mode == 1 && (m_MaxTravel > 3 || m_FootMoved)) m_MoveCovered++;
  PrintFormat("[BBB] SEATDIAG outcome trial=%1 requested=%2 boarded=%3 departed=%4 different=%5 firstMs=%6 acceptedCount=%7", m_Trial, m_Requested, m_Boarded, m_Departed, m_Different, m_FirstBoardMs, BBB_VehicleHelpers.s_iGunnerReplacements - m_AcceptedBefore);
  PrintFormat("[BBB] SEATDIAG window trial=%1 elapsedMs=%2 firstNativeAcceptedMs=%3", m_Trial, elapsed, m_FirstAcceptedMs);
  PrintFormat("[BBB] SEATDIAG context trial=%1 mode=%2 downed=%3 pinned=%4 fireShots=%5 vehicleTravel=%6 maxSpeed=%7 footMoved=%8", m_Trial, ModeName(), m_Downed, m_Pinned, fireShots, m_MaxTravel, m_MaxSpeed, m_FootMoved);
  EndTrial();
 }

 protected void EndTrial()
 {
  if (m_Truck) BBB_VehicleHelpers.s_mGunnerReplaceNext.Remove(m_Truck);
  foreach (ChimeraCharacter man : m_Men) if (man) SCR_EntityHelper.DeleteEntityAndChildren(man);
  foreach (IEntity enemy : m_Enemy.m_aMen) if (enemy) SCR_EntityHelper.DeleteEntityAndChildren(enemy);
  if (m_Group) SCR_EntityHelper.DeleteEntityAndChildren(m_Group);
  if (m_Enemy.m_Group) SCR_EntityHelper.DeleteEntityAndChildren(m_Enemy.m_Group);
  if (m_Move) SCR_EntityHelper.DeleteEntityAndChildren(m_Move);
  if (m_Truck) SCR_EntityHelper.DeleteEntityAndChildren(m_Truck);
  m_Men.Clear();
  m_Enemy = new BBB_AimSide();
  if (m_FireOnly || m_IdleOnly) m_Trial += 3;
  else m_Trial++;
  if (m_Trial < 12) { GetGame().GetCallqueue().CallLater(BeginTrial, 500); return; }
  PrintFormat("[BBB] SEATDIAG summary observed=%1 setupFailures=%2 neverOccupied=%3 boardedThenLeft=%4 stayed=%5 fireCovered=%6 moveCovered=%7", m_Observed, m_SetupFailures, m_Empty, m_Left, m_Stayed, m_FireCovered, m_MoveCovered);
  // The run's conditions, each counted: every trial observed, no setup failure, and (full and fire
  // runs) enemy fire covered every fire trial, (full run) motion covered every move trial.
  int trials = 12;
  if (m_FireOnly || m_IdleOnly) trials = 4;
  int expected = 2;
  int passed;
  if (m_Observed == trials) passed++;
  if (m_SetupFailures == 0) passed++;
  if (!m_IdleOnly)
  {
   expected++;
   if (m_FireCovered == 4) passed++;
  }
  if (!m_FireOnly && !m_IdleOnly)
  {
   expected++;
   if (m_MoveCovered == 4) passed++;
  }
  string verdict = "FAIL";
  if (passed == expected) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbseatdiag coverageOnly=1 passed=%2 failed=%3 expected=%4", verdict, passed, expected - passed, expected);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_TurretOccupancyTest.StartSeatDiagnostic();
 }
}

// Native group events catch enter/leave transitions shorter than a 100 ms sample.
modded class SCR_AIGroupUtilityComponent
{
 override protected void OnAgentCompartmentEntered(AIAgent agent, IEntity targetEntity, BaseCompartmentManagerComponent manager, int mgrID, int slotID, bool move)
 {
  if (agent && System.IsCLIParam("bbbTurretTrace")) PrintFormat("[BBB] SEATEVENT t=%1 event=entered actor=%2 target=%3 manager=%4 slot=%5 move=%6", GetGame().GetWorld().GetWorldTime(), agent.GetControlledEntity(), targetEntity, mgrID, slotID, move);
  super.OnAgentCompartmentEntered(agent, targetEntity, manager, mgrID, slotID, move);
 }
 override protected void OnAgentCompartmentLeft(AIAgent agent, IEntity targetEntity, BaseCompartmentManagerComponent manager, int mgrID, int slotID, bool move)
 {
  if (agent && System.IsCLIParam("bbbTurretTrace")) PrintFormat("[BBB] SEATEVENT t=%1 event=left actor=%2 target=%3 manager=%4 slot=%5 move=%6", GetGame().GetWorld().GetWorldTime(), agent.GetControlledEntity(), targetEntity, mgrID, slotID, move);
  super.OnAgentCompartmentLeft(agent, targetEntity, manager, mgrID, slotID, move);
 }
}
