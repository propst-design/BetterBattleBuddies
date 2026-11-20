class BBB_Stage4TestHooks : BBB_Hooks
{
 ref array<SCR_AIGroup> m_Groups = {};
 override void GetGroups(notnull array<SCR_AIGroup> groups) { groups.Copy(m_Groups); }
}

// Squad brain stage 4 A/B (-bbbStage4Test, -bbbStage4Case fire|flank|search). Two identical US
// fire teams side by side; nobody can be hurt; the enemies are passive and can't move.
//  fire:   four enemy teams 70 m north with two sheds beside and behind them, so both teams HOLD.
//          A keeps a base of fire, B holds without one (BBB_TacticDrill.s_aNoFire). Once both
//          hold the enemy pops up 2 s in every 8 (a fresh report, but mostly hidden). Over the
//          next 60 s A's bursts must be aimed at the sheds, not the bare report (BBB_CoverFire,
//          shared with fire and manoeuvre's cover orders). Shots in bursts and A vs B shots are
//          printed, not judged: bursts ended within a second and team noise swamped the effect,
//          so the HOLD base of fire is opt-in.
//  flank:  one enemy team 150 m north. A on the tactic assaults (its fire fixes the enemy), then
//          FLANKs with half the team, and within 120 s its farthest man stands at least 35 deg
//          round the enemy from where the team started and 15 deg more than B's (old rules).
//  search: one enemy team 100 m north with two sheds near it; once A has had contact for 15 s
//          the enemy team is removed (it slipped away). A must SEARCH and reach at least 2
//          search points within 100 s, one more than B comes near.
// Buildings, StaticWeapons, Morale, throws, rearm runs and relay are off.
class BBB_Stage4Test : BBB_AimTest
{
 static ref BBB_Stage4Test s_Stage4;
 // Cover for the enemy to be behind (fire and search cases): the shed the reaction test's site has.
 static const ResourceName SHED = "{B952B23CBCF785AB}Prefabs/Structures/Infrastructure/Naval/Sheds/FishermanShed_01/FishermanShed_01_V2.et";
 ref BBB_Stage4TestHooks m_Hooks;
 ref BBB_Hooks m_SavedHooks;
 ref array<SCR_AIGroup> m_Teams = {};
 ref array<SCR_AIGroup> m_Foes = {};
 string m_sCase;
 int m_iExpected;
 int m_Checks;
 int m_Failures;
 int m_Wait;
 int m_iMark = -1;
 vector m_vFoe;
 ref array<vector> m_vStart = {};
 ref array<float> m_fBestAngle = {};
 bool m_bSawTactic;
 ref array<vector> m_aPoints = {};
 int m_iBNear;
 ref array<int> m_iShots = {};
 ref map<IEntity, int> m_mInGun = new map<IEntity, int>();
 int m_iBurstShots;   // diagnostic: A's shots while in a base-of-fire burst
 int m_iBurstSeconds; // ...and man-seconds spent in one

 static void Start()
 {
  if (!Replication.IsServer() || !System.IsCLIParam("bbbStage4Test")) return;
  s_Stage4 = new BBB_Stage4Test();
  System.GetCLIParam("bbbStage4Case", s_Stage4.m_sCase);
  GetGame().GetCallqueue().CallLater(s_Stage4.Setup, 15000);
 }

 void S4Check(string name, bool ok, string detail = "")
 {
  m_Checks++;
  if (!ok) m_Failures++;
  PrintFormat("[BBB] STAGE4TEST %1 %2 ok=%3 %4", m_sCase, name, ok, detail);
 }

