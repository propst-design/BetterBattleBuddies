// Glass isn't cover (Jacob, 19 Nov 2026; BBB_SolidCover). On vanilla Arland:
// 1. Geometry: a man beside a spawned greenhouse, window pane or wire-net fence isn't shielded from a
//    threat across it (1, 3 and 5 rays); beside sandbags or a concrete wall he is. Tin and wooden
//    fences are reported. A greenhouse roof doesn't make him "in a building".
// 2. Vanilla cover points: around Arland's own greenhouses the cover manager offers points that lean
//    on them; BBB_SolidCover rejects them, and its filtered pick still finds real cover.
// 3. Behaviour (open ground, damage off): rifleman A posted in cover behind a greenhouse, rifleman B
//    behind sandbags 14 m along, an enemy team 45 m across both. A must leave and not take cover
//    against the greenhouse again; BBB must keep counting B's sandbags as cover (never deny or push
//    him out of them; vanilla's attack moves may still take him on).
class BBB_GlassCoverTest : BBB_FireManeuverTest
{
 static ref BBB_GlassCoverTest s_Glass;
 static const ResourceName GREENHOUSE = "{3299E6F34CE40185}Prefabs/Structures/Agriculture/Greenhouse/Greenhouse_01/Greenhouse_01.et";
 static const ResourceName WINDOW = "{51E35B4975CBC575}Prefabs/Structures/BuildingParts/Windows/Glass_102x130/Glass_102x130.et";
 static const ResourceName NET_FENCE = "{9AE2BF8692551FE0}Prefabs/Structures/Walls/BarbedWire/NetFence_USSR_01/NetFence_USSR_01_3m.et";
 static const ResourceName TIN_WALL = "{DF3905E54137495D}Prefabs/Structures/Walls/Metal/TinWall_01/TinWall_01_3m.et";
 static const ResourceName WOOD_FENCE = "{3A52FC23A1DCAE53}Prefabs/Structures/Walls/Wooden/WoodenFence_01/WoodenFence_01_2m.et";
 static const ResourceName CONCRETE = "{60E5729BEA18F875}Prefabs/Structures/Walls/Concrete/ConcreteWall_01/ConcreteWall_01_3m.et";
 static const float WATCH_S = 50;
 static const float ENEMY_M = 45;       // enemy team this far from the greenhouse, across it
 static const float NEAR_GLASS = 1.2;   // a cover point this close to the greenhouse's box leans on it (m)
 protected ref array<IEntity> m_aGreenhouses;
 protected ref array<IEntity> m_aMine;
 protected ref BBB_AimLane m_GLane;
 protected IEntity m_House;
 protected IEntity m_Man;
 protected SCR_AIUtilityComponent m_Utility;
 protected vector m_vPost, m_vThreat, m_vSandbagPost;
 protected IEntity m_Other;
 protected SCR_AIGroup m_OtherGroup;
 protected SCR_AIUtilityComponent m_OtherUtility;
 protected float m_fWatchStart;
 protected int m_iPassG, m_iFailG;
 protected int m_iGlassCoverS, m_iRealCoverS, m_iDeniedS, m_iTargetS;
 protected bool m_bPushed;
 protected float m_fLeftS = -1;
 protected int m_iLeftBefore, m_iReleasedBefore;

 static void MaybeStartGlass()
 {
  if (s_Glass || !Replication.IsServer() || !System.IsCLIParam("bbbGlassCoverTest")) return;
  s_Glass = new BBB_GlassCoverTest();
  GetGame().GetCallqueue().CallLater(s_Glass.BeginG, 12000);
 }

 protected void CheckG(string name, bool ok)
 {
  string result = "FAIL";
  if (ok) { result = "PASS"; m_iPassG++; } else m_iFailG++;
  PrintFormat("[BBB] GLASSTEST %1 %2", result, name);
 }

 protected void BeginG()
 {
  if (!BBB.IsStarted()) { GetGame().GetCallqueue().CallLater(BeginG, 1000); return; }
  m_aMine = {};
  UnitG();
  GeometryG();
  if (!PickHouse()) { FinishG(); return; }
  StartLive();
 }

 // ------------------------------------------------------------------ 1. unit + geometry

