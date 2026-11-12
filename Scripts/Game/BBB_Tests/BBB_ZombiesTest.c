// Zombie rules (-bbbZombiesTest 1, preset zombies, ~2.5 min; loads Creature Melee AI 4FD4A1BD550793F4 and
// Project Outbreak F1D4B9B5245B36ED from E:\ArmaReforgerMods\addons or -ZombieAddonsDir). One US rifleman at a
// time against one Outbreak shambler (a slow walker), both with damage off so the fight can't end early.
//  1 detection: Outbreak and Creature Melee AI found by class name; the rules on by default (auto); a shambler is a
//    zombie, a rifleman isn't; standalone BBB leaves the zombie group out of the groups it looks after; a host's
//    ZombieRules 0 / 1 switches the rules and its IsZombie adds an entity.
//  2 keep distance, A/B in one run: the shambler starts 14 m in front of the rifleman. Rules off (host says 0),
//    then on, same ground, 18 s each. Measured: how far the rifleman really got back from where he stood, away from the zombie's start.
//  3 surrender: a rifleman cut off, no spare magazines, surrender threshold 0 and chance 1 (as the morale test),
//    with a shambler 20 m off: he must not give up (and BBB must have reached the zombie-captor rule). Then a
//    USSR team on hold fire 25 m off: he gives up to them (the rule is per threat, men still take prisoners).
//  4 heal: a casualty 10 m from his squadmate, a shambler 18 m on the other side: the heal is held (vanilla
//    asked, BBB held it) and he doesn't go to the casualty; the shambler removed, he goes.
// Not covered: drag itself (needs ACE Carrying; its refusal goes through the same HoldHeal call as the heal),
// rearm, cluster suppression, squad tactics, vehicles (code paths only), hordes, sprinters, and how a real
// fight against a horde plays out: that's Jacob's play-test.
class BBB_ZombiesTestHooks : BBB_VanillaHooks
{
 int m_iRules = -1;
 IEntity m_Marked;
 override int ZombieRules() { return m_iRules; }
 override bool IsZombie(IEntity entity) { return m_Marked && entity == m_Marked; }
}

class BBB_ZombiesTest : BBB_AimTest
{
 static ref BBB_ZombiesTest s_Zombies;
 static const ResourceName ZGROUP = "{A07B0E7A0F100005}Prefabs/Groups/INDFOR/Group_Z.et";
 static const ResourceName SHAMBLER = "{85B3CAB184E940BB}Prefabs/Characters/Factions/INDFOR/Outbreak/Character_Shambler_Civilian.et";
 static const int EXPECTED = 19;
 static const float KEEP_START_M = 14;
 static const float KEEP_S = 18;
 protected int m_iChecks;
 protected int m_iFailures;
 protected ref BBB_ZombiesTestHooks m_Hooks;
 protected BBB_Hooks m_OldHooks;
 protected vector m_vSite;
 protected vector m_vDir;
 protected vector m_vSide;
 protected ref array<IEntity> m_aMine;
 // current phase
 protected IEntity m_Man;
 protected IEntity m_Zombie;
 protected IEntity m_Casualty;
 protected SCR_AIGroup m_ZGroup;
 protected SCR_AIGroup m_ManGroup;
 protected SCR_AIGroup m_Captors;
 protected vector m_vManStart;
 protected vector m_vZombieStart;
 protected float m_fBack;
 protected float m_fBackOff;
 protected int m_iSteps;
 protected int m_iPolls;
 protected int m_iCounter;
 protected bool m_bPerceived;
 protected bool m_bReached;

 static void MaybeStartZombies()
 {
  if (s_Zombies || !Replication.IsServer() || !System.IsCLIParam("bbbZombiesTest")) return;
  s_Zombies = new BBB_ZombiesTest();
  GetGame().GetCallqueue().CallLater(s_Zombies.Begin, 14000);
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
  PrintFormat("[BBB] ZOMBIETEST CHECK %1 %2 %3", name, res, detail);
 }

