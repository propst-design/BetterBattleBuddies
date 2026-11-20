// End-to-end night flares (20 Nov 2026: "not seeing the AI firing flares at night"). Two vanilla rifle
// squads, US and USSR, fight at midnight with their own kit. Nothing calls into BBB_Flares: BBB runs on
// its own (the vanilla adapter's hooks, every AI group) and the check is a real illumination flare in
// the air over the fight within the time limit. The existing flares preset drives TryIlluminate by
// hand; this one guards the autonomous path (BBB.Tick -> BBB_Flares.Tick) that a refactor can break.
// Every 10 s it logs each gate per squad, so a failure says which one held.
class BBB_NightFightTest : BBB_AimTest
{
 static ref BBB_NightFightTest s_NightFight;
 static const ResourceName NIGHT_SQUAD_US = "{DDF3799FA1387848}Prefabs/Groups/BLUFOR/Group_US_RifleSquad.et";
 static const ResourceName NIGHT_SQUAD_USSR = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const int NIGHT_LIMIT_S = 150;
 TimeAndWeatherManagerEntity m_Weather;
 SCR_AIGroup m_US;
 SCR_AIGroup m_USSR;
 vector m_Middle;
 int m_Wait;
 int m_Dispatched;
 int m_Checks;
 int m_Failures;
 bool m_Lit;
 ref array<IEntity> m_Seen = {};

 static void Start()
 {
  if (!Replication.IsServer() || !System.IsCLIParam("bbbNightFightTest")) return;
  s_NightFight = new BBB_NightFightTest();
  GetGame().GetCallqueue().CallLater(s_NightFight.Setup, 15000);
 }

 void Check(string name, bool ok)
 {
  m_Checks++;
  if (!ok) m_Failures++;
  PrintFormat("[BBB] NIGHTFIGHT %1 ok=%2", name, ok);
 }

