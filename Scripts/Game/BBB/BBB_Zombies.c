// Zombie rules (11 Nov 2026, Jacob: "rebalance how the soldiers behave in zombies mode").
// Project Outbreak (F1D4B9B5245B36ED) and the creature mods built on Creature Melee AI (4FD4A1BD550793F4)
// are found at runtime by script class name, so BBB has no dependency on either:
//  - every Creature Melee AI creature, Outbreak's zombies included, carries CMAI_CreatureBrainComponent;
//  - Outbreak's own zombie bodies carry Z_BodyComponent (that class exists only with Outbreak loaded).
// A host can add its own (BBB_Hooks.IsZombie) and switch the rules on or off (BBB_Hooks.ZombieRules).
//
// The rules are judged per threat, not per mission: a soldier facing men still fights men the normal way,
// even in a zombie mission. Zombies don't shoot, don't suppress, don't take cover and don't take prisoners,
// so the habits that pay against men cost lives against them. With the rules on:
//  - surrender: nobody gives up to a zombie (BBB_Morale: a zombie is never the captor);
//  - fire and manoeuvre, squad tactics: no bounds, flanks or searches against zombies alone. A squad whose
//    only contacts are zombies holds together and shoots (BBB_Tactic HOLD, no vanilla advances); one with
//    ground to take carries on with its order (BBB_FireManeuver counts only men as fresh contacts);
//  - suppression: no suppressive fire on a group of zombies (vanilla's cluster suppression is held);
//  - distance: a soldier whose target is a zombie inside KEEP_M steps back from it while aiming (vanilla's
//    own back-step from a too-close target, which normally starts at 3 m), unless he is indoors (a building
//    is the refuge), on a player's or Game Master's order, or on a committed BBB job;
//  - medics and drag: no treating and no dragging while a zombie is within CLEAR_M of the healer or the
//    casualty (get clear first), and a dragger drops the casualty to fight one inside DEFEND_M. Once no hold has
//    fired for RESUME_AFTER_MS and nobody is on his way, the group's heal job is run again (vanilla's own replan,
//    as after a failed heal), or a new one is added as vanilla's wounded report does: a held heal could sit
//    waiting on a medic who never went (11 Nov tests: no one went in 25 s after a 20 s hold);
//  - rearm: no trip to a body while a zombie is within CLEAR_M;
//  - vehicles: crews of any vehicle stay aboard rather than get out to heal with zombies about, and in a
//    fight against zombies alone the crew keep their seats and riders of a moving vehicle don't jump out.
// Only what the soldier himself perceived counts (his own targets, last detected position).
//  -bbbZombies auto|1|0   auto (default): on when Project Outbreak or Creature Melee AI is loaded, unless the
//                         host says otherwise; 1 always; 0 never.
// A casualty whose heal a zombie hold stopped (BBB_Zombies.HoldHeal), watched until someone goes to him.
class BBB_ZombieHeldHeal : Managed
{
 IEntity m_Casualty;
 SCR_AIGroup m_Group;
 float m_fHeldAt;   // world ms of the last hold
 float m_fNextTry;  // world ms
 int m_iTries;
}

class BBB_Zombies
{
 static const float KEEP_M = 10;      // a zombie target closer than this: step back from it
 static const float CLEAR_M = 30;     // no heal, drag or rearm trip while a zombie is this close
 static const float DEFEND_M = 15;    // a dragger lets go and fights a zombie this close
 static const float FRESH_S = 5;      // a zombie counts while detected this recently
 static const float HUMAN_S = 20;     // a man seen this recently makes it a fight with men
 static const float VEHICLE_M = 60;   // vehicles: zombies this close make it a zombie fight
 static const float RIDE_MS = 2;      // riders stay aboard a vehicle moving at least this fast (m/s)
 static const int CACHE_MAX = 1024;
 static const float RESUME_AFTER_MS = 3000;  // no hold for this long: the hold has lifted
 static const float RESUME_RETRY_MS = 10000; // asked again this often while nobody goes ...
 static const int RESUME_TRIES = 3;          // ... this many times

 protected static int s_iRead;
 protected static string s_sMode;              // "auto", "1", "0" (bare: static-init budget)
 protected static bool s_bLooked;
 protected static typename s_tBrain;           // CMAI_CreatureBrainComponent (Creature Melee AI)
 protected static typename s_tBody;            // Z_BodyComponent (Project Outbreak)
 protected static ref map<IEntity, bool> s_mCache;
 protected static ref array<BaseTarget> s_aTargets;
 protected static ref array<ref BBB_ZombieHeldHeal> s_aHeld;
 static int s_iHealsResumed;
 static int s_iSurrendersRefused, s_iBackSteps, s_iHealsHeld, s_iDragsRefused, s_iRearmsHeld, s_iClustersHeld, s_iSquadHolds, s_iStayedAboard;

