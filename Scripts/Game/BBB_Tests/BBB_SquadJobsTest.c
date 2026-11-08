// Squad leader stage 3, jobs (-bbbSquadJobsTest 1, Test-BBB.ps1 -Preset squadjobs; 7 Nov 2026). Fixture:
// a US squad built man by man (leader, machine gunner, four riflemen; no medic, so vanilla sends the
// nearest man with a bandage) holds a spot on a clear lane; a USSR fire team, invulnerable and holding
// fire, stands ENEMY_M up the lane so the squad is in contact and the gunner is firing. Everyone but
// the casualty is invulnerable. After SETTLE_S the casualty joins the squad CASUALTY_M from wherever the
// gunner has settled, on the side furthest from the other men, and goes down (chest bleed, unconscious)
// CASUALTY_DOWN_S later, so the gunner is his nearest squadmate and vanilla's helper. Placed then, not at
// spawn: the men move in the settle (the gunner ended 11-13 m from a rifleman spawned beside him in about
// half the runs: 190643, 191849, 202348). A spawn position, not a teleport; the gunner is checked again
// as he goes down, and a scene that still didn't happen makes the run INVALID, not a feature FAIL.
// Arms, a fresh squad each, AFTER_S after the casualty: off (-bbbSquadJobs off) and on.
// Measured (physical): who was sent (the medic pick, BBB_SquadLeader.s_LastRescuer); the gunner's rounds
// after the casualty (magazine counts); the closest anyone sent got to the casualty; leader's spares.
// Judged (6): spawned (2); gate, both arms: the gunner was the casualty's nearest squadmate (else the scene never
// happened: verdict INVALID, nothing else judged); off: the gunner was sent and the leader spared nobody; on: a
// rifleman was sent, the leader made that call (spared >= 1), and he reached the casualty (help still
// comes). The gunner's rounds after the casualty are logged, not judged. One run per arm: a measurement, not a statistic.
class BBB_SquadJobsTest : BBB_AimTest
{
 static ref BBB_SquadJobsTest s_Jobs;
 static const ResourceName US_MG = "{5B1996C05B1E51A4}Prefabs/Characters/Factions/BLUFOR/US_Army/Character_US_AR.et";
 static const ResourceName WP_DEFEND = "{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et";
 static const int EXPECTED = 6; // the premise gates the run (INVALID), it isn't a check
 static const float WOUND_TRY_S = 30; // wait this long for the gunner to be some rifleman's nearest squadmate
 static const float ENEMY_M = 150;
 static const float CASUALTY_M = 2;
 static const float CASUALTY_CLEAR_M = 4;  // no other squadmate this close to the casualty's spot
 static const float CASUALTY_DOWN_S = 1.5; // the new man's AI comes up before he is hit
 static const float SETTLE_S = 14;
 static const float AFTER_S = 40;
 static const float REACHED_M = 4;
 static const int OBSERVER = 22414;

 protected int m_iChecks;
 protected int m_iFailures;
 protected int m_iArm = -1;
 protected ref array<string> m_aArms;
 protected vector m_vFrom;
 protected vector m_vDir;
 protected vector m_vEnemyAt;
 protected SCR_AIGroup m_Squad;
 protected SCR_AIGroup m_Enemy;
 protected ref array<IEntity> m_aMen;
 protected ref array<IEntity> m_aFoes;
 protected IEntity m_Gunner;
 protected IEntity m_Leader;
 protected IEntity m_Casualty;
 protected IEntity m_Sent;
 protected bool m_bPremise;           // the gunner was the casualty's nearest squadmate
 protected float m_fWoundTries;
 protected ref map<string, bool> m_mPremise;
 protected BaseWeaponComponent m_GunnerWeapon;
 protected int m_iGunnerAmmo;
 protected int m_iGunnerRounds;
 protected float m_fClosest;
 protected int m_iSamples;
 protected int m_iSpared0;
 protected ref map<string, string> m_mSentWho;
 protected ref map<string, int> m_mRounds;
 protected ref map<string, float> m_mClosest;
 protected ref map<string, int> m_mSpared;