 protected void UnitG()
 {
  CheckG("prefab names", BBB_SolidCover.FlimsyName("{X}Prefabs/Structures/Agriculture/Greenhouse/Greenhouse_01.et") && BBB_SolidCover.FlimsyName("Some_Mod/Green_House_big.et")
   && BBB_SolidCover.FlimsyName("PolyTunnel_01.et") && !BBB_SolidCover.FlimsyName("{X}Prefabs/Structures/Houses/House_01.et") && !BBB_SolidCover.FlimsyName("Sandbag_01_long_high_burlap.et"));
 }

 protected IEntity SpawnAt(ResourceName prefab, vector at)
 {
  IEntity thing = Spawn(prefab, at, Vector(0, 0, 1));
  if (thing) m_aMine.Insert(thing);
  return thing;
 }

 // Beside the thing's face across the given (or thinnest, -1) horizontal axis, the threat 60 m across
 // it. How many of the 1, 3 and 5 ray checks say shielded (-1: no prefab).
 protected int Shielded(IEntity thing, int axis)
 {
  if (!thing) return -1;
  vector mins, maxs;
  thing.GetBounds(mins, maxs);
  if (axis < 0) { axis = 2; if (maxs[0] - mins[0] < maxs[2] - mins[2]) axis = 0; }
  vector centre = thing.CoordToParent((mins + maxs) * 0.5);
  vector across = thing.GetTransformAxis(axis);
  across[1] = 0;
  across.Normalize();
  float half = (maxs[axis] - mins[axis]) * 0.5;
  vector man = BBB.Ground(centre - across * (half + 0.6));
  vector threat = centre + across * 60;
  int count;
  array<int> rays = {1, 3, 5};
  foreach (int n : rays)
   if (BBB_Cover.Shielded(man, ECharacterStance.CROUCH, threat, n)) count++;
  return count;
 }

 protected void GeometryG()
 {
  int pierced = BBB_SolidCover.s_iPierced;
  IEntity house = SpawnAt(GREENHOUSE, BBB.Ground(Vector(1000, 0, 2300)));
  int along = Shielded(house, 0);
  int across = Shielded(house, 2);
  CheckG(string.Format("greenhouse isn't cover (shielded %1/3 and %2/3, pierced %3)", along, across, BBB_SolidCover.s_iPierced - pierced), along == 0 && across == 0);
  if (house)
  {
   vector mins, maxs;
   house.GetBounds(mins, maxs);
   vector inside = BBB.Ground(house.CoordToParent((mins + maxs) * 0.5));
   CheckG("greenhouse roof isn't a building", BBB_Buildings.RoofAt(inside) == null);
  }
  int window = Shielded(SpawnAt(WINDOW, BBB.Ground(Vector(1040, 0, 2300)) + Vector(0, 0.2, 0)), 2);
  CheckG(string.Format("window glass isn't cover (shielded %1/3)", window), window == 0);
  int net = Shielded(SpawnAt(NET_FENCE, BBB.Ground(Vector(1080, 0, 2300))), -1);
  CheckG(string.Format("wire-net fence isn't cover (shielded %1/3)", net), net == 0);
  IEntity bags = BBB_VanillaTest.SpawnWall(BBB.Ground(Vector(1120, 0, 2300)));
  if (bags) m_aMine.Insert(bags);
  int sandbag = Shielded(bags, -1);
  CheckG(string.Format("sandbags are cover (shielded %1/3)", sandbag), sandbag == 3);
  int concrete = Shielded(SpawnAt(CONCRETE, BBB.Ground(Vector(1160, 0, 2300))), -1);
  CheckG(string.Format("concrete wall is cover (shielded %1/3)", concrete), concrete == 3);
  int tin = Shielded(SpawnAt(TIN_WALL, BBB.Ground(Vector(1200, 0, 2300))), -1);
  int wood = Shielded(SpawnAt(WOOD_FENCE, BBB.Ground(Vector(1240, 0, 2300))), -1);
  PrintFormat("[BBB] GLASSTEST info tin wall shielded %1/3, wooden fence %2/3", tin, wood);
 }

 // ------------------------------------------------------------------ 2. vanilla cover points

 protected bool CollectHouse(IEntity entity)
 {
  if (entity.GetParent() || m_aMine.Contains(entity)) return true;
  if (BBB_SolidCover.Flimsy(entity)) m_aGreenhouses.Insert(entity);
  return true;
 }