 // Called by BBB_StaticInit at game start (static initializer budget: tools/check_static_init.py).
 static void BBB_InitStatics()
 {
  s_mCache = new map<IEntity, bool>();
  s_aTargets = {};
  s_aHeld = {};
  s_sMode = "auto";
 }

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_sMode = "auto";
  if (BBB_Settings.Get("bbbZombies", value)) s_sMode = value;
  if (s_mCache) s_mCache.Clear();
  PrintFormat("[BBB] ZOMBIES setting=%1 outbreak=%2 creatures=%3 host=%4 rules=%5", s_sMode, IsLoaded(), CreaturesLoaded(), HostRules(), On());
 }

 static void Stop()
 {
  if (s_mCache) s_mCache.Clear();
  if (s_aTargets) s_aTargets.Clear();
  if (s_aHeld) s_aHeld.Clear();
 }

 protected static void Look()
 {
  if (s_bLooked) return;
  s_bLooked = true;
  s_tBrain = String("CMAI_CreatureBrainComponent").ToType();
  s_tBody = String("Z_BodyComponent").ToType();
 }

 // Project Outbreak is loaded.
 static bool IsLoaded()
 {
  Look();
  if (s_tBody) return true;
  return false;
 }

 // Creature Melee AI is loaded (Outbreak and other creature mods build on it).
 static bool CreaturesLoaded()
 {
  Look();
  if (s_tBrain) return true;
  return false;
 }

 protected static int HostRules()
 {
  if (!BBB.s_Hooks) return -1;
  return BBB.s_Hooks.ZombieRules();
 }

 // The zombie rules are on for this mission.
 static bool On()
 {
  Read();
  if (s_sMode == "1") return true;
  if (s_sMode == "0") return false;
  int host = HostRules();
  if (host == 1) return true;
  if (host == 0) return false;
  return IsLoaded() || CreaturesLoaded();
 }

 // A zombie or melee creature (whether or not the rules are on).
 static bool IsZombie(IEntity entity)
 {
  ChimeraCharacter body = ChimeraCharacter.Cast(entity);
  if (!body) return false;
  Look();
  bool zombie;
  if (s_mCache.Find(body, zombie)) return zombie;
  if (s_tBrain && body.FindComponent(s_tBrain)) zombie = true;
  else if (s_tBody && body.FindComponent(s_tBody)) zombie = true;
  else if (BBB.s_Hooks && BBB.s_Hooks.IsZombie(body)) zombie = true;
  if (s_mCache.Count() >= CACHE_MAX) s_mCache.Clear();
  s_mCache.Set(body, zombie);
  return zombie;
 }

 // The rules are on and this is a zombie.
 static bool Is(IEntity entity)
 {
  return entity && On() && IsZombie(entity);
 }

 static bool Target(BaseTarget target)
 {
  return target && Is(target.GetTargetEntity());
 }

 // The nearest zombie this soldier perceives (detected at most FRESH_S ago) within range of a point,
 // by its last detected position: its distance, or -1 for none. Rules off: -1.
 static float NearestTo(SCR_AIUtilityComponent utility, vector point, float range)
 {
  if (!utility || !utility.m_PerceptionComponent || !On()) return -1;
  s_aTargets.Clear();
  utility.m_PerceptionComponent.GetTargetsList(s_aTargets, ETargetCategory.ENEMY);
  float best = -1;
  foreach (BaseTarget target : s_aTargets)
  {
   if (!target || target.IsDisarmed()) continue;
   float age = target.GetTimeSinceDetected();
   if (age < 0 || age > FRESH_S) continue;
   IEntity enemy = target.GetTargetEntity();
   if (!enemy || !BBB.IsConscious(enemy) || !IsZombie(enemy)) continue;
   float distance = vector.Distance(point, target.GetLastDetectedPosition());
   if (distance <= range && (best < 0 || distance < best)) best = distance;
  }
  s_aTargets.Clear();
  return best;
 }

 static float Nearest(SCR_AIUtilityComponent utility, float range)
 {
  if (!utility || !utility.m_OwnerEntity) return -1;
  return NearestTo(utility, utility.m_OwnerEntity.GetOrigin(), range);
 }

 // A man (not a zombie) this soldier detected at most maxAge s ago.
 static bool HumansKnown(SCR_AIUtilityComponent utility, float maxAge)
 {
  if (!utility || !utility.m_PerceptionComponent) return false;
  s_aTargets.Clear();
  utility.m_PerceptionComponent.GetTargetsList(s_aTargets, ETargetCategory.ENEMY);
  bool found;
  foreach (BaseTarget target : s_aTargets)
  {
   if (!target || target.IsDisarmed()) continue;
   float age = target.GetTimeSinceDetected();
   if (age < 0 || age > maxAge) continue;
   if (!IsZombie(target.GetTargetEntity())) { found = true; break; }
  }
  s_aTargets.Clear();
  return found;
 }

 // A fight against zombies alone: one within range, no man seen lately.
 static bool OnlyZombies(SCR_AIUtilityComponent utility, float range)
 {
  return Nearest(utility, range) >= 0 && !HumansKnown(utility, HUMAN_S);
 }

 // ---- the rules, one call per feature ----

 // Vanilla's back-step from a too-close target (SCR_AICombatMoveLogic_Attack), widened for zombies.
 static bool KeepAway(SCR_AIUtilityComponent utility, BaseTarget target, float distance)
 {
  if (distance >= KEEP_M || !utility || !Target(target) || !target.GetTargetEntity() || !BBB.IsConscious(target.GetTargetEntity())) return false;
  IEntity soldier = utility.m_OwnerEntity;
  if (!soldier || BBB.PlayerOf(soldier) || BBB_Threat.UnderRoof(soldier)) return false;
  if (BBB_OrderIntent.Blocks(utility) || BBB_Claims.Committed(soldier) || BBB_Drag.Busy(soldier)) return false;
  return true;
 }

 // A heal waits: a zombie close to the healer or the casualty.
 static bool HoldHeal(SCR_AIUtilityComponent utility, IEntity casualty)
 {
  if (!utility || !casualty || !On()) return false;
  if (Nearest(utility, CLEAR_M) < 0 && NearestTo(utility, casualty.GetOrigin(), CLEAR_M) < 0) return false;
  s_iHealsHeld++;
  Remember(casualty, utility.m_OwnerEntity);
  return true;
 }

 protected static void Remember(IEntity casualty, IEntity healer)
 {
  float now = GetGame().GetWorld().GetWorldTime();
  foreach (BBB_ZombieHeldHeal held : s_aHeld)
  {
   if (held.m_Casualty != casualty) continue;
   held.m_fHeldAt = now;
   held.m_iTries = 0;
   return;
  }
  BBB_ZombieHeldHeal entry = new BBB_ZombieHeldHeal();
  entry.m_Casualty = casualty;
  entry.m_Group = BBB_Medic.GroupOf(casualty);
  if (!entry.m_Group) entry.m_Group = BBB_Medic.GroupOf(healer);
  entry.m_fHeldAt = now;
  s_aHeld.Insert(entry);
 }

 // Once a second (BBB.Tick): a heal a zombie held, now unheld, gets going again. Only casualties a hold touched.
 static void Tick()
 {
  if (!s_aHeld || s_aHeld.IsEmpty()) return;
  float now = GetGame().GetWorld().GetWorldTime();
  for (int i = s_aHeld.Count() - 1; i >= 0; i--)
  {
   BBB_ZombieHeldHeal held = s_aHeld[i];
   IEntity casualty = held.m_Casualty;
   if (!casualty || !held.m_Group || !SCR_AIDamageHandling.IsAlive(casualty) || !SCR_AIDamageHandling.IsCharacterWounded(casualty)) { s_aHeld.Remove(i); continue; }
   if (now - held.m_fHeldAt < RESUME_AFTER_MS || now < held.m_fNextTry) continue;
   // Someone is on his way (BBB_Medic marks a healer whose heal is running): it resumed.
   bool going;
   foreach (IEntity healer, IEntity target : BBB_Medic.s_mGoingTo)
    if (target == casualty && healer) { going = true; break; }
   if (going || held.m_iTries >= RESUME_TRIES)
   {
    if (!going) PrintFormat("[BBB] ZOMBIES heal for %1: asked %2 times after the hold, nobody went", BBB_Medic.Name(casualty), held.m_iTries);
    s_aHeld.Remove(i);
    continue;
   }
   held.m_iTries++;
   held.m_fNextTry = now + RESUME_RETRY_MS;
   string how = ResumeHeal(held.m_Group, casualty);
   if (how.IsEmpty()) continue;
   s_iHealsResumed++;
   PrintFormat("[BBB] ZOMBIES heal for %1: the hold lifted, %2", BBB_Medic.Name(casualty), how);
   BBB_Think.Note(casualty, "Zombies", "zombies clear: his heal is asked for again (" + how + ")");
  }
 }

 // Vanilla's group heal job for this casualty runs again and picks a medic (what a failed heal does); none left:
 // a new one, as vanilla's wounded report adds it. "" when the group can't be asked.
 protected static string ResumeHeal(SCR_AIGroup group, IEntity casualty)
 {
  SCR_AIGroupUtilityComponent utility = group.GetGroupUtilityComponent();
  if (!utility) return "";
  array<ref AIActionBase> actions = {};
  utility.GetActions(actions);
  foreach (AIActionBase action : actions)
  {
   SCR_AIHealActivity heal = SCR_AIHealActivity.Cast(action);
   if (!heal || heal.m_EntityToHeal.m_Value != casualty) continue;
   heal.SetActionIsSuspended(false);
   return "the squad's heal job picks a medic again";
  }
  utility.AddAction(new SCR_AIHealActivity(utility, null, casualty));
  return "a new heal job for the squad";
 }

 // A dragger lets go: a zombie inside DEFEND_M.
 static bool Defend(SCR_AIUtilityComponent utility)
 {
  return Nearest(utility, DEFEND_M) >= 0;
 }

 // No trip to a body for ammunition.
 static bool HoldRearm(SCR_AIUtilityComponent utility)
 {
  if (Nearest(utility, CLEAR_M) < 0) return false;
  s_iRearmsHeld++;
  return true;
 }

 // A suppression cluster made only of zombies.
 static bool ZombieCluster(SCR_AITargetClusterState cluster)
 {
  if (!cluster || !cluster.m_Cluster || !cluster.m_Cluster.m_aEntities || cluster.m_Cluster.m_aEntities.IsEmpty() || !On()) return false;
  foreach (IEntity member : cluster.m_Cluster.m_aEntities)
   if (!IsZombie(member)) return false;
  return true;
 }

 // Vanilla's fight dismount turned down: zombies alone about, and he is crew (any seat but cargo), or a rider
 // of a vehicle still moving.
 static bool StayAboard(SCR_AIUtilityComponent utility, IEntity vehicle)
 {
  if (!utility || !vehicle || !On()) return false;
  IEntity soldier = utility.m_OwnerEntity;
  if (!soldier || BBB.PlayerOf(soldier) || BBB.Health(vehicle) <= 0 || BBB.s_Hooks.LeaveAlone(vehicle)) return false;
  if (!OnlyZombies(utility, VEHICLE_M)) return false;
  if (BBB_Transport.SeatOf(soldier) == ECompartmentType.CARGO)
  {
   Physics physics = vehicle.GetPhysics();
   if (!physics || physics.GetVelocity().Length() < RIDE_MS) return false;
  }
  s_iStayedAboard++;
  return true;
 }

 static string Describe()
 {
  // string.Format takes at most 9 arguments.
  string counts = string.Format("surrenders refused=%1 back-steps=%2 heals held=%3 heals resumed=%4 drags refused=%5 rearms held=%6 clusters held=%7", s_iSurrendersRefused, s_iBackSteps, s_iHealsHeld, s_iHealsResumed, s_iDragsRefused, s_iRearmsHeld, s_iClustersHeld);
  return string.Format("zombies on=%1 outbreak=%2 creatures=%3 %4", On(), IsLoaded(), CreaturesLoaded(), counts);
 }
}

