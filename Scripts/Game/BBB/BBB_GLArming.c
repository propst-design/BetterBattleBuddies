// Grenade launchers inside arming range (12 Nov 2026: an FIA M79 gunner emptied his grenades
// at a jeep too close for them to arm).
// A 40 mm grenade arms only after it has flown its fuze's safety distance (the round's trigger
// SafetyDistance: VOG-25 10 m, M406/M433 15 m); closer, it lands as a dud. The engine's weapon
// picker still hands a soldier his grenade launcher (M79, M203, GP-25, modded ones) at targets
// that close, and the fire tree only holds fire under the picker's own minimum range, which is
// shorter (14 m for vanilla 40 mm, measured to the target's centre). So against a jeep 14-17 m
// off he fired every grenade he had for nothing.
// BBB checks every pick last (called from the outermost EvaluateWeaponAndTarget layer,
// BBB_LauncherUse.c). An HE, frag or dual-purpose round in a grenade-launcher muzzle with its
// target closer than the round's arming distance + 5 m (+10 m for 4 s after a refusal, so he
// doesn't flip weapons at the line) is not fired:
//  - he takes his rifle or sidearm (the engine's own picker, with grenade launchers, grenades and
//    rocket launchers left out), or, if it offers nothing, the rifle muzzle of an under-barrel
//    launcher's own rifle;
//  - with nothing else (an M79 alone, a turret grenade launcher) the launcher stays in hand with
//    its minimum range raised to that line: the fire tree holds fire and vanilla's attack moves
//    step him back from the target until he is far enough to fire.
// Smoke and illumination rounds, and targets beyond arming range, are left alone. The arming
// distance is read from the round's prefab (enabled trigger components); a round with no trigger
// to read counts as 15 m. -bbbGLArming 0: off.
class BBB_GLArming
{
 static int s_iRead;
 static bool s_bOn = true;
 static bool s_bTestOff;   // test only: the A/B control round
 static int s_iRefused;    // picks refused under arming range
 static int s_iSwitched;   // of which he took another weapon or muzzle
 static int s_iHeld;       // of which he held the launcher, minimum range raised
 static const float MARGIN_M = 5;
 static const float STICKY_M = 5;
 static const float STICKY_MS = 4000;
 static const float UNKNOWN_ARM_M = 15;
 static ref map<string, float> s_mArm;

 // Called by BBB_StaticInit at game start (static initializer budget: tools/check_static_init.py).
 static void BBB_InitStatics()
 {
  s_mArm = new map<string, float>();
 }

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbGLArming", value) && value == "0");
  PrintFormat("[BBB] GL ARMING on=%1", s_bOn);
 }

 static void Stop()
 {
  if (s_mArm) s_mArm.Clear();
  s_iRead = 0;
 }

 // A grenade-launcher muzzle: an under-barrel one, a grenade launcher, or any muzzle loaded with a
 // round that is itself the projectile (a 40 mm round carries its own fuze trigger; a rifle
 // magazine doesn't). The last catches modded launchers declared as rifles.
 static bool IsLauncherMuzzle(BaseWeaponComponent weapon, BaseMuzzleComponent muzzle)
 {
  if (!weapon || !muzzle) return false;
  if (muzzle.GetMuzzleType() == EMuzzleType.MT_UGLMuzzle || weapon.GetWeaponType() == EWeaponType.WT_GRENADELAUNCHER) return true;
  return FuzeM(muzzle.GetMagazine()) >= 0;
 }

 // How far this round flies before it arms; 0 = no limit (smoke, illumination, a fuze armed at
 // the muzzle, not a grenade round). A launcher round with no trigger to read counts as 15 m.
 static float ArmingM(BaseMagazineComponent round, bool launcher)
 {
  float arm = FuzeM(round);
  if (arm < 0)
  {
   if (launcher && round && !NoFuze(round)) return UNKNOWN_ARM_M;
   return 0;
  }
  return arm;
 }

 protected static bool NoFuze(BaseMagazineComponent round)
 {
  MagazineUIInfo info = MagazineUIInfo.Cast(round.GetUIInfo());
  return info && (info.GetAmmoTypeFlags() & (EAmmoType.SMOKE | EAmmoType.ILLUMINATION)) != 0;
 }

 // The round's own fuze safety distance: 0 for smoke and illumination, -1 if the magazine
 // prefab has no trigger (a rifle magazine, a round that names its projectile elsewhere).
 // Cached per prefab.
 static float FuzeM(BaseMagazineComponent round)
 {
  if (!round || !round.GetOwner()) return -1;
  EntityPrefabData data = round.GetOwner().GetPrefabData();
  if (!data) return -1;
  string prefab = data.GetPrefabName();
  float arm;
  if (s_mArm.Find(prefab, arm)) return arm;
  arm = SafetyDistance(data.GetPrefab());
  if (arm > 0 && NoFuze(round)) arm = 0;
  s_mArm.Set(prefab, arm);
  return arm;
 }

 // The largest SafetyDistance of the prefab's enabled trigger components; -1 if it has none.
 static float SafetyDistance(BaseContainer prefab)
 {
  if (!prefab) return -1;
  BaseContainerList components = prefab.GetObjectArray("components");
  if (!components) return -1;
  float best = -1;
  for (int i = 0; i < components.Count(); i++)
  {
   BaseContainer component = components.Get(i);
   if (!component) continue;
   string kind = component.GetClassName();
   if (!kind.Contains("Trigger")) continue;
   bool enabled = true;
   component.Get("Enabled", enabled);
   if (!enabled) continue;
   float distance;
   if (component.Get("SafetyDistance", distance) && distance > best) best = distance;
  }
  return best;
 }
}