 protected bool PickHouse()
 {
  m_aGreenhouses = {};
  GetGame().GetWorld().QueryEntitiesByAABB(Vector(-500, -200, -500), Vector(6000, 800, 6000), CollectHouse);
  CheckG(string.Format("map greenhouses found (%1)", m_aGreenhouses.Count()), !m_aGreenhouses.IsEmpty());
  if (m_aGreenhouses.IsEmpty()) return false;
  NavmeshWorldComponent navmesh = Navmesh();
  if (!navmesh) return true;
  foreach (IEntity house : m_aGreenhouses)
   for (int x = -1; x <= 1; x++)
    for (int z = -1; z <= 1; z++)
     navmesh.LoadTileIn(house.GetOrigin() + Vector(x * 60, 0, z * 60));
  return true;
 }

 // Distance from a point to the house's box, flat (0 inside).
 protected float ToBox(IEntity house, vector point)
 {
  vector mins, maxs;
  house.GetBounds(mins, maxs);
  vector local = house.CoordToLocal(point);
  float dx = Math.Max(0, Math.Max(mins[0] - local[0], local[0] - maxs[0]));
  float dz = Math.Max(0, Math.Max(mins[2] - local[2], local[2] - maxs[2]));
  return Math.Sqrt(dx * dx + dz * dz);
 }

 protected CoverQueryProperties Query(vector centre, vector threat, float radius)
 {
  CoverQueryProperties query = new CoverQueryProperties();
  vector towards = threat - centre;
  towards[1] = 0;
  towards.Normalize();
  query.m_vSectorPos = centre;
  query.m_vSectorDir = towards;
  query.m_vThreatPos = threat + Vector(0, 1, 0);
  query.m_fSectorDistMin = 0;
  query.m_fSectorDistMax = radius;
  query.m_iMaxCoversToCheck = 25;
  query.m_vNearestPolyHalfExtend = Vector(1, 2, 1);
  query.m_fNmAreaCostScale = 0.333;
  query.m_fCoverToThreatAngleCosMin = 0.3;
  return query;
 }

 // A rifleman near the first greenhouse (his pathing asks the cover manager), then the cover points.
 protected void StartLive()
 {
  m_GLane = new BBB_AimLane();
  m_House = m_aGreenhouses[0];
  vector at = BBB.Ground(m_House.GetOrigin() + Vector(9, 0, 0));
  SCR_AIGroup.IgnoreSpawning(true);
  SCR_AIGroup group = SCR_AIGroup.Cast(Spawn(TEAM_US, at, Vector(0, 0, 1)));
  SCR_AIGroup.IgnoreSpawning(false);
  if (group)
  {
   m_GLane.m_aSpawned.Insert(group);
   group.m_aUnitPrefabSlots.Clear();
   group.m_aUnitPrefabSlots.Insert(RIFLE_US);
   m_Man = Spawn(RIFLE_US, at, Vector(0, 0, 1));
   if (m_Man) { group.AddAIEntityToGroup(m_Man); m_GLane.m_aSpawned.Insert(m_Man); }
  }
  m_GLane.m_A.m_Group = group;
  GetGame().GetCallqueue().CallLater(CoverPoints, 6000);
 }

 // Does this cover point lean on a greenhouse (a walk-layer ray to its geometry hits one)?
 protected bool LeansOnGlass(vector at, vector tall)
 {
  vector dir = tall - at;
  dir[1] = 0;
  float reach = Math.Clamp(dir.Length() + 0.5, 1.5, 4);
  if (dir.Length() < 0.05) return false;
  dir.Normalize();
  vector from = at + Vector(0, Math.Clamp((tall[1] - at[1]) * 0.5, 0.25, 0.6), 0);
  TraceParam trace = new TraceParam();
  trace.Start = from;
  trace.End = from + dir * reach;
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  if (GetGame().GetWorld().TraceMove(trace, BBB_SolidCover.Geometry) >= 1) return false;
  return BBB_SolidCover.Flimsy(trace.TraceEnt);
 }

