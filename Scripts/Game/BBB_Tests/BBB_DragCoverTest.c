// Drag cover depth (-bbbDragCoverTest 1, preset dragcover, ~1 min, no ACE needed): the spot picker
// alone, on a real sandbag wall. An "enemy" stands at a clear lane's start, a casualty lies 60 m
// down it, and a 10 m high sandbag wall stands 6 m behind him. The old rule (deep off) stops at the
// first hidden sample, just behind the wall, where a dragged casualty (who ends up TRAIL_M short
// of the spot) still lies in front of it. The new rule pushes the spot further along the same
// bearing, so where he really lies is behind the wall too. Also: the push never goes through a
// wall (Walkable), and the search cost per drag.
// Not covered: AI walking there with ACE (that's the bbbdrag test and Jacob's play-test), and
// cover shapes other than a straight wall.
class BBB_DragCoverTest : BBB_AimTest
{
 static ref BBB_DragCoverTest s_DragCover;
 // High wall: the short bag's collision is ~0.6 m, so which sample it hides was a matter of
 // centimetres (second run, 7 Nov).
 static const ResourceName SANDBAG = "{BE16EE8FAA315FE2}Prefabs/Props/Military/Sandbags/Sandbag_01_long_high_burlap.et";
 static const int EXPECTED = 11;
 static const float CASUALTY_M = 60;
 static const float WALL_M = 6; // hidden from ~1.5 m behind it: the 8 m ring is just past the edge
 protected int m_iChecks;
 protected int m_iFailures;
 protected vector m_vEnemy;
 protected vector m_vCasualty;
 protected vector m_vDir;
 protected ref array<IEntity> m_aBags;
 protected int m_iWantBags;

 static void MaybeStartDragCover()
 {
  if (s_DragCover || !Replication.IsServer() || !System.IsCLIParam("bbbDragCoverTest")) return;
  s_DragCover = new BBB_DragCoverTest();
  GetGame().GetCallqueue().CallLater(s_DragCover.Begin, 12000);
 }

 // A flat lane: on a slope the enemy looks down over the wall, and folds in the ground hide
 // spots on other bearings (the first run, 7 Nov: enemy 14 m above the casualty).
 override protected bool LaneClear(vector from, vector dir)
 {
  float y = GetGame().GetWorld().GetSurfaceY(from[0], from[2]);
  for (int i = 1; i <= 6; i++)
  {
   vector p = from + dir * (CASUALTY_M * 0.25 * i);
   if (Math.AbsFloat(GetGame().GetWorld().GetSurfaceY(p[0], p[2]) - y) > 1.5) return false;
   vector side = p + Vector(-dir[2], 0, dir[0]) * 15;
   if (Math.AbsFloat(GetGame().GetWorld().GetSurfaceY(side[0], side[2]) - y) > 1.5) return false;
   side = p - Vector(-dir[2], 0, dir[0]) * 15;
   if (Math.AbsFloat(GetGame().GetWorld().GetSurfaceY(side[0], side[2]) - y) > 1.5) return false;
  }
  return super.LaneClear(from, dir);
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
  PrintFormat("[BBB] DRAGCOVERTEST CHECK %1 %2 %3", name, res, detail);
 }

 override protected void Begin()
 {
  m_aBags = {};
  m_aCheckDistances.Clear();
  m_aCheckDistances.Insert(CASUALTY_M);
  m_aCheckDistances.Insert(CASUALTY_M + 15);
  m_aCheckDistances.Insert(CASUALTY_M + 30);
  if (!FindSite())
  {
   Check("site", false, "no clear lane");
   Finish();
   return;
  }
  m_vDir = m_aSiteDir[0];
  m_vEnemy = BBB.Ground(m_aSiteFrom[0]);
  m_vCasualty = BBB.Ground(m_vEnemy + m_vDir * CASUALTY_M);
  vector lateral = Vector(-m_vDir[2], 0, m_vDir[0]);
  vector centre = m_vCasualty + m_vDir * WALL_M;
  // Lay the bags long side across the lane, end to end, at least 10 m in all.
  vector facing = m_vDir;
  IEntity first = Spawn(SANDBAG, centre, facing);
  if (!first)
  {
   Check("wall-built", false, "sandbag prefab didn't spawn");
   Finish();
   return;
  }
  vector mins, maxs;
  first.GetBounds(mins, maxs);
  float wide = maxs[0] - mins[0];
  if (maxs[2] - mins[2] > wide)
  {
   wide = maxs[2] - mins[2];
   SCR_EntityHelper.DeleteEntityAndChildren(first);
   facing = lateral;
   first = Spawn(SANDBAG, centre, facing);
  }
  m_aBags.Insert(first);
  wide = Math.Max(wide - 0.1, 0.5);
  int each = Math.Ceil(5 / wide);
  for (int col = 1; col <= each; col++)
  {
   IEntity right = Spawn(SANDBAG, centre + lateral * (col * wide), facing);
   IEntity left = Spawn(SANDBAG, centre - lateral * (col * wide), facing);
   if (right) m_aBags.Insert(right);
   if (left) m_aBags.Insert(left);
  }
  m_iWantBags = 1 + 2 * each;
  PrintFormat("[BBB] DRAGCOVERTEST enemy %1, casualty %2, heading %3, wall of %4 bags (bounds %5 .. %6) %7 m behind him", m_vEnemy, m_vCasualty, m_vDir, m_aBags.Count(), mins, maxs, WALL_M);
  // Let the bags' physics settle into the trace world.
  GetGame().GetCallqueue().CallLater(Measure, 2000);
 }