modded class SCR_AICombatComponent
{
 protected float m_fBBBArmRefusedAt;

 float BBBMaxDist() { return m_fSelectedWeaponMaxDist; }

 // Last word on the weapon pick (see the header). The "before" values are the pick before this
 // evaluation, so a weapon event is only raised for a real change.
 protected void BBBArmingGuard(BaseWeaponComponent weaponBefore, int muzzleBefore, BaseMagazineComponent magazineBefore,
  ResourceName treeBefore, inout bool outWeaponEvent)
 {
  if (!m_SelectedWeaponComp) return;
  EWeaponType type = m_SelectedWeaponComp.GetWeaponType();
  if (type == EWeaponType.WT_FRAGGRENADE || type == EWeaponType.WT_SMOKEGRENADE || type == EWeaponType.WT_ROCKETLAUNCHER) return;
  BaseTarget target = GetCurrentTarget();
  if (!target) return;
  array<BaseMuzzleComponent> muzzles = {};
  m_SelectedWeaponComp.GetMuzzlesList(muzzles);
  if (m_iSelectedMuzzle < 0 || m_iSelectedMuzzle >= muzzles.Count()) return;
  BaseMuzzleComponent muzzle = muzzles[m_iSelectedMuzzle];
  if (!BBB_GLArming.IsLauncherMuzzle(m_SelectedWeaponComp, muzzle) && BBB_GLArming.FuzeM(m_SelectedMagazineComp) < 0) return;
  BBB_GLArming.Read();
  if (!BBB_GLArming.s_bOn || BBB_GLArming.s_bTestOff) return;
  BaseMagazineComponent round = m_SelectedMagazineComp;
  if (!round) round = muzzle.GetMagazine();
  float arm = BBB_GLArming.ArmingM(round, true);
  if (arm <= 0) return;
  float now = GetGame().GetWorld().GetWorldTime();
  float line = arm + BBB_GLArming.MARGIN_M;
  bool sticky = m_fBBBArmRefusedAt > 0 && now - m_fBBBArmRefusedAt < BBB_GLArming.STICKY_MS;
  if (sticky) line += BBB_GLArming.STICKY_M;
  float distance = target.GetDistance();
  if (distance >= line) return;
  m_fBBBArmRefusedAt = now;
  BBB_GLArming.s_iRefused++;
  BBB_Perf.Count("glarming.refused");
  bool turret = m_AIInfo && m_AIInfo.HasUnitState(EUnitState.IN_TURRET);
  if (!turret && BBBArmingOtherWeapon(target, muzzles, weaponBefore, muzzleBefore, magazineBefore, treeBefore, outWeaponEvent))
  {
   BBB_GLArming.s_iSwitched++;
   if (!sticky) BBB_Think.Note(GetOwner(), "Launcher", string.Format("target %1 m away, inside the grenade's %2 m arming range: rifle instead", Math.Round(distance), Math.Round(arm)));
   return;
  }
  // Nothing else to fire: keep the launcher, but not under the arming line. The fire tree holds
  // fire below the weapon's minimum range and the attack moves back off to it.
  m_fSelectedWeaponMinDist = Math.Max(m_fSelectedWeaponMinDist, line);
  BBB_GLArming.s_iHeld++;
  if (!sticky) BBB_Think.Note(GetOwner(), "Launcher", string.Format("target %1 m away, inside the grenade's %2 m arming range: holds fire, backs off", Math.Round(distance), Math.Round(arm)));
 }

 // His rifle or sidearm for this target, if he has one; true once selected.
 protected bool BBBArmingOtherWeapon(BaseTarget target, array<BaseMuzzleComponent> muzzles, BaseWeaponComponent weaponBefore,
  int muzzleBefore, BaseMagazineComponent magazineBefore, ResourceName treeBefore, inout bool outWeaponEvent)
 {
  BaseWeaponComponent weapon;
  int muzzleId = -1;
  BaseMagazineComponent magazine;
  float minDist, maxDist;
  bool direct = true;
  if (m_WeaponTargetSelector)
  {
   array<int> notThese = {EWeaponType.WT_GRENADELAUNCHER, EWeaponType.WT_FRAGGRENADE, EWeaponType.WT_SMOKEGRENADE, EWeaponType.WT_ROCKETLAUNCHER};
   if (m_WeaponTargetSelector.SelectWeaponAgainstUnitTypeAndDistance(target.GetUnitType(), target.GetDistance(), true, false, null, notThese))
   {
    m_WeaponTargetSelector.GetSelectedWeapon(weapon, muzzleId, magazine);
    m_WeaponTargetSelector.GetSelectedWeaponProperties(minDist, maxDist, direct);
    if (weapon)
    {
     // An under-barrel launcher's rifle can come back on its launcher muzzle.
     array<BaseMuzzleComponent> offered = {};
     weapon.GetMuzzlesList(offered);
     if (muzzleId < 0 || muzzleId >= offered.Count() || BBB_GLArming.IsLauncherMuzzle(weapon, offered[muzzleId])) weapon = null;
    }
   }
  }
  if (!weapon)
  {
   // The picker offered nothing (a rifle against armour, say): the rifle muzzle of his own
   // under-barrel launcher, if it has rounds in.
   if (m_SelectedWeaponComp.GetWeaponType() == EWeaponType.WT_GRENADELAUNCHER) return false;
   for (int i = 0; i < muzzles.Count(); i++)
   {
    if (i == m_iSelectedMuzzle || BBB_GLArming.IsLauncherMuzzle(m_SelectedWeaponComp, muzzles[i])) continue;
    BaseMagazineComponent loaded = muzzles[i].GetMagazine();
    if (!loaded || loaded.GetAmmoCount() <= 0) continue;
    weapon = m_SelectedWeaponComp;
    muzzleId = i;
    magazine = loaded;
    minDist = 0;
    maxDist = Math.Max(m_fSelectedWeaponMaxDist, 100);
    direct = true;
    break;
   }
   if (!weapon) return false;
  }
  if (weapon == weaponBefore && muzzleId == muzzleBefore)
  {
   m_SelectedWeaponResource = treeBefore;
   outWeaponEvent = magazine != magazineBefore;
  }
  else
  {
   array<BaseMuzzleComponent> list = {};
   weapon.GetMuzzlesList(list);
   EMuzzleType muzzleType = 0;
   if (muzzleId >= 0 && muzzleId < list.Count()) muzzleType = list[muzzleId].GetMuzzleType();
   m_SelectedWeaponResource = m_ConfigComponent.GetTreeNameForWeaponType(weapon.GetWeaponType(), muzzleType);
   outWeaponEvent = true;
  }
  m_SelectedWeaponComp = weapon;
  m_iSelectedMuzzle = muzzleId;
  m_SelectedMagazineComp = magazine;
  m_fSelectedWeaponMinDist = minDist;
  m_fSelectedWeaponMaxDist = maxDist;
  m_bSelectedWeaponDirectDamage = direct;
  return true;
 }
}
