class BBB_TeamsTestHooks : BBB_Hooks
{
 ref array<SCR_AIGroup> m_Groups = {};
 override void GetGroups(notnull array<SCR_AIGroup> groups) { groups.Copy(m_Groups); }
}

// Specialist teams A/B (-bbbTeamsTest): two lanes, each a vanilla USSR machine gun team and
// AT team, no enemy. Lane A uses specialist teams, lane B's soldiers are on the vanilla list.
// 1 pairing: the gunner and the man carrying his ammunition are paired from kit.
// 2 leash: each MG assistant is sent 45 m away by a plain move (priority 59, above formation);
//   A's walks back once past 20 m, B's (control) gets there.
// 3 supply: both gunners lose every spare; A's assistants bring theirs (Rearm defers to them).
// 4 pick-up: both AT gunners are killed; A's assistant takes the launcher from the body.
// 5 off: -bbbSpecialistTeams 0 drops every pair.
// Physical outcomes: distances, magazines in the gunners' kits, the launcher in his weapons.
class BBB_TeamsTest : BBB_AimTest
{
 static ref BBB_TeamsTest s_Teams;
 static const int EXPECTED = 14;
 static const ResourceName TEAM_USSR_MG = "{A2F75E45C66B1C0A}Prefabs/Groups/OPFOR/Group_USSR_MachineGunTeam.et";
 ref BBB_TeamsTestHooks m_Hooks;
 ref BBB_Hooks m_SavedHooks;
 ref array<SCR_AIGroup> m_MgTeams = {};
 ref array<SCR_AIGroup> m_AtTeams = {};
 ref array<ChimeraCharacter> m_MgGunner = {};
 ref array<ChimeraCharacter> m_MgMate = {};
 ref array<ChimeraCharacter> m_AtGunner = {};
 ref array<ChimeraCharacter> m_AtMate = {};
 ref array<string> m_MgWell = {};
 ref array<string> m_AtWell = {};
 ref array<float> m_MaxApart = {};
 ref array<int> m_MateStock = {};
 ref array<ref SCR_AIMoveIndividuallyBehavior> m_Wander = {};
 int m_Checks;
 int m_Failures;
 int m_Phase;
 int m_Wait;
 int m_LeashesBefore;

 static void Start()
 {
  if (!Replication.IsServer() || !System.IsCLIParam("bbbTeamsTest")) return;
  s_Teams = new BBB_TeamsTest();
  GetGame().GetCallqueue().CallLater(s_Teams.Setup, 15000);
 }

 void TeamCheck(string name, bool ok, string detail = "")
 {
  m_Checks++;
  if (!ok) m_Failures++;
  PrintFormat("[BBB] TEAMSTEST %1 ok=%2 %3", name, ok, detail);
 }