 // Each greenhouse in turn (the rifleman beside it: the cover manager paths from him), four threat
 // directions: the raw vanilla points within 12 m and the ones leaning on the greenhouse, then BBB's
 // filtered pick 30 m round. The live part runs at the greenhouse with real cover nearby.
 protected void CoverPoints()
 {
  ChimeraCoverManagerComponent manager = ChimeraCoverManagerComponent.Cast(GetGame().GetAIWorld().FindComponent(ChimeraCoverManagerComponent));
  AIPathfindingComponent path;
  if (m_Man) path = AIPathfindingComponent.Cast(m_Man.FindComponent(AIPathfindingComponent));
  AIAgent agent;
  if (m_GLane.m_A.m_Group) agent = m_GLane.m_A.m_Group.GetLeaderAgent();
  if (agent) m_Utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
  if (!manager || !path || !m_Utility) { CheckG("rifleman and cover manager", false); FinishG(); return; }
  int raw, leaning, rejected, kept, picks, picksBad;
  foreach (IEntity house : m_aGreenhouses)
  {
   vector mins, maxs;
   house.GetBounds(mins, maxs);
   vector centre = house.CoordToParent((mins + maxs) * 0.5);
   for (int side = 0; side < 4; side++)
   {
    float angle = side * 90 * Math.DEG2RAD;
    vector dir = Vector(Math.Sin(angle), 0, Math.Cos(angle));
    vector threat = BBB.Ground(centre + dir * 70);
    vector stand = BBB.Ground(centre - dir * 5);
    vector snapped;
    if (path.GetClosestPositionOnNavmesh(stand, Vector(3, 3, 3), snapped)) stand = snapped;
    m_Man.SetOrigin(stand);
    CoverQueryProperties query = Query(centre, threat, 12);
    array<ref SCR_AICoverLock> held = {};
    for (int n = 0; n < 12; n++)
    {
     vector at, tall;
     int tileX, tileY, id;
     if (!manager.GetBestCover("Soldiers", path, query, at, tall, tileX, tileY, id)) break;
     held.Insert(new SCR_AICoverLock(tileX, tileY, id, at, tall));
     raw++;
     if (!LeansOnGlass(at, tall)) continue;
     leaning++;
     if (BBB_SolidCover.PointSolid(at, tall, query.m_vThreatPos)) { kept++; continue; }
     rejected++;
    }
    foreach (SCR_AICoverLock lock : held) lock.Release();
    // BBB's pick (what vanilla's node now asks): never a greenhouse point.
    vector best, bestTall;
    int bx, by, bid;
    bool found = BBB_SolidCover.BestCover(manager, path, Query(centre, threat, 30), best, bestTall, bx, by, bid);
    if (found) { picks++; if (LeansOnGlass(best, bestTall)) picksBad++; }
   }
  }
  PrintFormat("[BBB] GLASSTEST info vanilla cover points by greenhouses: %1, %2 lean on one, %3 rejected, %4 kept (solid past the glass); BBB picks %5", raw, leaning, rejected, kept, picks);
  CheckG(string.Format("greenhouse cover points rejected (%1 of %2 leaning on one)", rejected, leaning), rejected == leaning);
  CheckG(string.Format("BBB's picks by greenhouses are real cover (%1 of %2 lean on one)", picksBad, picks), picks > 0 && picksBad == 0);
  LiveOpen();
 }

 // ------------------------------------------------------------------ 3. behaviour

 // Arland's farm greenhouses stand among sheds and fences that block any view, so the live part is
 // on open ground: a vanilla greenhouse and, 14 m along, sandbags; an enemy team 45 m across both.
 // Each rifleman (a group of one) first sees the enemy from beside his post (vanilla's own sight
 // doesn't pass a greenhouse: engine perception, out of a mod's reach), then is posted in cover the
 // way vanilla would have him: cover assigned, in it, crouched. A (greenhouse) must leave; B
 // (sandbags) must keep his.
 protected void LiveOpen()
 {
  vector site = BBB.Ground(Vector(1000, 0, 2400));
  vector across = Vector(0, 0, 1);
  vector along = Vector(1, 0, 0);
  m_House = SpawnAt(GREENHOUSE, site);
  IEntity bags = BBB_VanillaTest.SpawnWall(BBB.Ground(site + along * 14));
  if (bags) m_aMine.Insert(bags);
  if (!m_House || !bags) { CheckG("open site props", false); FinishG(); return; }
  vector mins, maxs;
  m_House.GetBounds(mins, maxs);
  vector centre = m_House.CoordToParent((mins + maxs) * 0.5);
  m_vThreat = BBB.Ground(centre + across * ENEMY_M + along * 7);
  m_vPost = BBB.Ground(centre - across * (Math.AbsFloat(vector.Dot(m_House.GetTransformAxis(0), across)) * (maxs[0] - mins[0]) * 0.5 + Math.AbsFloat(vector.Dot(m_House.GetTransformAxis(2), across)) * (maxs[2] - mins[2]) * 0.5 + 0.6));
  bags.GetBounds(mins, maxs);
  vector bagCentre = bags.CoordToParent((mins + maxs) * 0.5);
  float bagHalf = Math.AbsFloat(vector.Dot(bags.GetTransformAxis(0), across)) * (maxs[0] - mins[0]) * 0.5 + Math.AbsFloat(vector.Dot(bags.GetTransformAxis(2), across)) * (maxs[2] - mins[2]) * 0.5;
  m_vSandbagPost = BBB.Ground(bagCentre - across * (bagHalf + 0.6));
  PrintFormat("[BBB] GLASSTEST open site: greenhouse post shielded=%1, sandbag post shielded=%2", BBB_Cover.Shields(m_vPost, ECharacterStance.CROUCH, m_vThreat), BBB_Cover.Shields(m_vSandbagPost, ECharacterStance.CROUCH, m_vThreat));
  m_Man.SetOrigin(BBB.Ground(m_vPost - along * 5));
  m_Other = SpawnOne(BBB.Ground(m_vSandbagPost + along * 5));
  m_GLane.m_B.m_Group = SpawnSquad(true, m_vThreat, across * -1, m_GLane, 2);
  m_fWatchStart = Now();
  GetGame().GetCallqueue().CallLater(Sight, 2500, true);
 }