 void Setup()
 {
  BBB_Tactic.Read();
  BBB_CoverFire.Read();
  S4Check("defaults", BBB_Tactic.s_bOn && BBB_Tactic.s_bFlank && BBB_Tactic.s_bSearch && !BBB_Tactic.s_bFire && BBB_CoverFire.s_bOn);
  if (m_sCase == "fire") BBB_Tactic.s_bFire = true; // opt-in feature under test
  BBB_StaticWeapons.s_bOn = false;
  BBB_Buildings.s_bOn = false;
  BBB_Morale.s_bOn = false;
  BBB_Grenades.s_bOn = false;
  BBB_Smoke.s_bOn = false;
  BBB_Flares.s_bOn = false;
  BBB_ContactRelay.s_bOn = false;
  BBB_Rearm.s_bOn = false;
  m_SavedHooks = BBB.s_Hooks;
  m_Hooks = new BBB_Stage4TestHooks();
  BBB.s_Hooks = m_Hooks;
  vector centre = BBB.Ground("1340 0 2770");
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers) observers.InsertObserverSP(22149, centre[0], centre[2], null);
  bool made = true;
  for (int t = 0; t < 2; t++)
  {
   SCR_AIGroup team = SCR_AIGroup.Cast(Spawn(TEAM_US, centre + Vector(-20 + t * 40, 0, 0), "0 0 1"));
   made = made && team;
   m_Teams.Insert(team);
   m_vStart.Insert(vector.Zero);
   m_iShots.Insert(0);
   m_fBestAngle.Insert(0);
  }
  int count = 1;
  float ahead = 150;
  float spacing = 25;
  if (m_sCase == "fire") { count = 4; ahead = 70; spacing = 12; }
  if (m_sCase == "search") ahead = 100;
  for (int f = 0; f < count; f++)
  {
   SCR_AIGroup foe = SCR_AIGroup.Cast(Spawn(TEAM_USSR, centre + Vector((f - (count - 1) * 0.5) * spacing, 0, ahead), "0 0 -1"));
   made = made && foe;
   m_Foes.Insert(foe);
  }
  // Sheds beside and behind the enemy: real cover near where they are seen.
  if (m_sCase != "flank")
  {
   for (int c = -1; c <= 1; c += 2) made = made && Spawn(SHED, centre + Vector(c * 22, 0, ahead + 8), "0 0 1");
  }
  m_iExpected = 5;
  if (m_sCase == "flank" || m_sCase == "fire") m_iExpected = 4;
  S4Check("groups", made);
  if (!made || (m_sCase != "fire" && m_sCase != "flank" && m_sCase != "search")) { S4Finish(); return; }
  if (m_sCase == "fire") BBB_TacticDrill.s_aNoFire.Insert(m_Teams[1]);
  else BBB_Tactic.s_aVanilla.Insert(m_Teams[1]);
  m_Hooks.m_Groups.Insert(m_Teams[0]);
  m_Hooks.m_Groups.Insert(m_Teams[1]);
  GetGame().GetCallqueue().CallLater(Prepare, 4000);
 }

 void PrepareGroup(SCR_AIGroup group, bool enemy)
 {
  PinDetail(group);
  group.ActivateAI();
  EAIGroupCombatMode mode = EAIGroupCombatMode.FIRE_AT_WILL;
  if (enemy) mode = EAIGroupCombatMode.HOLD_FIRE;
  group.GetGroupUtilityComponent().SetCombatMode(mode);
  group.GetGroupUtilityComponent().EvaluateCombatMode();
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   PinDetail(agent);
   agent.ActivateAI();
   ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!body) continue;
   body.GetDamageManager().EnableDamageHandling(false);
   if (!enemy) continue;
   body.GetCharacterController().SetDisableWeaponControls(true);
   body.GetCharacterController().SetDisableMovementControls(true);
  }
 }

 void Prepare()
 {
  foreach (SCR_AIGroup team : m_Teams) PrepareGroup(team, false);
  foreach (SCR_AIGroup foe : m_Foes) PrepareGroup(foe, true);
  vector sum;
  foreach (SCR_AIGroup foe : m_Foes) { vector at; Centre(foe, at); sum = sum + at; }
  m_vFoe = sum * (1.0 / m_Foes.Count());
  for (int t = 0; t < 2; t++) { vector start; Centre(m_Teams[t], start); m_vStart[t] = start; }
  GetGame().GetCallqueue().CallLater(Watch, 1000, true);
 }

 bool Centre(SCR_AIGroup group, out vector centre)
 {
  centre = vector.Zero;
  if (!group) return false;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  int count;
  foreach (AIAgent agent : agents)
  {
   IEntity man = agent.GetControlledEntity();
   if (!man || !BBB.IsAlive(man)) continue;
   centre = centre + man.GetOrigin();
   count++;
  }
  if (count == 0) return false;
  centre = centre * (1.0 / count);
  return true;
 }

 // Shots a team fired since the last sample: drops in each loaded magazine (a reload resets).
 int Shots(SCR_AIGroup group)
 {
  int shots;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   IEntity man = agent.GetControlledEntity();
   if (!man) continue;
   BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(man.FindComponent(BaseWeaponManagerComponent));
   if (!weapons || !weapons.GetCurrentWeapon() || !weapons.GetCurrentWeapon().GetCurrentMagazine()) continue;
   int inGun = weapons.GetCurrentWeapon().GetCurrentMagazine().GetAmmoCount();
   int fired;
   if (m_mInGun.Contains(man) && inGun < m_mInGun.Get(man)) fired = m_mInGun.Get(man) - inGun;
   shots += fired;
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   if (utility && BBB_AISuppressCoverBehavior.Cast(utility.GetCurrentAction())) { m_iBurstShots += fired; m_iBurstSeconds++; }
   m_mInGun.Set(man, inGun);
  }
  return shots;
 }

 void Duck(bool down)
 {
  ECharacterStance stance = ECharacterStance.STAND;
  if (down) stance = ECharacterStance.PRONE;
  foreach (SCR_AIGroup foe : m_Foes)
  {
   array<AIAgent> agents = {};
   foe.GetAgents(agents);
   foreach (AIAgent agent : agents)
   {
    ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (body && body.GetCharacterController()) SCR_AIStanceHandling.SetStance(body.GetCharacterController(), stance);
   }
  }
 }

 float Angle(int t)
 {
  vector now;
  Centre(m_Teams[t], now);
  vector a = m_vStart[t] - m_vFoe;
  vector b = now - m_vFoe;
  return BBB_Squad.AngleBetween(Math.Atan2(a[0], a[2]) * Math.RAD2DEG, Math.Atan2(b[0], b[2]) * Math.RAD2DEG);
 }

 // The farthest any living man of team t stands round the enemy from where the team started.
 float ManAngle(int t)
 {
  vector a = m_vStart[t] - m_vFoe;
  float from = Math.Atan2(a[0], a[2]) * Math.RAD2DEG;
  float best;
  array<AIAgent> agents = {};
  m_Teams[t].GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   IEntity man = agent.GetControlledEntity();
   if (!man || !BBB.IsAlive(man)) continue;
   vector b = man.GetOrigin() - m_vFoe;
   best = Math.Max(best, BBB_Squad.AngleBetween(from, Math.Atan2(b[0], b[2]) * Math.RAD2DEG));
  }
  return best;
 }

 void Watch()
 {
  m_Wait++;
  int a = BBB_Tactic.TacticOf(m_Teams[0]);
  if (m_Wait % 5 == 0) PrintFormat("[BBB] STAGE4TEST %1 t=%2 A %3 B %4 | bursts %5 on cover %6 on report %7 refused %8 runs %9", m_sCase, m_Wait, BBB_Tactic.Describe(m_Teams[0]), BBB_Tactic.Describe(m_Teams[1]), BBB_TacticDrill.s_iBursts, BBB_CoverFire.s_iOnCover, BBB_CoverFire.s_iOnReport, BBB_CoverFire.s_iRefused, BBB_TacticDrill.s_iRuns);
  if (m_sCase == "fire") WatchFire(a);
  else if (m_sCase == "flank") WatchFlank(a);
  else WatchSearch(a);
 }

 void WatchFire(int a)
 {
  int b = BBB_Tactic.TacticOf(m_Teams[1]);
  if (m_iMark < 0 && a == BBB_Tactic.HOLD && b == BBB_Tactic.HOLD)
  {
   m_iMark = m_Wait;
   Shots(m_Teams[0]);
   Shots(m_Teams[1]);
  }
  if (m_iMark >= 0 && m_Wait > m_iMark)
  {
   m_iShots[0] = m_iShots[0] + Shots(m_Teams[0]);
   m_iShots[1] = m_iShots[1] + Shots(m_Teams[1]);
  }
  // The enemy ducks: up 2 s, prone 6 s (a real report every 8 s, but mostly out of sight).
  if (m_iMark >= 0) Duck((m_Wait - m_iMark) % 8 >= 2);
  if (m_iMark >= 0 && m_Wait - m_iMark < 60) return;
  if (m_iMark < 0 && m_Wait < 120) return;
  GetGame().GetCallqueue().Remove(Watch);
  S4Check("both-hold", m_iMark >= 0, string.Format("at t=%1", m_iMark));
  int firedA = m_iShots[0];
  int firedB = m_iShots[1];
  S4Check("bursts-on-the-cover", BBB_TacticDrill.s_iBursts > 0 && BBB_CoverFire.s_iOnCover > 0 && BBB_CoverFire.s_iOnReport == 0, string.Format("%1 bursts, %2 aims on cover, %3 on the bare report, %4 refused", BBB_TacticDrill.s_iBursts, BBB_CoverFire.s_iOnCover, BBB_CoverFire.s_iOnReport, BBB_CoverFire.s_iRefused));
  // Not judged (why the HOLD base of fire is opt-in): bursts end within a second here, and
  // team-to-team noise swamps any effect on shots fired.
  PrintFormat("[BBB] STAGE4TEST fire reference: %1 shots in %2 man-seconds of bursts; A %3 shots, B (no base of fire) %4", m_iBurstShots, m_iBurstSeconds, firedA, firedB);
  S4Finish();
 }

 void WatchFlank(int a)
 {
  if (a == BBB_Tactic.FLANK && m_iMark < 0) m_iMark = m_Wait;
  // Since 19 Nov only half the team goes round (the rest stay as the base of fire), so the
  // team centre moves half as far: judge the man farthest round, for A and B alike.
  for (int t = 0; t < 2; t++) m_fBestAngle[t] = Math.Max(m_fBestAngle[t], ManAngle(t));
  if (m_iMark >= 0 && m_Wait - m_iMark < 120 && a == BBB_Tactic.FLANK) return;
  if (m_iMark < 0 && m_Wait < 100) return;
  if (m_iMark >= 0 && a != BBB_Tactic.FLANK && m_Wait - m_iMark < 20) return;
  GetGame().GetCallqueue().Remove(Watch);
  S4Check("A-flanks", m_iMark >= 0, string.Format("at t=%1, now %2", m_iMark, BBB_Tactic.Describe(m_Teams[0])));
  S4Check("A-went-round", m_fBestAngle[0] >= 35 && m_fBestAngle[0] >= m_fBestAngle[1] + 15, string.Format("A %1 deg round the enemy, B %2 deg", Math.Round(m_fBestAngle[0]), Math.Round(m_fBestAngle[1])));
  S4Finish();
 }

 void WatchSearch(int a)
 {
  if (m_iMark < 0 && a != BBB_Tactic.NONE && a != BBB_Tactic.CONTACT && m_Wait >= 15)
  {
   // The enemy slips away.
   m_iMark = m_Wait;
   foreach (SCR_AIGroup foe : m_Foes)
   {
    array<AIAgent> agents = {};
    foe.GetAgents(agents);
    foreach (AIAgent agent : agents) SCR_EntityHelper.DeleteEntityAndChildren(agent.GetControlledEntity());
   }
   PrintFormat("[BBB] STAGE4TEST search: enemy removed at t=%1, A %2", m_Wait, BBB_Tactic.Describe(m_Teams[0]));
  }
  if (a == BBB_Tactic.SEARCH)
  {
   m_bSawTactic = true;
   BBB_TacticState state = BBB_Tactic.StateOf(m_Teams[0]);
   if (state) foreach (vector p : state.m_aPoints) if (!m_aPoints.Contains(p)) m_aPoints.Insert(p);
  }
  // Points B comes within 5 m of (any of A's search points).
  vector bAt;
  if (Centre(m_Teams[1], bAt)) foreach (vector point : m_aPoints) if (vector.DistanceXZ(bAt, point) <= 5) m_iBNear = Math.Max(m_iBNear, 1);
  if (m_iMark < 0 && m_Wait < 100) return;
  if (m_iMark >= 0 && m_Wait - m_iMark < 100) return;
  GetGame().GetCallqueue().Remove(Watch);
  S4Check("enemy-gone", m_iMark >= 0, string.Format("at t=%1", m_iMark));
  S4Check("A-searches", m_bSawTactic, string.Format("%1 points", m_aPoints.Count()));
  S4Check("A-reached-2-points", BBB_TacticDrill.s_iPointsDone >= 2 && BBB_TacticDrill.s_iPointsDone >= m_iBNear + 1, string.Format("A reached %1, B came near %2", BBB_TacticDrill.s_iPointsDone, m_iBNear));
  S4Finish();
 }

 void S4Finish()
 {
  if (m_SavedHooks) BBB.s_Hooks = m_SavedHooks;
  string verdict = "FAIL";
  if (m_Failures == 0 && m_Checks == m_iExpected) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbstage4-%2 passed=%3 failed=%4 expected=%5", verdict, m_sCase, m_Checks - m_Failures, m_Failures, m_iExpected);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 override protected void OnGameStart()
 {
  super.OnGameStart();
  BBB_Stage4Test.Start();
 }
}
