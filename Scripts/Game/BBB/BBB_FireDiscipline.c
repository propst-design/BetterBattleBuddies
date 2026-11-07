// Fire discipline (6 Nov 2026, Jacob: "The AI are wasting way too much ammo at range", then "way too
// much fire and not enough maneuver. Not even into cover or concealment": an FIA team and a USSR squad
// sprayed at each other at 200 m for most of their ammo and achieved little).
// What did it (code and the 6 Nov logs): BBB's two answers to a shooter a man can't see, contact fire
// (BBB_Combat: 25-900 m, every man for himself, 6 s every 26 s, boxes up to 70 m wide; 751 bursts in
// one match, 364 of them past 300 m) and return fire (BBB_ReturnFire: 3.5 s on every near miss, 6 s
// cooldown), ran at 69.5 and 71. Vanilla's own answer to fire it can't place,
// SCR_AIObserveThreatSystemBehavior, runs at 69 and its HideFromThreatSystem logic is the one that
// sprints a man to cover within 25 m and puts him prone. So BBB had men fire back where they stood
// instead of getting into cover, and their fire kept everyone's suppression up. Vanilla's cluster
// suppression adds to it (it fires faster with range, x1.25 at 600 m, out to 800 m), and exposed fire
// made a pinned man in the open fire 1.6x faster at any range.
// With this on:
//  - one gate for blind fire: contact fire and return fire both go through StartBlindFire (one 20 s
//    cooldown per man, one behaviour, BBB_AIContactFireBehavior), so one near miss can't start both;
//  - cover first: a man not in cover facing the threat (BBB_Cover) lets vanilla's hide move run. The
//    burst waits at priority 0 while he moves (SETTLE_S if he never starts, WAIT_MAX_S at most), then
//    fires BURST_S from where he ended up, at 69.5 as before;
//  - range: riflemen fire blind to RIFLE_M, machine guns to MG_M;
//  - who: at most SQUAD_BLIND men of a squad at once (waiting for cover counts); a machine gunner may
//    be one more;
//  - ammo: nobody fires blind with fewer than SPARE_MAGS spare magazines for the weapon (MG: SPARE_BELTS);
//  - vanilla's cluster suppression, on foot: held past CLUSTER_RIFLE_M / CLUSTER_MG_M and when short of
//    spare magazines (vehicle gunners keep vanilla's and Hot AI's rules);
//  - exposed fire: the faster fire and shorter first-shot wait only at a target within EXPOSED_M; at
//    range a pinned man in the open keeps his alert eyes but vanilla's pace (BBB_ExposedFire).
//  - pace at range, every kind of fire by a man on foot: vanilla's fire timing (SCR_AIGetFireTimes) gives
//    the same bursts at 200 m as at 50 m (0.75-4 rounds from a rifle, 2-7 from an MG, more on suppress);
//    only the aiming pause grows with range. Past PACE_FROM_M bursts are capped, falling to
//    RIFLE_BURST_FAR / MG_BURST_FAR at PACE_FULL_M, and the aiming pause grows to PAUSE_FAR times
//    vanilla's there (BBB_HotAI's copy of vanilla's fire timing carries the lines). Not with Hot AI's
//    fire timing on: that host asked for hot fire. Measured why (6 Nov, 200 m, rifle squads in sight):
//    1,700-1,900 rounds in 150 s for one or two men down; blind fire alone didn't account for it.
// Untouched: which target he fires at (vanilla's attack), and the squad's commanded covering fire
// (BBB_FireManeuver, BBB_TacticDrill: already 45-300 m and only on fresh reports) beyond the pace.
// Host option: -bbbFireDiscipline 0 (the old rules, for A/B).
class BBB_FireDiscipline
{
 static int s_iRead;
 static bool s_bOn = true;
 static const float RIFLE_M = 250;
 static const float MG_M = 500;
 static const float CLUSTER_RIFLE_M = 350;
 static const float CLUSTER_MG_M = 600;
 static const float EXPOSED_M = 150;
 static const int SQUAD_BLIND = 2;
 static const int SPARE_MAGS = 2;
 static const int SPARE_BELTS = 1;
 static const float SETTLE_S = 2;
 static const float WAIT_MAX_S = 6;
 static const float BURST_S = 4;
 static const float COOLDOWN_S = 20;
 static const float HALF_MIN_M = 3;
 static const float HALF_MAX_M = 15;
 static const float FIRE_RATE = 1.1;
 static const float PACE_FROM_M = 100;
 static const float PACE_FULL_M = 300;
 static const float RIFLE_BURST_NEAR = 3;	// rounds, at PACE_FROM_M
 static const float RIFLE_BURST_FAR = 1.2;	// at PACE_FULL_M: one aimed round a pull
 static const float MG_BURST_NEAR = 6;
 static const float MG_BURST_FAR = 3;
 static const float PAUSE_FAR = 2;
 // Counters for tests and the log.
 static int s_iStarted;		// blind bursts started
 static int s_iRange;		// refused: too far for his weapon
 static int s_iAmmo;		// refused: saving his last magazines
 static int s_iSquad;		// refused: enough of the squad already firing back
 static int s_iWaited;		// bursts that waited while he moved to cover
 static int s_iClusterHeld;	// cluster suppressions held (range or ammo)
 static float s_fFarthest;	// farthest blind burst started, m
 static int s_iMostAtOnce;	// most men of one squad with a blind burst at once

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbFireDiscipline", value) && value == "0");
  PrintFormat("[BBB] FIRE DISCIPLINE on=%1 blind rifle=%2m mg=%3m squad=%4 spare mags=%5 cluster rifle=%6m mg=%7m exposed=%8m", s_bOn, RIFLE_M, MG_M, SQUAD_BLIND, SPARE_MAGS, CLUSTER_RIFLE_M, CLUSTER_MG_M, EXPOSED_M);
 }

 static bool On()
 {
  Read();
  return s_bOn;
 }

 // Pace his fire at range: on, Hot AI's fire timing off, an AI on foot.
 static bool Paces(SCR_AIUtilityComponent utility)
 {
  if (!On() || BBB_HotAI.s_bOn || !utility || !utility.m_AIInfo) return false;
  if (utility.m_AIInfo.HasUnitState(EUnitState.IN_VEHICLE) || utility.m_AIInfo.HasUnitState(EUnitState.IN_TURRET)) return false;
  return BBB.PlayerOf(utility.m_OwnerEntity) == 0;
 }

 // 0 up to PACE_FROM_M, 1 from PACE_FULL_M.
 static float PaceShare(float distance)
 {
  return Math.Clamp((distance - PACE_FROM_M) / (PACE_FULL_M - PACE_FROM_M), 0, 1);
 }

 static float PacedBurst(float burst, float distance, bool gun)
 {
  float share = PaceShare(distance);
  if (share <= 0) return burst;
  float cap = RIFLE_BURST_NEAR + (RIFLE_BURST_FAR - RIFLE_BURST_NEAR) * share;
  if (gun) cap = MG_BURST_NEAR + (MG_BURST_FAR - MG_BURST_NEAR) * share;
  return Math.Min(burst, cap);
 }

 static float PacedPause(float distance)
 {
  return 1 + (PAUSE_FAR - 1) * PaceShare(distance);
 }

 static bool MachineGun(BaseWeaponComponent weapon)
 {
  return weapon && weapon.GetWeaponType() == EWeaponType.WT_MACHINEGUN;
 }

 // He still has the spare magazines (belts) a blind burst may not eat into.
 static bool Reserve(IEntity man, BaseWeaponComponent weapon)
 {
  if (!man || !weapon) return false;
  InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(man.FindComponent(InventoryStorageManagerComponent));
  if (!inventory) return true; // can't count them: vanilla's rules
  int need = SPARE_MAGS;
  if (MachineGun(weapon)) need = SPARE_BELTS;
  return inventory.GetMagazineCountByWeapon(weapon) >= need;
 }

 static SCR_AIGroup GroupOf(SCR_AIUtilityComponent utility)
 {
  if (!utility) return null;
  AIAgent agent = utility.GetAIAgent();
  if (!agent) return null;
  return SCR_AIGroup.Cast(agent.GetParentGroup());
 }

 // Men of this squad holding a blind burst now (firing or waiting to, from cover).
 static int Blind(SCR_AIGroup group)
 {
  if (!group) return 0;
  float now = GetGame().GetWorld().GetWorldTime() / 1000;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  int count;
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_AIUtilityComponent member = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   if (member && member.m_fBBBBlindUntil > now) count++;
  }
  return count;
 }

 // Why he may not fire blind now ("" when he may).
 static string Refuse(SCR_AIUtilityComponent utility, BaseWeaponComponent weapon, float distance)
 {
  if (utility.m_AIInfo && utility.m_AIInfo.HasUnitState(EUnitState.IN_VEHICLE)) return "in a vehicle";
  bool gun = MachineGun(weapon);
  float range = RIFLE_M;
  if (gun) range = MG_M;
  if (distance > range) { s_iRange++; return "too far to fire blind"; }
  if (!Reserve(utility.m_OwnerEntity, weapon)) { s_iAmmo++; return "saving his last magazines"; }
  int room = SQUAD_BLIND;
  if (gun) room++;
  if (Blind(GroupOf(utility)) >= room) { s_iSquad++; return "enough of the squad already firing back"; }
  return "";
 }

 // The one way a man fires at a shooter he can't see (contact fire, return fire): a burst into a box
 // round 'aim' (from his own hearing or his squad's report), after he has got into cover.
 static bool StartBlindFire(SCR_AIUtilityComponent utility, BaseWeaponComponent weapon, vector aim, float half, float distance, string source)
 {
  if (!utility || !weapon) return false;
  float now = GetGame().GetWorld().GetWorldTime() / 1000;
  if (now < utility.m_fBBBContactFireReady) return false;
  string why = Refuse(utility, weapon, distance);
  if (why != "")
  {
   BBB_Think.NoteUtility(utility, "FireDiscipline", source + " held: " + why + " (" + Math.Round(distance) + " m)");
   return false;
  }
  half = Math.Clamp(half, HALF_MIN_M, HALF_MAX_M);
  aim = BBB.Ground(aim);
  SCR_AISuppressionVolumeBox volume = new SCR_AISuppressionVolumeBox(Vector(aim[0] - half, aim[1] - 0.5, aim[2] - half), Vector(aim[0] + half, aim[1] + 2, aim[2] + half));
  BBB_AIContactFireBehavior fire = new BBB_AIContactFireBehavior(utility, null, volume, BURST_S, FIRE_RATE);
  fire.Disciplined(WAIT_MAX_S);
  utility.AddAction(fire);
  utility.m_fBBBContactFireReady = now + WAIT_MAX_S + BURST_S + COOLDOWN_S;
  utility.m_fBBBBlindUntil = now + WAIT_MAX_S + BURST_S;
  s_iStarted++;
  s_fFarthest = Math.Max(s_fFarthest, distance);
  s_iMostAtOnce = Math.Max(s_iMostAtOnce, Blind(GroupOf(utility)));
  BBB_Think.NoteUtility(utility, "FireDiscipline", source + ": cover first, then a burst at " + Math.Round(distance) + " m");
  return true;
 }

 // Vanilla's suppression of a spotted group, by a man on foot: held too far out or short of ammo.
 static bool HoldCluster(SCR_AIUtilityComponent utility, BaseWeaponComponent weapon, float distance)
 {
  if (!On() || !utility || !utility.m_AIInfo || !weapon) return false;
  if (utility.m_AIInfo.HasUnitState(EUnitState.IN_VEHICLE) || utility.m_AIInfo.HasUnitState(EUnitState.IN_TURRET)) return false;
  if (BBB.PlayerOf(utility.m_OwnerEntity)) return false;
  float range = CLUSTER_RIFLE_M;
  if (MachineGun(weapon)) range = CLUSTER_MG_M;
  return distance > range || !Reserve(utility.m_OwnerEntity, weapon);
 }
}

