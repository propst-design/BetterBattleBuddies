// Better Battle Buddies — Opportunistic Combat Rearm / Corpse Looting
// Ports bCombat (fnc_combat_gear / fnc_combat_ammo), LAMBS_Danger (fnc_doCheckBody),
// and Vcom AI (fn_RearmSelf) to Reforger. When AI soldiers run low on ammunition
// (< 2 magazines) or face armored threats without operational AT, they search nearby
// corpses and supply sources within 25 m to scavenge ammunition and AT weapons.
// Weapon recovery (-bbbRearmWeapon 0 off): a soldier seen with a primary weapon who no
// longer has one goes back for his own gun if it is still lying nearby, else takes the
// nearest primary off a body or the ground (one he carries ammunition for first) with its
// magazines. Unarmed, he holds the REARM claim so fire teams and building moves leave him
// out; vanilla already gives an unarmed soldier no attack and backs him off seen enemies.
// In heavy contact or a withdrawal he only grabs a gun within reach.

// Failure history is per soldier/body, expires, and has a fixed memory bound.
class BBB_RearmFailure
{
 ChimeraCharacter m_Soldier;
 IEntity m_Corpse;
 int m_Attempts;
 float m_Until;
}

class BBB_Rearm
{
 static bool s_bOn = true;
 static int s_iRead;
 static int s_iRearmCount;
 static int s_iMagazinesLooted;
 static int s_iLaunchersLooted;
 static bool s_bWeaponOn = true;
 static int s_iWeaponsRecovered;
 static int s_iOwnWeaponsRecovered;

 static float s_fSearchRadius = 25.0;
 static const float LOOT_REACH_M = 3.5;
 static const float LOOT_HEIGHT_M = 1.5;
 static const float TRIP_TIMEOUT_MS = 20000.0;
 static const float FAILURE_BACKOFF_MS = 60000.0;
 static const int MAX_BODY_ATTEMPTS = 2;
 static const int MAX_FAILURE_RECORDS = 128;
 static const float REARM_COOLDOWN_MS = 15000.0;
 static const int TARGET_MAGAZINE_COUNT = 4;
 static const int CRITICAL_MAGAZINE_COUNT = 2;
 static const float HEAVY_CONTACT = 0.85;
 static const float WEAPON_RECHECK_MS = 3000.0;
 static const float WEAPON_AFTER_GRAB_MS = 1500.0;
 static const float OWN_WEAPON_BONUS_M = 1000.0; // his own gun beats any other in reach
 static const float AMMO_BONUS_M = 15.0;         // a gun he carries magazines for beats a nearer one
 static const float NEAR_WEAPON_M = 10.0;        // close enough to outrank vanilla's retreat-from-target