 override protected void Begin()
 {
  m_aMine = {};
  if (!BBB.IsStarted())
  {
   GetGame().GetCallqueue().CallLater(Begin, 2000);
   return;
  }
  m_aCheckDistances.Clear();
  m_aCheckDistances.Insert(40);
  m_aCheckDistances.Insert(60);
  if (!FindSite())
  {
   Check("site", false, "no clear lane");
   Finish();
   return;
  }
  m_vDir = m_aSiteDir[0];
  m_vSide = Vector(-m_vDir[2], 0, m_vDir[0]);
  m_vSite = BBB.Ground(m_aSiteFrom[0]);
  // The test's own hooks: every AI group as standalone BBB (it derives from BBB_VanillaHooks), plus switches.
  m_OldHooks = BBB.s_Hooks;
  m_Hooks = new BBB_ZombiesTestHooks();
  BBB.s_Hooks = m_Hooks;
  BBB_Zombies.Stop();
  Check("outbreak-loaded", BBB_Zombies.IsLoaded(), "Z_BodyComponent found by name");
  Check("creatures-loaded", BBB_Zombies.CreaturesLoaded(), "CMAI_CreatureBrainComponent found by name");
  Check("rules-on-by-default", BBB_Zombies.On(), "setting auto, host has no opinion");
  // Far from the later phases.
  m_Zombie = SpawnZombie(m_vSite + m_vSide * 150, m_vDir);
  m_Man = SpawnMan(m_vSite + m_vSide * 150 - m_vDir * 60, m_vDir);
  GetGame().GetCallqueue().CallLater(Detection, 2500);
 }

 // ---------------------------------------------------------------- spawning