 protected void Measure()
 {
  BBB_Drag.Read();
  Check("default-on", BBB_Drag.s_bDeep, "bbbDragDeep defaults on");
  Check("wall-built", m_aBags.Count() == m_iWantBags && m_iWantBags >= 3, string.Format("%1 of %2", m_aBags.Count(), m_iWantBags));
  array<vector> enemies = {m_vEnemy};
  // Profile along the lane behind him: which points the wall hides.
  for (float d = 4; d <= 16; d += 1)
  {
   vector at;
   bool usable = BBB_Drag.Usable(m_vCasualty + m_vDir * d, null, at);
   PrintFormat("[BBB] DRAGCOVERTEST profile %1 m: usable %2, water %3, hidden %4, ground %5", d, usable, BBB_Terrain.IsWaterAt(at), BBB_Drag.Hidden(at, enemies, true), at[1]);
  }

  vector oldSpot, oldThin;
  bool oldFound = BBB_Drag.FindCoverFrom(m_vCasualty, null, enemies, false, oldSpot, oldThin);
  vector oldRest = Rest(oldSpot);
  float oldDepth = vector.DistanceXZ(m_vCasualty, oldSpot);
  Check("old-finds-cover-behind-wall", oldFound && oldDepth > WALL_M && oldDepth < WALL_M + 3, string.Format("found %1, %2 m from the casualty", oldFound, oldDepth));
  // The problem being fixed: with the old spot, the casualty is put down in view.
  Check("old-casualty-left-in-view", oldFound && !BBB_Drag.Hidden(oldRest, enemies, true), string.Format("casualty would lie %1 m from the start", vector.DistanceXZ(m_vCasualty, oldRest)));

  int deepAtStart = BBB_Drag.s_iDeep;
  vector spot, thin;
  bool found = BBB_Drag.FindCoverFrom(m_vCasualty, null, enemies, true, spot, thin);
  float depth = vector.DistanceXZ(m_vCasualty, spot);
  vector rest = Rest(spot);
  Check("deep-same-first-spot", found && vector.DistanceXZ(thin, oldSpot) < 0.01, string.Format("thin %1, old %2", thin, oldSpot));
  Check("deep-goes-further", found && depth >= oldDepth + BBB_Drag.DEEP_M * 0.5 - 0.1 && BBB_Drag.s_iDeep == deepAtStart + 1, string.Format("%1 m vs %2 m, deepened %3", depth, oldDepth, BBB_Drag.s_iDeep - deepAtStart));
  vector a = spot - m_vCasualty;
  vector b = oldSpot - m_vCasualty;
  a[1] = 0;
  b[1] = 0;
  a.Normalize();
  b.Normalize();
  Check("deep-same-bearing", vector.Dot(a, b) > 0.996, string.Format("cos %1", vector.Dot(a, b)));
  Check("deep-casualty-hidden", found && BBB_Drag.Hidden(spot, enemies, true) && BBB_Drag.Hidden(rest, enemies, true), string.Format("casualty would lie %1 m from the start", vector.DistanceXZ(m_vCasualty, rest)));

  // The push never goes through a wall: across the bags is blocked, beside them it's open.
  vector front = m_vCasualty + m_vDir * (WALL_M - 2);
  vector back = m_vCasualty + m_vDir * (WALL_M + 2);
  vector lateral = Vector(-m_vDir[2], 0, m_vDir[0]);
  Check("walk-blocked-by-wall", !BBB_Drag.Walkable(BBB.Ground(front), BBB.Ground(back)), "across the sandbags");
  Check("walk-open-beside", BBB_Drag.Walkable(BBB.Ground(front + lateral * 9), BBB.Ground(back + lateral * 9)), "9 m to the side");

  // Cost: the search runs once per drag start.
  int started = System.GetTickCount();
  for (int i = 0; i < 20; i++) BBB_Drag.FindCoverFrom(m_vCasualty, null, enemies, true, spot, thin);
  float elapsed = System.GetTickCount() - started;
  float perSearch = elapsed / 20;
  Check("search-cost", perSearch < 5, string.Format("%1 ms per search, one enemy", perSearch));
  PrintFormat("[BBB] DRAGCOVERTEST RESULT old spot %1 m (casualty at %2 m), new spot %3 m (casualty at %4 m)", Math.Round(oldDepth * 10) / 10, Math.Round(vector.DistanceXZ(m_vCasualty, oldRest) * 10) / 10, Math.Round(depth * 10) / 10, Math.Round(vector.DistanceXZ(m_vCasualty, rest) * 10) / 10);
  Finish();
 }

 // Where a dragged casualty ends up for a given spot.
 protected vector Rest(vector spot)
 {
  vector bearing = spot - m_vCasualty;
  bearing[1] = 0;
  bearing.Normalize();
  return BBB.Ground(spot - bearing * BBB_Drag.TRAIL_M);
 }

 override protected void Finish()
 {
  string verdict = "FAIL";
  if (m_iFailures == 0 && m_iChecks == EXPECTED) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbdragcover passed=%2 failed=%3 expected=%4", verdict, m_iChecks - m_iFailures, m_iFailures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_DragCoverTest.MaybeStartDragCover();
 }
}
