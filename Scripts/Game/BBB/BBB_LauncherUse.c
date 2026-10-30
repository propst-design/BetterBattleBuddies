// AT gunners and the round they load (BBB #14).
// Vanilla never fires a rocket at people: every rocket prefab is marked for vehicles only and the
// engine's weapon picker obeys that, so HE, frag and thermobaric rounds and the SMAW HEDP sit
// unused (measured: RPG-7 and SMAW gunners 0 launcher picks against a rifle team in the open; the
// RHS RPO-A, marked for infantry, the only launcher used). Three levers, each with its own switch:
// 1. On infantry (-bbbLauncherInfantry): on his own due weapon pick, after vanilla chose an
//    infantry target he can see, BBB may hand him his launcher:
//    - anti-personnel round (HE, frag, thermobaric, incendiary, dual-purpose HEDP): at a group
//      (another known enemy within 7 m of the target), a man partly behind cover, a man in a building;
//    - anti-armour round (HEAT, AP, any disposable tube): only at a man in a building.
//    Never: while enemy armour is known to him (last 90 s), closer than 35 m (anti-armour 25 m) or
//    farther than 300 m (disposables 200 m), with a friend within 12 m of the target, in HOLD FIRE,
//    in a vehicle, under a forced player/GM move, while BBB has him on another job (role claims) or
//    his squad is withdrawing. Smoke and illumination rounds are never picked this way.
//    A pick holds for up to 10 s (20 s with a swap) or until the rocket leaves; then 20 s pause.
// 2. The right warhead (-bbbLauncherSwap): if the round in the tube is wrong for the target and he
//    carries the right one, he reloads with it (the engine's own magazine switch): an anti-personnel
//    round for infantry only when it's safe to take the time (target 80 m+, his threat low), an
//    anti-armour round for enemy armour always (an HE round does nothing to a tank).
// 3. Loaded in contact (-bbbLauncherCombatLoad): a gunner with a reloadable launcher who carries
//    both kinds of round walks with the tube empty while he knows of no enemy (60 s), like a real
//    RPG gunner, and loads for the first target through 1 and 2 (vanilla loads its own pick for a
//    vehicle). A gunner with only one kind keeps his tube loaded: emptying it would only cost time.
// The engine still aims, fires and checks its own line of fire.
class BBB_LauncherUse
{
 static int s_iRead;
 static bool s_bOn = true;
 static bool s_bSwap = true;
 static bool s_bCombatLoad = true;
 static const float AP_MIN_M = 35;
 static const float AT_MIN_M = 25;
 static const float MAX_M = 300;
 static const float DISPOSABLE_MAX_M = 200;
 static const float SWAP_MIN_M = 80;
 static const float SWAP_MAX_THREAT = 0.4;
 static const float GROUP_M = 7;
 static const float FRIEND_M = 12;
 static const float ARMOUR_MEMORY_S = 90;
 static const float CONTACT_MEMORY_S = 60;
 static const float HOLD_MS = 10000;
 static const float SWAP_HOLD_MS = 25000;
 static const float PAUSE_MS = 20000;
 static const float CHECK_MS = 1500;
 static const int NONE = 0;
 static const int ANTI_PERSONNEL = 1;
 static const int ANTI_ARMOUR = 2;
 static int s_iPicked;
 static int s_iFired;
 static int s_iFiredAP;
 static int s_iSwapped;
 static int s_iArmourSwaps;
 static int s_iUnloaded;
 static bool s_bTrace; // test only: print why each hold ends
 static bool s_bTestNoSettle; // test only: leave tubes loaded (swap fixture)
 static ref map<string, int> s_mClass = new map<string, int>();

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbLauncherInfantry", value) && value == "0");
  s_bSwap = !(BBB_Settings.Get("bbbLauncherSwap", value) && value == "0");
  s_bCombatLoad = !(BBB_Settings.Get("bbbLauncherCombatLoad", value) && value == "0");
  PrintFormat("[BBB] LAUNCHERS on infantry=%1 swap=%2 combatLoad=%3", s_bOn, s_bSwap, s_bCombatLoad);
 }

 static void Stop()
 {
  s_mClass.Clear();
  s_iRead = 0;
 }

 // What a round is for, from its magazine's ammo-type flags (cached per prefab). RHS's SMAW
 // HEDP inherits a HEAT flag from its base container, so a dual-purpose name counts too.
 static int ClassOf(BaseMagazineComponent magazine)
 {
  if (!magazine || !magazine.GetOwner() || !magazine.GetOwner().GetPrefabData()) return NONE;
  string prefab = magazine.GetOwner().GetPrefabData().GetPrefabName();
  int kind;
  if (s_mClass.Find(prefab, kind)) return kind;
  kind = ANTI_ARMOUR;
  MagazineUIInfo info = MagazineUIInfo.Cast(magazine.GetUIInfo());
  EAmmoType flags = 0;
  if (info) flags = info.GetAmmoTypeFlags();
  string name = prefab;
  name.ToUpper();
  if ((flags & (EAmmoType.SMOKE | EAmmoType.ILLUMINATION)) != 0) kind = NONE;
  else if ((flags & (EAmmoType.HE | EAmmoType.FRAG | EAmmoType.INCENDIARY)) != 0 || name.Contains("HEDP")) kind = ANTI_PERSONNEL;
  s_mClass.Set(prefab, kind);
  return kind;
 }

 // The round in the tube (null if empty).
 static BaseMagazineComponent InTube(BaseWeaponComponent launcher)
 {
  BaseMagazineComponent round = launcher.GetCurrentMagazine();
  if (round && round.GetAmmoCount() > 0) return round;
  return null;
 }

 // A rocket he carries (not in the tube) for this launcher's well, of the wanted kind; with NONE,
 // any usable one, anti-personnel first. Null if he has none.
 static BaseMagazineComponent Spare(IEntity soldier, BaseWeaponComponent launcher, int want)
 {
  array<BaseMuzzleComponent> muzzles = {};
  launcher.GetMuzzlesList(muzzles);
  if (muzzles.IsEmpty() || !muzzles[0].GetMagazineWell()) return null;
  InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(soldier.FindComponent(InventoryStorageManagerComponent));
  if (!inventory) return null;
  string well = muzzles[0].GetMagazineWell().Type().ToString();
  BaseMagazineComponent loaded = launcher.GetCurrentMagazine();
  array<IEntity> items = {};
  inventory.GetItems(items);
  BaseMagazineComponent fallback;
  foreach (IEntity item : items)
  {
   if (!item) continue;
   BaseMagazineComponent magazine = BaseMagazineComponent.Cast(item.FindComponent(BaseMagazineComponent));
   if (!magazine || magazine == loaded || magazine.GetAmmoCount() <= 0 || !magazine.GetMagazineWell()) continue;
   if (magazine.GetMagazineWell().Type().ToString() != well) continue;
   int kind = ClassOf(magazine);
   if (kind == NONE) continue;
   if (want == NONE && kind == ANTI_PERSONNEL) return magazine;
   if (want != NONE && kind == want) return magazine;
   if (want == NONE && !fallback) fallback = magazine;
  }
  return fallback;
 }

 // His reloadable (not disposable) launcher, or null.
 static BaseWeaponComponent Reloadable(IEntity soldier)
 {
  BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(soldier.FindComponent(BaseWeaponManagerComponent));
  if (!weapons) return null;
  array<IEntity> list = {};
  weapons.GetWeaponsList(list);
  foreach (IEntity item : list)
  {
   BaseWeaponComponent weapon = BaseWeaponComponent.Cast(item.FindComponent(BaseWeaponComponent));
   if (weapon && weapon.GetWeaponType() == EWeaponType.WT_ROCKETLAUNCHER && !BBB_Launchers.IsDisposable(item)) return weapon;
  }
  return null;
 }

 // Enemy armour he knows of: rockets are kept for it.
 static bool ArmourKnown(PerceptionComponent perception)
 {
  array<BaseTarget> enemies = {};
  perception.GetTargetsList(enemies, ETargetCategory.ENEMY);
  foreach (BaseTarget enemy : enemies)
  {
   if (!Armour(enemy.GetUnitType())) continue;
   float since = enemy.GetTimeSinceDetected();
   if (since >= 0 && since < ARMOUR_MEMORY_S) return true;
  }
  return false;
 }

 static bool Armour(EAIUnitType type)
 {
  return type == EAIUnitType.UnitType_VehicleMedium || type == EAIUnitType.UnitType_VehicleHeavy;
 }

 // Any enemy he has detected in the last minute.
 static bool InContact(PerceptionComponent perception)
 {
  array<BaseTarget> enemies = {};
  perception.GetTargetsList(enemies, ETargetCategory.ENEMY);
  foreach (BaseTarget enemy : enemies)
  {
   float since = enemy.GetTimeSinceDetected();
   if (since >= 0 && since < CONTACT_MEMORY_S) return true;
  }
  return false;
 }

 // Another enemy soldier he knows of close to the target.
 static bool Grouped(PerceptionComponent perception, BaseTarget target)
 {
  vector at = target.GetLastSeenPosition();
  array<BaseTarget> enemies = {};
  perception.GetTargetsList(enemies, ETargetCategory.ENEMY);
  foreach (BaseTarget enemy : enemies)
  {
   if (enemy == target || enemy.GetUnitType() != EAIUnitType.UnitType_Infantry || enemy.IsDisarmed()) continue;
   float since = enemy.GetTimeSinceDetected();
   if (since < 0 || since > 10) continue;
   if (vector.DistanceXZ(enemy.GetLastDetectedPosition(), at) <= GROUP_M) return true;
  }
  return false;
 }

 // Why this target is worth a round of this kind ("" if it isn't).
 static string Reason(PerceptionComponent perception, BaseTarget target, int kind)
 {
  IEntity entity = target.GetTargetEntity();
  if (entity && BBB_Buildings.RoofAt(entity.GetOrigin())) return "a man in a building";
  if (kind != ANTI_PERSONNEL) return "";
  if (Grouped(perception, target)) return "a group";
  if (target.GetTraceFraction() < 0.9) return "a man behind cover";
  return "";
 }

 // Loaded in contact: out of contact, a gunner with both kinds of round empties his tube into
 // his kit. Called from BBB_Launchers.Tick (every 2 s, AI soldiers on foot only).
 // Puts a loaded round back into his kit. Swapping by reload alone makes the engine drop the old
 // round on the ground (measured: ejected SMAW HEDP, ammo 1, no parent), so a swap stows it first.
 static bool Stow(IEntity soldier, BaseMagazineComponent round)
 {
  if (!soldier || !round || !round.GetOwner()) return false;
  SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(soldier.FindComponent(SCR_InventoryStorageManagerComponent));
  if (!inventory) return false;
  IEntity entity = round.GetOwner();
  BaseInventoryStorageComponent into = inventory.FindStorageForItem(entity, EStoragePurpose.PURPOSE_DEPOSIT);
  return into && inventory.TryMoveItemToStorage(entity, into);
 }

 static void Settle(SCR_AIGroup group, IEntity soldier)
 {
  Read();
  if (!s_bCombatLoad || s_bTestNoSettle || BBB.HasPlayer(group)) return;
  ChimeraCharacter character = ChimeraCharacter.Cast(soldier);
  if (!character || character.IsInVehicle()) return;
  BaseWeaponComponent launcher = Reloadable(soldier);
  if (!launcher) return;
  BaseMagazineComponent round = InTube(launcher);
  if (!round) return;
  BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(soldier.FindComponent(BaseWeaponManagerComponent));
  if (weapons && weapons.GetCurrentWeapon() == launcher) return; // in his hands: he's using it
  int kind = ClassOf(round);
  int other = ANTI_PERSONNEL;
  if (kind == ANTI_PERSONNEL) other = ANTI_ARMOUR;
  if (!Spare(soldier, launcher, other)) return;
  PerceptionComponent perception = PerceptionComponent.Cast(soldier.FindComponent(PerceptionComponent));
  if (!perception || InContact(perception)) return;
  if (!Stow(soldier, round)) return;
  s_iUnloaded++;
  BBB_Perf.Count("launcher.unloaded");
  BBB_Think.Note(soldier, "Launcher", "no enemy about: carries his " + BBB_Launchers.WeaponName(launcher.GetOwner()) + " empty, loads for the first target");
 }
}

