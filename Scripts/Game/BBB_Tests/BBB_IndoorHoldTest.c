class BBB_IndoorHoldHooks : BBB_Hooks
{
 ref array<SCR_AIGroup> m_Groups = {};
 override void GetGroups(notnull array<SCR_AIGroup> groups)
 {
  groups.Clear();
  foreach (SCR_AIGroup group : m_Groups) if (group) groups.Insert(group);
 }
}

// Indoor hold A/B (-bbbIndoorHoldTest). A US fire team is placed inside a real Arland
// building beside a window that sees a USSR team standing 45 m outside (both fire at
// will, damage off, enemy held in place). Phase OFF runs vanilla combat movement with the
// hold disabled, phase ON the same placement with it enabled. Measured: how many of the
// team physically leave the building (no roof of that building over them for 3 s) and
// rounds they fire. Setup places soldiers; production code never teleports.
class BBB_IndoorHoldTest : BBB_AimTest
{
 static ref BBB_IndoorHoldTest s_Hold;
 static const float PHASE_S = 40;
 static const float LANE_M = 45;
 ref BBB_IndoorHoldHooks m_Hooks;
 ref BBB_Hooks m_SavedHooks;
 SCR_AIGroup m_Team;
 SCR_AIGroup m_Enemy;
 IEntity m_Building;
 vector m_Window;
 vector m_LaneEnd;
 ref array<vector> m_Inside = {};
 ref array<vector> m_Places = {};
 ref array<IEntity> m_Candidates = {};
 ref map<IEntity, float> m_Outside = new map<IEntity, float>();
 ref map<IEntity, bool> m_Left = new map<IEntity, bool>();
 ref map<IEntity, int> m_Ammo = new map<IEntity, int>();
 int m_Fired;
 int m_Samples;
 int m_ProneIndoors;
 int m_IndoorSamples;
 bool m_bOnPhase;
 float m_At;
 int m_Checks;
 int m_Failures;
 int m_OffLeft = -1;
 int m_OffFired;
 int m_OffProne;
 int m_OffIndoor;

 static void Start()
 {
  if (!Replication.IsServer() || !System.IsCLIParam("bbbIndoorHoldTest")) return;
  s_Hold = new BBB_IndoorHoldTest();
  GetGame().GetCallqueue().CallLater(s_Hold.Setup, 15000);
 }

 void Assert(string label, bool passed, string detail = "")
 {
  m_Checks++;
  if (!passed) m_Failures++;
  PrintFormat("[BBB] INDOORHOLD CHECK %1 ok=%2 %3", label, passed, detail);
 }

 bool Collect(IEntity entity)
 {
  if (BaseBuilding.Cast(entity)) m_Candidates.Insert(entity);
  return true;
 }

 bool IgnoreBodies(notnull IEntity entity)
 {
  return !ChimeraCharacter.Cast(entity);
 }

 bool Lane(vector from, vector to)
 {
  for (int i = 1; i < 30; i++)
  {
   vector sample = from + (to - from) * (i / 30.0);
   if (GetGame().GetWorld().GetSurfaceY(sample[0], sample[2]) > sample[1] - 0.2) return false;
  }
  return BBB_Buildings.ViewClear(from, to);
 }