 void Setup()
 {
  BBB_Teams.Read();
  TeamCheck("default-on", BBB_Teams.s_bOn);
  BBB_StaticWeapons.s_bOn = false;
  BBB_FireManeuver.s_bOn = false;
  BBB_Buildings.s_bOn = false;
  BBB_Morale.s_bOn = false;
  BBB_Grenades.s_bOn = false;
  BBB_Smoke.s_bOn = false;
  BBB_Flares.s_bOn = false;
  BBB_ContactRelay.s_bOn = false;
  m_SavedHooks = BBB.s_Hooks;
  m_Hooks = new BBB_TeamsTestHooks();
  BBB.s_Hooks = m_Hooks;
  vector base = BBB.Ground("1340 0 2770");
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers) observers.InsertObserverSP(22150, base[0], base[2], null);
  bool made = true;
  for (int i = 0; i < 2; i++)
  {
   vector at = base + Vector(i * 160, 0, 0);
   SCR_AIGroup mg = SCR_AIGroup.Cast(Spawn(TEAM_USSR_MG, BBB.Ground(at), "0 0 1"));
   SCR_AIGroup atTeam = SCR_AIGroup.Cast(Spawn(TEAM_USSR_AT, BBB.Ground(at + Vector(40, 0, 0)), "0 0 1"));
   made = made && mg && atTeam;
   m_MgTeams.Insert(mg);
   m_AtTeams.Insert(atTeam);
   m_Hooks.m_Groups.Insert(mg);
   m_Hooks.m_Groups.Insert(atTeam);
   m_MgGunner.Insert(null);
   m_MgMate.Insert(null);
   m_AtGunner.Insert(null);
   m_AtMate.Insert(null);
   m_MgWell.Insert("");
   m_AtWell.Insert("");
   m_MaxApart.Insert(0);
   m_MateStock.Insert(0);
   m_Wander.Insert(null);
  }
  TeamCheck("groups", made);
  if (!made) { FinishTeams(); return; }
  GetGame().GetCallqueue().CallLater(Prepare, 4000);
 }

 void PrepareGroup(SCR_AIGroup group, bool oldWay)
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
   if (!body) continue;
   body.GetDamageManager().EnableDamageHandling(false);
   if (oldWay) BBB_Teams.s_aVanilla.Insert(body);
  }
 }

 // His machine gun or launcher's well, and who in his team carries ammunition for it.
 void FindRoles(SCR_AIGroup group, out ChimeraCharacter gunner, out ChimeraCharacter mate, out string well)
 {
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  // The assistant carries ammunition no weapon of his takes (the vanilla AT team has two RPG
  // gunners and one assistant); his gunner is the nearest man whose weapon takes it.
  foreach (AIAgent agent : agents)
  {
   ChimeraCharacter soldier = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!soldier || mate) continue;
   array<string> takes = {};
   bool launcher;
   BBB_Teams.Kit(soldier, takes, launcher);
   array<string> foreign = {};
   BBB_Teams.ForeignWells(soldier, takes, foreign);
   if (!foreign.IsEmpty()) { mate = soldier; well = foreign[0]; }
  }
  if (!mate) return;
  float best = float.MAX;
  foreach (AIAgent other : agents)
  {
   ChimeraCharacter soldier = ChimeraCharacter.Cast(other.GetControlledEntity());
   if (!soldier || soldier == mate) continue;
   array<string> takes2 = {};
   bool launcher2;
   BaseMuzzleComponent muzzle = BBB_Teams.Kit(soldier, takes2, launcher2);
   if (!muzzle || BBB_Teams.WellOf(muzzle) != well) continue;
   float d = vector.DistanceSq(soldier.GetOrigin(), mate.GetOrigin());
   if (d < best) { best = d; gunner = soldier; }
  }
 }

 void Prepare()
 {
  for (int i = 0; i < 2; i++)
  {
   PrepareGroup(m_MgTeams[i], i == 1);
   PrepareGroup(m_AtTeams[i], i == 1);
   ChimeraCharacter gunner, mate;
   string well;
   FindRoles(m_MgTeams[i], gunner, mate, well);
   m_MgGunner[i] = gunner; m_MgMate[i] = mate; m_MgWell[i] = well;
   gunner = null; mate = null; well = "";
   FindRoles(m_AtTeams[i], gunner, mate, well);
   m_AtGunner[i] = gunner; m_AtMate[i] = mate; m_AtWell[i] = well;
   PrintFormat("[BBB] TEAMSTEST lane %1: MG gunner %2 mate %3 (%4), AT gunner %5 mate %6 (%7)", i, m_MgGunner[i], m_MgMate[i], m_MgWell[i], m_AtGunner[i], m_AtMate[i], m_AtWell[i]);
  }
  GetGame().GetCallqueue().CallLater(Poll, 1000, true);
 }

 void Next(int phase)
 {
  m_Phase = phase;
  m_Wait = 0;
 }

 int Spare(ChimeraCharacter gunner)
 {
  if (!gunner) return 0;
  array<string> takes = {};
  bool launcher;
  BaseMuzzleComponent muzzle = BBB_Teams.Kit(gunner, takes, launcher);
  SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(gunner.FindComponent(SCR_InventoryStorageManagerComponent));
  if (!muzzle || !inv) return 0;
  return inv.GetMagazineCountByMuzzle(gunner, muzzle);
 }

 int StockOf(ChimeraCharacter soldier, string well)
 {
  array<IEntity> items = {};
  return BBB_Teams.Stock(soldier, well, items);
 }

 // Every spare of his own well (the round in the gun stays).
 void Strip(ChimeraCharacter gunner)
 {
  if (!gunner) return;
  array<string> takes = {};
  bool launcher;
  BaseMuzzleComponent muzzle = BBB_Teams.Kit(gunner, takes, launcher);
  SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(gunner.FindComponent(SCR_InventoryStorageManagerComponent));
  if (!muzzle || !inv) return;
  BaseMagazineComponent loaded = muzzle.GetMagazine();
  array<IEntity> items = {};
  BBB_Teams.Stock(gunner, BBB_Teams.WellOf(muzzle), items);
  foreach (IEntity item : items)
  {
   if (loaded && loaded.GetOwner() == item) continue;
   inv.TryRemoveItemFromInventory(item);
   SCR_EntityHelper.DeleteEntityAndChildren(item);
  }
 }

 bool HasLauncher(ChimeraCharacter soldier, string well)
 {
  array<string> takes = {};
  bool launcher;
  BaseMuzzleComponent muzzle = BBB_Teams.Kit(soldier, takes, launcher);
  return muzzle && launcher && BBB_Teams.WellOf(muzzle) == well;
 }

 void Poll()
 {
  m_Wait++;
  if (m_Wait > 60) { TeamCheck("phase-timeout", false, string.Format("phase=%1", m_Phase)); FinishTeams(); return; }
  if (m_Phase == 0)
  {
   BBB_Pair mg = BBB_Teams.PairOf(m_MgGunner[0]);
   BBB_Pair at = BBB_Teams.PairOf(m_AtGunner[0]);
   bool mgOk = mg && mg.m_Gunner == m_MgGunner[0] && mg.m_Assistant == m_MgMate[0] && mg.m_sWell == m_MgWell[0] && !mg.m_bLauncher;
   bool atOk = at && at.m_Gunner == m_AtGunner[0] && at.m_Assistant == m_AtMate[0] && at.m_sWell == m_AtWell[0] && at.m_bLauncher;
   if (!(mgOk && atOk) && m_Wait < 12) return;
   TeamCheck("mg-gunner-paired-with-his-ammo-bearer", mgOk, string.Format("mate %1 well %2", m_MgMate[0], m_MgWell[0]));
   TeamCheck("at-gunner-paired-with-his-assistant", atOk, string.Format("mate %1 well %2", m_AtMate[0], m_AtWell[0]));
   bool unpaired = !BBB_Teams.PairOf(m_MgGunner[1]) && !BBB_Teams.PairOf(m_AtGunner[1]);
   TeamCheck("vanilla-lane-unpaired", unpaired, string.Format("pairs %1", BBB_Teams.Count()));
   if (!m_MgMate[0] || !m_MgMate[1]) { FinishTeams(); return; }
   m_LeashesBefore = BBB_Teams.s_iLeashes;
   for (int i = 0; i < 2; i++)
   {
    SCR_AIUtilityComponent utility = BBB_StaticWeapons.UtilityOf(m_MgMate[i]);
    vector away = BBB.Ground(m_MgGunner[i].GetOrigin() + Vector(0, 0, 45));
    m_Wander[i] = new SCR_AIMoveIndividuallyBehavior(utility, null, away, priority: SCR_AIActionBase.PRIORITY_BEHAVIOR_MOVE_INDIVIDUALLY + 1, radius: 1.0);
    if (utility) utility.AddAction(m_Wander[i]);
   }
   Next(1);
   return;
  }
  if (m_Phase == 1)
  {
   for (int l = 0; l < 2; l++)
    m_MaxApart[l] = Math.Max(m_MaxApart[l], vector.DistanceXZ(m_MgMate[l].GetOrigin(), m_MgGunner[l].GetOrigin()));
   if (m_Wait % 5 == 0) PrintFormat("[BBB] TEAMSTEST leash t=%1: apart A %2 m (max %3), B %4 m (max %5)", m_Wait, (int)vector.DistanceXZ(m_MgMate[0].GetOrigin(), m_MgGunner[0].GetOrigin()), (int)m_MaxApart[0], (int)vector.DistanceXZ(m_MgMate[1].GetOrigin(), m_MgGunner[1].GetOrigin()), (int)m_MaxApart[1]);
   if (m_Wait < 30) return;
   TeamCheck("leash-ran", BBB_Teams.s_iLeashes > m_LeashesBefore, string.Format("leashes %1", BBB_Teams.s_iLeashes - m_LeashesBefore));
   TeamCheck("assistant-kept-near-gunner", m_MaxApart[0] <= 32, string.Format("max apart %1 m", m_MaxApart[0].ToString(4, 1)));
   TeamCheck("control-assistant-wandered-off", m_MaxApart[1] >= 35, string.Format("max apart %1 m", m_MaxApart[1].ToString(4, 1)));
   for (int w = 0; w < 2; w++)
    if (BBB_Teams.Active(m_Wander[w])) m_Wander[w].Fail();
   for (int s = 0; s < 2; s++)
   {
    Strip(m_MgGunner[s]);
    Strip(m_AtGunner[s]);
    m_MateStock[s] = StockOf(m_MgMate[s], m_MgWell[s]);
   }
   PrintFormat("[BBB] TEAMSTEST stripped: MG spare A %1 B %2, AT spare A %3 B %4, MG mate stock A %5 B %6, AT mate stock A %7", Spare(m_MgGunner[0]), Spare(m_MgGunner[1]), Spare(m_AtGunner[0]), Spare(m_AtGunner[1]), m_MateStock[0], m_MateStock[1], StockOf(m_AtMate[0], m_AtWell[0]));
   TeamCheck("rearm-leaves-gunner-to-his-assistant", BBB_Teams.Supplies(m_MgGunner[0]) && !BBB_Teams.Supplies(m_MgGunner[1]));
   // An assistant busy treating someone can't come: the gunner looks after himself meanwhile.
   BBB_Medic.s_mGoingTo.Set(m_MgMate[0], m_MgGunner[1]);
   bool heldBack = !BBB_Teams.Supplies(m_MgGunner[0]);
   BBB_Medic.s_mGoingTo.Remove(m_MgMate[0]);
   TeamCheck("busy-assistant-gunner-not-left-waiting", heldBack && BBB_Teams.Supplies(m_MgGunner[0]));
   Next(2);
   return;
  }
  if (m_Phase == 2)
  {
   bool mgDone = Spare(m_MgGunner[0]) > 0;
   bool atDone = Spare(m_AtGunner[0]) > 0;
   if (m_Wait % 5 == 0) PrintFormat("[BBB] TEAMSTEST supply t=%1: MG spare A %2 B %3, AT spare A %4 B %5, MG mate apart A %6 m", m_Wait, Spare(m_MgGunner[0]), Spare(m_MgGunner[1]), Spare(m_AtGunner[0]), Spare(m_AtGunner[1]), (int)vector.DistanceXZ(m_MgMate[0].GetOrigin(), m_MgGunner[0].GetOrigin()));
   if (!(mgDone && atDone) && m_Wait < 30) return;
   TeamCheck("mg-assistant-handed-over", mgDone && StockOf(m_MgMate[0], m_MgWell[0]) < m_MateStock[0], string.Format("gunner spare %1, mate stock %2 -> %3", Spare(m_MgGunner[0]), m_MateStock[0], StockOf(m_MgMate[0], m_MgWell[0])));
   TeamCheck("at-assistant-handed-over", atDone, string.Format("gunner spare %1, mate stock %2", Spare(m_AtGunner[0]), StockOf(m_AtMate[0], m_AtWell[0])));
   PrintFormat("[BBB] TEAMSTEST vanilla lane after %1 s: MG spare %2, AT spare %3 (recorded)", m_Wait, Spare(m_MgGunner[1]), Spare(m_AtGunner[1]));
   for (int k = 0; k < 2; k++)
   {
    SCR_DamageManagerComponent damage = SCR_DamageManagerComponent.GetDamageManager(m_AtGunner[k]);
    if (!damage) continue;
    damage.EnableDamageHandling(true);
    damage.Kill(Instigator.CreateInstigator(null));
   }
   Next(3);
   return;
  }
  if (m_Phase == 3)
  {
   bool took = HasLauncher(m_AtMate[0], m_AtWell[0]);
   if (m_Wait % 5 == 0) PrintFormat("[BBB] TEAMSTEST pickup t=%1: A has launcher %2 (%3 m from body), B %4", m_Wait, took, (int)vector.DistanceXZ(m_AtMate[0].GetOrigin(), m_AtGunner[0].GetOrigin()), HasLauncher(m_AtMate[1], m_AtWell[1]));
   if (!took && m_Wait < 35) return;
   TeamCheck("assistant-took-fallen-gunners-launcher", took, string.Format("pickups %1", BBB_Teams.s_iPickups));
   PrintFormat("[BBB] TEAMSTEST vanilla lane: assistant has the launcher %1 (recorded)", HasLauncher(m_AtMate[1], m_AtWell[1]));
   BBB_Teams.s_bOn = false;
   BBB_Teams.Tick();
   bool dropped = BBB_Teams.Count() == 0 && !BBB_Teams.Busy(m_MgMate[0]);
   BBB_Teams.Tick(); // stays empty while off
   dropped = dropped && BBB_Teams.Count() == 0;
   TeamCheck("off-switch-drops-pairs", dropped);
   FinishTeams();
  }
 }

 void FinishTeams()
 {
  GetGame().GetCallqueue().Remove(Poll);
  PrintFormat("[BBB] TEAMSTEST paired=%1 leashes=%2 handovers=%3 items=%4 failed=%5 pickups=%6", BBB_Teams.s_iPaired, BBB_Teams.s_iLeashes, BBB_Teams.s_iHandovers, BBB_Teams.s_iItemsHanded, BBB_Teams.s_iSupplyFailed, BBB_Teams.s_iPickups);
  if (m_SavedHooks) BBB.s_Hooks = m_SavedHooks;
  string verdict = "FAIL";
  if (m_Failures == 0 && m_Checks == EXPECTED) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbteams passed=%2 failed=%3 expected=%4", verdict, m_Checks - m_Failures, m_Failures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 override protected void OnGameStart()
 {
  super.OnGameStart();
  BBB_TeamsTest.Start();
 }
}
