// Fire discipline in a real fight (-bbbFireDisciplineTest, 6 Nov 2026). Jacob's scene: two squads
// slugging it out at 200 m, spraying, achieving little for most of their ammo. A US rifle squad and a
// USSR rifle squad each defend their own spot -bbbFireDisciplineRange metres apart (default 200) for
// FIGHT_S; everyone can be hurt and shoots back; every BBB feature runs as set by the launch
// parameters. A/B: run once as is (discipline on) and once with -bbbFireDiscipline 0; the lane name
// says which. One fight is noise: the RESULT line is for adding up across runs, not a verdict.
// Measured (physical): rounds that left each side's magazines, men down on each side, man-seconds in
// cover (vanilla's cover flag), how far the men moved from where they started, how many men of one
// squad were executing a blind burst at the same moment, and the blind bursts started.
// Judged: fixture validity (squads spawned, contact within 60 s, both sides fired); with discipline
// on also that no squad ever had more than SQUAD_BLIND + 1 men firing blind at once and that no blind
// burst started beyond machine-gun range.
class BBB_FireDisciplineMan : Managed
{
 IEntity m_Man;
 vector m_vStart;
 BaseMagazineComponent m_Magazine;
 int m_iLast;
 float m_fFarthest;
}

class BBB_FireDisciplineSide : Managed
{
 string m_sName;
 SCR_AIGroup m_Group;
 ref array<ref BBB_FireDisciplineMan> m_aMen = {};
 int m_iRounds;
 int m_iGunRounds;	// of which machine guns
 int m_iCoverS;
 int m_iMostBlind;
}

class BBB_FireDisciplineTest : BBB_AimTest
{
 static ref BBB_FireDisciplineTest s_Discipline;
 static const ResourceName SQUAD_US = "{DDF3799FA1387848}Prefabs/Groups/BLUFOR/Group_US_RifleSquad.et";
 static const ResourceName SQUAD_USSR = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const ResourceName WP_DEFEND = "{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et";
 static const int FIGHT_S = 150;
 static const int OBSERVER = 22311;
 ref BBB_FireDisciplineSide m_Us;
 ref BBB_FireDisciplineSide m_Ru;
 float m_fRange = 200;
 bool m_bOn;
 int m_iT;
 int m_iContactAt = -1;
 int m_iChecks;
 int m_iFailures;
 int m_iExpected;
 int m_iStarted0, m_iWaited0, m_iCluster0, m_iResponses0, m_iReturn0;

 static void Start()
 {
  if (!Replication.IsServer() || !System.IsCLIParam("bbbFireDisciplineTest")) return;
  s_Discipline = new BBB_FireDisciplineTest();
  GetGame().GetCallqueue().CallLater(s_Discipline.Setup, 15000);
 }

 void Judge(string name, bool ok, string detail = "")
 {
  m_iChecks++;
  if (!ok) m_iFailures++;
  PrintFormat("[BBB] FIREDISCIPLINETEST %1 ok=%2 %3", name, ok, detail);
 }

