class BBB_ContactRelayTestHooks : BBB_Hooks
{
 ref array<SCR_AIGroup> m_Groups = {};
 bool m_Veto;
 override void GetGroups(notnull array<SCR_AIGroup> groups) { groups.Copy(m_Groups); }
 override bool AllowContactRelay(SCR_AIGroup group) { return !m_Veto; }
}

// Contact relay A/B (-bbbContactRelayTest): spotter team A sees a passive enemy team at ~120 m;
// receiver team B is ~450 m away with its own perception turned down so it cannot see him
// itself. Off, vetoed and under a player order, B gets nothing and stays put; on, B's group
// perception gets an unidentified report where A saw him, B's fireteams move toward it, and B
// fires no shot while it hasn't identified anyone. A team with its radios removed, out of voice
// range, gets nothing.
class BBB_ContactRelayTest : BBB_AimTest
{
 static ref BBB_ContactRelayTest s_Relay;
 static const int EXPECTED = 15;
 ref BBB_ContactRelayTestHooks m_Hooks;
 ref BBB_Hooks m_SavedHooks;
 SCR_AIGroup m_Spotter;
 SCR_AIGroup m_Receiver;
 SCR_AIGroup m_Silent;
 SCR_AIGroup m_Enemy;
 IEntity m_EnemyBody;
 vector m_vSeenAt;
 float m_fStartGap;
 int m_iAmmo;
 int m_Checks;
 int m_Failures;
 int m_Phase;
 int m_Wait;
 bool m_bBlindShot;
 SCR_AIWaypoint m_Order;

 static void Start()
 {
  if (!Replication.IsServer() || !System.IsCLIParam("bbbContactRelayTest")) return;
  s_Relay = new BBB_ContactRelayTest();
  GetGame().GetCallqueue().CallLater(s_Relay.Setup, 15000);
 }

 void RelayCheck(string name, bool ok, string detail = "")
 {
  m_Checks++;
  if (!ok) m_Failures++;
  PrintFormat("[BBB] RELAYTEST %1 ok=%2 %3", name, ok, detail);
 }