modded class SCR_AICombatComponent
{
 protected BaseTarget m_BBBLaunchTarget;
 protected BaseWeaponComponent m_BBBLauncher;
 protected int m_iBBBLauncherMuzzle;
 protected BaseMagazineComponent m_BBBLaunchRound;
 protected bool m_bBBBRoundSeated;
 protected float m_fBBBLaunchMin;
 protected float m_fBBBLaunchMax;
 protected float m_fBBBLaunchUntil;
 protected float m_fBBBLaunchPauseUntil;
 protected float m_fBBBLaunchCheckAt;
 protected BaseMagazineComponent m_BBBArmourRound;

 override void EvaluateWeaponAndTarget(out bool outWeaponEvent, out bool outSelectedTargetChanged,
  out BaseTarget outPrevTarget, out BaseTarget outCurrentTarget,
  out bool outRetreatTargetChanged, out bool outCompartmentChanged)
 {
  float stamp = m_fNextWeaponTargetEvaluation_ms;
  BaseWeaponComponent weaponBefore = m_SelectedWeaponComp;
  int muzzleBefore = m_iSelectedMuzzle;
  BaseMagazineComponent magazineBefore = m_SelectedMagazineComp;
  ResourceName treeBefore = m_SelectedWeaponResource;
  super.EvaluateWeaponAndTarget(outWeaponEvent, outSelectedTargetChanged, outPrevTarget, outCurrentTarget, outRetreatTargetChanged, outCompartmentChanged);
  if (stamp == m_fNextWeaponTargetEvaluation_ms) return;
  int started;
  if (BBB_Perf.s_bOn) started = System.GetTickCount();
  if (BBBHoldLauncher() || BBBPickLauncher())
  {
   // Vanilla re-picked his rifle this time round; put the launcher back, and only call it a
   // weapon change if it is one compared with before this evaluation.
   if (m_BBBLauncher == weaponBefore && m_iBBBLauncherMuzzle == muzzleBefore)
   {
    m_SelectedWeaponResource = treeBefore;
    outWeaponEvent = m_BBBLaunchRound != magazineBefore;
   }
   else
   {
    array<BaseMuzzleComponent> muzzles = {};
    m_BBBLauncher.GetMuzzlesList(muzzles);
    EMuzzleType muzzleType = 0;
    if (m_iBBBLauncherMuzzle >= 0 && m_iBBBLauncherMuzzle < muzzles.Count()) muzzleType = muzzles[m_iBBBLauncherMuzzle].GetMuzzleType();
    m_SelectedWeaponResource = m_ConfigComponent.GetTreeNameForWeaponType(EWeaponType.WT_ROCKETLAUNCHER, muzzleType);
    outWeaponEvent = true;
   }
   m_SelectedWeaponComp = m_BBBLauncher;
   m_iSelectedMuzzle = m_iBBBLauncherMuzzle;
   m_SelectedMagazineComp = m_BBBLaunchRound;
   m_fSelectedWeaponMinDist = m_fBBBLaunchMin;
   m_fSelectedWeaponMaxDist = m_fBBBLaunchMax;
   m_bSelectedWeaponDirectDamage = true;
  }
  else if (BBBArmourRound())
  {
   m_SelectedMagazineComp = m_BBBArmourRound;
   outWeaponEvent = m_SelectedWeaponComp != weaponBefore || m_iSelectedMuzzle != muzzleBefore || m_SelectedMagazineComp != magazineBefore;
  }
  if (BBB_Perf.s_bOn) BBB_Perf.Add("ev.launcher", System.GetTickCount() - started);
 }

 // Vanilla chose his launcher for enemy armour with an anti-personnel round: load an anti-armour
 // one instead if he carries it.
 protected bool BBBArmourRound()
 {
  BaseTarget target = GetCurrentTarget();
  if (!target || !BBB_LauncherUse.Armour(target.GetUnitType()) || !m_SelectedWeaponComp || m_SelectedWeaponComp.GetWeaponType() != EWeaponType.WT_ROCKETLAUNCHER)
  {
   m_BBBArmourRound = null;
   return false;
  }
  BBB_LauncherUse.Read();
  if (!BBB_LauncherUse.s_bSwap || BBB_LauncherUse.ClassOf(m_SelectedMagazineComp) != BBB_LauncherUse.ANTI_PERSONNEL) return false;
  if (m_BBBArmourRound && m_BBBArmourRound.GetAmmoCount() > 0) return true;
  m_BBBArmourRound = BBB_LauncherUse.Spare(GetOwner(), m_SelectedWeaponComp, BBB_LauncherUse.ANTI_ARMOUR);
  if (!m_BBBArmourRound) return false;
  // No room to stow the loaded round: fire it (HEDP still hurts armour) rather than drop it.
  BaseMagazineComponent loaded = BBB_LauncherUse.InTube(m_SelectedWeaponComp);
  if (loaded && !BBB_LauncherUse.Stow(GetOwner(), loaded))
  {
   m_BBBArmourRound = null;
   return false;
  }
  BBB_LauncherUse.s_iArmourSwaps++;
  BBB_Perf.Count("launcher.armourswap");
  BBB_Think.Note(GetOwner(), "Launcher", "armour: loads his " + BBB_Launchers.WeaponName(m_BBBArmourRound.GetOwner()) + " instead");
  return true;
 }

 protected void BBBEndLaunch(float now, string why)
 {
  if (BBB_LauncherUse.s_bTrace) PrintFormat("[BBB] LAUNCHTRACE %1: hold ends, %2", GetOwner().GetID(), why);
  m_BBBLaunchTarget = null;
  m_BBBLauncher = null;
  m_BBBLaunchRound = null;
  m_bBBBRoundSeated = false;
  m_fBBBLaunchPauseUntil = now + BBB_LauncherUse.PAUSE_MS;
 }

 // Keeps a pick while its target is still the one vanilla chose and in sight, until the
 // rocket has left the tube or the hold runs out.
 protected bool BBBHoldLauncher()
 {
  if (!m_BBBLaunchTarget) return false;
  float now = GetGame().GetWorld().GetWorldTime();
  // Fired: the round he loaded for this target was in the tube and now is gone or spent
  // (a swap unloads the old round first, so the tube's count alone can't tell).
  if (m_bBBBRoundSeated && (!m_BBBLaunchRound || m_BBBLaunchRound.GetAmmoCount() <= 0 || !m_BBBLauncher || BBB_LauncherUse.InTube(m_BBBLauncher) != m_BBBLaunchRound))
  {
   BBB_LauncherUse.s_iFired++;
   if (BBB_LauncherUse.ClassOf(m_BBBLaunchRound) == BBB_LauncherUse.ANTI_PERSONNEL) BBB_LauncherUse.s_iFiredAP++;
   BBB_Perf.Count("launcher.fired");
   BBBEndLaunch(now, "fired");
   return false;
  }
  if (m_BBBLauncher && m_BBBLaunchRound && BBB_LauncherUse.InTube(m_BBBLauncher) == m_BBBLaunchRound) m_bBBBRoundSeated = true;
  string why;
  if (!m_BBBLauncher || !m_BBBLaunchRound) why = "round gone";
  else if (now >= m_fBBBLaunchUntil) why = "out of time";
  else if (!BBBCanUse()) why = "can't use";
  // A launcher reload takes ~10 s (SMAW, measured). Dropping the pick mid-reload puts the rifle
  // back up and wastes it, so target hops and a man ducking out of sight only count once the
  // round is seated; then follow vanilla to another man the rocket suits.
  BaseTarget current = GetCurrentTarget();
  if (why.IsEmpty() && current && BBB_LauncherUse.Armour(current.GetUnitType())) why = "armour";
  if (why.IsEmpty() && m_bBBBRoundSeated && current != m_BBBLaunchTarget && !BBBTakeOver(current)) why = "target changed";
  if (why.IsEmpty() && m_bBBBRoundSeated && !IsTargetVisible(m_BBBLaunchTarget) && m_BBBLaunchTarget.GetTimeSinceSeen() >= 3) why = "target out of sight";
  if (!why.IsEmpty())
  {
   BBBEndLaunch(now, why);
   return false;
  }
  return true;
 }

 // A new target for the loaded launcher: visible enemy infantry in the round's range with no
 // friendlies near it. The reason was weighed when he chose to load; with the round in the
 // tube he fires it at the man vanilla is fighting rather than lower it again.
 protected bool BBBTakeOver(BaseTarget target)
 {
  if (!target || !m_Perception || target.GetUnitType() != EAIUnitType.UnitType_Infantry || target.GetTargetCategory() != ETargetCategory.ENEMY) return false;
  if (target.IsDisarmed() || !IsTargetVisible(target)) return false;
  float distance = target.GetDistance();
  if (distance < m_fBBBLaunchMin || distance > m_fBBBLaunchMax) return false;
  SCR_AIGroup group = BBBGroup();
  if (group && !BBB_Grenades.IsBlastAreaClearOfFriendlies(target.GetLastSeenPosition(), group, BBB_LauncherUse.FRIEND_M)) return false;
  m_BBBLaunchTarget = target;
  return true;
 }

 // Test only: the hold as text.
 string BBBLaunchState()
 {
  if (!m_BBBLaunchTarget) return "no hold";
  float now = GetGame().GetWorld().GetWorldTime();
  string round = "none";
  if (m_BBBLaunchRound) round = BBB_Launchers.WeaponName(m_BBBLaunchRound.GetOwner());
  return string.Format("hold %1 seated=%2 %3 s left", round, m_bBBBRoundSeated, Math.Round((m_fBBBLaunchUntil - now) / 1000));
 }

 protected SCR_AIGroup BBBGroup()
 {
  AIAgent agent = GetAiAgent();
  if (!agent) return null;
  return SCR_AIGroup.Cast(agent.GetParentGroup());
 }

 // The soldier is free to stop and fire a launcher.
 protected bool BBBCanUse()
 {
  IEntity owner = GetOwner();
  if (!m_Utility || !m_AIInfo || !owner || BBB.PlayerOf(owner)) return false;
  if (GetCombatMode() == EAIGroupCombatMode.HOLD_FIRE) return false;
  if (m_AIInfo.HasUnitState(EUnitState.IN_VEHICLE) || m_AIInfo.HasUnitState(EUnitState.UNCONSCIOUS)) return false;
  if (BBB_Claims.Role(owner) > 0 || BBB_OrderIntent.Blocks(m_Utility)) return false;
  SCR_AIGroup group = BBBGroup();
  return !group || !BBB_Morale.IsWithdrawing(group);
 }

 protected bool BBBPickLauncher()
 {
  float now = GetGame().GetWorld().GetWorldTime();
  if (now < m_fBBBLaunchCheckAt || now < m_fBBBLaunchPauseUntil) return false;
  BaseTarget target = GetCurrentTarget();
  if (!target || target.GetUnitType() != EAIUnitType.UnitType_Infantry || target.GetTargetCategory() != ETargetCategory.ENEMY) return false;
  // Cheap filters every evaluation; the rest at most every 1.5 s.
  if (!m_WeaponTargetSelector.HasWeaponOfType(EWeaponType.WT_ROCKETLAUNCHER, false)) return false;
  BBB_LauncherUse.Read();
  if (!BBB_LauncherUse.s_bOn || target.IsDisarmed() || !IsTargetVisible(target)) return false;
  m_fBBBLaunchCheckAt = now + BBB_LauncherUse.CHECK_MS;
  if (!m_Perception || !BBBCanUse()) return false;
  float distance = target.GetDistance();
  if (distance < BBB_LauncherUse.AT_MIN_M || distance > BBB_LauncherUse.MAX_M) return false;
  BaseWeaponComponent launcher = m_WeaponTargetSelector.FindWeaponOfType(EWeaponType.WT_ROCKETLAUNCHER, false);
  if (!launcher || !launcher.GetOwner()) return false;
  if (BBB_LauncherUse.ArmourKnown(m_Perception)) return false;
  IEntity owner = GetOwner();
  bool disposable = BBB_Launchers.IsDisposable(launcher.GetOwner());
  // The round for this target: the one in the tube, or for an empty tube a carried one
  // (anti-personnel first); a wrong one in the tube is swapped when there's time.
  BaseMagazineComponent round = BBB_LauncherUse.InTube(launcher);
  bool swap;
  if (!round) round = BBB_LauncherUse.Spare(owner, launcher, BBB_LauncherUse.NONE);
  if (!round) return false;
  int kind = BBB_LauncherUse.ClassOf(round);
  if (kind == BBB_LauncherUse.NONE) return false;
  if (disposable) kind = BBB_LauncherUse.ANTI_ARMOUR;
  string reason = BBB_LauncherUse.Reason(m_Perception, target, kind);
  if (reason.IsEmpty() && kind == BBB_LauncherUse.ANTI_ARMOUR && !disposable && BBB_LauncherUse.s_bSwap && BBB_LauncherUse.InTube(launcher) == round
   && distance >= BBB_LauncherUse.SWAP_MIN_M && m_Utility.m_ThreatSystem && m_Utility.m_ThreatSystem.GetThreatMeasure() < BBB_LauncherUse.SWAP_MAX_THREAT)
  {
   BaseMagazineComponent better = BBB_LauncherUse.Spare(owner, launcher, BBB_LauncherUse.ANTI_PERSONNEL);
   if (better)
   {
    reason = BBB_LauncherUse.Reason(m_Perception, target, BBB_LauncherUse.ANTI_PERSONNEL);
    if (!reason.IsEmpty()) { round = better; kind = BBB_LauncherUse.ANTI_PERSONNEL; swap = true; }
   }
  }
  if (reason.IsEmpty()) return false;
  float minimum = BBB_LauncherUse.AT_MIN_M;
  if (kind == BBB_LauncherUse.ANTI_PERSONNEL) minimum = BBB_LauncherUse.AP_MIN_M;
  float maximum = BBB_LauncherUse.MAX_M;
  if (disposable) maximum = BBB_LauncherUse.DISPOSABLE_MAX_M;
  if (distance < minimum || distance > maximum) return false;
  SCR_AIGroup group = BBBGroup();
  if (group && !BBB_Grenades.IsBlastAreaClearOfFriendlies(target.GetLastSeenPosition(), group, BBB_LauncherUse.FRIEND_M)) return false;
  if (swap && !BBB_LauncherUse.Stow(owner, BBB_LauncherUse.InTube(launcher))) return false;
  m_BBBLaunchTarget = target;
  m_BBBLauncher = launcher;
  m_iBBBLauncherMuzzle = 0;
  m_BBBLaunchRound = round;
  m_bBBBRoundSeated = false;
  m_fBBBLaunchMin = minimum;
  m_fBBBLaunchMax = maximum;
  m_fBBBLaunchUntil = now + BBB_LauncherUse.HOLD_MS;
  if (swap || BBB_LauncherUse.InTube(launcher) != round) m_fBBBLaunchUntil = now + BBB_LauncherUse.SWAP_HOLD_MS;
  BBB_LauncherUse.s_iPicked++;
  if (swap) BBB_LauncherUse.s_iSwapped++;
  BBB_Perf.Count("launcher.picked");
  string action = "fires his ";
  if (swap) action = "swaps to his ";
  else if (BBB_LauncherUse.InTube(launcher) != round) action = "loads his ";
  BBB_Think.Note(owner, "Launcher", string.Format("%1%2 for %3, %4 m", action, BBB_Launchers.WeaponName(round.GetOwner()), reason, Math.Round(distance)));
  return true;
 }
}