 protected IEntity SpawnOne(vector at)
 {
  SCR_AIGroup.IgnoreSpawning(true);
  SCR_AIGroup group = SCR_AIGroup.Cast(Spawn(TEAM_US, at, Vector(0, 0, 1)));
  SCR_AIGroup.IgnoreSpawning(false);
  if (!group) return null;
  m_GLane.m_aSpawned.Insert(group);
  group.m_aUnitPrefabSlots.Clear();
  group.m_aUnitPrefabSlots.Insert(RIFLE_US);
  IEntity man = Spawn(RIFLE_US, at, Vector(0, 0, 1));
  if (man) { group.AddAIEntityToGroup(man); m_GLane.m_aSpawned.Insert(man); }
  m_OtherGroup = group;
  return man;
 }

 protected SCR_AIUtilityComponent UtilityOf(IEntity man)
 {
  if (!man) return null;
  AIControlComponent control = AIControlComponent.Cast(man.FindComponent(AIControlComponent));
  if (!control || !control.GetControlAIAgent()) return null;
  return SCR_AIUtilityComponent.Cast(control.GetControlAIAgent().FindComponent(SCR_AIUtilityComponent));
 }

 protected void Arm()
 {
  Hook(m_GLane.m_A, false);
  Hook(m_GLane.m_B, false);
  array<IEntity> all = {};
  all.Copy(m_GLane.m_B.m_aMen);
  all.Insert(m_Man);
  all.Insert(m_Other);
  if (m_OtherGroup) PinDetail(m_OtherGroup);
  foreach (IEntity man : all)
  {
   ChimeraCharacter character = ChimeraCharacter.Cast(man);
   if (!character) continue;
   if (character.GetDamageManager()) character.GetDamageManager().EnableDamageHandling(false);
   AIControlComponent control = AIControlComponent.Cast(man.FindComponent(AIControlComponent));
   if (control) PinDetail(control.GetControlAIAgent());
  }
 }

 protected void Sight()
 {
  if (m_GLane.m_B.m_aMen.IsEmpty()) Arm();
  m_OtherUtility = UtilityOf(m_Other);
  vector threat;
  bool seenA = BBB_Cover.ThreatOf(m_Utility, threat);
  bool seenB = m_OtherUtility && BBB_Cover.ThreatOf(m_OtherUtility, threat);
  if (!(seenA && seenB) && Now() - m_fWatchStart < 30) return;
  GetGame().GetCallqueue().Remove(Sight);
  CheckG(string.Format("both saw the enemy from beside their posts (A %1, B %2, after %3 s)", seenA, seenB, (int)(Now() - m_fWatchStart)), seenA && seenB);
  if (!seenA || !seenB) { FinishG(); return; }
  PostIn(m_Man, m_Utility, m_vPost, m_House.GetOrigin());
  PostIn(m_Other, m_OtherUtility, m_vSandbagPost, m_vSandbagPost + Vector(0, 0, 1));
  m_iLeftBefore = BBB_Cover.s_iLeft;
  m_iReleasedBefore = BBB_Cover.s_iReleased;
  m_fWatchStart = Now();
  GetGame().GetCallqueue().CallLater(Watch, 1000, true);
 }

