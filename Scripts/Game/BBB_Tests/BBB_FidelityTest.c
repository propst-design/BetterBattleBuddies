// Fidelity tiers (BBB #24). Unit checks on the tier table (Standard's column equals the constants it
// replaced) and the governor's lines, a sandbag geometry check of the multi-ray cover test, then a ramp
// of native firefights with damage off: 20, then 50, then 100 combatants (5 v 5 lanes), STEP_S each.
// BBB's whole tick cost (every tick.* stage, fast lane included, ms per second) and the server frame
// are read over the last WINDOW_S of each step, with the effective tier the governor chose.
// -bbbFidelity picks the tier, -bbbFidelityGovernor 0 holds it. This measures cost and the governor,
// not behaviour: it says nothing about hits taken or time exposed.
class BBB_FidelityTest : BBB_FireManeuverTest
{
 static ref BBB_FidelityTest s_Fid;
 static const float STEP_S = 40;
 static const float WINDOW_S = 24;
 protected ref array<ref BBB_AimLane> m_LanesF = {};
 protected ref array<int> m_aTargets = {20, 50, 100};
 protected int m_iStep;
 protected float m_fStart;
 protected bool m_bWindow;
 protected int m_iFastBefore;
 protected int m_iContactSum, m_iAliveSum;
 protected int m_iPassF, m_iFailF;
 protected string m_sSummary = "";

 static void MaybeStartFid()
 {
  if (s_Fid || !Replication.IsServer() || !System.IsCLIParam("bbbFidelityTest")) return;
  s_Fid = new BBB_FidelityTest();
  GetGame().GetCallqueue().CallLater(s_Fid.BeginF, 12000);
 }

 protected void CheckF(string name, bool ok)
 {
  string result = "FAIL";
  if (ok) { result = "PASS"; m_iPassF++; } else m_iFailF++;
  PrintFormat("[BBB] FIDTEST %1 %2", result, name);
 }

 protected void BeginF()
 {
  if (!BBB.IsStarted()) { GetGame().GetCallqueue().CallLater(BeginF, 1000); return; }
  BBB_Perf.s_bOn = true;
  UnitChecksF();
  GeometryF();
  m_fStart = Now();
  Grow(m_aTargets[0]);
  GetGame().GetCallqueue().CallLater(TickF, 1000, true);
 }

 // ------------------------------------------------------------------ unit checks