 void Setup()
 {
  BBB_ContactRelay.Read();
  RelayCheck("default-on", BBB_ContactRelay.s_bOn);
  BBB_ContactRelay.s_bOn = false;
  m_SavedHooks = BBB.s_Hooks;
  m_Hooks = new BBB_ContactRelayTestHooks();
  BBB.s_Hooks = m_Hooks;
  vector point = BBB.Ground("1340 0 2770");
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers) observers.InsertObserverSP(22148, point[0], point[2], null);
  m_Spotter = SCR_AIGroup.Cast(Spawn(TEAM_US, point, "0.5 0 1"));
  m_Enemy = SCR_AIGroup.Cast(Spawn(TEAM_USSR, point + Vector(55, 0, 105), "-0.5 0 -1"));
  m_Receiver = SCR_AIGroup.Cast(Spawn(TEAM_US, point + Vector(-260, 0, -300), "0 0 1"));
  m_Silent = SCR_AIGroup.Cast(Spawn(TEAM_US, point + Vector(220, 0, -300), "0 0 1"));
  bool made = m_Spotter && m_Enemy && m_Receiver && m_Silent;
  RelayCheck("groups", made);
  if (!made) { FinishRelay(); return; }
  m_Hooks.m_Groups.Insert(m_Spotter);
  m_Hooks.m_Groups.Insert(m_Receiver);
  GetGame().GetCallqueue().CallLater(Prepare, 4000);
 }

 void PrepareGroup(SCR_AIGroup group, float perceptionFactor)
 {
  PinDetail(group);
  group.ActivateAI();
  EAIGroupCombatMode mode = EAIGroupCombatMode.FIRE_AT_WILL;
  if (group == m_Enemy) mode = EAIGroupCombatMode.HOLD_FIRE;
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
   AIControlComponent control = AIControlComponent.Cast(body.FindComponent(AIControlComponent));
   if (control) control.ActivateAI();
   body.GetDamageManager().EnableDamageHandling(false);
   if (group == m_Enemy)
   {
    body.GetCharacterController().SetDisableWeaponControls(true);
    body.GetCharacterController().SetDisableMovementControls(true);
    if (!m_EnemyBody) m_EnemyBody = body;
   }
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   if (utility && utility.m_PerceptionComponent) utility.m_PerceptionComponent.SetPerceptionFactor(perceptionFactor);
  }
 }

 void Prepare()
 {
  PrepareGroup(m_Spotter, 50);
  PrepareGroup(m_Enemy, 1);
  PrepareGroup(m_Receiver, 0.01);
  PrepareGroup(m_Silent, 0.01);
  float now = GetGame().GetWorld().GetWorldTime();
  float range = BBB_ContactRelay.GroupRadioRange(m_Receiver, now);
  RelayCheck("receiver-has-radio", range >= 1000, string.Format("range=%1", range));
  // The silent team loses every radio it carries.
  array<AIAgent> agents = {};
  m_Silent.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   IEntity body = agent.GetControlledEntity();
   SCR_GadgetManagerComponent gadgets = SCR_GadgetManagerComponent.GetGadgetManager(body);
   SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(body.FindComponent(SCR_InventoryStorageManagerComponent));
   if (!gadgets || !inventory) continue;
   array<SCR_GadgetComponent> radios = gadgets.GetGadgetsByType(EGadgetType.RADIO);
   foreach (SCR_GadgetComponent radio : radios)
   {
    if (!radio || !radio.GetOwner()) continue;
    IEntity item = radio.GetOwner();
    if (inventory.TryRemoveItemFromInventory(item)) SCR_EntityHelper.DeleteEntityAndChildren(item);
   }
  }
  BBB_ContactRelay.Stop();
  RelayCheck("silent-team-has-no-radio", BBB_ContactRelay.GroupRadioRange(m_Silent, now + 60000) <= 0);
  m_iAmmo = Ammo(m_Receiver);
  GetGame().GetCallqueue().CallLater(Poll, 1000, true);
 }

 int Ammo(SCR_AIGroup group)
 {
  int total;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!body || !body.GetWeaponManager() || !body.GetWeaponManager().GetCurrentWeapon()) continue;
   BaseMagazineComponent magazine = body.GetWeaponManager().GetCurrentWeapon().GetCurrentMagazine();
   if (magazine) total += magazine.GetAmmoCount();
  }
  return total;
 }

 // The group's own record of the enemy (-1: none), and its category.
 int Known(SCR_AIGroup group, out EAITargetInfoCategory category, out vector at)
 {
  SCR_AIGroupUtilityComponent utility = group.GetGroupUtilityComponent();
  if (!utility || !utility.m_Perception) return -1;
  array<AIAgent> agents = {};
  m_Enemy.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   int id = utility.m_Perception.m_aTargetEntities.Find(agent.GetControlledEntity());
   if (id < 0) continue;
   category = utility.m_Perception.m_aTargets[id].m_eCategory;
   at = utility.m_Perception.m_aTargets[id].m_vWorldPos;
   return id;
  }
  return -1;
 }

 bool Identified(SCR_AIGroup group)
 {
  EAITargetInfoCategory category;
  vector at;
  return Known(group, category, at) >= 0 && category == EAITargetInfoCategory.IDENTIFIED;
 }

 float Gap()
 {
  IEntity anchor = BBB.AnchorOf(m_Receiver);
  if (!anchor || !m_EnemyBody) return 0;
  return vector.DistanceXZ(anchor.GetOrigin(), m_EnemyBody.GetOrigin());
 }

 void Next(int phase)
 {
  m_Phase = phase;
  m_Wait = 0;
 }

 void Poll()
 {
  m_Wait++;
  // No blind fire: the receiver never shoots while it hasn't identified anyone itself.
  if (!Identified(m_Receiver) && Ammo(m_Receiver) < m_iAmmo) m_bBlindShot = true;
  if (m_Wait > 60) { RelayCheck("phase-timeout", false, string.Format("phase=%1", m_Phase)); FinishRelay(); return; }
  EAITargetInfoCategory category;
  vector at;
  if (m_Phase == 0)
  {
   // The spotter's own soldiers identify the enemy.
   if (!Identified(m_Spotter)) return;
   Known(m_Spotter, category, m_vSeenAt);
   RelayCheck("spotter-identified", true, string.Format("receiver gap %1 m", (int)Gap()));
   m_fStartGap = Gap();
   Next(1);
   return;
  }
  if (m_Phase == 1)
  {
   // Off: nothing reaches the receiver and it stays put.
   if (m_Wait < 15) return;
   RelayCheck("off-no-report", Known(m_Receiver, category, at) < 0);
   RelayCheck("off-receiver-stays", m_fStartGap - Gap() < 8, string.Format("moved %1 m toward", (int)(m_fStartGap - Gap())));
   BBB_ContactRelay.s_bOn = true;
   m_Hooks.m_Veto = true;
   Next(2);
   return;
  }
  if (m_Phase == 2)
  {
   if (m_Wait < 10) return;
   RelayCheck("host-veto-no-report", Known(m_Receiver, category, at) < 0);
   m_Hooks.m_Veto = false;
   // A Defend on the spot: a Move there could complete at once, leave the group with no
   // current order and let the report through (failed 2 of 4 runs, 27-28 Oct).
   m_Order = SCR_AIWaypoint.Cast(Spawn("{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et", BBB.AnchorOf(m_Receiver).GetOrigin(), "0 0 1"));
   if (m_Order)
   {
    m_Order.SetPriorityLevel(SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
    m_Receiver.AddWaypoint(m_Order);
   }
   Next(3);
   return;
  }
  if (m_Phase == 3)
  {
   if (m_Wait < 10) return;
   RelayCheck("player-order-no-report", m_Order && m_Receiver.GetCurrentWaypoint() == m_Order && Known(m_Receiver, category, at) < 0, string.Format("order current %1", m_Order && m_Receiver.GetCurrentWaypoint() == m_Order));
   if (m_Order) m_Receiver.RemoveWaypoint(m_Order);
   m_Order = null;
   // A report about the spotter's current sighting is needed again (FRESH_S).
   BBB_ContactRelay.Stop();
   m_fStartGap = Gap();
   Next(4);
   return;
  }
  if (m_Phase == 4)
  {
   // On: the report arrives, unidentified, at the spot the spotter saw.
   if (Known(m_Receiver, category, at) < 0) return;
   RelayCheck("report-arrives", BBB_ContactRelay.s_iDelivered > 0, string.Format("after %1 s", m_Wait));
   RelayCheck("report-unidentified", category == EAITargetInfoCategory.DETECTED || category == EAITargetInfoCategory.LOST, typename.EnumToString(EAITargetInfoCategory, category));
   RelayCheck("report-where-seen", vector.DistanceXZ(at, m_vSeenAt) < 15, string.Format("%1 m from the spotter's sighting", (int)vector.DistanceXZ(at, m_vSeenAt)));
   Next(5);
   return;
  }
  if (m_Phase == 5)
  {
   // Vanilla investigates the report with the receiver's own fireteams.
   float moved = m_fStartGap - Gap();
   if (moved < 20 && m_Wait < 50) return;
   RelayCheck("receiver-moves-toward", moved >= 20, string.Format("moved %1 m toward in %2 s", (int)moved, m_Wait));
   RelayCheck("no-blind-fire", !m_bBlindShot, string.Format("ammo %1 -> %2, identified %3", m_iAmmo, Ammo(m_Receiver), Identified(m_Receiver)));
   m_Hooks.m_Groups.Insert(m_Silent);
   Next(6);
   return;
  }
  if (m_Phase == 6)
  {
   // No radio, out of voice range: nothing.
   if (m_Wait < 15) return;
   RelayCheck("no-radio-no-report", Known(m_Silent, category, at) < 0, string.Format("gap %1 m", (int)vector.DistanceXZ(BBB.AnchorOf(m_Silent).GetOrigin(), BBB.AnchorOf(m_Spotter).GetOrigin())));
   FinishRelay();
  }
 }

 void FinishRelay()
 {
  GetGame().GetCallqueue().Remove(Poll);
  BBB_ContactRelay.Stop();
  if (m_SavedHooks) BBB.s_Hooks = m_SavedHooks;
  string verdict = "FAIL";
  if (m_Failures == 0 && m_Checks == EXPECTED) verdict = "PASS";
  PrintFormat("[BBB] RELAYTEST queued=%1 delivered=%2", BBB_ContactRelay.s_iQueued, BBB_ContactRelay.s_iDelivered);
  PrintFormat("[ACR] VERDICT %1 test=bbbcontactrelay passed=%2 failed=%3 expected=%4", verdict, m_Checks - m_Failures, m_Failures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 override protected void OnGameStart()
 {
  super.OnGameStart();
  BBB_ContactRelayTest.Start();
 }
}
