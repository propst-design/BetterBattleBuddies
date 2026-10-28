// Native outcome fixture. Setup may place soldiers/seat a driver; production never
// teleports. No PASS for missing fixtures, dispatch counters or skipped outcomes.
class BBB_ReactionHooks : BBB_Hooks
{
 ref array<SCR_AIGroup> m_Groups = {};
 bool m_Veto;
 override void GetGroups(notnull array<SCR_AIGroup> groups)
 {
  groups.Clear();
  foreach (SCR_AIGroup group : m_Groups) if (group) groups.Insert(group);
 }
 override bool AllowArmorReaction(SCR_AIGroup group) { return !m_Veto; }
 override bool AllowBuildings(SCR_AIGroup group) { return !m_Veto; }
}

class BBB_ReactionTest : BBB_AimTest
{
 static ref BBB_ReactionTest s_Reaction;
 ref BBB_ReactionHooks m_Hooks;
 ref BBB_Hooks m_SavedHooks;
 SCR_AIGroup m_Crew;
 SCR_AIGroup m_Enemy;
 SCR_AIGroup m_Infantry;
 IEntity m_Truck;
 ChimeraCharacter m_Driver;
 SCR_AIUtilityComponent m_DriverUtility;
 SCR_AIUtilityComponent m_InfantryUtility;
 AIControlComponent m_DriverControl;
 AIWaypoint m_Order;
 int m_Checks;
 int m_Failures;
 int m_Phase;
 float m_At;
 vector m_TruckStart;
 vector m_InfantryStart;
 vector m_TargetCover;
 IEntity m_TargetBuilding;
 ref array<IEntity> m_Buildings = {};
 bool m_NativeContact;
 bool m_NativeEntry;
 bool m_NativeHold;
 int m_MissilesBefore;
 BaseWeaponComponent m_ATWeapon;
 IEntity m_TestProjectile;
 vector m_ProjectileStart;
 int m_ReactionsBefore;
 ref array<vector> m_Sites = {};
 ref map<IEntity, vector> m_InfantryOrigins = new map<IEntity, vector>();
 int m_Site;

 static void Start()
 {
  if (!Replication.IsServer() || !System.IsCLIParam("bbbReactionTest")) return;
  s_Reaction = new BBB_ReactionTest();
  GetGame().GetCallqueue().CallLater(s_Reaction.Setup, 15000);
 }

 void Assert(string label, bool passed, string detail = "")
 {
  m_Checks++;
  if (!passed) m_Failures++;
  PrintFormat("[BBB] REACTION CHECK %1 ok=%2 %3", label, passed, detail);
 }

 bool CollectBuilding(IEntity entity)
 {
  if (BaseBuilding.Cast(entity)) m_Buildings.Insert(entity);
  return true;
 }

 bool IgnoreFixtureBodies(notnull IEntity entity)
 {
  if (ChimeraCharacter.Cast(entity)) return false;
  return entity != m_Truck && entity.GetRootParent() != m_Truck;
 }

 bool ClearLane(vector from, vector to)
 {
  vector eye = BBB.Ground(from) + Vector(0, 1.2, 0);
  vector aim = BBB.Ground(to) + Vector(0, 1.2, 0);
  for (int i = 1; i < 40; i++)
  {
   vector sample = eye + (aim - eye) * (i / 40.0);
   if (GetGame().GetWorld().GetSurfaceY(sample[0], sample[2]) > sample[1] - 0.1) return false;
  }
  TraceParam trace = new TraceParam();
  trace.Start = eye;
  trace.End = aim;
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  return GetGame().GetWorld().TraceMove(trace, IgnoreFixtureBodies) >= 0.99;
 }