 void Setup()
 {
  Assert("default-on", BBB_Buildings.s_bHold);
  m_SavedHooks = BBB.s_Hooks;
  m_Hooks = new BBB_IndoorHoldHooks();
  BBB.s_Hooks = m_Hooks;
  // Isolate vanilla combat movement: these would also hold or move the team.
  BBB_Tactic.s_bOn = false;
  BBB_FireManeuver.s_bOn = false;
  BBB_Morale.s_bOn = false;
  BBB_Grenades.s_bOn = false;
  BBB_Smoke.s_bOn = false;
  BBB_Flares.s_bOn = false;
  BBB_StaticWeapons.s_bOn = false;
  BBB_Rearm.s_bOn = false;
  BBB_ContactRelay.s_bOn = false;
  vector centre = BBB.Ground("3030 0 1700");
  NavmeshWorldComponent nav = Navmesh();
  if (nav)
  {
   for (int x = -5; x <= 5; x++)
    for (int z = -5; z <= 5; z++) nav.LoadTileIn(centre + Vector(x * 60, 0, z * 60));
  }
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers) observers.InsertObserverSP(22137, centre[0], centre[2], null);
  m_Team = SCR_AIGroup.Cast(Spawn(TEAM_US, centre + Vector(0, 0, -60), "0 0 1"));
  m_Enemy = SCR_AIGroup.Cast(Spawn(TEAM_USSR, centre + Vector(0, 0, -90), "0 0 1"));
  Assert("fixtures", m_Team && m_Enemy);
  if (!m_Team || !m_Enemy) { FinishHold(); return; }
  m_Hooks.m_Groups.Insert(m_Team);
  GetGame().GetWorld().QueryEntitiesBySphere(centre, 300, Collect);
  GetGame().GetCallqueue().CallLater(Survey, 5000);
 }

 void Prepare(SCR_AIGroup group)
 {
  PinDetail(group);
  group.ActivateAI();
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   PinDetail(agent);
   agent.ActivateAI();
   ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (body) body.GetDamageManager().EnableDamageHandling(false);
  }
 }

 // A ground-floor indoor navmesh point beside an opening with a clear 45 m lane out.
 void Survey()
 {
  Prepare(m_Team); Prepare(m_Enemy);
  array<AIAgent> agents = {};
  m_Team.GetAgents(agents);
  AIPathfindingComponent path = AIPathfindingComponent.Cast(agents[0].GetControlledEntity().FindComponent(AIPathfindingComponent));
  array<vector> dirs = {};
  for (int d = 0; d < 16; d++) dirs.Insert(Vector(Math.Sin(d * Math.PI / 8), 0, Math.Cos(d * Math.PI / 8)));
  foreach (IEntity building : m_Candidates)
  {
   vector mins, maxs;
   building.GetWorldBounds(mins, maxs);
   vector size = maxs - mins;
   if (size[0] < 6 || size[2] < 6 || size[0] > 40 || size[2] > 40 || size[1] < 2.5) continue;
   // A house with walls and windows, not an open-fronted shed or carport.
   // -bbbIndoorHoldOpen 1: the open-fronted shed instead, where vanilla empties out every run.
   string prefab = building.GetPrefabData().GetPrefabName();
   bool open = System.IsCLIParam("bbbIndoorHoldOpen");
   if (open && !prefab.Contains("Shed")) continue;
   if (!open && (!prefab.Contains("Houses") || prefab.Contains("Shed") || prefab.Contains("Garage"))) continue;
   array<vector> inside = {};
   for (float x = mins[0] + 1; x < maxs[0] - 1; x += 1.5)
   {
    for (float z = mins[2] + 1; z < maxs[2] - 1; z += 1.5)
    {
     // Floors often stand on a plinth above the terrain: probe from the building's base.
     vector probe = Vector(x, Math.Max(GetGame().GetWorld().GetSurfaceY(x, z), mins[1]) + 1.0, z);
     vector snapped;
     if (!path.GetClosestPositionOnNavmesh(probe, "0.6 2 0.6", snapped)) continue;
     if (vector.DistanceXZ(probe, snapped) > 0.6 || BBB_Buildings.RoofAt(snapped) != building) continue;
     // Well inside: the roof also covers 1.2 m round the point.
     if (!open && BBB_Buildings.RoofAt(snapped + "1.2 0 0") != building || BBB_Buildings.RoofAt(snapped + "-1.2 0 0") != building || BBB_Buildings.RoofAt(snapped + "0 0 1.2") != building || BBB_Buildings.RoofAt(snapped + "0 0 -1.2") != building) continue;
     inside.Insert(snapped);
    }
   }
   PrintFormat("[BBB] INDOORHOLD survey %1 inside=%2", prefab, inside.Count());
   if (inside.Count() < 4) continue;
   foreach (vector spot : inside)
   {
    foreach (vector dir : dirs)
    {
     vector end = BBB.Ground(spot + dir * LANE_M);
     if (BBB_Buildings.RoofAt(end)) continue;
     if (!Lane(spot + Vector(0, 1.0, 0), end + Vector(0, 1.3, 0)) && !Lane(spot + Vector(0, 1.55, 0), end + Vector(0, 1.3, 0))) continue;
     m_Building = building;
     m_Window = spot;
     m_LaneEnd = end;
     m_Inside = inside;
     break;
    }
    if (m_Building) break;
   }
   if (m_Building) break;
  }
  Assert("window-fixture", m_Building != null, string.Format("buildings=%1 window=%2 lane=%3", m_Candidates.Count(), m_Window, m_LaneEnd));
  if (!m_Building) { FinishHold(); return; }
  // The team's places: the window spot and the indoor points nearest it.
  array<vector> pool = {};
  pool.Copy(m_Inside);
  while (m_Places.Count() < agents.Count() && !pool.IsEmpty())
  {
   int best = 0;
   for (int i = 1; i < pool.Count(); i++)
    if (vector.Distance(pool[i], m_Window) < vector.Distance(pool[best], m_Window)) best = i;
   m_Places.Insert(pool[best]);
   pool.Remove(best);
  }
  array<AIAgent> enemies = {};
  m_Enemy.GetAgents(enemies);
  vector side = Vector(m_LaneEnd[2] - m_Window[2], 0, m_Window[0] - m_LaneEnd[0]).Normalized();
  int n;
  foreach (AIAgent enemy : enemies)
  {
   ChimeraCharacter body = ChimeraCharacter.Cast(enemy.GetControlledEntity());
   body.SetOrigin(BBB.Ground(m_LaneEnd + side * (n * 1.5)));
   body.GetCharacterController().OverrideMaxSpeed(0);
   n++;
  }
  // The reported self-kill: a frag lobbed from inside at an enemy seen through the window.
  IEntity thrower = agents[0].GetControlledEntity();
  thrower.SetOrigin(m_Window);
  Assert("no-frag-lob-from-indoors", !BBB_Grenades.IsArcClear(thrower, m_LaneEnd), string.Format("from=%1 to=%2", m_Window, m_LaneEnd));
  PrintFormat("[BBB] INDOORHOLD building=%1 indoorPoints=%2 window=%3 enemy=%4", m_Building.GetPrefabData().GetPrefabName(), m_Inside.Count(), m_Window, m_LaneEnd);
  BeginPhase(false);
 }

 void Place()
 {
  array<AIAgent> agents = {};
  m_Team.GetAgents(agents);
  vector face = m_LaneEnd - m_Window;
  for (int i = 0; i < agents.Count() && i < m_Places.Count(); i++)
  {
   IEntity body = agents[i].GetControlledEntity();
   body.SetOrigin(m_Places[i]);
   body.SetAngles(face.VectorToAngles());
  }
 }

 void BeginPhase(bool on)
 {
  m_bOnPhase = on;
  BBB_Buildings.Stop();
  BBB_Buildings.s_bHold = on;
  Place();
  m_Outside.Clear(); m_Left.Clear(); m_Ammo.Clear();
  m_Fired = 0; m_Samples = 0; m_ProneIndoors = 0; m_IndoorSamples = 0;
  m_At = Now();
  PrintFormat("[BBB] INDOORHOLD phase hold=%1 start", on);
  GetGame().GetCallqueue().CallLater(Observe, 1000, true);
 }

 void Observe()
 {
  array<AIAgent> agents = {};
  m_Team.GetAgents(agents);
  m_Samples++;
  foreach (AIAgent agent : agents)
  {
   ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!body) continue;
   bool indoors = BBB_Buildings.RoofAt(body.GetOrigin()) == m_Building;
   if (indoors)
   {
    m_Outside.Remove(body);
    m_IndoorSamples++;
    if (body.GetCharacterController().GetStance() == ECharacterStance.PRONE) m_ProneIndoors++;
   }
   else
   {
    if (!m_Outside.Contains(body))
    {
     m_Outside.Set(body, Now());
     SCR_AIUtilityComponent u = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
     string behavior = "none";
     string request = "none";
     if (u && u.GetCurrentBehavior()) behavior = u.GetCurrentBehavior().Type().ToString();
     if (u && u.m_CombatMoveState && u.m_CombatMoveState.GetRequest()) request = u.m_CombatMoveState.GetRequest().Type().ToString() + " reason " + typename.EnumToString(SCR_EAICombatMoveReason, u.m_CombatMoveState.GetRequest().m_eReason);
     PrintFormat("[BBB] INDOORHOLD out hold=%1 t=%2 man=%3 behavior=%4 request=%5 at=%6", m_bOnPhase, Now() - m_At, body, behavior, request, body.GetOrigin());
    }
    // Left: out from under the roof for 3 s and away from the doorway, not a flicker at a wall.
    float fromPlace = float.MAX;
    foreach (vector place : m_Places) fromPlace = Math.Min(fromPlace, vector.DistanceXZ(place, body.GetOrigin()));
    if (Now() - m_Outside.Get(body) >= 3 && fromPlace > 4) m_Left.Set(body, true);
   }
   SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(body.FindComponent(SCR_AICombatComponent));
   if (combat && combat.GetCurrentWeapon() && combat.GetCurrentWeapon().GetCurrentMagazine())
   {
    int ammo = combat.GetCurrentWeapon().GetCurrentMagazine().GetAmmoCount();
    if (m_Ammo.Contains(body) && ammo < m_Ammo.Get(body)) m_Fired += m_Ammo.Get(body) - ammo;
    m_Ammo.Set(body, ammo);
   }
  }
  if (Now() - m_At < PHASE_S) return;
  GetGame().GetCallqueue().Remove(Observe);
  PrintFormat("[BBB] INDOORHOLD phase hold=%1 left=%2/%3 fired=%4 proneIndoors=%5/%6 holds=%7 stance=%8 shelters=%9", m_bOnPhase, m_Left.Count(), agents.Count(), m_Fired, m_ProneIndoors, m_IndoorSamples, BBB_Buildings.s_iHolds, BBB_Buildings.s_iStance + BBB_Buildings.s_iRises, BBB_Buildings.s_iShelters);
  if (!m_bOnPhase)
  {
   m_OffLeft = m_Left.Count();
   m_OffFired = m_Fired;
   m_OffProne = m_ProneIndoors;
   m_OffIndoor = m_IndoorSamples;
   BBB_Buildings.s_iHolds = 0;
   BeginPhase(true);
   return;
  }
  Assert("baseline-reproduces-leaving", m_OffLeft >= 1, string.Format("off left=%1", m_OffLeft));
  Assert("hold-keeps-team-inside", m_Left.Count() < m_OffLeft && m_Left.Count() <= 1, string.Format("off left=%1 on left=%2", m_OffLeft, m_Left.Count()));
  Assert("holding-team-fires", m_Fired > 0, string.Format("off fired=%1 on fired=%2", m_OffFired, m_Fired));
  Assert("hold-path-ran", BBB_Buildings.s_iHolds > 0, string.Format("holds=%1 (coverage only)", BBB_Buildings.s_iHolds));
  PrintFormat("[BBB] INDOORHOLD prone indoors off=%1/%2 on=%3/%4 (reported, not judged)", m_OffProne, m_OffIndoor, m_ProneIndoors, m_IndoorSamples);
  FinishHold();
 }

 void FinishHold()
 {
  GetGame().GetCallqueue().Remove(Observe);
  BBB_Buildings.Stop();
  BBB_Buildings.s_bHold = true;
  if (m_SavedHooks) BBB.s_Hooks = m_SavedHooks;
  string verdict = "PASS";
  if (m_Failures > 0) verdict = "FAIL";
  PrintFormat("[ACR] VERDICT %1 test=bbb-indoorhold checks=%2 failed=%3", verdict, m_Checks, m_Failures);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_IndoorHoldTest.Start();
 }
}