 protected void PostIn(IEntity man, SCR_AIUtilityComponent utility, vector at, vector towards)
 {
  man.SetOrigin(at);
  SCR_AICombatMoveState state = utility.m_CombatMoveState;
  vector tall = at + (towards - at).Normalized() * 0.8 + Vector(0, 1, 0);
  state.AssignCover(new SCR_AICoverLock(0, 0, -1, at, tall));
  state.m_bInCover = true;
  state.m_bExposedInCover = true;
  ChimeraCharacter body = ChimeraCharacter.Cast(man);
  if (body) SCR_AIStanceHandling.SetStance(body.GetCharacterController(), ECharacterStance.CROUCH);
 }

 protected void Watch()
 {
  float t = Now() - m_fWatchStart;
  if (!m_Man || !BBB.IsAlive(m_Man) || !m_Other || !BBB.IsAlive(m_Other)) { CheckG("riflemen alive", false); EndWatch(); return; }
  vector threat;
  if (BBB_Cover.ThreatOf(m_Utility, threat)) m_iTargetS++;
  // A: off the greenhouse, and not back in cover against it.
  SCR_AICombatMoveState state = m_Utility.m_CombatMoveState;
  vector here = m_Man.GetOrigin();
  float toHouse = ToBox(m_House, here);
  bool inCover = state && state.m_bInCover;
  bool againstGlass = inCover && toHouse <= NEAR_GLASS;
  if (m_fLeftS < 0 && (vector.DistanceXZ(here, m_vPost) > 2 || !inCover)) m_fLeftS = t;
  else if (againstGlass && m_fLeftS >= 0) m_iGlassCoverS++;
  // B: in his sandbag cover, and BBB agrees it's cover.
  SCR_AICombatMoveState other = m_OtherUtility.m_CombatMoveState;
  bool atBags = other && other.m_bInCover && vector.DistanceXZ(m_Other.GetOrigin(), m_vSandbagPost) < 2;
  bool held = atBags && BBB_Cover.InCover(m_OtherUtility);
  if (held) m_iRealCoverS++;
  else if (atBags) m_iDeniedS++;
  if (other && other.m_fBBBCoverLeftAt > 0) m_bPushed = true;
  if (((int)t) % 5 == 0) PrintFormat("[BBB] GLASSTEST t=%1 A at=%2 house=%3m inCover=%4 trueCover=%5 | B at=%6 held=%7", (int)t, here, toHouse.ToString(4, 1), inCover, BBB_Cover.InCover(m_Utility), m_Other.GetOrigin(), held);
  if (t >= WATCH_S) EndWatch();
 }

 protected void EndWatch()
 {
  GetGame().GetCallqueue().Remove(Watch);
  CheckG(string.Format("A knew the enemy (%1 s with a target)", m_iTargetS), m_iTargetS > 0);
  CheckG(string.Format("A left the greenhouse (after %1 s; TrueCover left %2, released %3)", m_fLeftS, BBB_Cover.s_iLeft - m_iLeftBefore, BBB_Cover.s_iReleased - m_iReleasedBefore), m_fLeftS >= 0 && m_fLeftS <= 20);
  CheckG(string.Format("A not back in cover against the greenhouse (%1 s)", m_iGlassCoverS), m_iGlassCoverS <= 3);
  // Vanilla's attack moves may take him on from a post (as from any); BBB must never deny the sandbags.
  CheckG(string.Format("B's sandbags stay cover (%1 s in them, BBB said no %2 s, pushed out %3)", m_iRealCoverS, m_iDeniedS, m_bPushed), m_iRealCoverS >= 3 && m_iDeniedS == 0 && !m_bPushed);
  FinishG();
 }

 protected void FinishG()
 {
  if (m_GLane) Clear(m_GLane);
  foreach (IEntity thing : m_aMine)
   if (thing) SCR_EntityHelper.DeleteEntityAndChildren(thing);
  string verdict = "PASS";
  if (m_iFailG > 0 || m_iPassG == 0) verdict = "FAIL";
  PrintFormat("[BBB] GLASSTEST RESULT pierced=%1 rejected=%2", BBB_SolidCover.s_iPierced, BBB_SolidCover.s_iRejected);
  PrintFormat("[ACR] VERDICT %1 test=bbbglasscover pass=%2 fail=%3", verdict, m_iPassG, m_iFailG);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_GlassCoverTest.MaybeStartGlass();
 }
}