 protected static ref map<IEntity, float> s_mNextRearmCheck;
 protected static ref map<IEntity, IEntity> s_mTargetCorpse;
 protected static ref map<ChimeraCharacter, ref SCR_AIMoveIndividuallyBehavior> s_mRearmAction;
 protected static ref map<ChimeraCharacter, float> s_mTripStarted;
 protected static ref array<ref BBB_RearmFailure> s_aFailures;
 protected static ref array<IEntity> s_aQueryCorpseHits;
 protected static ref map<ChimeraCharacter, IEntity> s_mPrimary;      // the primary last seen on him
 protected static ref map<ChimeraCharacter, IEntity> s_mTargetWeapon; // the gun he is walking to
 // Called by BBB_StaticInit at game start (static initializer budget: tools/check_static_init.py).
 static void BBB_InitStatics()
 {
  s_mNextRearmCheck = new map<IEntity, float>();
  s_mTargetCorpse = new map<IEntity, IEntity>();
  s_mRearmAction = new map<ChimeraCharacter, ref SCR_AIMoveIndividuallyBehavior>();
  s_mTripStarted = new map<ChimeraCharacter, float>();
  s_aFailures = {};
  s_aQueryCorpseHits = {};
  s_mPrimary = new map<ChimeraCharacter, IEntity>();
  s_mTargetWeapon = new map<ChimeraCharacter, IEntity>();
 }

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string val;
  s_bOn = !(BBB_Settings.Get("bbbRearm", val) && val == "0");
  if (BBB_Settings.Get("bbbRearmRadius", val)) s_fSearchRadius = Math.Clamp(val.ToFloat(), 5.0, 100.0);
  s_bWeaponOn = !(BBB_Settings.Get("bbbRearmWeapon", val) && val == "0");
  PrintFormat("[BBB] REARM on=%1 radius=%2 weapon=%3", s_bOn, s_fSearchRadius, s_bWeaponOn);
 }

 static bool NeedsAmmo(ChimeraCharacter soldier, out BaseMuzzleComponent outMuzzle, out int outCount)
 {
  outMuzzle = null;
  outCount = 0;
  if (!soldier) return false;
  SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(soldier.FindComponent(SCR_InventoryStorageManagerComponent));
  BaseWeaponManagerComponent wm = BaseWeaponManagerComponent.Cast(soldier.FindComponent(BaseWeaponManagerComponent));
  if (!inv || !wm) return false;
  // His main gun, whatever he holds right now (a pistol or launcher in hand used to end the run).
  BaseWeaponComponent weapon;
  array<IEntity> weapons = {};
  wm.GetWeaponsList(weapons);
  foreach (IEntity wEnt : weapons)
  {
   BaseWeaponComponent w = BaseWeaponComponent.Cast(wEnt.FindComponent(BaseWeaponComponent));
   if (w && (w.GetWeaponType() == EWeaponType.WT_RIFLE || w.GetWeaponType() == EWeaponType.WT_MACHINEGUN || w.GetWeaponType() == EWeaponType.WT_SNIPERRIFLE))
   {
    weapon = w;
    break;
   }
  }
  if (!weapon) weapon = wm.GetCurrentWeapon();
  if (!weapon) return false;
  array<BaseMuzzleComponent> muzzles = {};
  weapon.GetMuzzlesList(muzzles);
  foreach (BaseMuzzleComponent m : muzzles)
  {
   if (m && m.GetMagazineWell())
   {
    outMuzzle = m;
    break;
   }
  }
  if (!outMuzzle) return false;
  outCount = inv.GetMagazineCountByMuzzle(soldier, outMuzzle);
  BaseMagazineComponent curMag = outMuzzle.GetMagazine();
  int inGun = 0;
  if (curMag) inGun = curMag.GetAmmoCount();
  if (outCount == 0 && inGun == 0) return true;
  return outCount < CRITICAL_MAGAZINE_COUNT;
 }

 // No spare magazine left for his main gun: the fight can't wait for him to find one.
 static bool OutOfSpares(ChimeraCharacter soldier)
 {
  BaseMuzzleComponent muzzle;
  int count;
  return NeedsAmmo(soldier, muzzle, count) && count == 0;
 }

 static bool NeedsAntiArmor(ChimeraCharacter soldier, SCR_AIGroup group)
 {
  if (!soldier || !group) return false;
  if (BBB_Armour.HasOperationalAT(soldier)) return false;
  SCR_AIGroupUtilityComponent groupUtil = SCR_AIGroupUtilityComponent.Cast(group.FindComponent(SCR_AIGroupUtilityComponent));
  if (!groupUtil || !groupUtil.m_Perception) return false;
  vector soldierPos = soldier.GetOrigin();
  Faction soldierFac = group.GetFaction();
  foreach (IEntity target : groupUtil.m_Perception.m_aTargetEntities)
  {
   if (!BBB.StillFighting(target)) continue;
   Faction targetFac = null;
   FactionAffiliationComponent aff = FactionAffiliationComponent.Cast(target.FindComponent(FactionAffiliationComponent));
   if (aff) targetFac = aff.GetAffiliatedFaction();
   if (soldierFac && targetFac && !soldierFac.IsFactionEnemy(targetFac)) continue;
   if (vector.Distance(soldierPos, target.GetOrigin()) > 300.0) continue;
   if (BBB_Armour.IsCombatVehicle(target)) return true;
  }
  return false;
 }

 static bool CorpseHasMatchingMag(IEntity corpse, BaseMuzzleComponent muzzle, notnull array<IEntity> outMags)
 {
  outMags.Clear();
  if (!corpse || !muzzle || !muzzle.GetMagazineWell()) return false;
  InventoryStorageManagerComponent corpseInv = InventoryStorageManagerComponent.Cast(corpse.FindComponent(InventoryStorageManagerComponent));
  if (!corpseInv) return false;
  array<IEntity> items = {};
  corpseInv.GetItems(items);
  string targetWell = muzzle.GetMagazineWell().Type().ToString();
  foreach (IEntity item : items)
  {
   if (!item) continue;
   MagazineComponent mag = MagazineComponent.Cast(item.FindComponent(MagazineComponent));
   if (!mag || !mag.GetMagazineWell() || mag.GetAmmoCount() <= 0) continue;
   if (mag.GetMagazineWell().Type().ToString() == targetWell)
    outMags.Insert(item);
  }
  return !outMags.IsEmpty();
 }

 // Use the launcher's actual muzzle wells, including mod-defined wells. No name heuristics.
 static bool LauncherLoaded(BaseWeaponComponent weapon)
 {
  if (!weapon) return false;
  array<BaseMuzzleComponent> muzzles = {};
  weapon.GetMuzzlesList(muzzles);
  foreach (BaseMuzzleComponent muzzle : muzzles)
  {
   if (!muzzle) continue;
   if (muzzle.GetAmmoCount() > 0) return true;
   BaseMagazineComponent mag = muzzle.GetMagazine();
   if (mag && mag.GetAmmoCount() > 0) return true;
  }
  return false;
 }

 static bool CorpseHasAT(IEntity corpse, out IEntity outLauncher, notnull array<IEntity> outRockets)
 {
  outLauncher = null;
  outRockets.Clear();
  if (!corpse) return false;
  InventoryStorageManagerComponent corpseInv = InventoryStorageManagerComponent.Cast(corpse.FindComponent(InventoryStorageManagerComponent));
  if (!corpseInv) return false;
  array<IEntity> items = {};
  corpseInv.GetItems(items);
  foreach (IEntity item : items)
  {
   if (!item) continue;
   BaseWeaponComponent weapon = BaseWeaponComponent.Cast(item.FindComponent(BaseWeaponComponent));
   if (!weapon || weapon.GetWeaponType() != EWeaponType.WT_ROCKETLAUNCHER) continue;
   array<BaseMuzzleComponent> muzzles = {};
   weapon.GetMuzzlesList(muzzles);
   array<IEntity> rockets = {};
   foreach (BaseMuzzleComponent muzzle : muzzles)
   {
    SCR_MuzzleInMagComponent projectileMuzzle = SCR_MuzzleInMagComponent.Cast(muzzle);
    if (projectileMuzzle && !projectileMuzzle.CanBeReloaded()) continue;
    array<IEntity> matching = {};
    CorpseHasMatchingMag(corpse, muzzle, matching);
    foreach (IEntity rocket : matching)
    {
     // A magazine already inside the launcher travels with it.
     if (muzzle.GetMagazine() && muzzle.GetMagazine().GetOwner() == rocket) continue;
     if (!rockets.Contains(rocket)) rockets.Insert(rocket);
    }
   }
   if (!LauncherLoaded(weapon) && rockets.IsEmpty()) continue;
   outLauncher = item;
   outRockets.Copy(rockets);
   return true;
  }
  return false;
 }

 // Enforce geometry at the transfer boundary too, so callers cannot bypass it.
 static bool CanLoot(ChimeraCharacter soldier, IEntity corpse)
 {
  if (!soldier || !corpse) return false;
  vector from = soldier.GetOrigin();
  vector to = corpse.GetOrigin();
  if (vector.Distance(from, to) > LOOT_REACH_M || Math.AbsFloat(from[1] - to[1]) > LOOT_HEIGHT_M) return false;
  TraceParam trace = new TraceParam();
  trace.Start = from + Vector(0, 0.8, 0);
  trace.End = to + Vector(0, 0.3, 0);
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  array<IEntity> excluded = {soldier, corpse};
  trace.ExcludeArray = excluded;
  return GetGame().GetWorld().TraceMove(trace, null) >= 0.999;
 }

 static void RestoreItem(InventoryStorageManagerComponent sourceInv, BaseInventoryStorageComponent sourceStorage, IEntity item, vector fallback)
 {
  if (!item) return;
  InventoryItemComponent itemComp = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
  if (itemComp && itemComp.GetParentSlot()) return;
  if (sourceInv && sourceStorage && sourceInv.TryInsertItemInStorage(item, sourceStorage)) return;
  if (sourceInv && sourceInv.TryInsertItem(item)) return;
  // Keep a detached item physically available if its original storage disappeared/filled.
  item.SetOrigin(fallback + Vector(0, 0.2, 0));
 }

 static bool TransferItem(SCR_InventoryStorageManagerComponent soldierInv, IEntity item, IEntity corpse = null)
 {
  if (!soldierInv || !item) return false;
  InventoryItemComponent itemComp = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
  InventoryStorageSlot slot = null;
  if (itemComp) slot = itemComp.GetParentSlot();
  BaseInventoryStorageComponent sourceStorage = null;
  if (slot) sourceStorage = slot.GetStorage();
  InventoryStorageManagerComponent corpseInv = null;
  vector fallback = item.GetOrigin();
  if (corpse)
  {
   fallback = corpse.GetOrigin();
   corpseInv = InventoryStorageManagerComponent.Cast(corpse.FindComponent(InventoryStorageManagerComponent));
  }
  // Prefer an atomic storage move; some equipped corpse slots require explicit removal.
  BaseInventoryStorageComponent destination = soldierInv.FindStorageForItem(item, EStoragePurpose.PURPOSE_ANY);
  if (destination && soldierInv.TryMoveItemToStorage(item, destination)) return true;
  if (sourceStorage && (!corpseInv || !corpseInv.TryRemoveItemFromStorage(item, sourceStorage))) return false;
  bool ok = soldierInv.TryInsertItem(item, EStoragePurpose.PURPOSE_DEPOSIT);
  if (!ok) ok = soldierInv.TryInsertItem(item, EStoragePurpose.PURPOSE_ANY);
  if (!ok)
  {
   BaseInventoryStorageComponent target = soldierInv.FindStorageForItem(item, EStoragePurpose.PURPOSE_DEPOSIT);
   if (!target) target = soldierInv.FindStorageForItem(item, EStoragePurpose.PURPOSE_ANY);
   if (target) ok = soldierInv.TryMoveItemToStorage(item, target);
  }
  if (!ok) RestoreItem(corpseInv, sourceStorage, item, fallback);
  return ok;
 }

 // Only use a free weapon slot; EquipWeapon may replace the active rifle via a
 // chained drop operation. Failed insertion restores the launcher to the body.
 static bool EquipLauncher(SCR_InventoryStorageManagerComponent inv, IEntity launcher, IEntity corpse)
 {
  return EquipFreeSlot(inv, launcher, corpse);
 }

 // A weapon into a free weapon slot, from a body (corpse) or off the ground (corpse null).
 static bool EquipFreeSlot(SCR_InventoryStorageManagerComponent inv, IEntity weapon, IEntity corpse)
 {
  if (!inv || !weapon) return false;
  array<BaseInventoryStorageComponent> storages = {};
  inv.GetStorages(storages, EStoragePurpose.PURPOSE_WEAPON_PROXY);
  InventoryItemComponent itemComp = InventoryItemComponent.Cast(weapon.FindComponent(InventoryItemComponent));
  BaseInventoryStorageComponent source = null;
  if (itemComp && itemComp.GetParentSlot()) source = itemComp.GetParentSlot().GetStorage();
  InventoryStorageManagerComponent corpseInv = null;
  if (corpse) corpseInv = InventoryStorageManagerComponent.Cast(corpse.FindComponent(InventoryStorageManagerComponent));
  // A gun on the ground must really be loose: anything slotted belongs to someone.
  if (!corpse && source) return false;
  // Corpses may disallow direct storage moves. As for magazines, detach once
  // and restore on failure, without replacing any weapon the soldier carries.
  if (source && (!corpseInv || !corpseInv.TryRemoveItemFromStorage(weapon, source))) return false;
  foreach (BaseInventoryStorageComponent storage : storages)
  {
   for (int i = 0; i < storage.GetSlotsCount(); i++)
   {
    InventoryStorageSlot slot = storage.GetSlot(i);
    if (!slot || slot.GetAttachedEntity()) continue;
    if (inv.TryInsertItemInStorage(weapon, storage, i)) return true;
   }
  }
  if (corpse) RestoreItem(corpseInv, source, weapon, corpse.GetOrigin());
  return false;
 }

 static void TransferBack(SCR_InventoryStorageManagerComponent soldierInv, IEntity corpse, IEntity item)
 {
  InventoryStorageManagerComponent corpseInv = InventoryStorageManagerComponent.Cast(corpse.FindComponent(InventoryStorageManagerComponent));
  if (!corpseInv) return;
  if (corpseInv.TryInsertItem(item)) return;
  if (soldierInv.TryRemoveItemFromInventory(item)) RestoreItem(corpseInv, null, item, corpse.GetOrigin());
 }

 static bool LootFromCorpse(ChimeraCharacter soldier, IEntity corpse, bool needsAmmo, BaseMuzzleComponent muzzle, bool needsAT, SCR_AIGroup group)
 {
  if (!CanLoot(soldier, corpse) || BBB.HasPlayer(group)) return false;
  AIControlComponent control = AIControlComponent.Cast(soldier.FindComponent(AIControlComponent));
  AIAgent agent = null;
  if (control) agent = control.GetAIAgent();
  SCR_AIUtilityComponent utility = null;
  if (agent) utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
  CharacterControllerComponent controller = soldier.GetCharacterController();
  if (!controller || controller.IsPlayerControlled() || BBB.PlayerOf(soldier) || BBB_OrderIntent.Blocks(utility)) return false;
  SCR_InventoryStorageManagerComponent soldierInv = SCR_InventoryStorageManagerComponent.Cast(soldier.FindComponent(SCR_InventoryStorageManagerComponent));
  if (!soldierInv) return false;
  int magsMoved = 0;
  if (needsAmmo && muzzle) magsMoved = TakeMags(soldier, soldierInv, corpse, muzzle);
  int atMoved = 0;
  if (needsAT)
  {
   IEntity launcher;
   array<IEntity> rockets = {};
   if (CorpseHasAT(corpse, launcher, rockets))
   {
    if (EquipLauncher(soldierInv, launcher, corpse))
    {
     foreach (IEntity rocket : rockets)
     {
      if (TransferItem(soldierInv, rocket, corpse))
       s_iMagazinesLooted++;
     }
     BaseWeaponComponent acquired = BaseWeaponComponent.Cast(launcher.FindComponent(BaseWeaponComponent));
     bool operational = LauncherLoaded(acquired);
     array<BaseMuzzleComponent> acquiredMuzzles = {};
     if (acquired) acquired.GetMuzzlesList(acquiredMuzzles);
     foreach (BaseMuzzleComponent acquiredMuzzle : acquiredMuzzles)
     {
      SCR_MuzzleInMagComponent projectileMuzzle = SCR_MuzzleInMagComponent.Cast(acquiredMuzzle);
      if (projectileMuzzle && !projectileMuzzle.CanBeReloaded()) continue;
      array<IEntity> compatible = {};
      if (CorpseHasMatchingMag(soldier, acquiredMuzzle, compatible)) operational = true;
     }
     BaseWeaponManagerComponent wm = BaseWeaponManagerComponent.Cast(soldier.FindComponent(BaseWeaponManagerComponent));
     array<IEntity> equipped = {};
     if (wm) wm.GetWeaponsList(equipped);
     if (operational && equipped.Contains(launcher))
     {
      atMoved++;
      s_iLaunchersLooted++;
     }
     else
     {
      // Do not leave an unusable tube occupying the recipient's weapon slot.
      TransferBack(soldierInv, corpse, launcher);
     }
    }
   }
  }
  if (magsMoved > 0 || atMoved > 0)
  {
   s_iRearmCount++;
   BBB_Perf.Count("rearm.looted");
   string gName = "group";
   if (group) gName = BBB.s_Hooks.NameOf(group);
   PrintFormat("[BBB] REARM %1: %2 looted from corpse (mags=%3 at=%4)", gName, soldier, magsMoved, atMoved);
   BBB_Think.Note(soldier, "Rearm", "looted a body: " + magsMoved + " mags, " + atMoved + " AT");
   return true;
  }
  return false;
 }

 // Magazines for this muzzle off the body, up to the target count.
 static int TakeMags(ChimeraCharacter soldier, SCR_InventoryStorageManagerComponent soldierInv, IEntity corpse, BaseMuzzleComponent muzzle)
 {
  int moved = 0;
  if (!soldier || !soldierInv || !corpse || !muzzle) return 0;
  array<IEntity> mags = {};
  CorpseHasMatchingMag(corpse, muzzle, mags);
  int currentCount = soldierInv.GetMagazineCountByMuzzle(soldier, muzzle);
  foreach (IEntity mag : mags)
  {
   if (currentCount >= TARGET_MAGAZINE_COUNT) break;
   if (TransferItem(soldierInv, mag, corpse))
   {
    currentCount++;
    moved++;
    s_iMagazinesLooted++;
   }
  }
  return moved;
 }

 // ---- Weapon recovery ----

 static bool IsPrimaryType(EWeaponType type)
 {
  return type == EWeaponType.WT_RIFLE || type == EWeaponType.WT_MACHINEGUN || type == EWeaponType.WT_SNIPERRIFLE;
 }

 static BaseWeaponComponent PrimaryComponent(IEntity weapon)
 {
  if (!weapon) return null;
  BaseWeaponComponent w = BaseWeaponComponent.Cast(weapon.FindComponent(BaseWeaponComponent));
  if (!w || !IsPrimaryType(w.GetWeaponType())) return null;
  return w;
 }

 // The primary in his weapon slots (null: none).
 static IEntity PrimaryOf(ChimeraCharacter soldier)
 {
  if (!soldier) return null;
  BaseWeaponManagerComponent wm = BaseWeaponManagerComponent.Cast(soldier.FindComponent(BaseWeaponManagerComponent));
  if (!wm) return null;
  array<IEntity> weapons = {};
  wm.GetWeaponsList(weapons);
  foreach (IEntity wEnt : weapons)
  {
   if (PrimaryComponent(wEnt)) return wEnt;
  }
  return null;
 }

 // Lost it: seen with a primary before, none now. Men who never carried one (pistol-only
 // crews and pilots) are left as they are.
 static bool LostPrimary(ChimeraCharacter soldier)
 {
  if (!soldier) return false;
  IEntity primary = PrimaryOf(soldier);
  if (primary)
  {
   s_mPrimary.Set(soldier, primary);
   return false;
  }
  return s_mPrimary.Contains(soldier);
 }

 static IEntity LastPrimary(ChimeraCharacter soldier)
 {
  if (!soldier) return null;
  return s_mPrimary.Get(soldier);
 }

 static BaseMuzzleComponent WellMuzzle(BaseWeaponComponent weapon)
 {
  if (!weapon) return null;
  array<BaseMuzzleComponent> muzzles = {};
  weapon.GetMuzzlesList(muzzles);
  foreach (BaseMuzzleComponent m : muzzles)
  {
   if (m && m.GetMagazineWell()) return m;
  }
  return null;
 }

 // 2: he carries magazines for it; 1: loaded, or the body has magazines for it; 0: useless.
 static int WeaponAmmo(ChimeraCharacter soldier, BaseWeaponComponent weapon, IEntity corpse)
 {
  BaseMuzzleComponent muzzle = WellMuzzle(weapon);
  if (!muzzle) return 0;
  array<IEntity> mags = {};
  if (CorpseHasMatchingMag(soldier, muzzle, mags)) return 2;
  BaseMagazineComponent loaded = muzzle.GetMagazine();
  if (loaded && loaded.GetAmmoCount() > 0) return 1;
  if (corpse && CorpseHasMatchingMag(corpse, muzzle, mags)) return 1;
  return 0;
 }

 // Someone else in BBB is already walking to this gun.
 static bool WeaponTaken(ChimeraCharacter soldier, IEntity weapon)
 {
  for (int i = 0; i < s_mTargetWeapon.Count(); i++)
  {
   ChimeraCharacter other = s_mTargetWeapon.GetKey(i);
   if (other && other != soldier && s_mTargetWeapon.GetElement(i) == weapon) return true;
  }
  return false;
 }

 // The best usable primary within radius: his own first, then one he has ammunition for,
 // then the nearest. outSource is the body it is on, or the weapon itself when loose.
 static IEntity FindWeapon(ChimeraCharacter soldier, float radius, out IEntity outSource)
 {
  outSource = null;
  if (!soldier) return null;
  s_aQueryCorpseHits.Clear();
  GetGame().GetWorld().QueryEntitiesBySphere(soldier.GetOrigin(), radius, QueryCorpseHit, null, EQueryEntitiesFlags.ALL);
  IEntity ownGun = LastPrimary(soldier);
  vector soldierPos = soldier.GetOrigin();
  IEntity best = null;
  float bestScore = radius + 1.0;
  foreach (IEntity ent : s_aQueryCorpseHits)
  {
   if (!ent || ent == soldier || BodyBlocked(soldier, ent)) continue;
   array<IEntity> candidates = {};
   IEntity corpse = null;
   ChimeraCharacter ch = ChimeraCharacter.Cast(ent);
   if (ch)
   {
    CharacterControllerComponent ctl = ch.GetCharacterController();
    if (!ctl || !ctl.IsDead()) continue;
    InventoryStorageManagerComponent corpseInv = InventoryStorageManagerComponent.Cast(ch.FindComponent(InventoryStorageManagerComponent));
    if (!corpseInv) continue;
    array<IEntity> items = {};
    corpseInv.GetItems(items);
    foreach (IEntity item : items)
    {
     if (PrimaryComponent(item)) candidates.Insert(item);
    }
    corpse = ent;
   }
   else
   {
    if (!PrimaryComponent(ent)) continue;
    InventoryItemComponent itemComp = InventoryItemComponent.Cast(ent.FindComponent(InventoryItemComponent));
    if (!itemComp || itemComp.GetParentSlot()) continue; // slotted: carried, racked or in a crate
    candidates.Insert(ent);
   }
   float d = vector.Distance(soldierPos, ent.GetOrigin());
   if (d > radius) continue;
   foreach (IEntity gun : candidates)
   {
    if (WeaponTaken(soldier, gun)) continue;
    int ammo = WeaponAmmo(soldier, PrimaryComponent(gun), corpse);
    if (ammo == 0) continue;
    float score = d;
    if (gun == ownGun) score -= OWN_WEAPON_BONUS_M;
    if (ammo == 2) score -= AMMO_BONUS_M;
    if (score < bestScore)
    {
     bestScore = score;
     best = gun;
     outSource = ent;
    }
   }
  }
  s_aQueryCorpseHits.Clear();
  return best;
 }

 // Takes the gun (and its magazines off a body). Same safety gates as LootFromCorpse.
 static bool PickUpWeapon(ChimeraCharacter soldier, IEntity weapon, IEntity source, SCR_AIGroup group)
 {
  if (!soldier || !weapon || !source || !CanLoot(soldier, source) || BBB.HasPlayer(group)) return false;
  AIControlComponent control = AIControlComponent.Cast(soldier.FindComponent(AIControlComponent));
  AIAgent agent = null;
  if (control) agent = control.GetAIAgent();
  SCR_AIUtilityComponent utility = null;
  if (agent) utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
  CharacterControllerComponent controller = soldier.GetCharacterController();
  if (!controller || controller.IsPlayerControlled() || BBB.PlayerOf(soldier) || BBB_OrderIntent.Blocks(utility) || BBB_Morale.IsPrisoner(soldier)) return false;
  if (PrimaryOf(soldier)) return false;
  SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(soldier.FindComponent(SCR_InventoryStorageManagerComponent));
  if (!inv) return false;
  IEntity corpse = null;
  if (source != weapon) corpse = source;
  bool ownGun = weapon == LastPrimary(soldier);
  if (!EquipFreeSlot(inv, weapon, corpse)) return false;
  BaseWeaponManagerComponent wm = BaseWeaponManagerComponent.Cast(soldier.FindComponent(BaseWeaponManagerComponent));
  array<IEntity> equipped = {};
  if (wm) wm.GetWeaponsList(equipped);
  if (!equipped.Contains(weapon)) return false;
  BaseWeaponComponent weaponComp = PrimaryComponent(weapon);
  int mags = 0;
  if (corpse) mags = TakeMags(soldier, inv, corpse, WellMuzzle(weaponComp));
  // Shoulder it if his hands are empty or hold a pistol (BBB_WeaponReady covers the rest).
  BaseWeaponComponent inHand = null;
  if (wm) inHand = wm.GetCurrentWeapon();
  if (weaponComp && (!inHand || inHand.GetWeaponType() == EWeaponType.WT_HANDGUN) && !controller.IsChangingItem())
   controller.SelectWeapon(weaponComp);
  s_mPrimary.Set(soldier, weapon);
  s_iWeaponsRecovered++;
  if (ownGun) s_iOwnWeaponsRecovered++;
  BBB_Perf.Count("rearm.weapon");
  string fromWhere = "the ground";
  if (corpse) fromWhere = "a body";
  string whose = "a";
  if (ownGun) whose = "his own";
  string gName = "group";
  if (group) gName = BBB.s_Hooks.NameOf(group);
  PrintFormat("[BBB] REARM %1: %2 picked up %3 weapon from %4 (mags=%5)", gName, soldier, whose, fromWhere, mags);
  BBB_Think.Note(soldier, "Rearm", "picked up " + whose + " weapon from " + fromWhere);
  return true;
 }

 // Unarmed: hold him out of BBB's fight jobs and send him for a gun.
 static void CheckWeaponFor(ChimeraCharacter soldier, SCR_AIUtilityComponent utility, SCR_AIGroup group, float nowMs)
 {
  // A rescue or a static gun outranks this (an unarmed man can drag or crew a gun).
  if (!BBB_Claims.Can(soldier, BBB_Claims.REARM))
  {
   CancelRearmAction(soldier);
   return;
  }
  // Just disarmed (not held yet): look now, not after an ammo check's cooldown.
  if (BBB_Claims.s_bOn && BBB_Claims.Role(soldier) != BBB_Claims.REARM) s_mNextRearmCheck.Remove(soldier);
  BBB_Claims.Take(soldier, BBB_Claims.REARM);
  if (s_mNextRearmCheck.Contains(soldier) && nowMs < s_mNextRearmCheck.Get(soldier)) return;
  bool reachOnly = BBB_Morale.IsWithdrawing(group);
  if (utility && utility.m_ThreatSystem)
  {
   float sup, sh, inVal, en;
   utility.m_ThreatSystem.GetThreatValues(sup, sh, inVal, en);
   if (sup >= HEAVY_CONTACT) reachOnly = true;
  }
  // A zombie close (BBB_Zombies): a gun at his feet, not a walk for one.
  if (BBB_Zombies.HoldRearm(utility)) reachOnly = true;
  ExpireTrip(soldier, nowMs);
  float radius = s_fSearchRadius;
  if (reachOnly) radius = LOOT_REACH_M;
  IEntity source;
  IEntity weapon = FindWeapon(soldier, radius, source);
  SCR_AIMoveIndividuallyBehavior curAction = s_mRearmAction.Get(soldier);
  bool actionActive = curAction && !curAction.GetRemoveAction() && curAction.GetActionState() != EAIActionState.FAILED && curAction.GetActionState() != EAIActionState.COMPLETED;
  if (!weapon)
  {
   CancelRearmAction(soldier);
   BBB_Claims.Take(soldier, BBB_Claims.REARM);
   s_mNextRearmCheck.Set(soldier, nowMs + WEAPON_RECHECK_MS);
   BBB_Think.Note(soldier, "Rearm", "no weapon: staying out of the fight");
   return;
  }
  if (CanLoot(soldier, source))
  {
   if (actionActive) curAction.Complete();
   CancelRearmAction(soldier);
   if (!PickUpWeapon(soldier, weapon, source, group))
   {
    RecordFailure(soldier, source, nowMs);
    BBB_Claims.Take(soldier, BBB_Claims.REARM);
   }
   s_mNextRearmCheck.Set(soldier, nowMs + WEAPON_AFTER_GRAB_MS);
   return;
  }
  if (reachOnly || !utility) return;
  if (actionActive && s_mTargetWeapon.Get(soldier) == weapon) return;
  if (actionActive) curAction.Fail();
  s_mTargetCorpse.Set(soldier, source);
  s_mTargetWeapon.Set(soldier, weapon);
  s_mTripStarted.Set(soldier, nowMs);
  // As an out-of-spares run (91, over attacking); a gun a few steps away also beats
  // vanilla's retreat-from-target (110). Danger, medical and player orders still outrank it.
  float tripPriority = 91;
  if (vector.Distance(soldier.GetOrigin(), source.GetOrigin()) <= NEAR_WEAPON_M) tripPriority = SCR_AIActionBase.PRIORITY_BEHAVIOR_RETREAT_FROM_TARGET + 1;
  SCR_AIMoveIndividuallyBehavior moveAction = new SCR_AIMoveIndividuallyBehavior(utility, null, source.GetOrigin(), priority: tripPriority, ent: source, radius: 1.0);
  s_mRearmAction.Set(soldier, moveAction);
  utility.AddAction(moveAction);
  string what = "a weapon";
  if (weapon == LastPrimary(soldier)) what = "his own weapon";
  BBB_Think.Note(soldier, "Rearm", "going for " + what);
 }

 protected static bool QueryCorpseHit(IEntity entity)
 {
  if (entity) s_aQueryCorpseHits.Insert(entity);
  return true;
 }

 static void PruneFailures(float nowMs)
 {
  for (int i = s_aFailures.Count() - 1; i >= 0; i--)
  {
   BBB_RearmFailure entry = s_aFailures[i];
   if (!entry.m_Soldier || !entry.m_Corpse || nowMs >= entry.m_Until) s_aFailures.Remove(i);
  }
 }

 static bool BodyBlocked(ChimeraCharacter soldier, IEntity corpse)
 {
  foreach (BBB_RearmFailure entry : s_aFailures)
   if (entry.m_Soldier == soldier && entry.m_Corpse == corpse && entry.m_Attempts >= MAX_BODY_ATTEMPTS) return true;
  return false;
 }

 static void RecordFailure(ChimeraCharacter soldier, IEntity corpse, float nowMs)
 {
  if (!soldier || !corpse) return;
  PruneFailures(nowMs);
  foreach (BBB_RearmFailure entry : s_aFailures)
  {
   if (entry.m_Soldier != soldier || entry.m_Corpse != corpse) continue;
   entry.m_Attempts++;
   entry.m_Until = nowMs + FAILURE_BACKOFF_MS;
   return;
  }
  if (s_aFailures.Count() >= MAX_FAILURE_RECORDS) s_aFailures.RemoveOrdered(0); // the oldest
  BBB_RearmFailure failure = new BBB_RearmFailure();
  failure.m_Soldier = soldier;
  failure.m_Corpse = corpse;
  failure.m_Attempts = 1;
  failure.m_Until = nowMs + FAILURE_BACKOFF_MS;
  s_aFailures.Insert(failure);
 }

 static IEntity FindLootSource(ChimeraCharacter soldier, bool needsAmmo, BaseMuzzleComponent muzzle, bool needsAT)
 {
  if (!soldier) return null;
  s_aQueryCorpseHits.Clear();
  GetGame().GetWorld().QueryEntitiesBySphere(soldier.GetOrigin(), s_fSearchRadius, QueryCorpseHit, null, EQueryEntitiesFlags.ALL);
  IEntity best = null;
  float bestDist = s_fSearchRadius * s_fSearchRadius;
  vector soldierPos = soldier.GetOrigin();
  foreach (IEntity ent : s_aQueryCorpseHits)
  {
   if (!ent || ent == soldier || BodyBlocked(soldier, ent)) continue;
   ChimeraCharacter ch = ChimeraCharacter.Cast(ent);
   if (ch)
   {
    CharacterControllerComponent ctl = ch.GetCharacterController();
    if (!ctl || !ctl.IsDead()) continue;
   }
   else
   {
    if (BBB.IsAlive(ent)) continue;
    if (!ent.FindComponent(InventoryStorageManagerComponent)) continue;
   }
   bool useful = false;
   if (needsAmmo && muzzle)
   {
    array<IEntity> mags = {};
    if (CorpseHasMatchingMag(ent, muzzle, mags)) useful = true;
   }
   if (!useful && needsAT)
   {
    IEntity launcher;
    array<IEntity> rockets = {};
    if (CorpseHasAT(ent, launcher, rockets)) useful = true;
   }
   if (!useful) continue;
   float d = vector.DistanceSq(soldierPos, ent.GetOrigin());
   if (d < bestDist)
   {
    bestDist = d;
    best = ent;
   }
  }
  s_aQueryCorpseHits.Clear();
  return best;
 }

 static void CancelRearmAction(ChimeraCharacter soldier)
 {
  if (!soldier) return;
  if (s_mRearmAction.Contains(soldier))
  {
   SCR_AIMoveIndividuallyBehavior act = s_mRearmAction.Get(soldier);
   if (act && act.GetActionState() != EAIActionState.COMPLETED && act.GetActionState() != EAIActionState.FAILED)
    act.Fail();
   s_mRearmAction.Remove(soldier);
  }
  s_mTargetCorpse.Remove(soldier);
  s_mTargetWeapon.Remove(soldier);
  s_mTripStarted.Remove(soldier);
  BBB_Claims.Release(soldier, BBB_Claims.REARM);
 }

 static bool InRearmAction(ChimeraCharacter soldier)
 {
  if (!soldier) return false;
  return s_mRearmAction.Contains(soldier);
 }

 static void SetRearmActionForTest(ChimeraCharacter soldier, SCR_AIMoveIndividuallyBehavior act, IEntity corpse)
 {
  if (!soldier) return;
  s_mRearmAction.Set(soldier, act);
  s_mTargetCorpse.Set(soldier, corpse);
  s_mTripStarted.Set(soldier, GetGame().GetWorld().GetWorldTime());
 }

 static bool ExpireTrip(ChimeraCharacter soldier, float nowMs)
 {
  SCR_AIMoveIndividuallyBehavior curAction = s_mRearmAction.Get(soldier);
  IEntity previous = s_mTargetCorpse.Get(soldier);
  bool actionActive = curAction && !curAction.GetRemoveAction() && curAction.GetActionState() != EAIActionState.FAILED && curAction.GetActionState() != EAIActionState.COMPLETED;
  bool timedOut = s_mTripStarted.Contains(soldier) && nowMs - s_mTripStarted.Get(soldier) >= TRIP_TIMEOUT_MS;
  if (previous && (!actionActive || timedOut) && !CanLoot(soldier, previous))
  {
   RecordFailure(soldier, previous, nowMs);
   CancelRearmAction(soldier);
   return true;
  }
  return false;
 }

 // A host can remove a squad from its autonomous roster on possession/order
 // changes. Release existing trips even when CheckRearmFor will no longer run.
 static void WatchTrips(notnull array<SCR_AIGroup> groups)
 {
  Read();
  for (int i = s_mRearmAction.Count() - 1; i >= 0; i--)
  {
   ChimeraCharacter soldier = s_mRearmAction.GetKey(i);
   if (!soldier)
   {
    SCR_AIMoveIndividuallyBehavior action = s_mRearmAction.GetElement(i);
    if (action) action.Fail();
    s_mRearmAction.RemoveElement(i);
    continue;
   }
   AIControlComponent control = AIControlComponent.Cast(soldier.FindComponent(AIControlComponent));
   AIAgent agent = null;
   if (control) agent = control.GetAIAgent();
   SCR_AIGroup group = null;
   SCR_AIUtilityComponent utility = null;
   if (agent)
   {
    group = SCR_AIGroup.Cast(agent.GetParentGroup());
    utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   }
   CharacterControllerComponent controller = soldier.GetCharacterController();
   if (!controller || controller.IsPlayerControlled() || !s_bOn || !BBB.IsAlive(soldier) || BBB.PlayerOf(soldier) || !group || !groups.Contains(group) || BBB.HasPlayer(group) || BBB_OrderIntent.Blocks(utility)) CancelRearmAction(soldier);
   // Still on his way: keep him (a higher job that took him is seen in CheckRearmFor).
   else if (BBB_Claims.Can(soldier, BBB_Claims.REARM)) BBB_Claims.Take(soldier, BBB_Claims.REARM);
  }
  float nowMs = GetGame().GetWorld().GetWorldTime();
  PruneFailures(nowMs);
  for (int j = s_mNextRearmCheck.Count() - 1; j >= 0; j--)
   if (!s_mNextRearmCheck.GetKey(j) || nowMs >= s_mNextRearmCheck.GetElement(j)) s_mNextRearmCheck.RemoveElement(j);
  for (int k = s_mTripStarted.Count() - 1; k >= 0; k--)
   if (!s_mTripStarted.GetKey(k)) s_mTripStarted.RemoveElement(k);
  for (int n = s_mTargetCorpse.Count() - 1; n >= 0; n--)
   if (!s_mTargetCorpse.GetKey(n)) s_mTargetCorpse.RemoveElement(n);
  for (int w = s_mTargetWeapon.Count() - 1; w >= 0; w--)
   if (!s_mTargetWeapon.GetKey(w)) s_mTargetWeapon.RemoveElement(w);
  for (int p = s_mPrimary.Count() - 1; p >= 0; p--)
   if (!s_mPrimary.GetKey(p) || !BBB.IsAlive(s_mPrimary.GetKey(p))) s_mPrimary.RemoveElement(p);
 }

 static void CheckRearmFor(SCR_AIGroup group)
 {
  Read();
  if (!group) return;
  bool groupBlocked = !s_bOn || BBB.HasPlayer(group);
  float nowMs = GetGame().GetWorld().GetWorldTime();
  PruneFailures(nowMs);
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(group, agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   ChimeraCharacter soldier = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (groupBlocked || !soldier || !BBB.IsAlive(soldier) || soldier.IsInVehicle() || BBB.PlayerOf(soldier))
   {
    if (soldier) CancelRearmAction(soldier);
    continue;
   }
   CompartmentAccessComponent acc = soldier.GetCompartmentAccessComponent();
   if (acc && (acc.GetCompartment() != null || acc.IsGettingIn() || acc.IsGettingOut()))
   {
    CancelRearmAction(soldier);
    continue;
   }
   if (BBB_Drag.Busy(soldier) || BBB_Medic.s_mGoingTo.Contains(soldier) || BBB_Teams.Busy(soldier))
   {
    CancelRearmAction(soldier);
    continue;
   }
   CharacterControllerComponent ctl = soldier.GetCharacterController();
   if (!ctl || ctl.IsUnconscious() || ctl.IsPlayerControlled())
   {
    CancelRearmAction(soldier);
    continue;
   }
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   if (BBB_OrderIntent.Blocks(utility))
   {
    CancelRearmAction(soldier);
    continue;
   }
   // No primary any more: get one back before anything else this system does.
   if (s_bWeaponOn && LostPrimary(soldier) && !BBB_Morale.IsPrisoner(soldier))
   {
    CheckWeaponFor(soldier, utility, group, nowMs);
    continue;
   }
   // A bounding or withdrawing man isn't sent to loot a body mid-move, unless he is out of
   // spares: then the run outranks the bound (claims drop him from the fire team).
   if ((utility && utility.m_BBBFireJob && !OutOfSpares(soldier)) || BBB_Morale.IsWithdrawing(group))
   {
    CancelRearmAction(soldier);
    continue;
   }
   // Taken for a rescue or a static gun: that job has him now.
   if (!BBB_Claims.Can(soldier, BBB_Claims.REARM))
   {
    CancelRearmAction(soldier);
    continue;
   }
   // Zombies (BBB_Zombies): no trip to a body with one close; clear them first.
   if (BBB_Zombies.HoldRearm(utility))
   {
    CancelRearmAction(soldier);
    continue;
   }
   if (s_mNextRearmCheck.Contains(soldier) && nowMs < s_mNextRearmCheck.Get(soldier)) continue;
   if (utility && utility.m_ThreatSystem)
   {
    float sup, sh, inVal, en;
    utility.m_ThreatSystem.GetThreatValues(sup, sh, inVal, en);
    if (sup >= 0.85)
    {
     CancelRearmAction(soldier);
     continue;
    }
   }

   BaseMuzzleComponent muzzle;
   int currentMags;
   bool needsAmmo = NeedsAmmo(soldier, muzzle, currentMags);
   bool needsAT = NeedsAntiArmor(soldier, group);
   if (!needsAmmo && !needsAT)
   {
    CancelRearmAction(soldier);
    s_mNextRearmCheck.Set(soldier, nowMs + REARM_COOLDOWN_MS);
    continue;
   }
   // A gunner whose assistant carries his ammunition waits for him instead of a body.
   if (!needsAT && BBB_Teams.Supplies(soldier))
   {
    CancelRearmAction(soldier);
    s_mNextRearmCheck.Set(soldier, nowMs + 3000.0);
    continue;
   }

   ExpireTrip(soldier, nowMs);
   SCR_AIMoveIndividuallyBehavior curAction = s_mRearmAction.Get(soldier);
   bool actionActive = curAction && !curAction.GetRemoveAction() && curAction.GetActionState() != EAIActionState.FAILED && curAction.GetActionState() != EAIActionState.COMPLETED;
   IEntity corpse = FindLootSource(soldier, needsAmmo, muzzle, needsAT);
   if (!corpse)
   {
    CancelRearmAction(soldier);
    s_mNextRearmCheck.Set(soldier, nowMs + 6000.0);
    continue;
   }

   float dist = vector.Distance(soldier.GetOrigin(), corpse.GetOrigin());
   if (CanLoot(soldier, corpse))
   {
    if (curAction && curAction.GetActionState() != EAIActionState.COMPLETED && curAction.GetActionState() != EAIActionState.FAILED)
     curAction.Complete();
    CancelRearmAction(soldier);
    if (!LootFromCorpse(soldier, corpse, needsAmmo, muzzle, needsAT, group)) RecordFailure(soldier, corpse, nowMs);
    s_mNextRearmCheck.Set(soldier, nowMs + REARM_COOLDOWN_MS);
   }
   else if (utility && dist <= s_fSearchRadius)
   {
    if (!actionActive || !s_mTargetCorpse.Contains(soldier) || s_mTargetCorpse.Get(soldier) != corpse)
    {
     if (curAction && actionActive) curAction.Fail();
     s_mTargetCorpse.Set(soldier, corpse);
     s_mTripStarted.Set(soldier, nowMs);
     // Out of spares in a fight, the run beats attacking (90) like Buildings' indoor move (91);
     // danger, medical and player orders still outrank it. Otherwise it waits for a lull.
     float tripPriority = SCR_AIActionBase.PRIORITY_BEHAVIOR_MOVE_INDIVIDUALLY + 5;
     if (OutOfSpares(soldier)) tripPriority = Math.Max(tripPriority, 91);
     SCR_AIMoveIndividuallyBehavior moveAction = new SCR_AIMoveIndividuallyBehavior(utility, null, corpse.GetOrigin(), priority: tripPriority, ent: corpse, radius: 2.0);
     s_mRearmAction.Set(soldier, moveAction);
     utility.AddAction(moveAction);
     BBB_Claims.Take(soldier, BBB_Claims.REARM);
    }
   }
  }
 }

 static void Stop()
 {
  foreach (ChimeraCharacter s, SCR_AIMoveIndividuallyBehavior act : s_mRearmAction)
  {
   if (act && act.GetActionState() != EAIActionState.COMPLETED && act.GetActionState() != EAIActionState.FAILED)
    act.Complete();
   BBB_Claims.Release(s, BBB_Claims.REARM);
  }
  s_mRearmAction.Clear();
  s_mTripStarted.Clear();
  s_aFailures.Clear();
  s_mNextRearmCheck.Clear();
  s_mTargetCorpse.Clear();
  s_mTargetWeapon.Clear();
  s_mPrimary.Clear();
  s_aQueryCorpseHits.Clear();
  s_iRearmCount = 0;
  s_iWeaponsRecovered = 0;
  s_iOwnWeaponsRecovered = 0;
  s_iMagazinesLooted = 0;
  s_iLaunchersLooted = 0;
 }
}
