// Specialist teams (Jacob, 1 Nov 2026): a machine gunner or AT gunner and the assistant who
// carries his ammunition stay together and work the gun together.
// Pairing comes from kit, not prefab names (modded factions work): a man carrying a magazine
// or rocket for a well no weapon of his takes is an assistant; he is paired with the nearest
// squadmate whose machine gun or reloadable launcher takes it. One assistant per gunner.
// Re-paired every PAIR_MS, a couple of squads a tick (a death, a swap or a loot changes the kit).
// Leash: an assistant more than LEASH_M from his gunner walks back to within HEEL_M (above
// formation moves; above attacking only when he is LOST_M away in contact, so in a fight he
// keeps shooting unless he is really cut off).
// Ammo: at the gunner's last spare magazine (no spare rocket for a launcher) the assistant
// walks over and hands him half his stock (at least one), above attacking once the gunner
// has no spare at all. A gunner's kit is often full at his last spare (a PKM box doesn't fit
// until he loads it): then he tries again once the gunner has fewer spares than that.
// While he can, Rearm doesn't send the gunner to a body (Supplies()).
// Gunner dead with his launcher on him: the assistant takes it and the rockets on the body.
// Never: a player's group or soldier, an order that blocks (OrderIntent), a squad withdrawing,
// a man dragging or healing, a man another BBB job holds. Supply and pick-up claim REARM.
//  -bbbSpecialistTeams 0   off
class BBB_Pair : Managed
{
 SCR_AIGroup m_Group;
 ChimeraCharacter m_Gunner;
 ChimeraCharacter m_Assistant;
 string m_sWell;
 bool m_bLauncher;
 BaseMuzzleComponent m_GunMuzzle; // his gun's, from the last pairing
 ref SCR_AIMoveIndividuallyBehavior m_Move;
 vector m_vMoveTo;
 int m_iJob;
 float m_fJobSince;
 float m_fNextSupply;
 float m_fNextLeash;
 int m_iFullAt = 1000; // the gunner's spares when his kit last had no room
}

class BBB_Teams
{
 static const int NONE = 0;
 static const int LEASH = 1;
 static const int SUPPLY = 2;
 static const int PICKUP = 3;
 static const float PAIR_MS = 8000;
 static const int PAIR_GROUPS_PER_TICK = 2; // spread the kit reads over ticks
 static const float LEASH_M = 20;
 static const float HEEL_M = 6;
 static const float LOST_M = 40;
 static const float LEASH_TIMEOUT_MS = 30000;
 static const float LEASH_PAUSE_MS = 8000;
 static const float SUPPLY_TIMEOUT_MS = 25000;
 static const float SUPPLY_BACKOFF_MS = 30000;
 static const float REISSUE_M = 5;
 static const int MG_TARGET = 4;      // spare magazines a handover tops a gunner up to
 static const int LAUNCHER_TARGET = 2;
 static bool s_bOn = true;
 protected static int s_iRead;
 protected static ref array<ref BBB_Pair> s_aPairs = {};
 protected static ref map<SCR_AIGroup, float> s_mNextPair = new map<SCR_AIGroup, float>();
 static ref array<IEntity> s_aVanilla = {}; // test only: soldiers never paired
 static int s_iPaired;    // tests read these
 static int s_iLeashes;
 static int s_iHandovers;
 static int s_iItemsHanded;
 static int s_iSupplyFailed;
 static int s_iPickups;

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbSpecialistTeams", value) && value == "0");
  PrintFormat("[BBB] SPECIALIST TEAMS on=%1 leash=%2m heel=%3m", s_bOn, LEASH_M, HEEL_M);
 }

 static float Now() { return GetGame().GetWorld().GetWorldTime(); }

 // A short name for log lines (formatting the entity itself prints its prefab and position: slow).
 static string Tag(IEntity soldier)
 {
  if (!soldier) return "none";
  return string.Format("#%1", soldier.GetID());
 }

 static string WellOf(BaseMuzzleComponent muzzle)
 {
  if (!muzzle || !muzzle.GetMagazineWell()) return "";
  return muzzle.GetMagazineWell().Type().ToString();
 }

 // The wells his weapons take, and the muzzle of his machine gun or reloadable launcher.
 static BaseMuzzleComponent Kit(ChimeraCharacter soldier, notnull array<string> takes, out bool launcher)
 {
  takes.Clear();
  launcher = false;
  BaseMuzzleComponent special;
  if (!soldier) return null;
  BaseWeaponManagerComponent wm = BaseWeaponManagerComponent.Cast(soldier.FindComponent(BaseWeaponManagerComponent));
  if (!wm) return null;
  array<IEntity> weapons = {};
  wm.GetWeaponsList(weapons);
  foreach (IEntity weaponEntity : weapons)
  {
   if (!weaponEntity) continue;
   BaseWeaponComponent weapon = BaseWeaponComponent.Cast(weaponEntity.FindComponent(BaseWeaponComponent));
   if (!weapon) continue;
   EWeaponType type = weapon.GetWeaponType();
   array<BaseMuzzleComponent> muzzles = {};
   weapon.GetMuzzlesList(muzzles);
   foreach (BaseMuzzleComponent muzzle : muzzles)
   {
    string well = WellOf(muzzle);
    if (well == "") continue;
    if (!takes.Contains(well)) takes.Insert(well);
    if (special) continue;
    if (type == EWeaponType.WT_MACHINEGUN)
    {
     special = muzzle;
    }
    else if (type == EWeaponType.WT_ROCKETLAUNCHER)
    {
     SCR_MuzzleInMagComponent inMag = SCR_MuzzleInMagComponent.Cast(muzzle);
     if (inMag && !inMag.CanBeReloaded()) continue;
     special = muzzle;
     launcher = true;
    }
   }
  }
  return special;
 }

 // Loaded magazines/rockets of this well he carries (callers only ask about wells none of his