// Vanilla's step back from a target closer than the weapon's minimum (3 m) also runs for a zombie inside
// BBB_Zombies.KEEP_M: he backs off at a run while aiming (2 s at a time, repeated while it stays close).
modded class SCR_AICombatMoveLogic_Attack
{
 override protected bool MoveFromTargetCondition()
 {
  if (super.MoveFromTargetCondition()) return true;
  return BBB_Zombies.KeepAway(m_Utility, m_Target, m_fTargetDist);
 }

 override protected void PushRequestMoveFromTarget()
 {
  super.PushRequestMoveFromTarget();
  if (m_fTargetDist < 3 || !BBB_Zombies.Target(m_Target) || !m_Utility) return;
  BBB_Zombies.s_iBackSteps++;
  BBB_Think.NoteUtility(m_Utility, "Zombies", string.Format("zombie at %1 m: steps back while aiming", Math.Round(m_fTargetDist)));
 }
}

// No suppressive fire on a group of zombies: they don't duck.
modded class SCR_AISuppressGroupClusterBehavior
{
 protected float m_fBBBZombieAt;
 protected bool m_bBBBZombieHeld;

 override float CustomEvaluate()
 {
  float priority = super.CustomEvaluate();
  if (priority <= 0) return priority;
  float now = GetGame().GetWorld().GetWorldTime();
  if (now >= m_fBBBZombieAt)
  {
   m_fBBBZombieAt = now + 1000;
   bool was = m_bBBBZombieHeld;
   m_bBBBZombieHeld = BBB_Zombies.ZombieCluster(m_ClusterState);
   if (m_bBBBZombieHeld && !was)
   {
    BBB_Zombies.s_iClustersHeld++;
    if (m_Utility) BBB_Think.NoteUtility(m_Utility, "Zombies", "no suppressive fire on zombies: aimed shots only");
   }
  }
  if (m_bBBBZombieHeld) return 0;
  return priority;
 }
}