 protected void Activate(SCR_AIGroup group)
 {
  if (!group) return;
  PinDetail(group);
  group.ActivateAI();
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   PinDetail(agent);
   agent.ActivateAI();
  }
 }

 protected void NoDamage(IEntity man)
 {
  ChimeraCharacter body = ChimeraCharacter.Cast(man);
  if (body && body.GetDamageManager()) body.GetDamageManager().EnableDamageHandling(false);
 }

 protected IEntity SpawnZombie(vector at, vector facing)
 {
  SCR_AIGroup.IgnoreSpawning(true);
  m_ZGroup = SCR_AIGroup.Cast(Spawn(ZGROUP, at, facing));
  SCR_AIGroup.IgnoreSpawning(false);
  IEntity zombie = Spawn(SHAMBLER, at, facing);
  if (m_ZGroup) m_aMine.Insert(m_ZGroup);
  if (!zombie) return null;
  m_aMine.Insert(zombie);
  if (m_ZGroup) m_ZGroup.AddAIEntityToGroup(zombie);
  NoDamage(zombie);
  Activate(m_ZGroup);
  return zombie;
 }

 protected IEntity SpawnMan(vector at, vector facing)
 {
  SCR_AIGroup.IgnoreSpawning(true);
  m_ManGroup = SCR_AIGroup.Cast(Spawn(GROUP_US, at, facing));
  SCR_AIGroup.IgnoreSpawning(false);
  IEntity man = Spawn(RIFLE_US, at, facing);
  if (m_ManGroup) m_aMine.Insert(m_ManGroup);
  if (!man) return null;
  m_aMine.Insert(man);
  if (m_ManGroup) m_ManGroup.AddAIEntityToGroup(man);
  NoDamage(man);
  Activate(m_ManGroup);
  SCR_AIUtilityComponent utility = BBB_VehiclePosture.Utility(man);
  if (utility && utility.m_CombatComponent) utility.m_CombatComponent.SetPerceptionFactor(10);
  return man;
 }

 protected void Clean()
 {
  // Characters before their groups.
  for (int pass = 0; pass < 2; pass++)
  {
   foreach (IEntity item : m_aMine)
   {
    if (!item) continue;
    bool isGroup = SCR_AIGroup.Cast(item) != null;
    if ((pass == 0) == isGroup) continue;
    SCR_EntityHelper.DeleteEntityAndChildren(item);
   }
  }
  m_aMine.Clear();
  m_Man = null;
  m_Zombie = null;
  m_Casualty = null;
  m_Captors = null;
 }

 protected bool Perceives(IEntity man, IEntity enemy)
 {
  SCR_AIUtilityComponent utility = BBB_VehiclePosture.Utility(man);
  if (!utility || !utility.m_PerceptionComponent || !enemy) return false;
  return utility.m_PerceptionComponent.GetTargetPerceptionObject(enemy, ETargetCategory.ENEMY) != null;
 }

 // ---------------------------------------------------------------- 1 detection

 protected void Detection()
 {
  Check("shambler-is-zombie", m_Zombie && BBB_Zombies.IsZombie(m_Zombie), string.Format("spawned %1", m_Zombie != null));
  Check("rifleman-not-zombie", m_Man && !BBB_Zombies.IsZombie(m_Man), string.Format("spawned %1", m_Man != null));
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  Check("standalone-skips-zombie-group", m_ZGroup && m_ManGroup && !groups.Contains(m_ZGroup) && groups.Contains(m_ManGroup), string.Format("%1 groups listed, zombie group listed %2, rifle group listed %3", groups.Count(), groups.Contains(m_ZGroup), groups.Contains(m_ManGroup)));
  m_Hooks.m_iRules = 0;
  bool offByHost = !BBB_Zombies.On();
  m_Hooks.m_iRules = 1;
  bool onByHost = BBB_Zombies.On();
  m_Hooks.m_iRules = -1;
  Check("host-switches-rules", offByHost && onByHost, string.Format("host 0 -> off %1, host 1 -> on %2", offByHost, onByHost));
  m_Hooks.m_Marked = m_Man;
  BBB_Zombies.Stop(); // the rifleman's answer is cached
  bool marked = BBB_Zombies.IsZombie(m_Man);
  m_Hooks.m_Marked = null;
  BBB_Zombies.Stop();
  Check("host-adds-zombie", marked && !BBB_Zombies.IsZombie(m_Man), string.Format("marked %1", marked));
  Clean();
  m_Hooks.m_iRules = 0;
  GetGame().GetCallqueue().CallLater(KeepStart, 1000);
 }

 // ---------------------------------------------------------------- 2 keep distance (off, then on)

 protected void KeepStart()
 {
  // Both arms on the same ground (the first run put the second arm 40 m further over, where the shambler
  // couldn't get past an obstacle: "[CMAI][Vault] REFUSED", it never came in and wasn't seen).
  vector at = m_vSite + m_vSide * 40;
  m_vManStart = BBB.Ground(at);
  m_vZombieStart = BBB.Ground(at + m_vDir * KEEP_START_M);
  m_Man = SpawnMan(m_vManStart, m_vDir);
  m_Zombie = SpawnZombie(m_vZombieStart, -m_vDir);
  m_fBack = 0;
  m_iPolls = 0;
  m_bPerceived = false;
  m_iSteps = BBB_Zombies.s_iBackSteps;
  if (!m_Man || !m_Zombie)
  {
   Check("keep-spawned", false, "rifleman or shambler didn't spawn");
   Finish();
   return;
  }
  GetGame().GetCallqueue().CallLater(KeepPoll, 500, true);
 }

 protected void KeepPoll()
 {
  m_iPolls++;
  if (m_Man && m_Zombie && Perceives(m_Man, m_Zombie)) m_bPerceived = true;
  vector away = m_vManStart - m_vZombieStart;
  away[1] = 0;
  away.Normalize();
  if (m_Man) m_fBack = Math.Max(m_fBack, vector.Dot(m_Man.GetOrigin() - m_vManStart, away));
  if (KEEP_S * 2 > m_iPolls) return;
  GetGame().GetCallqueue().Remove(KeepPoll);
  float gap = -1;
  if (m_Man && m_Zombie) gap = vector.DistanceXZ(m_Man.GetOrigin(), m_Zombie.GetOrigin());
  int steps = BBB_Zombies.s_iBackSteps - m_iSteps;
  bool on = m_Hooks.m_iRules != 0;
  PrintFormat("[BBB] ZOMBIETEST keep rules=%1: got back %2 m, back-steps %3, perceived %4, gap at the end %5 m", on, Math.Round(m_fBack * 10) / 10, steps, m_bPerceived, Math.Round(gap));
  if (!on)
  {
   m_fBackOff = m_fBack;
   Check("keep-off-zombie-perceived", m_bPerceived, "the rifleman saw the shambler as an enemy");
   Check("keep-off-no-zombie-steps", steps == 0, string.Format("%1 back-steps counted with the rules off", steps));
   Clean();
   m_Hooks.m_iRules = -1;
   GetGame().GetCallqueue().CallLater(KeepStart, 1000);
   return;
  }
  Check("keep-on-zombie-perceived", m_bPerceived, "the rifleman saw the shambler as an enemy");
  Check("keep-on-steps-back", steps > 0, string.Format("%1 back-steps from a zombie inside %2 m", steps, BBB_Zombies.KEEP_M));
  Check("keep-on-got-back", m_fBack >= 4, string.Format("%1 m back from where he stood", m_fBack));
  Check("keep-ab", m_fBack >= m_fBackOff + 3, string.Format("rules on %1 m, off %2 m", m_fBack, m_fBackOff));
  Clean();
  GetGame().GetCallqueue().CallLater(SurrenderStart, 1000);
 }

 // ---------------------------------------------------------------- 3 surrender

 protected void SurrenderStart()
 {
  BBB_Morale.s_bOn = false;       // no withdrawal (surrender has its own switch)
  BBB_Rearm.s_bOn = false;        // he'd go looking for magazines first
  BBB_Tactic.s_bOn = false;
  BBB_Morale.s_fSurrenderThreat = 0;
  BBB_Morale.s_fSurrenderChance = 1;
  BBB_Morale.s_iForceFate = BBB_Morale.FATE_GENUINE;
  BBB_Morale.s_fPrisonerMinS = 600;
  vector at = m_vSite + m_vSide * 120;
  m_vManStart = BBB.Ground(at);
  m_Man = SpawnMan(m_vManStart, m_vDir);
  m_Zombie = SpawnZombie(at + m_vDir * 20, -m_vDir);
  if (!m_Man || !m_Zombie)
  {
   Check("surrender-spawned", false, "rifleman or shambler didn't spawn");
   Finish();
   return;
  }
  StripMagazines(ChimeraCharacter.Cast(m_Man));
  m_iCounter = BBB_Zombies.s_iSurrendersRefused;
  m_iPolls = 0;
  m_bPerceived = false;
  GetGame().GetCallqueue().CallLater(SurrenderPoll, 500, true);
 }

 protected void SurrenderPoll()
 {
  m_iPolls++;
  if (m_Man && m_Zombie && Perceives(m_Man, m_Zombie)) m_bPerceived = true;
  if (m_iPolls < 24 && !BBB_Morale.IsPrisoner(m_Man)) return;
  GetGame().GetCallqueue().Remove(SurrenderPoll);
  int refused = BBB_Zombies.s_iSurrendersRefused - m_iCounter;
  Check("no-surrender-to-zombie", m_Man && !BBB_Morale.IsPrisoner(m_Man) && m_bPerceived, string.Format("prisoner %1, saw the shambler %2", BBB_Morale.IsPrisoner(m_Man), m_bPerceived));
  Check("zombie-captor-rule-reached", refused > 0, string.Format("%1 surrender checks found only a zombie to give up to", refused));
  // Control: men on hold fire 25 m off, the shambler still about.
  SCR_AIGroup.IgnoreSpawning(false);
  m_Captors = SCR_AIGroup.Cast(Spawn(TEAM_USSR, BBB.Ground(m_vManStart - m_vDir * 25), m_vDir));
  if (m_Captors)
  {
   m_aMine.Insert(m_Captors);
   m_Captors.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.HOLD_FIRE);
   GetGame().GetCallqueue().CallLater(CaptorsReady, 1500);
  }
  m_iPolls = 0;
  GetGame().GetCallqueue().CallLater(ControlPoll, 500, true);
 }

 protected void CaptorsReady()
 {
  if (!m_Captors) return;
  array<AIAgent> agents = {};
  m_Captors.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   IEntity body = agent.GetControlledEntity();
   if (body) { m_aMine.Insert(body); NoDamage(body); }
  }
 }

 protected void ControlPoll()
 {
  m_iPolls++;
  if (m_iPolls < 40 && !BBB_Morale.IsPrisoner(m_Man)) return;
  GetGame().GetCallqueue().Remove(ControlPoll);
  Check("surrender-to-men-still", m_Captors && BBB_Morale.IsPrisoner(m_Man), string.Format("captors spawned %1, prisoner %2 after %3 s", m_Captors != null, BBB_Morale.IsPrisoner(m_Man), m_iPolls * 0.5));
  Clean();
  BBB_Morale.s_bOn = true;
  BBB_Rearm.s_bOn = true;
  BBB_Tactic.s_bOn = true;
  GetGame().GetCallqueue().CallLater(HealStart, 1000);
 }

 protected void StripMagazines(ChimeraCharacter soldier)
 {
  if (!soldier) return;
  BaseMuzzleComponent muzzle;
  int count;
  BBB_Rearm.NeedsAmmo(soldier, muzzle, count);
  SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(soldier.FindComponent(SCR_InventoryStorageManagerComponent));
  if (!inventory || !muzzle || !muzzle.GetMagazineWell()) return;
  string well = muzzle.GetMagazineWell().Type().ToString();
  array<IEntity> carried = {};
  inventory.GetItems(carried);
  foreach (IEntity item : carried)
  {
   if (!item) continue;
   MagazineComponent magazine = MagazineComponent.Cast(item.FindComponent(MagazineComponent));
   if (!magazine || !magazine.GetMagazineWell() || magazine.GetMagazineWell().Type().ToString() != well) continue;
   inventory.TryRemoveItemFromInventory(item);
   SCR_EntityHelper.DeleteEntityAndChildren(item);
  }
 }

 // ---------------------------------------------------------------- 4 heal held, then resumed

 protected void HealStart()
 {
  vector at = m_vSite - m_vSide * 40;
  // He faces the shambler: the rule reads only what he perceives, and one spawned behind him went unseen while
  // he set off for the casualty (integration run 20261111-182942: "go" 2 s in, held 0).
  m_Man = SpawnMan(at, -m_vDir);
  SCR_AIGroup squad = m_ManGroup;
  m_Casualty = Spawn(RIFLE_US, at + m_vDir * 10, m_vDir);
  if (m_Casualty)
  {
   m_aMine.Insert(m_Casualty);
   if (squad) squad.AddAIEntityToGroup(m_Casualty);
   Activate(squad);
  }
  m_Zombie = SpawnZombie(at - m_vDir * 18, m_vDir);
  if (!m_Man || !m_Casualty || !m_Zombie)
  {
   Check("heal-spawned", false, "a rifleman or the shambler didn't spawn");
   Finish();
   return;
  }
  m_iPolls = 0;
  GetGame().GetCallqueue().CallLater(KnockOut, 500, true);
 }

 // The casualty goes down once his squadmate has seen the shambler (10 s at most).
 protected void KnockOut()
 {
  m_iPolls++;
  m_bPerceived = m_Man && m_Zombie && Perceives(m_Man, m_Zombie);
  if (!m_bPerceived && m_iPolls < 20) return;
  GetGame().GetCallqueue().Remove(KnockOut);
  ChimeraCharacter character = ChimeraCharacter.Cast(m_Casualty);
  SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(character.GetDamageManager());
  if (damage)
  {
   damage.AddParticularBleeding("Chest");
   damage.ForceUnconsciousness(0);
  }
  m_iCounter = BBB_Zombies.s_iHealsHeld;
  m_iPolls = 0;
  m_bReached = false;
  GetGame().GetCallqueue().CallLater(HeldPoll, 500, true);
 }

 protected bool AtCasualty()
 {
  return m_Man && m_Casualty && vector.DistanceXZ(m_Man.GetOrigin(), m_Casualty.GetOrigin()) <= 3.5;
 }

 protected void HeldPoll()
 {
  m_iPolls++;
  if (AtCasualty()) m_bReached = true;
  if (m_iPolls < 24) return;
  GetGame().GetCallqueue().Remove(HeldPoll);
  int held = BBB_Zombies.s_iHealsHeld - m_iCounter;
  bool down = m_Casualty && ChimeraCharacter.Cast(m_Casualty).GetCharacterController().IsUnconscious();
  Check("heal-held-near-zombie", held > 0 && !m_bReached && down, string.Format("held %1 times, went to the casualty %2, casualty down %3, had seen the shambler first %4", held, m_bReached, down, m_bPerceived));
  // The shambler goes: the way is clear.
  if (m_Zombie) SCR_EntityHelper.DeleteEntityAndChildren(m_Zombie);
  m_Zombie = null;
  m_iPolls = 0;
  GetGame().GetCallqueue().CallLater(ClearPoll, 500, true);
 }

 protected void ClearPoll()
 {
  m_iPolls++;
  if (!AtCasualty() && m_iPolls < 50) return;
  GetGame().GetCallqueue().Remove(ClearPoll);
  Check("heal-goes-when-clear", AtCasualty(), string.Format("reached the casualty after %1 s", m_iPolls * 0.5));
  Clean();
  Finish();
 }

 override protected void Finish()
 {
  if (m_OldHooks) BBB.s_Hooks = m_OldHooks;
  BBB_Zombies.Stop();
  PrintFormat("[BBB] ZOMBIETEST %1", BBB_Zombies.Describe());
  string verdict = "FAIL";
  if (m_iFailures == 0 && m_iChecks == EXPECTED) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbzombies passed=%2 failed=%3 expected=%4", verdict, m_iChecks - m_iFailures, m_iFailures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_ZombiesTest.MaybeStartZombies();
 }
}