 void Setup()
 {
  string value;
  if (System.GetCLIParam("bbbFireDisciplineRange", value) && value.ToFloat() > 0) m_fRange = value.ToFloat();
  m_bOn = BBB_FireDiscipline.On();
  BBB_ReturnFire.Read();
  m_iExpected = 3;
  if (m_bOn) m_iExpected = 5;
  // The first lane on the map with crouched line of sight at half and full range (BBB_AimTest.FindSite),
  // so both squads see each other as in the fight this is about. The assault test's site (1340,2770,
  // north) gave first contact at 90 s at 200 m, one-sided, and none at 400 m. Fallback: that site.
  vector from = BBB.Ground(Vector(1340, 0, 2770));
  vector dir = "0 0 1";
  m_aCheckDistances.Clear();
  m_aCheckDistances.Insert(m_fRange * 0.5);
  m_aCheckDistances.Insert(m_fRange);
  if (FindSite())
  {
   from = BBB.Ground(m_aSiteFrom[0]);
   dir = m_aSiteDir[0];
   LoadLane();
  }
  vector to = BBB.Ground(from + dir * m_fRange);
  vector mid = from + dir * (m_fRange * 0.5);
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers) observers.InsertObserverSP(OBSERVER, mid[0], mid[2], null);
  m_Us = new BBB_FireDisciplineSide();
  m_Us.m_sName = "US";
  m_Us.m_Group = SCR_AIGroup.Cast(Spawn(SQUAD_US, from, dir));
  m_Ru = new BBB_FireDisciplineSide();
  m_Ru.m_sName = "USSR";
  m_Ru.m_Group = SCR_AIGroup.Cast(Spawn(SQUAD_USSR, to, dir * -1));
  string lane = "B (old rules)";
  if (m_bOn) lane = "A (discipline)";
  PrintFormat("[BBB] FIREDISCIPLINETEST lane %1 range %2 m, US at %3, USSR at %4, return fire %5", lane, m_fRange, from, to, BBB_ReturnFire.s_bOn);
  bool made = m_Us.m_Group && m_Ru.m_Group;
  Judge("groups", made);
  if (!made) { FinishDiscipline(); return; }
  GetGame().GetCallqueue().CallLater(Prepare, 4000);
 }

 void PrepareSide(BBB_FireDisciplineSide side)
 {
  SCR_AIGroup group = side.m_Group;
  PinDetail(group);
  group.ActivateAI();
  group.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.FIRE_AT_WILL);
  group.GetGroupUtilityComponent().EvaluateCombatMode();
  AIWaypoint wp = AIWaypoint.Cast(Spawn(WP_DEFEND, group.GetOrigin(), "0 0 1"));
  if (wp) group.AddWaypoint(wp);
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   PinDetail(agent);
   agent.ActivateAI();
   IEntity man = agent.GetControlledEntity();
   if (!man) continue;
   BBB_FireDisciplineMan entry = new BBB_FireDisciplineMan();
   entry.m_Man = man;
   entry.m_vStart = man.GetOrigin();
   side.m_aMen.Insert(entry);
  }
 }

 void Prepare()
 {
  PrepareSide(m_Us);
  PrepareSide(m_Ru);
  m_iStarted0 = BBB_FireDiscipline.s_iStarted;
  m_iWaited0 = BBB_FireDiscipline.s_iWaited;
  m_iCluster0 = BBB_FireDiscipline.s_iClusterHeld;
  m_iResponses0 = BBB_Combat.s_iResponses;
  m_iReturn0 = BBB_ReturnFire.s_iFires;
  BBB_FireDiscipline.s_fFarthest = 0;
  PrintFormat("[BBB] FIREDISCIPLINETEST %1 US, %2 USSR", m_Us.m_aMen.Count(), m_Ru.m_aMen.Count());
  GetGame().GetCallqueue().CallLater(Watch, 1000, true);
 }

 // Rounds that left his loaded magazine since the last look (a reload starts a new count).
 void SampleRounds(BBB_FireDisciplineSide side, BBB_FireDisciplineMan entry)
 {
  if (!entry.m_Man) return;
  BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(entry.m_Man.FindComponent(BaseWeaponManagerComponent));
  if (!weapons || !weapons.GetCurrentWeapon()) return;
  BaseMagazineComponent magazine = weapons.GetCurrentWeapon().GetCurrentMagazine();
  if (!magazine) return;
  int rounds = magazine.GetAmmoCount();
  if (magazine == entry.m_Magazine && rounds < entry.m_iLast)
  {
   side.m_iRounds += entry.m_iLast - rounds;
   if (BBB_FireDiscipline.MachineGun(weapons.GetCurrentWeapon())) side.m_iGunRounds += entry.m_iLast - rounds;
  }
  entry.m_Magazine = magazine;
  entry.m_iLast = rounds;
 }

 SCR_AIUtilityComponent UtilityOf(IEntity man)
 {
  AIControlComponent control = AIControlComponent.Cast(man.FindComponent(AIControlComponent));
  if (!control || !control.GetAIAgent()) return null;
  return SCR_AIUtilityComponent.Cast(control.GetAIAgent().FindComponent(SCR_AIUtilityComponent));
 }

 void SampleSide(BBB_FireDisciplineSide side)
 {
  int blind;
  foreach (BBB_FireDisciplineMan entry : side.m_aMen)
  {
   SampleRounds(side, entry);
   if (Down(entry.m_Man)) continue;
   entry.m_fFarthest = Math.Max(entry.m_fFarthest, vector.DistanceXZ(entry.m_vStart, entry.m_Man.GetOrigin()));
   SCR_AIUtilityComponent utility = UtilityOf(entry.m_Man);
   if (!utility) continue;
   if (utility.m_CombatMoveState && utility.m_CombatMoveState.m_bInCover) side.m_iCoverS++;
   if (BBB_AIContactFireBehavior.Cast(utility.GetCurrentBehavior()) || BBB_AIReturnFireBehavior.Cast(utility.GetCurrentBehavior())) blind++;
  }
  side.m_iMostBlind = Math.Max(side.m_iMostBlind, blind);
 }

 int DownOf(BBB_FireDisciplineSide side)
 {
  int count;
  foreach (BBB_FireDisciplineMan entry : side.m_aMen)
   if (Down(entry.m_Man)) count++;
  return count;
 }

 float MovedOf(BBB_FireDisciplineSide side)
 {
  if (side.m_aMen.IsEmpty()) return 0;
  float total;
  foreach (BBB_FireDisciplineMan entry : side.m_aMen) total += entry.m_fFarthest;
  return total / side.m_aMen.Count();
 }

 void Watch()
 {
  m_iT++;
  SampleSide(m_Us);
  SampleSide(m_Ru);
  vector seen;
  if (m_iContactAt < 0 && BBB_Squad.Threat(m_Us.m_Group, BBB_Squad.RECENT_S, seen) && BBB_Squad.Threat(m_Ru.m_Group, BBB_Squad.RECENT_S, seen)) m_iContactAt = m_iT;
  if (m_iT % 15 == 0) PrintFormat("[BBB] FIREDISCIPLINETEST t=%1 rounds US %2 USSR %3, down US %4 USSR %5, blind bursts %6", m_iT, m_Us.m_iRounds, m_Ru.m_iRounds, DownOf(m_Us), DownOf(m_Ru), BBB_Combat.s_iResponses - m_iResponses0 + BBB_ReturnFire.s_iFires - m_iReturn0);
  bool wiped = DownOf(m_Us) == m_Us.m_aMen.Count() || DownOf(m_Ru) == m_Ru.m_aMen.Count();
  if (m_iT < FIGHT_S && !wiped) return;
  GetGame().GetCallqueue().Remove(Watch);
  int usDown = DownOf(m_Us);
  int ruDown = DownOf(m_Ru);
  int rounds = m_Us.m_iRounds + m_Ru.m_iRounds;
  int down = usDown + ruDown;
  float perDown = rounds;
  if (down > 0) perDown = perDown / down;
  int blindBursts = BBB_Combat.s_iResponses - m_iResponses0 + BBB_ReturnFire.s_iFires - m_iReturn0;
  string lane = "B";
  if (m_bOn) lane = "A";
  PrintFormat("[BBB] FIREDISCIPLINETEST RESULT lane=%1 range=%2 t=%3 contact=%4 roundsUS=%5 roundsRU=%6 rounds=%7 downUS=%8/%9", lane, m_fRange, m_iT, m_iContactAt, m_Us.m_iRounds, m_Ru.m_iRounds, rounds, usDown, m_Us.m_aMen.Count());
  PrintFormat("[BBB] FIREDISCIPLINETEST RESULT2 lane=%1 downRU=%2/%3 roundsPerDown=%4 blindBursts=%5 waitedForCover=%6 clusterHeld=%7 coverS=%8 movedUS=%9", lane, ruDown, m_Ru.m_aMen.Count(), Math.Round(perDown), blindBursts, BBB_FireDiscipline.s_iWaited - m_iWaited0, BBB_FireDiscipline.s_iClusterHeld - m_iCluster0, m_Us.m_iCoverS + m_Ru.m_iCoverS, Math.Round(MovedOf(m_Us)));
  PrintFormat("[BBB] FIREDISCIPLINETEST RESULT3 lane=%1 movedRU=%2 mostBlindUS=%3 mostBlindRU=%4 farthestBlind=%5 refusedRange=%6 refusedSquad=%7 refusedAmmo=%8 mgUS=%9", lane, Math.Round(MovedOf(m_Ru)), m_Us.m_iMostBlind, m_Ru.m_iMostBlind, Math.Round(BBB_FireDiscipline.s_fFarthest), BBB_FireDiscipline.s_iRange, BBB_FireDiscipline.s_iSquad, BBB_FireDiscipline.s_iAmmo, m_Us.m_iGunRounds);
  PrintFormat("[BBB] FIREDISCIPLINETEST RESULT4 lane=%1 mgRU=%2", lane, m_Ru.m_iGunRounds);
  Judge("contact", m_iContactAt >= 0 && m_iContactAt <= 60, string.Format("t=%1", m_iContactAt));
  Judge("both-fired", m_Us.m_iRounds > 0 && m_Ru.m_iRounds > 0, string.Format("US %1, USSR %2", m_Us.m_iRounds, m_Ru.m_iRounds));
  if (m_bOn)
  {
   int cap = BBB_FireDiscipline.SQUAD_BLIND + 1;
   Judge("squad-blind-cap", m_Us.m_iMostBlind <= cap && m_Ru.m_iMostBlind <= cap, string.Format("most at once US %1, USSR %2, cap %3", m_Us.m_iMostBlind, m_Ru.m_iMostBlind, cap));
   Judge("blind-range", BBB_FireDiscipline.s_fFarthest <= BBB_FireDiscipline.MG_M, string.Format("farthest %1 m, %2 started", Math.Round(BBB_FireDiscipline.s_fFarthest), BBB_FireDiscipline.s_iStarted - m_iStarted0));
  }
  FinishDiscipline();
 }

 void FinishDiscipline()
 {
  string verdict = "FAIL";
  if (m_iFailures == 0 && m_iChecks == m_iExpected) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbfirediscipline passed=%2 failed=%3 expected=%4", verdict, m_iChecks - m_iFailures, m_iFailures, m_iExpected);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 override protected void OnGameStart()
 {
  super.OnGameStart();
  BBB_FireDisciplineTest.Start();
 }
}