 protected void UnitChecksF()
 {
  CheckF("table has a row per knob", BBB_Fidelity.Rows() == BBB_Fidelity.KNOBS);
  int s = BBB_Fidelity.STANDARD;
  bool standard = BBB_Fidelity.At(BBB_Fidelity.FAST_LANE_MS, s) == 0;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.SQUAD_REFRESH_S, s) == BBB_Squad.REFRESH_S;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.LEADER_LOOK_S, s) == BBB_SquadLeader.LOOK_S;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.LEADER_CUE_S, s) == BBB_SquadLeader.CUE_EVERY_S;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.COVER_RECHECK_MS, s) == BBB_Cover.RECHECK_MS;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.COVER_RAYS, s) == 1;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.TACTIC_LOOK_S, s) == BBB_Tactic.LOOK_S;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.TACTIC_STAGGER, s) == BBB_Tactic.STAGGER;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.RELAY_STAGGER, s) == BBB_ContactRelay.STAGGER;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.GRENADE_CHECK_MS, s) == BBB_Grenades.CHECK_INTERVAL_MS;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.LAUNCHER_CHECK_MS, s) == BBB_LauncherUse.CHECK_MS;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.LAUNCHERS_CHECK_MS, s) == BBB_Launchers.CHECK_MS;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.STATIC_CHECK_MS, s) == BBB_StaticWeapons.CHECK_INTERVAL_MS;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.FM_ADMISSIONS, s) == BBB_FireManeuver.MAX_ADMISSIONS_PER_TICK;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.PAIR_GROUPS, s) == BBB_Teams.PAIR_GROUPS_PER_TICK;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.HELDGUNS_EVERY, s) == BBB_HeldGuns.WINCHESTER_EVERY;
  standard = standard && BBB_Fidelity.At(BBB_Fidelity.MEDIC_TRACES, s) == BBB_Medic.MAX_TRACES;
  CheckF("Standard column equals the constants it replaced", standard);
  // Intervals only shrink up the tiers, counts (rays, admissions, traces) only grow; the fast lane
  // exists only above Standard.
  array<int> counts = {BBB_Fidelity.COVER_RAYS, BBB_Fidelity.FM_ADMISSIONS, BBB_Fidelity.PAIR_GROUPS, BBB_Fidelity.MEDIC_TRACES};
  bool monotone = true;
  for (int knob = 1; knob < BBB_Fidelity.KNOBS; knob++)
  {
   float standardValue = BBB_Fidelity.At(knob, BBB_Fidelity.STANDARD);
   float high = BBB_Fidelity.At(knob, BBB_Fidelity.HIGH);
   float uncapped = BBB_Fidelity.At(knob, BBB_Fidelity.UNCAPPED);
   if (counts.Contains(knob)) { if (high < standardValue || uncapped < high) monotone = false; }
   else if (high > standardValue || uncapped > high || uncapped <= 0) monotone = false;
  }
  bool lane = BBB_Fidelity.At(BBB_Fidelity.FAST_LANE_MS, BBB_Fidelity.HIGH) > 0 && BBB_Fidelity.At(BBB_Fidelity.FAST_LANE_MS, BBB_Fidelity.UNCAPPED) > 0;
  CheckF("higher tiers re-check sooner and test more", monotone && lane);
  int u = BBB_Fidelity.s_iUncappedMax;
  int h = BBB_Fidelity.s_iHighMax;
  bool lines = BBB_Fidelity.Ceiling(u, 0) == BBB_Fidelity.UNCAPPED && BBB_Fidelity.Ceiling(u + 1, 0) == BBB_Fidelity.HIGH
   && BBB_Fidelity.Ceiling(h, 0) == BBB_Fidelity.HIGH && BBB_Fidelity.Ceiling(h + 1, 0) == BBB_Fidelity.STANDARD;
  // Hysteresis: just under a line is not low enough to step back up.
  bool margin = BBB_Fidelity.Ceiling(u - 1, BBB_Fidelity.GOVERN_MARGIN) == BBB_Fidelity.HIGH && BBB_Fidelity.Ceiling(h - 1, BBB_Fidelity.GOVERN_MARGIN) == BBB_Fidelity.STANDARD
   && BBB_Fidelity.Ceiling(u - BBB_Fidelity.GOVERN_MARGIN, BBB_Fidelity.GOVERN_MARGIN) == BBB_Fidelity.UNCAPPED;
  CheckF(string.Format("governor lines (Uncapped <= %1, High <= %2) and step-up margin", u, h), lines && margin);
  string value;
  int asked = 0;
  if (System.GetCLIParam("bbbFidelity", value)) asked = value.ToInt();
  CheckF(string.Format("setting read (asked %1, set %2)", asked, BBB_Fidelity.s_iSet), BBB_Fidelity.s_iSet == asked);
 }

 // A sandbag wall: square on, every ray count says covered and the far-back control doesn't; end on
 // (the threat looking down its length), each ray count must match the rays that physically pass
 // through its thickness by its bounds (more than half hit: covered).
 protected void GeometryF()
 {
  vector at = BBB.Ground(Vector(1000, 0, 2300));
  IEntity wall = BBB_VanillaTest.SpawnWall(at);
  if (!wall) { CheckF("cover geometry (no sandbag prefab)", false); return; }
  vector mins, maxs;
  wall.GetBounds(mins, maxs);
  vector centre = wall.CoordToParent((mins + maxs) * 0.5);
  centre[1] = at[1];
  vector along = wall.GetTransformAxis(0);
  vector across = wall.GetTransformAxis(2);
  float lengthAlong = maxs[0] - mins[0];
  float thick = maxs[2] - mins[2];
  if (thick > lengthAlong)
  {
   vector swap = along; along = across; across = swap;
   float t = lengthAlong; lengthAlong = thick; thick = t;
  }
  along[1] = 0; along.Normalize();
  across[1] = 0; across.Normalize();
  vector behind = BBB.Ground(centre - across * (thick * 0.5 + 0.6));
  vector threatAcross = centre + across * 60;
  bool square = BBB_Cover.Shielded(behind, ECharacterStance.CROUCH, threatAcross, 1) && BBB_Cover.Shielded(behind, ECharacterStance.CROUCH, threatAcross, 3)
   && BBB_Cover.Shielded(behind, ECharacterStance.CROUCH, threatAcross, 5)
   && !BBB_Cover.Shielded(behind - across * 10, ECharacterStance.CROUCH, threatAcross, 5);
  CheckF(string.Format("cover rays, square on (wall %1 x %2 m)", lengthAlong.ToString(4, 2), thick.ToString(4, 2)), square);
  vector end = BBB.Ground(centre + along * (lengthAlong * 0.5 + 0.6));
  vector threatAlong = centre - along * 60;
  string got = "";
  bool endOn = true;
  int judged;
  array<int> counts = {1, 3, 5};
  foreach (int rays : counts)
  {
   int hits;
   bool ambiguous;
   for (int i = 0; i < rays; i++)
   {
    float offset = BBB_Cover.SHOULDERS * ((i + 0.5) / rays - 0.5);
    float inside = thick * 0.5 - Math.AbsFloat(offset);
    if (Math.AbsFloat(inside) < 0.05) ambiguous = true;
    if (inside > 0) hits++;
   }
   bool expected = hits * 2 > rays;
   bool shielded = BBB_Cover.Shielded(end, ECharacterStance.CROUCH, threatAlong, rays);
   got += string.Format(" %1:%2/%3", rays, shielded, expected);
   if (ambiguous) continue;
   judged++;
   if (shielded != expected) endOn = false;
  }
  CheckF(string.Format("cover rays, end on (got/expected%1)", got), endOn && judged >= 2);
  SCR_EntityHelper.DeleteEntityAndChildren(wall);
 }

 // ------------------------------------------------------------------ the ramp

 // Lane sites: 34 m apart along three rows of Arland's farmland, kept only where a man at the near end
 // can see one at the far end (eye height, 80 m): blind lanes measured idle soldiers (first runs).
 protected int m_iSite;
 protected bool NextSite(out vector from, out vector dir)
 {
  array<float> rows = {2520, 2770, 2350, 2900};
  dir = Vector(0.087, 0, 0.996);
  while (m_iSite < rows.Count() * 30)
  {
   float x = 1100 + (m_iSite % 30) * 34;
   float z = rows[m_iSite / 30];
   m_iSite++;
   from = BBB.Ground(Vector(x, 0, z));
   vector to = BBB.Ground(from + dir * 80);
   if (BBB_FiringCover.Ray(from + Vector(0, 1.6, 0), to + Vector(0, 1.6, 0)) >= 0.99) return true;
  }
  return false;
 }

 protected void Grow(int target)
 {
  int men = m_LanesF.Count() * 10;
  while (men < target)
  {
   vector from, dir;
   if (!NextSite(from, dir)) { CheckF("enough clear lane sites", false); return; }
   BBB_AimLane lane = new BBB_AimLane();
   lane.m_vFrom = from;
   lane.m_vDir = dir;
   lane.m_vTo = BBB.Ground(lane.m_vFrom + lane.m_vDir * 80);
   lane.m_A.m_Group = SpawnSquad(false, lane.m_vFrom, lane.m_vDir, lane, 5);
   lane.m_B.m_Group = SpawnSquad(true, lane.m_vTo, lane.m_vDir * -1, lane, 5);
   lane.m_fStart = Now();
   m_LanesF.Insert(lane);
   men += 10;
  }
 }

 protected void PinNew()
 {
  foreach (BBB_AimLane lane : m_LanesF)
  {
   if (lane.m_bHooked || Now() - lane.m_fStart < 3) continue;
   lane.m_bHooked = true;
   Hook(lane.m_A, false);
   Hook(lane.m_B, false);
   array<BBB_AimSide> sides = {lane.m_A, lane.m_B};
   foreach (BBB_AimSide side : sides)
    foreach (IEntity man : side.m_aMen)
    {
     ChimeraCharacter character = ChimeraCharacter.Cast(man);
     if (character && character.GetDamageManager()) character.GetDamageManager().EnableDamageHandling(false);
    }
  }
 }

 // Living men and how many of them have a current target.
 protected int Alive(out int contacts)
 {
  int count;
  contacts = 0;
  foreach (BBB_AimLane lane : m_LanesF)
  {
   array<SCR_AIGroup> groups = {lane.m_A.m_Group, lane.m_B.m_Group};
   foreach (SCR_AIGroup group : groups)
   {
    array<AIAgent> agents = {};
    if (group) group.GetAgents(agents);
    foreach (AIAgent agent : agents)
    {
     if (!agent || !BBB.IsAlive(agent.GetControlledEntity())) continue;
     count++;
     SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
     if (utility && utility.m_CombatComponent && utility.m_CombatComponent.GetCurrentTarget()) contacts++;
    }
   }
  }
  return count;
 }

 protected void TickF()
 {
  PinNew();
  float into = Now() - m_fStart - m_iStep * STEP_S;
  if (!m_bWindow && into >= STEP_S - WINDOW_S)
  {
   m_bWindow = true;
   BBB_Perf.Take(); // a fresh window
   m_iFastBefore = BBB.s_iFastTicks;
   m_iContactSum = 0;
   m_iAliveSum = 0;
  }
  if (m_bWindow)
  {
   int seen;
   m_iAliveSum += Alive(seen);
   m_iContactSum += seen;
  }
  if (!m_bWindow || into < STEP_S) return;
  Measure();
  m_bWindow = false;
  m_iStep++;
  if (m_iStep < m_aTargets.Count()) { Grow(m_aTargets[m_iStep]); return; }
  FinishFid();
 }

 protected void Measure()
 {
  string stages = BBB_Perf.Take();
  float tickMs;
  float frameMs;
  string top = "";
  float topMs;
  array<string> tokens = {};
  stages.Split(" ", tokens, true);
  foreach (string token : tokens)
  {
   int eq = token.IndexOf("=");
   if (eq < 0) continue;
   string name = token.Substring(0, eq);
   string rest = token.Substring(eq + 1, token.Length() - eq - 1);
   if (name == "frame") { rest.Replace("ms", ""); frameMs = rest.ToFloat(); continue; }
   if (!name.StartsWith("tick.") || name.StartsWith("tick.vh.")) continue; // vh.* are parts of tick.vehicleHelpers
   int slash = rest.IndexOf("/");
   if (slash < 0) continue;
   float ms = rest.Substring(0, slash).ToFloat();
   tickMs += ms;
   if (ms > topMs) { topMs = ms; top = name; }
  }
  int target = m_aTargets[m_iStep];
  int contacts;
  int alive = Alive(contacts);
  int fast = BBB.s_iFastTicks - m_iFastBefore;
  int tierSet = BBB_Fidelity.s_iSet;
  int expected = tierSet;
  if (tierSet != BBB_Fidelity.STANDARD && BBB_Fidelity.s_bGovernor) expected = Math.Min(tierSet, BBB_Fidelity.Ceiling(target, 0));
  PrintFormat("[BBB] FIDTEST STEP n=%1 set=%2 governor=%3 tier=%4 counted=%5 alive=%6 contactShare=%7 tickMsPerS=%8 frameMs=%9", target, BBB_Fidelity.NameOf(tierSet), BBB_Fidelity.s_bGovernor, BBB_Fidelity.NameOf(BBB_Fidelity.s_iTier), BBB_Fidelity.s_iCombatants, alive, (m_iContactSum / Math.Max(1.0, m_iAliveSum)).ToString(4, 2), tickMs.ToString(5, 2), frameMs.ToString(4, 1));
  PrintFormat("[BBB] FIDTEST STAGES n=%1 top=%2 (%3) fastTicks=%4 |%5", target, top, topMs.ToString(4, 2), fast, stages);
  m_sSummary += string.Format(" n%1:%2:%3ms:%4fr", target, BBB_Fidelity.NameOf(BBB_Fidelity.s_iTier), tickMs.ToString(5, 2), frameMs.ToString(4, 1));
  CheckF(string.Format("n=%1: all spawned men alive (%2)", target, alive), alive >= target - 2 && alive <= target);
  // Contact averaged over the window (men with a current target per second).
  float share = m_iContactSum / Math.Max(1.0, m_iAliveSum);
  CheckF(string.Format("n=%1: half the men in contact over the window (%2)", target, share.ToString(4, 2)), share >= 0.5);
  CheckF(string.Format("n=%1: effective tier %2 (expected %3)", target, BBB_Fidelity.NameOf(BBB_Fidelity.s_iTier), BBB_Fidelity.NameOf(expected)), BBB_Fidelity.s_iTier == expected);
  // The governor counts only above Standard (Standard costs what it did).
  if (tierSet != BBB_Fidelity.STANDARD) CheckF(string.Format("n=%1: governor counted %2", target, BBB_Fidelity.s_iCombatants), BBB_Fidelity.s_iCombatants >= alive - 2 && BBB_Fidelity.s_iCombatants <= alive + 2);
  if (expected == BBB_Fidelity.STANDARD) CheckF(string.Format("n=%1: no fast lane at Standard (%2 runs)", target, fast), fast == 0);
  else CheckF(string.Format("n=%1: fast lane ran (%2 runs in %3 s)", target, fast, WINDOW_S), WINDOW_S * 1000 / BBB_Fidelity.At(BBB_Fidelity.FAST_LANE_MS, expected) * 0.5 <= fast);
 }

 protected void FinishFid()
 {
  GetGame().GetCallqueue().Remove(TickF);
  foreach (BBB_AimLane lane : m_LanesF) Clear(lane);
  m_LanesF.Clear();
  string verdict = "PASS";
  if (m_iFailF > 0 || m_iPassF == 0) verdict = "FAIL";
  PrintFormat("[BBB] FIDTEST RESULT set=%1 governor=%2 changes=%3 |%4", BBB_Fidelity.NameOf(BBB_Fidelity.s_iSet), BBB_Fidelity.s_bGovernor, BBB_Fidelity.s_iChanges, m_sSummary);
  PrintFormat("[ACR] VERDICT %1 test=bbbfidelity pass=%2 fail=%3", verdict, m_iPassF, m_iFailF);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_FidelityTest.MaybeStartFid();
 }
}