 void Setup()
 {
  Check("bbb-started", BBB.IsStarted());
  m_Weather = ChimeraWorld.CastFrom(GetGame().GetWorld()).GetTimeAndWeatherManager();
  Check("weather", m_Weather != null);
  if (!m_Weather || !BBB.IsStarted()) { FinishNight(); return; }
  m_Weather.SetHoursMinutesSeconds(0, 0, 0);
  vector us = BBB.Ground("1340 0 2770");
  vector ussr = BBB.Ground("1340 0 2900");
  m_Middle = (us + ussr) * 0.5;
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers) observers.InsertObserverSP(22161, m_Middle[0], m_Middle[2], null);
  m_US = SCR_AIGroup.Cast(Spawn(NIGHT_SQUAD_US, us, "0 0 1"));
  m_USSR = SCR_AIGroup.Cast(Spawn(NIGHT_SQUAD_USSR, ussr, "0 0 -1"));
  Check("squads", m_US && m_USSR);
  if (!m_US || !m_USSR) { FinishNight(); return; }
  GetGame().GetCallqueue().CallLater(Prepare, 5000);
 }

 // Damage off so the fight lasts; otherwise the soldiers are left entirely to the game and BBB.
 void PrepareGroup(SCR_AIGroup group)
 {
  PinDetail(group);
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  int kit;
  foreach (AIAgent agent : agents)
  {
   PinDetail(agent);
   ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!body) continue;
   body.GetDamageManager().EnableDamageHandling(false);
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   BaseWeaponComponent weapon;
   BaseMagazineComponent magazine;
   int muzzle;
   if (BBB_Flares.FindEquipment(utility, weapon, magazine, muzzle)) kit++;
  }
  PrintFormat("[BBB] NIGHTFIGHT %1 men=%2 carrying flares=%3", group, agents.Count(), kit);
 }

 void Prepare()
 {
  PrepareGroup(m_US);
  PrepareGroup(m_USSR);
  Check("night", SCR_AIWorldHandling.IsLowLightEnvironment());
  Check("flares-on", BBB_Flares.s_bOn);
  array<SCR_AIGroup> groups = {};
  BBB.Groups(groups);
  Check("bbb-looks-after-both", groups.Contains(m_US) && groups.Contains(m_USSR));
  m_Dispatched = BBB_Flares.s_iDispatched;
  GetGame().GetCallqueue().CallLater(Poll, 1000, true);
 }

 void Poll()
 {
  m_Wait++;
  GetGame().GetWorld().QueryEntitiesBySphere(m_Middle, 400, CollectFlare);
  if (m_Wait % 10 == 0) { Report(m_US); Report(m_USSR); }
  if (m_Lit || m_Wait >= NIGHT_LIMIT_S)
  {
   PrintFormat("[BBB] NIGHTFIGHT after %1 s: requests=%2 cancelled=%3 lit=%4", m_Wait, BBB_Flares.s_iDispatched - m_Dispatched, BBB_Flares.s_iCancelled, m_Lit);
   Check("flare-requested", BBB_Flares.s_iDispatched > m_Dispatched);
   Check("flare-in-the-air", m_Lit);
   FinishNight();
  }
 }

 bool CollectFlare(IEntity entity)
 {
  if (!entity || !entity.GetPrefabData() || m_Seen.Contains(entity)) return true;
  string name = entity.GetPrefabData().GetPrefabName();
  if (!name.Contains("FlareEffect")) return true;
  vector point = entity.GetOrigin();
  float height = point[1] - GetGame().GetWorld().GetSurfaceY(point[0], point[2]);
  if (height < 10) return true;
  m_Seen.Insert(entity);
  m_Lit = true;
  PrintFormat("[BBB] NIGHTFIGHT illumination %1 at %2 height %3 after %4 s", name, point, height, m_Wait);
  return true;
 }

 // Each gate TryIlluminate passes, in order, for this squad.
 void Report(SCR_AIGroup group)
 {
  if (!group) return;
  float now = GetGame().GetWorld().GetWorldTime();
  vector target;
  BaseTarget contact;
  bool known = BBB_Flares.KnownContact(group, target, contact);
  int seen, allowed, available, kit;
  array<AIAgent> agents = {};
  BBB.Members(group, agents);
  foreach (AIAgent agent : agents)
  {
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   if (!utility) continue;
   if (utility.m_PerceptionComponent)
   {
    array<BaseTarget> targets = {};
    utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
    foreach (BaseTarget t : targets) if (BBB_Flares.RecentTarget(t)) { seen++; break; }
   }
   if (!BBB_Flares.Allowed(utility)) continue;
   allowed++;
   if (utility.m_AIInfo && utility.m_AIInfo.GetAIState() == EUnitAIState.AVAILABLE) available++;
   BaseWeaponComponent weapon;
   BaseMagazineComponent magazine;
   int muzzle;
   if (BBB_Flares.FindEquipment(utility, weapon, magazine, muzzle)) kit++;
  }
  SCR_AIGroupInfoComponent info = SCR_AIGroupInfoComponent.Cast(group.FindComponent(SCR_AIGroupInfoComponent));
  bool infoAllows = !info || info.IsIllumFlareAllowed();
  PrintFormat("[BBB] NIGHTFIGHT t=%1 %2 men=%3 seeing=%4 known=%5 at=%6 area=%7 groupReady=%8 hook=%9", m_Wait, group, agents.Count(), seen, known, target, known && BBB_Flares.AreaReady(target, now), BBB_Flares.GroupReady(group, now), BBB.s_Hooks.AllowFlares(group));
  PrintFormat("[BBB] NIGHTFIGHT t=%1 %2 allowed=%3 available=%4 withKit=%5 infoAllows=%6 handles=%7 night=%8", m_Wait, group, allowed, available, kit, infoAllows, BBB_Flares.Handles(group), SCR_AIWorldHandling.IsLowLightEnvironment());
 }

 void FinishNight()
 {
  GetGame().GetCallqueue().Remove(Poll);
  string verdict = "FAIL";
  if (m_Failures == 0 && m_Checks > 0) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbnightfight passed=%2 failed=%3", verdict, m_Checks - m_Failures, m_Failures);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 override protected void OnGameStart()
 {
  super.OnGameStart();
  BBB_NightFightTest.Start();
 }
}