 static void MaybeStartJobs()
 {
  if (s_Jobs || !Replication.IsServer() || !System.IsCLIParam("bbbSquadJobsTest")) return;
  s_Jobs = new BBB_SquadJobsTest();
  GetGame().GetCallqueue().CallLater(s_Jobs.Begin, 12000);
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
  PrintFormat("[BBB] SQUADJOBSTEST CHECK %1 %2 %3", name, res, detail);
 }

 override protected void Begin()
 {
  m_aArms = {"off", "on"};
  if (System.IsCLIParam("bbbSquadJobsSwap")) m_aArms = {"on", "off"}; // arm order check
  m_aMen = {};
  m_aFoes = {};
  m_mSentWho = new map<string, string>();
  m_mRounds = new map<string, int>();
  m_mClosest = new map<string, float>();
  m_mSpared = new map<string, int>();
  m_mPremise = new map<string, bool>();
  m_aCheckDistances.Clear();
  m_aCheckDistances.Insert(ENEMY_M * 0.5);
  m_aCheckDistances.Insert(ENEMY_M);
  m_vFrom = BBB.Ground(Vector(1340, 0, 2770));
  m_vDir = "0 0 1";
  if (FindSite())
  {
   m_vFrom = BBB.Ground(m_aSiteFrom[0]);
   m_vDir = m_aSiteDir[0];
   LoadLane();
  }
  m_vEnemyAt = BBB.Ground(m_vFrom + m_vDir * ENEMY_M);
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers) observers.InsertObserverSP(OBSERVER, m_vFrom[0], m_vFrom[2], null);
  BBB_SquadLeader.Read();
  PrintFormat("[BBB] SQUADJOBSTEST lane at %1 heading %2, enemy at %3 (sight %4)", m_vFrom, m_vDir, m_vEnemyAt, Visible(m_vFrom, m_vEnemyAt));
  NextArm();
 }

 protected void NextArm()
 {
  m_iArm++;
  if (m_iArm >= m_aArms.Count())
  {
   Judge();
   return;
  }
  string arm = m_aArms[m_iArm];
  BBB_SquadLeader.s_bJobs = arm == "on";
  m_aMen.Clear();
  m_aFoes.Clear();
  m_Gunner = null;
  m_Leader = null;
  m_Casualty = null;
  m_Sent = null;
  m_GunnerWeapon = null;
  m_iGunnerRounds = 0;
  m_fClosest = 999;
  m_iSamples = 0;
  m_fWoundTries = 0;
  m_bPremise = false;
  BBB_SquadLeader.s_LastRescuer = null;
  // The squad, man by man: the first added leads. Side offsets across the lane.
  vector side = Vector(m_vDir[2], 0, -m_vDir[0]);
  SCR_AIGroup.IgnoreSpawning(true);
  m_Squad = SCR_AIGroup.Cast(Spawn(TEAM_US, m_vFrom, m_vDir));
  SCR_AIGroup.IgnoreSpawning(false);
  if (m_Squad)
  {
   m_Squad.m_aUnitPrefabSlots.Clear();
   m_Leader = Add(RIFLE_US, m_vFrom - side * 14);
   m_Gunner = Add(US_MG, m_vFrom);
   Add(RIFLE_US, m_vFrom + side * 6);
   Add(RIFLE_US, m_vFrom + side * 10);
   Add(RIFLE_US, m_vFrom + side * 13);
   Add(RIFLE_US, m_vFrom - side * 10);
  }
  m_Enemy = SCR_AIGroup.Cast(Spawn(TEAM_USSR, m_vEnemyAt, m_vDir * -1));
  PrintFormat("[BBB] SQUADJOBSTEST arm %1: jobs %2", arm, BBB_SquadLeader.s_bJobs);
  GetGame().GetCallqueue().CallLater(Ready, 4000);
 }

 protected IEntity Add(ResourceName prefab, vector at)
 {
  m_Squad.m_aUnitPrefabSlots.Insert(prefab);
  IEntity man = Spawn(prefab, BBB.Ground(at), m_vDir);
  if (!man) return null;
  m_Squad.AddAIEntityToGroup(man);
  m_aMen.Insert(man);
  return man;
 }

 protected void Ready()
 {
  bool leaderFirst = m_Squad && m_Squad.GetLeaderEntity() == m_Leader;
  bool spawned = m_Squad && m_aMen.Count() == 6 && m_Gunner && m_Enemy && leaderFirst;
  array<AIAgent> agents = {};
  if (m_Enemy) m_Enemy.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!body) continue;
   PinDetail(agent);
   body.GetDamageManager().EnableDamageHandling(false);
   m_aFoes.Insert(body);
  }
  agents.Clear();
  if (m_Squad) m_Squad.GetAgents(agents);
  foreach (AIAgent member : agents)
  {
   PinDetail(member);
   ChimeraCharacter man = ChimeraCharacter.Cast(member.GetControlledEntity());
   if (man) man.GetDamageManager().EnableDamageHandling(false);
  }
  Check(m_aArms[m_iArm] + "-spawned", spawned && m_aFoes.Count() >= 3, string.Format("squad %1, foes %2, leader first %3", m_aMen.Count(), m_aFoes.Count(), leaderFirst));
  if (!spawned)
  {
   EndArm();
   return;
  }
  Stay(m_Enemy, m_vEnemyAt, EAIGroupCombatMode.HOLD_FIRE);
  AIWaypoint held = Stay(m_Squad, m_vFrom, EAIGroupCombatMode.FIRE_AT_WILL);
  // A tight hold keeps the men bunched (a wide one let them spread 20 m in the settle, so the gunner was
  // often nobody's nearest squadmate and the scene never came: runs 190643, 191849).
  if (held) held.SetCompletionRadius(6);
  GetGame().GetCallqueue().CallLater(Wound, SETTLE_S * 1000);
 }

 protected AIWaypoint Stay(SCR_AIGroup group, vector at, EAIGroupCombatMode mode)
 {
  group.ActivateAI();
  group.GetGroupUtilityComponent().SetCombatMode(mode);
  AIWaypoint stay = AIWaypoint.Cast(Spawn(WP_DEFEND, at, "0 0 1"));
  if (stay) group.AddWaypoint(stay);
  return stay;
 }

 protected SCR_AIUtilityComponent UtilityOf(IEntity man)
 {
  if (!man) return null;
  AIControlComponent control = AIControlComponent.Cast(man.FindComponent(AIControlComponent));
  if (!control || !control.GetControlAIAgent()) return null;
  return SCR_AIUtilityComponent.Cast(control.GetControlAIAgent().FindComponent(SCR_AIUtilityComponent));
 }

 protected void Wound()
 {
  // The casualty's spot: CASUALTY_M from the gunner, on whichever of 12 bearings is furthest from every
  // other squadmate; usable when none of them is within CASUALTY_CLEAR_M of it.
  vector spot;
  float best = -1;
  vector at = m_Gunner.GetOrigin();
  for (int k = 0; k < 12; k++)
  {
   float angle = Math.PI2 * k / 12.0;
   vector candidate = at + Vector(Math.Sin(angle), 0, Math.Cos(angle)) * CASUALTY_M;
   float clear = float.MAX;
   foreach (IEntity other : m_aMen)
   {
    if (!other || other == m_Gunner) continue;
    float apart = vector.DistanceXZ(other.GetOrigin(), candidate);
    if (apart < clear) clear = apart;
   }
   if (clear <= best) continue;
   best = clear;
   spot = candidate;
  }
  // Not yet (someone is standing on the gunner): try again shortly.
  if (best < CASUALTY_CLEAR_M && m_fWoundTries < WOUND_TRY_S)
  {
   m_fWoundTries += 2;
   GetGame().GetCallqueue().CallLater(Wound, 2000);
   return;
  }
  m_Casualty = Add(RIFLE_US, spot);
  if (m_Casualty)
  {
   AIControlComponent control = AIControlComponent.Cast(m_Casualty.FindComponent(AIControlComponent));
   if (control) PinDetail(control.GetControlAIAgent());
  }
  PrintFormat("[BBB] SQUADJOBSTEST casualty joins %1 m from the gunner, nearest other squadmate %2 m (tries %3 s)", CASUALTY_M, best, m_fWoundTries);
  GetGame().GetCallqueue().CallLater(GoDown, CASUALTY_DOWN_S * 1000);
 }

 // Is the gunner the casualty's nearest squadmate right now?
 protected bool GunnerNearest()
 {
  if (!m_Casualty || !m_Gunner) return false;
  IEntity nearest;
  float nearestDistance = float.MAX;
  foreach (IEntity other : m_aMen)
  {
   if (!other || other == m_Casualty) continue;
   float apart = vector.DistanceXZ(other.GetOrigin(), m_Casualty.GetOrigin());
   if (apart >= nearestDistance) continue;
   nearest = other;
   nearestDistance = apart;
  }
  return nearest == m_Gunner;
 }

 protected void GoDown()
 {
  m_bPremise = GunnerNearest();
  ChimeraCharacter character = ChimeraCharacter.Cast(m_Casualty);
  SCR_CharacterDamageManagerComponent damage;
  if (character) damage = SCR_CharacterDamageManagerComponent.Cast(character.GetDamageManager());
  if (damage)
  {
   damage.EnableDamageHandling(true);
   damage.AddParticularBleeding("Chest");
   damage.ForceUnconsciousness(0);
  }
  BBB_SquadLeader.s_LastRescuer = null;
  m_iSpared0 = BBB_SquadLeader.s_iSpared;
  SCR_AIUtilityComponent gunner = UtilityOf(m_Gunner);
  if (gunner && gunner.m_CombatComponent) m_GunnerWeapon = gunner.m_CombatComponent.GetCurrentWeapon();
  m_iGunnerAmmo = -1;
  if (m_GunnerWeapon && m_GunnerWeapon.GetCurrentMagazine()) m_iGunnerAmmo = m_GunnerWeapon.GetCurrentMagazine().GetAmmoCount();
  bool down = character && character.GetCharacterController().IsUnconscious();
  bool contact = m_Squad.GetGroupUtilityComponent().m_Perception.m_MostDangerousCluster != null;
  float gunnerApart = 999;
  if (m_Casualty) gunnerApart = vector.DistanceXZ(m_Gunner.GetOrigin(), m_Casualty.GetOrigin());
  PrintFormat("[BBB] SQUADJOBSTEST casualty down (unconscious %1, gunner nearest %4), gunner %2 m away, squad in contact %3", down, gunnerApart, contact, m_bPremise);
  GetGame().GetCallqueue().CallLater(Sample, 500, true);
 }

 protected void Sample()
 {
  m_iSamples++;
  float t = 0.5 * m_iSamples;
  if (!m_Sent && BBB_SquadLeader.s_LastRescuer) m_Sent = BBB_SquadLeader.s_LastRescuer;
  if (m_Sent && m_Casualty)
  {
   float d = vector.DistanceXZ(m_Sent.GetOrigin(), m_Casualty.GetOrigin());
   if (d < m_fClosest) m_fClosest = d;
  }
  // The gunner's rounds (a new box counts from its own start).
  SCR_AIUtilityComponent gunner = UtilityOf(m_Gunner);
  if (gunner && gunner.m_CombatComponent)
  {
   BaseWeaponComponent weapon = gunner.m_CombatComponent.GetCurrentWeapon();
   if (weapon && weapon == m_GunnerWeapon && weapon.GetCurrentMagazine())
   {
    int ammo = weapon.GetCurrentMagazine().GetAmmoCount();
    if (m_iGunnerAmmo >= 0 && ammo < m_iGunnerAmmo) m_iGunnerRounds += m_iGunnerAmmo - ammo;
    m_iGunnerAmmo = ammo;
   }
  }
  if ((m_iSamples % 10) == 0)
  {
   string who = "nobody yet";
   if (m_Sent) who = Role(m_Sent) + ", " + Math.Round(vector.DistanceXZ(m_Sent.GetOrigin(), m_Casualty.GetOrigin())).ToString() + " m from the casualty";
   PrintFormat("[BBB] SQUADJOBSTEST t=%1 sent: %2; gunner rounds since %3", t, who, m_iGunnerRounds);
  }
  if (t < AFTER_S) return;
  GetGame().GetCallqueue().Remove(Sample);
  EndArm();
 }

 protected string Role(IEntity man)
 {
  if (!man) return "nobody";
  if (man == m_Gunner) return "the machine gunner";
  if (man == m_Leader) return "the leader";
  return "a rifleman";
 }

 protected void EndArm()
 {
  string arm = m_aArms[m_iArm];
  m_mSentWho.Set(arm, Role(m_Sent));
  m_mRounds.Set(arm, m_iGunnerRounds);
  m_mClosest.Set(arm, m_fClosest);
  m_mSpared.Set(arm, BBB_SquadLeader.s_iSpared - m_iSpared0);
  m_mPremise.Set(arm, m_bPremise);
  PrintFormat("[BBB] SQUADJOBSTEST RESULT %1: sent %2 (closest %3 m), gunner rounds after the casualty %4, leader spared %5", arm, Role(m_Sent), m_fClosest, m_iGunnerRounds, BBB_SquadLeader.s_iSpared - m_iSpared0);
  foreach (IEntity man : m_aMen)
   if (man) SCR_EntityHelper.DeleteEntityAndChildren(man);
  foreach (IEntity foe : m_aFoes)
   if (foe) SCR_EntityHelper.DeleteEntityAndChildren(foe);
  if (m_Squad) SCR_EntityHelper.DeleteEntityAndChildren(m_Squad);
  if (m_Enemy) SCR_EntityHelper.DeleteEntityAndChildren(m_Enemy);
  GetGame().GetCallqueue().CallLater(NextArm, 3000);
 }

 protected string GotS(map<string, string> values, string arm)
 {
  if (!values.Contains(arm)) return "missing";
  return values.Get(arm);
 }

 protected int GotI(map<string, int> values, string arm)
 {
  if (!values.Contains(arm)) return -1;
  return values.Get(arm);
 }

 protected float GotF(map<string, float> values, string arm)
 {
  if (!values.Contains(arm)) return 999;
  return values.Get(arm);
 }

 protected void Judge()
 {
  // The scene: in both arms the gunner was the casualty's nearest squadmate when he went down. Without it
  // the run says nothing about the feature: INVALID, and the feature checks aren't judged.
  bool sceneOff = m_mPremise.Contains("off") && m_mPremise.Get("off");
  bool sceneOn = m_mPremise.Contains("on") && m_mPremise.Get("on");
  if (!sceneOff || !sceneOn)
  {
   PrintFormat("[BBB] SQUADJOBSTEST CHECK premise-both-arms INVALID off %1, on %2 (scene not established; feature not judged)", sceneOff, sceneOn);
   PrintFormat("[ACR] VERDICT INVALID test=bbbsquadjobs reason=scene-not-established passed=%1 failed=%2 expected=%3", m_iChecks - m_iFailures, m_iFailures, EXPECTED);
   Print("[ACR] TEST COMPLETE");
   return;
  }
  Print("[BBB] SQUADJOBSTEST premise-both-arms established: the gunner was the casualty's nearest squadmate in both arms");
  Check("off-gunner-sent", GotS(m_mSentWho, "off") == "the machine gunner" && GotI(m_mSpared, "off") == 0, string.Format("sent %1, spared %2", GotS(m_mSentWho, "off"), GotI(m_mSpared, "off")));
  Check("on-rifleman-sent", GotS(m_mSentWho, "on") == "a rifleman", GotS(m_mSentWho, "on"));
  // The leader's own decision (vanilla would have sent the gunner); the gunner's rounds are only logged:
  // a quick bandage barely takes him off the gun (first runs: 61-77 rounds while healing).
  Check("on-spared", GotI(m_mSpared, "on") >= 1, string.Format("%1 (gunner rounds after the casualty: on %2, off %3)", GotI(m_mSpared, "on"), GotI(m_mRounds, "on"), GotI(m_mRounds, "off")));
  Check("on-help-came", GotF(m_mClosest, "on") <= REACHED_M, string.Format("closest %1 m (off arm %2 m)", GotF(m_mClosest, "on"), GotF(m_mClosest, "off")));
  Finish();
 }

 override protected void Finish()
 {
  string verdict = "FAIL";
  if (m_iFailures == 0 && m_iChecks == EXPECTED) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=bbbsquadjobs passed=%2 failed=%3 expected=%4", verdict, m_iChecks - m_iFailures, m_iFailures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_SquadJobsTest.MaybeStartJobs();
 }
}