// weapons take, or a body whose launcher has already been taken with its round).
 static int Stock(IEntity soldier, string well, notnull array<IEntity> items)
 {
  items.Clear();
  if (!soldier || well == "") return 0;
  InventoryStorageManagerComponent inv = InventoryStorageManagerComponent.Cast(soldier.FindComponent(InventoryStorageManagerComponent));
  if (!inv) return 0;
  array<IEntity> carried = {};
  inv.GetItems(carried);
  foreach (IEntity item : carried)
  {
   if (!item) continue;
   MagazineComponent magazine = MagazineComponent.Cast(item.FindComponent(MagazineComponent));
   if (!magazine || !magazine.GetMagazineWell() || magazine.GetAmmoCount() <= 0) continue;
   if (magazine.GetMagazineWell().Type().ToString() != well) continue;
   items.Insert(item);
  }
  return items.Count();
 }

 // Wells he carries ammunition for but no weapon of his takes (walks his inventory: tests only).
 static void ForeignWells(ChimeraCharacter soldier, notnull array<string> takes, notnull array<string> wells)
 {
  wells.Clear();
  InventoryStorageManagerComponent inv = InventoryStorageManagerComponent.Cast(soldier.FindComponent(InventoryStorageManagerComponent));
  if (!inv) return;
  array<IEntity> carried = {};
  inv.GetItems(carried);
  foreach (IEntity item : carried)
  {
   if (!item) continue;
   MagazineComponent magazine = MagazineComponent.Cast(item.FindComponent(MagazineComponent));
   if (!magazine || !magazine.GetMagazineWell() || magazine.GetAmmoCount() <= 0) continue;
   string well = magazine.GetMagazineWell().Type().ToString();
   if (!takes.Contains(well) && !wells.Contains(well)) wells.Insert(well);
  }
 }

 // Magazines or rockets for this gun he carries: the AI's own cached count (the one vanilla's
 // resupply uses to find a provider), not a walk over his inventory.
 static int Carries(ChimeraCharacter soldier, BaseMuzzleComponent gun)
 {
  if (!soldier || !gun || !gun.GetMagazineWell()) return 0;
  SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(soldier.FindComponent(SCR_AICombatComponent));
  if (!combat) return 0;
  return combat.GetMagazineCount(gun.GetMagazineWell().Type(), true);
 }

 static bool HasPairIn(SCR_AIGroup group)
 {
  foreach (BBB_Pair pair : s_aPairs)
   if (pair.m_Group == group) return true;
  return false;
 }

 // On foot, awake, AI-driven.
 static bool Usable(ChimeraCharacter soldier)
 {
  if (!BBB.IsConscious(soldier) || soldier.IsInVehicle() || BBB.PlayerOf(soldier)) return false;
  CharacterControllerComponent controller = soldier.GetCharacterController();
  if (!controller || controller.IsPlayerControlled()) return false;
  CompartmentAccessComponent access = soldier.GetCompartmentAccessComponent();
  if (access && (access.GetCompartment() || access.IsGettingIn() || access.IsGettingOut())) return false;
  return !s_aVanilla.Contains(soldier);
 }

 // The assistant is his own to send: on foot and AI-driven, no player/GM order, his squad not
 // withdrawing, not dragging, treating or rearming.
 static bool Free(BBB_Pair pair, SCR_AIUtilityComponent utility)
 {
  ChimeraCharacter assistant = pair.m_Assistant;
  if (!assistant || !utility || !Usable(assistant) || BBB_OrderIntent.Blocks(utility) || BBB_Morale.IsWithdrawing(pair.m_Group)) return false;
  return !BBB_Drag.Busy(assistant) && !BBB_Medic.s_mGoingTo.Contains(assistant) && !BBB_Rearm.InRearmAction(assistant);
 }

 static bool Active(SCR_AIMoveIndividuallyBehavior move)
 {
  return move && !move.GetRemoveAction() && move.GetActionState() != EAIActionState.FAILED && move.GetActionState() != EAIActionState.COMPLETED;
 }

 // ---------------------------------------------------------------- queries for other features

 static BBB_Pair PairOf(IEntity soldier)
 {
  if (!soldier) return null;
  foreach (BBB_Pair pair : s_aPairs)
   if (pair.m_Assistant == soldier || pair.m_Gunner == soldier) return pair;
  return null;
 }

 // His assistant is on a supply or pick-up run, or walking back to him.
 static bool Busy(IEntity soldier)
 {
  if (!soldier) return false;
  foreach (BBB_Pair pair : s_aPairs)
   if (pair.m_Assistant == soldier && pair.m_iJob != NONE) return true;
  return false;
 }

 // His assistant will bring his magazines (Rearm leaves him to it).
 static bool Supplies(IEntity gunner)
 {
  if (!s_bOn || !gunner) return false;
  foreach (BBB_Pair pair : s_aPairs)
  {
   if (pair.m_Gunner != gunner || !pair.m_Assistant) continue;
   // Only an assistant who can actually come: one under an order, withdrawing, dragging or
   // treating someone, or taken for a higher job would leave his gunner waiting on nobody.
   if (!Free(pair, BBB_StaticWeapons.UtilityOf(pair.m_Assistant))) return false;
   if (pair.m_iJob != SUPPLY && !BBB_Claims.Can(pair.m_Assistant, BBB_Claims.REARM)) return false;
   if (Now() < pair.m_fNextSupply) return false;
   SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(gunner.FindComponent(SCR_InventoryStorageManagerComponent));
   if (!pair.m_GunMuzzle || !inv || inv.GetMagazineCountByMuzzle(pair.m_Gunner, pair.m_GunMuzzle) >= pair.m_iFullAt) return false;
   array<IEntity> items = {};
   return Stock(pair.m_Assistant, pair.m_sWell, items) > 0;
  }
  return false;
 }

 static int Count() { return s_aPairs.Count(); }

 // ---------------------------------------------------------------- tick

 static void Tick()
 {
  Read();
  if (!s_bOn)
  {
   if (!s_aPairs.IsEmpty()) Stop();
   return;
  }
  float now = Now();
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  for (int i = s_aPairs.Count() - 1; i >= 0; i--)
   if (!s_aPairs[i].m_Group || !groups.Contains(s_aPairs[i].m_Group)) Drop(i);
  int budget = PAIR_GROUPS_PER_TICK;
  foreach (SCR_AIGroup group : groups)
  {
   if (!group) continue;
   if (BBB.HasPlayer(group)) { DropGroup(group); continue; }
   if (budget <= 0 || (s_mNextPair.Contains(group) && now < s_mNextPair.Get(group))) continue;
   budget--;
   s_mNextPair.Set(group, now + PAIR_MS);
   PairGroup(group);
  }
  for (int g = s_mNextPair.Count() - 1; g >= 0; g--)
   if (!s_mNextPair.GetKey(g)) s_mNextPair.RemoveElement(g);
  for (int p = s_aPairs.Count() - 1; p >= 0; p--)
   Run(p, now);
 }

 protected static void PairGroup(SCR_AIGroup group)
 {
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(group, agents);
  array<ChimeraCharacter> gunners = {};
  array<BaseMuzzleComponent> gunMuzzles = {};
  array<bool> gunLaunchers = {};
  array<ChimeraCharacter> members = {};
  array<ref array<string>> memberTakes = {};
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   ChimeraCharacter soldier = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!soldier || !BBB.IsAlive(soldier) || BBB.PlayerOf(soldier) || s_aVanilla.Contains(soldier)) continue;
   array<string> takes = {};
   bool launcher;
   BaseMuzzleComponent special = Kit(soldier, takes, launcher);
   if (special)
   {
    gunners.Insert(soldier);
    gunMuzzles.Insert(special);
    gunLaunchers.Insert(launcher);
   }
   members.Insert(soldier);
   memberTakes.Insert(takes);
  }
  if (gunners.IsEmpty() && !HasPairIn(group)) return; // nothing to pair, nothing to drop
  // Keep a running pick-up (its gunner is dead, so he isn't among the gunners any more).
  array<ChimeraCharacter> taken = {};
  for (int i = s_aPairs.Count() - 1; i >= 0; i--)
  {
   BBB_Pair old = s_aPairs[i];
   if (old.m_Group != group) continue;
   if (old.m_iJob == PICKUP) { taken.Insert(old.m_Assistant); continue; }
   int at = gunners.Find(old.m_Gunner);
   int mi = members.Find(old.m_Assistant);
   // Still a match: keep him (and his running job).
   if (at >= 0 && mi >= 0 && WellOf(gunMuzzles[at]) == old.m_sWell && !memberTakes[mi].Contains(old.m_sWell) && Carries(old.m_Assistant, gunMuzzles[at]) > 0)
   {
    old.m_GunMuzzle = gunMuzzles[at];
    taken.Insert(old.m_Assistant);
    taken.Insert(old.m_Gunner);
    continue;
   }
   if (!BBB.IsAlive(old.m_Gunner) && old.m_bLauncher && old.m_Assistant && BBB.IsAlive(old.m_Assistant)) { taken.Insert(old.m_Assistant); continue; } // Run() starts the pick-up
   Drop(i);
  }
  for (int gi = 0; gi < gunners.Count(); gi++)
  {
   ChimeraCharacter gunner = gunners[gi];
   if (taken.Contains(gunner)) continue;
   string well = WellOf(gunMuzzles[gi]);
   ChimeraCharacter best;
   float bestDist = float.MAX;
   for (int c = 0; c < members.Count(); c++)
   {
    ChimeraCharacter carrier = members[c];
    if (carrier == gunner || taken.Contains(carrier) || memberTakes[c].Contains(well)) continue;
    float d = vector.DistanceSq(carrier.GetOrigin(), gunner.GetOrigin());
    if (d >= bestDist || Carries(carrier, gunMuzzles[gi]) <= 0) continue;
    bestDist = d;
    best = carrier;
   }
   if (!best) continue;
   BBB_Pair pair = new BBB_Pair();
   pair.m_Group = group;
   pair.m_Gunner = gunner;
   pair.m_Assistant = best;
   pair.m_sWell = well;
   pair.m_bLauncher = gunLaunchers[gi];
   pair.m_GunMuzzle = gunMuzzles[gi];
   s_aPairs.Insert(pair);
   taken.Insert(gunner);
   taken.Insert(best);
   s_iPaired++;
   string kind = "machine gun";
   if (pair.m_bLauncher) kind = "launcher";
   PrintFormat("[BBB] TEAMS %1: %2 assists %3 (%4, %5)", BBB.s_Hooks.NameOf(group), Tag(best), Tag(gunner), kind, well);
   BBB_Think.Note(best, "Teams", "assistant to the " + kind + " gunner");
   BBB_Think.Note(gunner, "Teams", "has an assistant carrying his ammunition");
  }
 }

 protected static void Run(int index, float now)
 {
  BBB_Pair pair = s_aPairs[index];
  ChimeraCharacter assistant = pair.m_Assistant;
  if (!assistant || !BBB.IsAlive(assistant)) { Drop(index); return; }
  SCR_AIUtilityComponent utility = BBB_StaticWeapons.UtilityOf(assistant);
  if (!Free(pair, utility)) { EndJob(pair); return; }
  if (!pair.m_Gunner || !BBB.IsAlive(pair.m_Gunner))
  {
   if (!PickUp(pair, utility, now)) Drop(index);
   return;
  }
  // A downed or mounted gunner is the medic's or the crew's business.
  if (!Usable(pair.m_Gunner)) { EndJob(pair); return; }
  if (Supply(pair, utility, now)) return;
  Leash(pair, utility, now);
 }

 protected static void Order(BBB_Pair pair, SCR_AIUtilityComponent utility, int job, IEntity target, float priority, float radius, float now)
 {
  if (Active(pair.m_Move)) pair.m_Move.Fail();
  if (pair.m_iJob != job) pair.m_fJobSince = now;
  pair.m_iJob = job;
  pair.m_vMoveTo = target.GetOrigin();
  pair.m_Move = new SCR_AIMoveIndividuallyBehavior(utility, null, pair.m_vMoveTo, priority: priority, ent: target, radius: radius);
  utility.AddAction(pair.m_Move);
 }

 // The move's target has walked off since it was given.
 protected static bool Stale(BBB_Pair pair, IEntity target)
 {
  return !Active(pair.m_Move) || vector.DistanceXZ(target.GetOrigin(), pair.m_vMoveTo) > REISSUE_M;
 }

 protected static void EndJob(BBB_Pair pair)
 {
  if (Active(pair.m_Move)) pair.m_Move.Fail();
  pair.m_Move = null;
  if (pair.m_iJob == SUPPLY || pair.m_iJob == PICKUP) BBB_Claims.Release(pair.m_Assistant, BBB_Claims.REARM);
  pair.m_iJob = NONE;
 }

 protected static void Drop(int index)
 {
  BBB_Pair pair = s_aPairs[index];
  EndJob(pair);
  s_aPairs.Remove(index);
 }

 protected static void DropGroup(SCR_AIGroup group)
 {
  for (int i = s_aPairs.Count() - 1; i >= 0; i--)
   if (s_aPairs[i].m_Group == group) Drop(i);
 }

 // At his last spare the assistant brings him more. True while that is the job.
 protected static bool Supply(BBB_Pair pair, SCR_AIUtilityComponent utility, float now)
 {
  ChimeraCharacter gunner = pair.m_Gunner;
  ChimeraCharacter assistant = pair.m_Assistant;
  // His gun as of the last pairing (a swap is caught there, within PAIR_MS).
  BaseMuzzleComponent muzzle = pair.m_GunMuzzle;
  SCR_InventoryStorageManagerComponent gunnerInv = SCR_InventoryStorageManagerComponent.Cast(gunner.FindComponent(SCR_InventoryStorageManagerComponent));
  if (!muzzle || !gunnerInv) return false;
  int spare = gunnerInv.GetMagazineCountByMuzzle(gunner, muzzle);
  int threshold = 1;
  if (pair.m_bLauncher) threshold = 0;
  // Cheap checks first: his kit is only read when the gunner is short.
  bool wanted = spare <= threshold && spare < pair.m_iFullAt && now >= pair.m_fNextSupply;
  array<IEntity> stock = {};
  if (wanted) wanted = Stock(assistant, pair.m_sWell, stock) > 0;
  if (wanted && pair.m_iJob != SUPPLY && !BBB_Claims.Can(assistant, BBB_Claims.REARM)) wanted = false;
  if (wanted && pair.m_iJob == SUPPLY && !BBB_Claims.Holds(assistant, BBB_Claims.REARM)) wanted = false; // a higher job took him
  if (!wanted)
  {
   if (pair.m_iJob == SUPPLY) EndJob(pair);
   return false;
  }
  if (BBB_Rearm.CanLoot(assistant, gunner))
  {
   EndJob(pair);
   Hand(pair, gunnerInv, spare, stock, now);
   return true;
  }
  if (pair.m_iJob == SUPPLY && now - pair.m_fJobSince > SUPPLY_TIMEOUT_MS)
  {
   EndJob(pair);
   pair.m_fNextSupply = now + SUPPLY_BACKOFF_MS;
   s_iSupplyFailed++;
   PrintFormat("[BBB] TEAMS %1: %2 couldn't reach his gunner with ammunition", BBB.s_Hooks.NameOf(pair.m_Group), Tag(assistant));
   BBB_Think.Note(assistant, "Teams", "couldn't reach his gunner with ammunition");
   return false;
  }
  // No spare left: the gun falls silent without him, so he goes before attacking (90).
  float priority = 89;
  if (spare == 0) priority = 91;
  if (pair.m_iJob != SUPPLY || Stale(pair, gunner))
  {
   bool fresh = pair.m_iJob != SUPPLY;
   Order(pair, utility, SUPPLY, gunner, priority, 2.0, now);
   if (fresh)
   {
    PrintFormat("[BBB] TEAMS %1: %2 brings ammunition to %3 (gunner has %4 spare)", BBB.s_Hooks.NameOf(pair.m_Group), Tag(assistant), Tag(gunner), spare);
    BBB_Think.Note(assistant, "Teams", "bringing ammunition to his gunner");
   }
  }
  BBB_Claims.Take(assistant, BBB_Claims.REARM);
  return true;
 }

 protected static void Hand(BBB_Pair pair, SCR_InventoryStorageManagerComponent gunnerInv, int spare, notnull array<IEntity> stock, float now)
 {
  int target = MG_TARGET;
  if (pair.m_bLauncher) target = LAUNCHER_TARGET;
  int give = Math.Max(1, (stock.Count() + 1) / 2);
  give = Math.Min(give, Math.Max(1, target - spare));
  int moved;
  foreach (IEntity item : stock)
  {
   if (moved >= give) break;
   if (BBB_Rearm.TransferItem(gunnerInv, item, pair.m_Assistant)) moved++;
  }
  pair.m_fNextSupply = now + 5000;
  if (moved == 0)
  {
   // No room in his kit: try again once he has loaded a spare.
   pair.m_iFullAt = spare;
   s_iSupplyFailed++;
   PrintFormat("[BBB] TEAMS %1: %2 reached %3 but his kit is full (%4 spare)", BBB.s_Hooks.NameOf(pair.m_Group), Tag(pair.m_Assistant), Tag(pair.m_Gunner), spare);
   return;
  }
  pair.m_iFullAt = 1000;
  s_iHandovers++;
  s_iItemsHanded += moved;
  BBB_Perf.Count("teams.handover");
  PrintFormat("[BBB] TEAMS %1: %2 handed %3 to %4 (had %5 spare)", BBB.s_Hooks.NameOf(pair.m_Group), Tag(pair.m_Assistant), moved, Tag(pair.m_Gunner), spare);
  BBB_Think.Note(pair.m_Assistant, "Teams", "handed " + moved + " to his gunner");
  BBB_Think.Note(pair.m_Gunner, "Teams", "took " + moved + " from his assistant");
 }

 protected static void Leash(BBB_Pair pair, SCR_AIUtilityComponent utility, float now)
 {
  ChimeraCharacter assistant = pair.m_Assistant;
  ChimeraCharacter gunner = pair.m_Gunner;
  float d = vector.DistanceXZ(assistant.GetOrigin(), gunner.GetOrigin());
  if (pair.m_iJob == LEASH)
  {
   if (d <= HEEL_M || BBB_Claims.Role(assistant) != BBB_Claims.NONE || now - pair.m_fJobSince > LEASH_TIMEOUT_MS) { EndJob(pair); pair.m_fNextLeash = now + LEASH_PAUSE_MS; return; }
   if (Active(pair.m_Move) && !Stale(pair, gunner)) return;
   if (!Active(pair.m_Move) && d <= LEASH_M) { EndJob(pair); return; }
  }
  else
  {
   if (d <= LEASH_M || now < pair.m_fNextLeash || BBB_Claims.Role(assistant) != BBB_Claims.NONE) return;
  }
  float priority = SCR_AIActionBase.PRIORITY_BEHAVIOR_MOVE_INDIVIDUALLY + 2;
  bool contact = utility.m_ThreatSystem && utility.m_ThreatSystem.GetState() >= EAIThreatState.ALERTED;
  if (contact && d > LOST_M) priority = 89;
  bool fresh = pair.m_iJob != LEASH;
  Order(pair, utility, LEASH, gunner, priority, HEEL_M - 1, now);
  if (fresh)
  {
   s_iLeashes++;
   PrintFormat("[BBB] TEAMS %1: %2 rejoins his gunner (%3 m)", BBB.s_Hooks.NameOf(pair.m_Group), Tag(assistant), (int)d);
   BBB_Think.Note(assistant, "Teams", "rejoining his gunner, " + (int)d + " m away");
  }
 }

 // The launcher on a dead gunner whose rockets this assistant carries.
 protected static IEntity LauncherOn(IEntity body, string well)
 {
  InventoryStorageManagerComponent inv = InventoryStorageManagerComponent.Cast(body.FindComponent(InventoryStorageManagerComponent));
  if (!inv) return null;
  array<IEntity> items = {};
  inv.GetItems(items);
  foreach (IEntity item : items)
  {
   if (!item) continue;
   BaseWeaponComponent weapon = BaseWeaponComponent.Cast(item.FindComponent(BaseWeaponComponent));
   if (!weapon || weapon.GetWeaponType() != EWeaponType.WT_ROCKETLAUNCHER) continue;
   array<BaseMuzzleComponent> muzzles = {};
   weapon.GetMuzzlesList(muzzles);
   foreach (BaseMuzzleComponent muzzle : muzzles)
    if (WellOf(muzzle) == well) return item;
  }
  return null;
 }

 // His gunner is dead: take the launcher. False: nothing to do (drop the pair).
 protected static bool PickUp(BBB_Pair pair, SCR_AIUtilityComponent utility, float now)
 {
  ChimeraCharacter assistant = pair.m_Assistant;
  IEntity body = pair.m_Gunner;
  if (!pair.m_bLauncher || !body) return false;
  IEntity launcher = LauncherOn(body, pair.m_sWell);
  if (!launcher || BBB_Armour.HasOperationalAT(assistant)) return false;
  if (pair.m_iJob != PICKUP && !BBB_Claims.Can(assistant, BBB_Claims.REARM)) return true; // wait for the higher job
  if (pair.m_iJob == PICKUP && !BBB_Claims.Holds(assistant, BBB_Claims.REARM)) { EndJob(pair); return true; }
  if (BBB_Rearm.CanLoot(assistant, body))
  {
   EndJob(pair);
   SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(assistant.FindComponent(SCR_InventoryStorageManagerComponent));
   if (!inv || !BBB_Rearm.EquipLauncher(inv, launcher, body))
   {
    PrintFormat("[BBB] TEAMS %1: %2 couldn't take his gunner's launcher (no free slot)", BBB.s_Hooks.NameOf(pair.m_Group), Tag(assistant));
    return false;
   }
   array<IEntity> rockets = {};
   Stock(body, pair.m_sWell, rockets);
   int moved;
   foreach (IEntity rocket : rockets)
    if (BBB_Rearm.TransferItem(inv, rocket, body)) moved++;
   s_iPickups++;
   BBB_Perf.Count("teams.pickup");
   s_mNextPair.Set(pair.m_Group, 0);
   PrintFormat("[BBB] TEAMS %1: %2 took his fallen gunner's launcher (+%3 rockets)", BBB.s_Hooks.NameOf(pair.m_Group), Tag(assistant), moved);
   BBB_Think.Note(assistant, "Teams", "took his fallen gunner's launcher");
   return false;
  }
  if (pair.m_iJob == PICKUP && now - pair.m_fJobSince > SUPPLY_TIMEOUT_MS)
  {
   PrintFormat("[BBB] TEAMS %1: %2 couldn't reach his gunner's body", BBB.s_Hooks.NameOf(pair.m_Group), Tag(assistant));
   return false;
  }
  if (pair.m_iJob != PICKUP || !Active(pair.m_Move))
  {
   if (pair.m_iJob != PICKUP)
   {
    PrintFormat("[BBB] TEAMS %1: %2 goes for his fallen gunner's launcher", BBB.s_Hooks.NameOf(pair.m_Group), Tag(assistant));
    BBB_Think.Note(assistant, "Teams", "going for his fallen gunner's launcher");
   }
   Order(pair, utility, PICKUP, body, 89, 1.5, now);
  }
  BBB_Claims.Take(assistant, BBB_Claims.REARM);
  return true;
 }

 static void Stop()
 {
  foreach (BBB_Pair pair : s_aPairs) EndJob(pair);
  s_aPairs.Clear();
  s_mNextPair.Clear();
  s_aVanilla.Clear();
 }
}