modded class SCR_AIUtilityComponent
{
 float m_fBBBBlindUntil;	// world time (s) his blind burst ends at the latest (0: none); squad count
}

// Vanilla's cluster suppression is the group's order to fire on a spotted group; it speeds up with range.
modded class SCR_AISuppressGroupClusterBehavior
{
 protected float m_fBBBDisciplineAt;
 protected bool m_bBBBDisciplineHeld;

 override float CustomEvaluate()
 {
  float priority = super.CustomEvaluate();
  if (priority <= 0 || !m_ClusterState || !m_Utility || !m_CombatComponent) return priority;
  float now = GetGame().GetWorld().GetWorldTime();
  if (now >= m_fBBBDisciplineAt)
  {
   m_fBBBDisciplineAt = now + 1000;
   bool was = m_bBBBDisciplineHeld;
   float distance = GetDistance(m_ClusterState);
   m_bBBBDisciplineHeld = BBB_FireDiscipline.HoldCluster(m_Utility, m_CombatComponent.GetCurrentWeapon(), distance);
   if (m_bBBBDisciplineHeld && !was)
   {
    BBB_FireDiscipline.s_iClusterHeld++;
    BBB_Think.NoteUtility(m_Utility, "FireDiscipline", "holds suppressive fire on a group " + Math.Round(distance) + " m away (too far, or saving his last magazines)");
   }
  }
  if (m_bBBBDisciplineHeld) return 0;
  return priority;
 }
}