 void Setup()
 {
  Assert("default-on", BBB_ArmorReaction.s_bOn && BBB_Buildings.s_bOn);
  Assert("segment-near", BBB_ArmorReaction.SegmentDistance("0 1 0", "-20 1 0", "20 1 0") < 0.01);
  Assert("segment-behind", BBB_ArmorReaction.SegmentDistance("-20 1 0", "0 1 0", "20 1 0") > 19);
  Assert("segment-zero", Math.AbsFloat(BBB_ArmorReaction.SegmentDistance("0 0 5", "0 0 0", "0 0 0") - 5) < 0.01);
  Assert("line-lateral", Math.AbsFloat(BBB_ArmorReaction.LineDistance("4 0 0", "0 0 0", "0 0 1") - 4) < 0.01);
  m_SavedHooks = BBB.s_Hooks;
  m_Hooks = new BBB_ReactionHooks();
  BBB.s_Hooks = m_Hooks;
  BBB_Morale.s_bOn = false;
  BBB_StaticWeapons.s_bOn = false;
  BBB_Grenades.s_bOn = false;
  BBB_FireManeuver.s_bOn = false;
  BBB_ArmorReaction.s_bOn = true;
  BBB_Buildings.s_bOn = true;
  vector point = BBB.Ground("3050 0 1700");
  NavmeshWorldComponent nav = Navmesh();
  if (nav)
  {
   for (int x = -5; x <= 5; x++)
    for (int z = -5; z <= 5; z++) nav.LoadTileIn(point + Vector(x * 60, 0, z * 60));
  }
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers) observers.InsertObserverSP(22135, point[0], point[2], null);
  m_Crew = SCR_AIGroup.Cast(Spawn(TEAM_US, point + Vector(15, 0, 0), "0 0 1"));
  m_Infantry = SCR_AIGroup.Cast(Spawn(TEAM_US, BBB.Ground("3024 0 1700.6"), "0 0 1"));
  m_Enemy = SCR_AIGroup.Cast(Spawn(TEAM_USSR_AT, point + Vector(0, 0, 80), "0 0 -1"));
  m_Truck = Spawn(GUN_TRUCK, point + Vector(25, 0, 0), "0 0 1");
  Assert("fixtures", m_Crew && m_Infantry && m_Enemy && m_Truck);
  if (!m_Crew || !m_Infantry || !m_Enemy || !m_Truck) { FinishReaction(); return; }
  m_Hooks.m_Groups.Insert(m_Crew);
  m_Hooks.m_Groups.Insert(m_Infantry);
  GetGame().GetCallqueue().CallLater(Prepare, 5000);
 }

 void PrepareGroup(SCR_AIGroup group)
 {
  PinDetail(group);
  group.ActivateAI();
  group.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.HOLD_FIRE);
  group.GetGroupUtilityComponent().EvaluateCombatMode();
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   PinDetail(agent);
   agent.ActivateAI();
   ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (body)
   {
    body.GetDamageManager().EnableDamageHandling(false);
    AIControlComponent control = AIControlComponent.Cast(body.FindComponent(AIControlComponent));
    if (control) control.ActivateAI();
   }
  }
 }

 void Prepare()
 {
  PrepareGroup(m_Crew); PrepareGroup(m_Enemy); PrepareGroup(m_Infantry);
  array<AIAgent> agents = {};
  m_Crew.GetAgents(agents);
  m_Driver = ChimeraCharacter.Cast(agents[0].GetControlledEntity());
  m_DriverUtility = BBB_StaticWeapons.UtilityOf(m_Driver);
  m_DriverControl = AIControlComponent.Cast(m_Driver.FindComponent(AIControlComponent));
  array<BaseCompartmentSlot> slots = {};
  BBB_VehicleHelpers.GetVehicleCompartments(m_Truck, slots, ECompartmentType.PILOT);
  Assert("driver-seat", !slots.IsEmpty() && m_Driver && m_DriverUtility && m_DriverControl);
  if (slots.IsEmpty() || !m_DriverUtility) { FinishReaction(); return; }
  m_Driver.GetCompartmentAccessComponent().GetInVehicle(m_Truck, slots[0], true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false);
  m_DriverControl.DeactivateAI();
  agents.Clear();
  m_Infantry.GetAgents(agents);
  m_InfantryUtility = SCR_AIUtilityComponent.Cast(agents[0].FindComponent(SCR_AIUtilityComponent));
  Assert("infantry-fixture-ownership", BBB_Buildings.GroupOf(m_InfantryUtility) == m_Infantry && !BBB_Transport.VehicleOf(m_InfantryUtility.m_OwnerEntity));
  // Choose a native indoor cover fixture near an existing Arland building.
  GetGame().GetWorld().QueryEntitiesBySphere(BBB.Ground("3050 0 1700"), 220, CollectBuilding);
  PrintFormat("[BBB] REACTION buildings=%1", m_Buildings.Count());
  GetGame().GetCallqueue().CallLater(ChooseBuilding, 5000);
 }

 void ChooseBuilding()
 {
  BBB_Buildings.Stop();
  AIPathfindingComponent path = AIPathfindingComponent.Cast(m_InfantryUtility.m_OwnerEntity.FindComponent(AIPathfindingComponent));
  vector knownSite;
  knownSite = BBB.Ground("3024 0 1700.6");
  vector onMesh;
  if (path.GetClosestPositionOnNavmesh(knownSite, "2 2 2", onMesh)) knownSite = onMesh;
  m_Sites.Insert(knownSite);
  PrintFormat("[BBB] REACTION exterior fixture sites=%1", m_Sites.Count());
  TrySite();
 }

 void TrySite()
 {
  if (m_Site >= m_Sites.Count())
  {
   Assert("native-indoor-cover-fixture", false, string.Format("queries=%1 sites=%2", BBB_Buildings.s_iQueries, m_Site));
   ArmorChecks();
   return;
  }
  BBB_Buildings.Stop();
  m_InfantryUtility.m_OwnerEntity.SetOrigin(m_Sites[m_Site]);
  m_InfantryStart = m_Sites[m_Site];
  m_Site++;
  // Allow native movement/pathfinding to observe the fixture placement before
  // asking it to score routes from that position.
  GetGame().GetCallqueue().CallLater(ProbeSite, 1500);
 }

 void ProbeSite()
 {
  SCR_AICoverLock cover;
  bool found = BBB_Buildings.FindIndoorCover(m_InfantryUtility, m_InfantryStart + Vector(0, 0, 40), m_TargetCover, m_TargetBuilding, cover);
  if (!found && m_Site < 5) { m_Sites.Insert(m_InfantryStart); TrySite(); return; }
  if (!found) { TrySite(); return; }
  cover.Release();
  Assert("native-indoor-cover-fixture", found, string.Format("start=%1 cover=%2 building=%3", m_InfantryStart, m_TargetCover, m_TargetBuilding));
  array<AIAgent> enemies = {};
  m_Enemy.GetAgents(enemies);
  vector enemyPos = m_InfantryStart + Vector(40, 0, 30);
  array<vector> sightDirs = {"1 0 0", "-1 0 0", "0 0 1", "0 0 -1", "0.707 0 0.707", "-0.707 0 0.707"};
  foreach (vector sightDir : sightDirs)
  {
   if (ClearLane(m_InfantryStart, m_InfantryStart + sightDir * 50)) { enemyPos = m_InfantryStart + sightDir * 50; break; }
  }
  foreach (AIAgent enemy : enemies)
  {
   ChimeraCharacter body = ChimeraCharacter.Cast(enemy.GetControlledEntity());
   body.SetOrigin(BBB.Ground(enemyPos));
   body.GetCharacterController().OverrideMaxSpeed(0);
   enemyPos += Vector(1, 0, 0);
  }
  array<AIAgent> friends = {};
  m_Infantry.GetAgents(friends);
  foreach (AIAgent friend : friends)
   if (friend != m_InfantryUtility.GetAIAgent()) friend.GetControlledEntity().SetOrigin(m_InfantryStart + Vector(20, 0, -10));
  foreach (AIAgent friendAtStart : friends)
   m_InfantryOrigins.Set(friendAtStart.GetControlledEntity(), friendAtStart.GetControlledEntity().GetOrigin());
  vector face = enemyPos - m_InfantryStart;
  m_InfantryUtility.m_OwnerEntity.SetAngles(face.VectorToAngles());
  m_Hooks.m_Veto = true;
  Assert("building-host-veto", !BBB_Buildings.Begin(m_InfantryUtility, enemyPos));
  m_Hooks.m_Veto = false;
  Assert("building-null-safe", !BBB_Buildings.Begin(null, enemyPos));
  Assert("no-roof-outdoors", !BBB_Buildings.RoofAt(m_InfantryStart));
  BuildingFixtureReady();
 }

 // Derived acceptance fixtures may install an order after native setup completes.
 void BuildingFixtureReady()
 {
  m_At = Now();
  GetGame().GetCallqueue().CallLater(ObserveBuilding, 1000, true);
 }

 void ObserveBuilding()
 {
  array<AIAgent> infantry = {};
  m_Infantry.GetAgents(infantry);
  foreach (AIAgent member : infantry)
  {
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(member.FindComponent(SCR_AIUtilityComponent));
   vector known;
   if (BBB_Buildings.RecentContact(utility, known)) m_NativeContact = true;
   IEntity body = member.GetControlledEntity();
   if (body && m_InfantryOrigins.Contains(body) && vector.Distance(m_InfantryOrigins.Get(body), body.GetOrigin()) > 2 && BBB_Buildings.RoofAt(body.GetOrigin())) m_NativeEntry = true;
  }
  foreach (BBB_BuildingJob job : BBB_Buildings.s_Jobs)
  {
   if (job.m_Group == m_Infantry && job.m_Arrived > 0 && GetGame().GetWorld().GetWorldTime() - job.m_Arrived > 4000 && BBB_Buildings.RoofAt(job.m_Utility.m_OwnerEntity.GetOrigin()) == job.m_Building) m_NativeHold = true;
  }
  if (Now() - m_At < 45 && (!m_NativeEntry || BBB_Buildings.s_iArrived == 0 || !m_NativeHold)) return;
  GetGame().GetCallqueue().Remove(ObserveBuilding);
  Assert("native-contact", m_NativeContact);
  Assert("native-entered-building", m_NativeEntry, string.Format("start=%1 end=%2", m_InfantryStart, m_InfantryUtility.m_OwnerEntity.GetOrigin()));
  Assert("observed-cover-arrival", BBB_Buildings.s_iArrived > 0, string.Format("jobs=%1 arrivals=%2 failed=%3", BBB_Buildings.s_iStarted, BBB_Buildings.s_iArrived, BBB_Buildings.s_iFailed));
  Assert("native-held-indoor-cover", m_NativeHold);
  m_Hooks.m_Veto = true;
  BBB_Buildings.Tick();
  Assert("building-host-cancels", BBB_Buildings.s_Jobs.IsEmpty());
  m_Hooks.m_Veto = false;
  ArmorChecks();
 }

 void ArmorChecks()
 {
  BBB_Buildings.s_bOn = false;
  BBB_Buildings.Stop();
  BBB_ArmorReaction.Stop();
  CarControllerComponent car = CarControllerComponent.Cast(m_Truck.FindComponent(CarControllerComponent));
  m_Truck.SetOrigin(BBB.Ground("3075 0 1700"));
  m_Truck.SetAngles("0 0 0");
  m_Truck.GetPhysics().SetVelocity(vector.Zero);
  car.ForceStartEngine();
  GetGame().GetCallqueue().CallLater(StartReverse, 2000);
 }

 void StartReverse()
 {
  m_DriverControl.ActivateAI();
  Assert("driver-seated", BBB_ArmorReaction.Driver(m_Truck) == m_Driver);
  Assert("driver-initially-active", m_DriverControl.IsAIActivated());
  vector line = m_Truck.GetOrigin() + Vector(0, 1, 5);
  IEntity enemy = m_Enemy.GetLeaderEntity();
  BBB_ArmorReaction.s_bOn = false;
  Assert("armor-disabled-baseline", !BBB_ArmorReaction.React(m_Truck, enemy, line, "0 0 -1") && BBB_ArmorReaction.s_Escapes.IsEmpty());
  BBB_ArmorReaction.s_bOn = true;
  m_Hooks.m_Veto = true;
  Assert("armor-host-veto", !BBB_ArmorReaction.React(m_Truck, enemy, line, "0 0 -1"));
  m_Hooks.m_Veto = false;
  m_Order = AIWaypoint.Cast(Spawn("{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et", m_Truck.GetOrigin() + Vector(0, 0, 25), "0 0 1"));
  SCR_AIWaypoint.Cast(m_Order).SetPriorityLevel(SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
  m_Crew.AddWaypoint(m_Order);
  Assert("armor-move-order-veto", !BBB_ArmorReaction.React(m_Truck, enemy, line, "0 0 -1"));
  m_Crew.RemoveWaypoint(m_Order);
  m_TruckStart = m_Truck.GetOrigin();
  bool reacted = BBB_ArmorReaction.React(m_Truck, enemy, line, "0 0 -1");
  Assert("armor-reverse-started", reacted && BBB_ArmorReaction.s_Escapes.Count() == 1 && !m_DriverControl.IsAIActivated(), string.Format("clearance=%1", BBB_ArmorReaction.Clearance(m_Truck)));
  Assert("armor-cooldown", !BBB_ArmorReaction.React(m_Truck, enemy, line, "0 0 -1"));
  m_At = Now();
  GetGame().GetCallqueue().CallLater(ObserveReverse, 1000, true);
  GetGame().GetCallqueue().CallLater(TraceReverse, 250, true);
 }

 void TraceReverse()
 {
  if (!m_Truck || Now() - m_At > 3.5) { GetGame().GetCallqueue().Remove(TraceReverse); return; }
  CarControllerComponent c = CarControllerComponent.Cast(m_Truck.FindComponent(CarControllerComponent));
  vector m[4]; m_Truck.GetWorldTransform(m);
  PrintFormat("[BBB] REACTION trace t=%1 fwd=%2 gear=%3 thr=%4 clutch=%5 brake=%6 rpm=%7 jobs=%8 ai=%9", Now() - m_At, vector.Dot(m_Truck.GetPhysics().GetVelocity(), m[2]), c.GetSimulation().GetGear(), c.GetSimulation().GetThrottle(), c.GetSimulation().GetClutch(), c.GetSimulation().GetBrake(), c.GetSimulation().EngineGetRPM(), BBB_ArmorReaction.s_Escapes.Count(), m_DriverControl.IsAIActivated());
 }

 void ObserveReverse()
 {
  if (Now() - m_At < 3) return;
  GetGame().GetCallqueue().Remove(ObserveReverse);
  vector moved = m_Truck.GetOrigin() - m_TruckStart;
  CarControllerComponent motionCar = CarControllerComponent.Cast(m_Truck.FindComponent(CarControllerComponent));
  PrintFormat("[BBB] REACTION motion jobs=%1 ai=%2 gear=%3 throttle=%4 brake=%5 awake=%6 frames=%7 engine=%8 rpm=%9", BBB_ArmorReaction.s_Escapes.Count(), m_DriverControl.IsAIActivated(), motionCar.GetSimulation().GetGear(), motionCar.GetSimulation().GetThrottle(), motionCar.GetSimulation().GetBrake(), m_Truck.GetPhysics().IsActive(), BBB.s_iFrames, motionCar.IsEngineOn(), motionCar.GetSimulation().EngineGetRPM());
  PrintFormat("[BBB] REACTION motion2 hb=%1 hbsim=%2 persistent=%3 clutch=%4 simEngine=%5 speed=%6", motionCar.GetHandBrake(), motionCar.GetSimulation().IsHandbrakeOn(), motionCar.GetPersistentHandBrake(), motionCar.GetSimulation().GetClutch(), motionCar.GetSimulation().EngineIsOn(), m_Truck.GetPhysics().GetVelocity().Length());
  Assert("observed-reverse-motion", BBB_ArmorReaction.s_Escapes.Count() == 1 && moved[2] < -0.4, string.Format("delta=%1", moved));
  m_Crew.AddWaypoint(m_Order);
  GetGame().GetCallqueue().CallLater(AfterOrder, 300);
 }

 void AfterOrder()
 {
  Assert("order-cancels-reverse", BBB_ArmorReaction.s_Escapes.IsEmpty());
  Assert("restores-owned-driver-ai", m_DriverControl.IsAIActivated());
  Assert("preserves-order", m_Crew.GetCurrentWaypoint() == m_Order);
  BBB_ArmorReaction.Stop();
  m_Crew.RemoveWaypoint(m_Order);
  m_DriverControl.DeactivateAI();
  BBB_ArmorReaction.React(m_Truck, m_Enemy.GetLeaderEntity(), m_Truck.GetOrigin(), "0 0 -1");
  BBB_ArmorReaction.Stop();
  Assert("cleanup-keeps-unowned-ai-off", !m_DriverControl.IsAIActivated());
  m_DriverControl.ActivateAI();
  BeginNativeAT();
 }

 void BeginNativeAT()
 {
  // Controlled projectile/event fixture. This verifies the callback and segment
  // response path, independently of native firing animation and shot initialization.
  BBB_ArmorReaction.Stop();
  m_Hooks.m_Veto = false;
  m_DriverControl.ActivateAI();
  array<ref AIActionBase> orders = {};
  m_DriverUtility.FindActionsOfInheritedType(SCR_AIBehaviorBase, orders);
  foreach (AIActionBase order : orders)
   if (order.EvaluatePriorityLevel() >= SCR_AIActionBase.PRIORITY_LEVEL_PLAYER) order.Fail();
  BBB_ArmorReaction.Tick();
  Assert("flyby-fixture-eligible", BBB_ArmorReaction.Allowed(m_Truck, m_Crew, m_DriverUtility));
  array<AIAgent> enemies = {};
  m_Enemy.GetAgents(enemies);
  foreach (AIAgent enemy : enemies)
  {
   SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(enemy.GetControlledEntity().FindComponent(SCR_AICombatComponent));
   if (combat && combat.FindWeaponOfType(EWeaponType.WT_ROCKETLAUNCHER)) m_ATWeapon = combat.FindWeaponOfType(EWeaponType.WT_ROCKETLAUNCHER);
  }
  Assert("native-at-launcher-present", m_ATWeapon && m_ATWeapon.GetCurrentMagazine());
  if (!m_ATWeapon || !m_ATWeapon.GetCurrentMagazine()) { FinishReaction(); return; }
  ResourceName ammunition = m_ATWeapon.GetCurrentMagazine().GetOwner().GetPrefabData().GetPrefabName();
  vector direction = m_Truck.GetTransformAxis(2) * -1;
  m_ProjectileStart = m_Truck.GetOrigin() + m_Truck.GetTransformAxis(2) * 20 + m_Truck.GetTransformAxis(0) * 4 + Vector(0, 1.5, 0);
  m_TestProjectile = Spawn(ammunition, m_ProjectileStart, direction);
  Assert("native-rocket-spawn", m_TestProjectile != null, ammunition);
  if (!m_TestProjectile) { FinishReaction(); return; }
  m_TestProjectile.SetOrigin(m_ProjectileStart);
  ProjectileMoveComponent movement = ProjectileMoveComponent.Cast(m_TestProjectile.FindComponent(ProjectileMoveComponent));
  Assert("native-rocket-movement-component", movement != null);
  if (!movement) { FinishReaction(); return; }
  m_MissilesBefore = BBB_ArmorReaction.s_iMissiles;
  m_ReactionsBefore = BBB_ArmorReaction.s_iReactions;
  array<GenericComponent> effects = {};
  m_ATWeapon.GetCurrentMuzzle().FindComponents(SCR_MuzzleEffectComponent, effects);
  Assert("native-launcher-event-component", !effects.IsEmpty());
  foreach (GenericComponent effect : effects)
  {
   SCR_MuzzleEffectComponent callback = SCR_MuzzleEffectComponent.Cast(effect);
   callback.OnFired(m_ATWeapon.GetOwner(), m_ATWeapon.GetCurrentMuzzle(), m_TestProjectile);
   callback.OnFired(m_ATWeapon.GetOwner(), m_ATWeapon.GetCurrentMuzzle(), m_TestProjectile);
  }
  Assert("rocket-callback-deduplicated", BBB_ArmorReaction.s_iMissiles == m_MissilesBefore + 1);
  // Sample an observed segment across a near-miss corridor. No low-level Launch
  // call: that API requires engine-owned shot initialization unavailable here.
  m_TestProjectile.SetOrigin(m_ProjectileStart + direction * 40);
  BBB_ArmorReaction.Frame();
  GetGame().GetCallqueue().CallLater(ObserveNativeAT, 1000);
 }

 void ObserveNativeAT()
 {
  Assert("controlled-rocket-segment-response", BBB_ArmorReaction.s_iReactions > m_ReactionsBefore,
   string.Format("before=%1 after=%2", m_ReactionsBefore, BBB_ArmorReaction.s_iReactions));
  FinishReaction();
 }

 void FinishReaction()
 {
  GetGame().GetCallqueue().Remove(ObserveBuilding);
  GetGame().GetCallqueue().Remove(ObserveReverse);
  GetGame().GetCallqueue().Remove(ObserveNativeAT);
  BBB_Buildings.Stop(); BBB_ArmorReaction.Stop();
  BBB.s_Hooks = m_SavedHooks;
  string verdict = "PASS";
  if (m_Failures > 0) verdict = "FAIL";
  PrintFormat("[ACR] VERDICT %1 test=bbb-reactions checks=%2 failed=%3", verdict, m_Checks, m_Failures);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_ReactionTest.Start();
 }
}
