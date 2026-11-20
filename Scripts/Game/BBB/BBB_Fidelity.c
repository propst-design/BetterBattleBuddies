// Fidelity tiers (BBB #24, Jacob 31 Oct; built 19 Nov 2026). BBB holds every feature to about 1 ms per
// tick so a 100-AI battle stays playable, which also means a 20-AI Game Master scenario gets the same
// slow re-evaluations and single rays. -bbbFidelity lets a small battle buy more:
//   Standard (0): today's constants, exactly. High (1) and Uncapped (2): shorter re-evaluation
//   intervals (the squad leader's look and cue, the squad picture, tactic looks, grenade / launcher /
//   static gun checks, true-cover verdicts), more groups served per tick, and more rays per test
//   (true-cover exposure across the torso, medic danger lines).
// Every costed knob is one row of the table below (Standard, High, Uncapped); features read their row
// through F()/I() instead of a constant. Standard's column must equal the constant it replaced.
// Combatant governor (-bbbFidelityGovernor, on): every GOVERN_EVERY ticks it counts the living AI
// soldiers BBB looks after; above -bbbFidelityUncappedMax the effective tier is at most High, above
// -bbbFidelityHighMax at most Standard; it steps back up only once the count is GOVERN_MARGIN below
// the line. A host may cap the tier too (BBB_Hooks.MaxFidelity, default: no cap).
// Hard rules don't move with the tier: fidelity buys precision and speed of BBB's own re-evaluation,
// never knowledge (no target, aim point or position a soldier didn't perceive), and no knob changes
// a behaviour's odds (cooldowns, chances, priorities and durations stay where they are).
class BBB_Fidelity
{
 static const int STANDARD = 0;
 static const int HIGH = 1;
 static const int UNCAPPED = 2;

 // The knobs, in the table's row order (BBB_InitStatics).
 static const int FAST_LANE_MS = 0;      // BBB's fast lane (BBB.FastTick) period; 0 = none
 static const int SQUAD_REFRESH_S = 1;   // BBB_Squad picture lifetime
 static const int LEADER_LOOK_S = 2;     // BBB_SquadLeader look per squad (cue, fire split, switches)
 static const int LEADER_CUE_S = 3;      // BBB_SquadLeader: a cued man is cued again after this
 static const int COVER_RECHECK_MS = 4;  // BBB_Cover verdict lifetime
 static const int COVER_RAYS = 5;        // BBB_Cover rays across the torso (majority must hit cover)
 static const int TACTIC_LOOK_S = 6;     // BBB_Tactic: next look at a squad in contact
 static const int TACTIC_STAGGER = 7;    // BBB_Tactic: squads not in contact, one in N per tick
 static const int RELAY_STAGGER = 8;     // BBB_ContactRelay: each group sends every Nth tick
 static const int GRENADE_CHECK_MS = 9;  // BBB_Grenades per-group look
 static const int LAUNCHER_CHECK_MS = 10; // BBB_LauncherUse: launcher-on-infantry look per man
 static const int LAUNCHERS_CHECK_MS = 11; // BBB_Launchers: tube and loadout pass
 static const int STATIC_CHECK_MS = 12;  // BBB_StaticWeapons per-group look
 static const int FM_ADMISSIONS = 13;    // BBB_FireManeuver: groups admitted per tick
 static const int PAIR_GROUPS = 14;      // BBB_Teams: groups paired per tick
 static const int HELDGUNS_EVERY = 15;   // BBB_HeldGuns: dry-gunner look every Nth tick
 static const int MEDIC_TRACES = 16;     // BBB_Medic: danger lines checked per casualty
 static const int KNOBS = 17;

 static const int GOVERN_EVERY = 5;      // ticks between combatant counts
 static const int GOVERN_MARGIN = 5;     // combatants below a line before the tier steps back up

 protected static ref array<float> s_aTable;   // KNOBS rows of (Standard, High, Uncapped)
 protected static ref array<string> s_aNames;
 protected static int s_iRead;
 protected static int s_iTicks;
 static int s_iSet;               // the tier the setting asks for
 static int s_iTier;              // the effective tier (setting, host cap, governor)
 static int s_iCeiling = 2;       // the governor's ceiling
 static int s_iCombatants = -1;   // at the last count
 static bool s_bGovernor = true;
 static int s_iUncappedMax = 30;
 static int s_iHighMax = 60;
 static int s_iChanges;           // effective tier changes this mission (tests read it)

 // Called by BBB_StaticInit at game start (static initializer budget: tools/check_static_init.py).
 static void BBB_InitStatics()
 {
  s_aTable = {};
  s_aNames = {};
  //  knob                 Standard  High  Uncapped
  Row("fastLaneMs",          0,     500,   250);
  Row("squadRefreshS",       1,     0.5,   0.25);
  Row("leaderLookS",         1,     0.5,   0.25);
  Row("leaderCueS",          3,     2,     1);
  Row("coverRecheckMs",      1000,  500,   250);
  Row("coverRays",           1,     3,     5);
  Row("tacticLookS",         2,     1,     1);
  Row("tacticStagger",       4,     2,     1);
  Row("relayStagger",        4,     2,     1);
  Row("grenadeCheckMs",      2000,  1000,  1000);
  Row("launcherCheckMs",     1500,  1000,  500);
  Row("launchersCheckMs",    2000,  1000,  1000);
  Row("staticCheckMs",       2000,  1000,  1000);
  Row("fmAdmissions",        4,     8,     16);
  Row("pairGroups",          2,     4,     8);
  Row("heldGunsEvery",       2,     1,     1);
  Row("medicTraces",         6,     10,    16);
 }

 protected static void Row(string name, float standard, float high, float uncapped)
 {
  s_aNames.Insert(name);
  s_aTable.Insert(standard);
  s_aTable.Insert(high);
  s_aTable.Insert(uncapped);
 }

 // A knob at the effective tier.
 static float F(int knob)
 {
  if (!s_aTable || knob < 0 || knob >= KNOBS) return 0;
  return s_aTable[knob * 3 + s_iTier];
 }

 static int I(int knob)
 {
  return Math.Round(F(knob));
 }

 // A knob at a given tier (tests, the Think panel).
 static float At(int knob, int tier)
 {
  if (!s_aTable || knob < 0 || knob >= KNOBS || tier < STANDARD || tier > UNCAPPED) return 0;
  return s_aTable[knob * 3 + tier];
 }

 static int Rows()
 {
  if (!s_aNames) return 0;
  return s_aNames.Count();
 }

 static string NameOf(int tier)
 {
  if (tier == HIGH) return "High";
  if (tier == UNCAPPED) return "Uncapped";
  return "Standard";
 }

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_iSet = STANDARD;
  if (BBB_Settings.Get("bbbFidelity", value)) s_iSet = Math.ClampInt(value.ToInt(), STANDARD, UNCAPPED);
  if (BBB_Settings.Get("bbbFidelityGovernor", value)) s_bGovernor = value == "1";
  if (BBB_Settings.Get("bbbFidelityUncappedMax", value)) s_iUncappedMax = Math.Max(1, value.ToInt());
  if (BBB_Settings.Get("bbbFidelityHighMax", value)) s_iHighMax = Math.Max(1, value.ToInt());
  s_iCeiling = UNCAPPED;
  s_iCombatants = -1;
  s_iTicks = 0;
  s_iChanges = 0;
  PrintFormat("[BBB] FIDELITY set=%1 governor=%2 (Uncapped up to %3 combatants, High up to %4)", NameOf(s_iSet), s_bGovernor, s_iUncappedMax, s_iHighMax);
  Apply("mission start");
 }

 // From BBB.Tick, once a second: the governor's count every GOVERN_EVERY ticks.
 static void Tick()
 {
  Read();
  if (s_iSet == STANDARD) return; // nothing to step down from: no count either (Standard costs what it did)
  if (s_iTicks++ % GOVERN_EVERY != 0) return;
  s_iCombatants = Count();
  if (s_bGovernor)
  {
   int down = Ceiling(s_iCombatants, 0);
   if (down < s_iCeiling) s_iCeiling = down;
   else
   {
    int up = Ceiling(s_iCombatants, GOVERN_MARGIN);
    if (up > s_iCeiling) s_iCeiling = up;
   }
  }
  Apply("governor");
 }

 // The highest tier this many combatants allow; margin: how far below a line they must be.
 static int Ceiling(int combatants, int margin)
 {
  if (combatants > s_iHighMax - margin) return STANDARD;
  if (combatants > s_iUncappedMax - margin) return HIGH;
  return UNCAPPED;
 }

 // Living AI soldiers in the groups BBB looks after (players and their own bodies don't count).
 protected static int Count()
 {
  array<SCR_AIGroup> groups = {};
  BBB.Groups(groups);
  array<AIAgent> agents = {};
  int count;
  foreach (SCR_AIGroup group : groups)
  {
   if (!group) continue;
   BBB.Members(group, agents);
   foreach (AIAgent agent : agents)
   {
    if (!agent) continue;
    IEntity man = agent.GetControlledEntity();
    if (BBB.IsAlive(man) && BBB.PlayerOf(man) == 0) count++;
   }
  }
  return count;
 }

 protected static void Apply(string why)
 {
  int tier = s_iSet;
  if (s_bGovernor && s_iCeiling < tier) tier = s_iCeiling;
  if (BBB.s_Hooks)
  {
   int cap = BBB.s_Hooks.MaxFidelity();
   if (cap >= STANDARD && cap < tier) tier = cap;
  }
  if (tier == s_iTier) return;
  s_iTier = tier;
  s_iChanges++;
  PrintFormat("[BBB] FIDELITY tier=%1 (set %2, combatants %3, %4)", NameOf(s_iTier), NameOf(s_iSet), s_iCombatants, why);
  BBB.FidelityChanged();
 }

 static void Stop()
 {
  s_iTier = STANDARD;
  s_iCeiling = UNCAPPED;
  s_iCombatants = -1;
  s_iRead = -1;
 }
}
