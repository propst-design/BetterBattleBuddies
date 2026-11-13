// Grenade launchers inside arming range (-bbbGLArmingTest 1, preset glarming; about 3 minutes).
// Prefab read: VOG-25 arms at 10 m, M406 and M433 at 15 m, a 40 mm flare and smoke round have no
// limit. Then the grenadier lane (BBB_GrenadierTest: two US M203 grenadiers with M433 HEDP, a
// BRDM-2 with only an enemy driver on hold fire), 40 s a round:
//  0. 10 m, the guard off (control): the 40 mm the engine fires there (the bug);
//  1. 10 m, the guard on: no 40 mm fired, and the guard turned picks away;
//  2. 80 m, the guard on: 40 mm fired, the guard never stepped in.
// -bbbGLArmingM79 1 (preset glarmingm79, loads ARMA-RY's M79 mod) plays the case seen in play
// instead: two M79 grenadiers stripped of their rifles (M79 only) and a UAZ jeep at 15 m, 15 m and
// 80 m. The engine gives the 40 mm a minimum range of 14 m against a 15 m fuze, measured to the
// target's centre, so at 15 m the control must show it firing; with the guard he must not, and
// must hold the launcher (nothing else to fire). In the M203 run the control is reported, not
// checked: at 10 m the engine's own 14 m minimum already holds fire (12 Nov).
// A round counts 40 mm carried and loaded at the hook and at the end, and logs what the picker
// chose (weapon and muzzle type, minimum range). "[BBB] GLARMTEST ...".
class BBB_GLArmingTest : BBB_GrenadierTest
{
 static ref BBB_GLArmingTest s_Arm;
 static bool s_bM79;
 static const ResourceName M79_MAN = "{386D76E51396F48D}Prefabs/Characters/Factions/BLUFOR/US_Army/ARY_Character_US_GL_M79.et";
 static const ResourceName UAZ = "{259EE7B78C51B624}Prefabs/Vehicles/Wheeled/UAZ469/UAZ469.et";
 static const float ARM_ROUND_S = 40;
 static const ResourceName M406 = "{5375FA7CB1F68573}Prefabs/Weapons/Ammo/Ammo_Grenade_HE_M406.et";
 static const ResourceName M433 = "{1663496AE5B9F10B}Prefabs/Weapons/Ammo/Ammo_Grenade_HEDP_M433.et";
 static const ResourceName VOG25 = "{262F0D09C4130826}Prefabs/Weapons/Ammo/Ammo_Grenade_HE_VOG25.et";
 static const ResourceName FLARE = "{98DB57ECEDC81CC2}Prefabs/Weapons/Ammo/Ammo_Flare_40mm_M583A1_White.et";
 static const ResourceName SMOKE = "{E9E666CCF19FB22B}Prefabs/Weapons/Ammo/Ammo_Grenade_Smoke_US_base.et";
 protected int m_iArmChecks;
 protected int m_iArmFailures;
 protected int m_iRefusedAtStart;
 protected int m_iStartCount;
 protected ref array<int> m_aFired;
 protected ref array<int> m_aRefused;
 protected ref array<int> m_aEnemy;
 protected string m_sPick;
 protected int m_iHeldAtStart;
 protected ref array<int> m_aHeld;
 protected ref array<int> m_aLast;      // per grenadier, 40 mm he had at the last tick
 protected int m_iClose;                // 40 mm fired this round from inside CLOSE_M of the vehicle
 protected float m_fNearestShot;
 protected ref array<int> m_aClose;
 static const float CLOSE_M = 20;       // M433's 15 m fuze + the guard's 5 m

 static void MaybeStartArm()
 {
  if (s_Arm || !Replication.IsServer() || !System.IsCLIParam("bbbGLArmingTest")) return;
  s_bM79 = System.IsCLIParam("bbbGLArmingM79");
  s_Arm = new BBB_GLArmingTest();
  GetGame().GetCallqueue().CallLater(s_Arm.Begin, 10000);
 }

 void BBB_GLArmingTest()
 {
  m_aCheckDistances = {10, 80};
  m_aFired = {};
  m_aRefused = {};
  m_aEnemy = {};
  m_aHeld = {};
  m_aLast = {};
  m_aClose = {};
 }

 protected void ArmCheck(string name, bool ok, string detail)
 {
  m_iArmChecks++;
  if (!ok) m_iArmFailures++;
  PrintFormat("[BBB] GLARMTEST %1 ok=%2 %3", name, ok, detail);
 }

 // The arming distance BBB reads off a real spawned round.
 protected float ArmOf(ResourceName prefab, vector at)
 {
  IEntity item = Spawn(prefab, at, "0 0 1");
  if (!item) return -1;
  float arm = BBB_GLArming.ArmingM(BaseMagazineComponent.Cast(item.FindComponent(BaseMagazineComponent)), true);
  SCR_EntityHelper.DeleteEntityAndChildren(item);
  return arm;
 }

 override protected void Begin()
 {
  BBB_GLArming.s_iRead = 0;
  BBB_GLArming.Read();
  ArmCheck("default-on", BBB_GLArming.s_bOn, "");
  // Throws, smoke and flares stay out of the 40 mm count.
  BBB_Grenades.s_bOn = false;
  BBB_Smoke.s_bOn = false;
  BBB_Flares.s_bOn = false;
  if (!FindSite())
  {
   Print("[BBB] GLARMTEST no lane");
   Print("[ACR] VERDICT FAIL test=bbbglarming passed=0 failed=1 expected=10");
   Print("[ACR] TEST COMPLETE");
   return;
  }
  vector spot = m_aSiteFrom[0] - m_aSiteDir[0] * 20;
  float vog = ArmOf(VOG25, spot);
  float m433 = ArmOf(M433, spot);
  float m406 = ArmOf(M406, spot);
  float flare = ArmOf(FLARE, spot);
  float smoke = ArmOf(SMOKE, spot);
  ArmCheck("arm-vog25", vog == 10, "m=" + vog);
  ArmCheck("arm-m433", m433 == 15, "m=" + m433);
  ArmCheck("arm-m406", m406 == 15, "m=" + m406);
  ArmCheck("flare-free", flare == 0, "m=" + flare);
  ArmCheck("smoke-free", smoke == 0, "m=" + smoke);
  LoadLane();
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers) observers.InsertObserverSP(OBSERVER_KEY, m_aSiteFrom[0][0], m_aSiteFrom[0][2], null);
  // US HEDP v BRDM-2 (M79 run: US M79 v UAZ) at 10 m (control), 10 m, 80 m.
  float near = 10;
  if (s_bM79) near = 15;
  m_aKind.Insert(0); m_aRange.Insert(near);
  m_aKind.Insert(0); m_aRange.Insert(near);
  m_aKind.Insert(0); m_aRange.Insert(80);
  m_fNextRound = Now() + 5;
  GetGame().GetCallqueue().CallLater(Tick, 500, true);
 }

 // Every 40 mm the grenadiers carry or have loaded (HE, HEDP, VOG-25).
 protected int Count40()
 {
  int n;
  foreach (IEntity man : m_aGL) n += Count40Of(man);
  return n;
 }

 protected int Count40Of(IEntity man)
 {
  if (!man) return 0;
  int n;
  SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(man.FindComponent(SCR_InventoryStorageManagerComponent));
  if (inventory)
  {
   n += inventory.GetDepositItemCountByResource(man, M433);
   n += inventory.GetDepositItemCountByResource(man, M406);
   n += inventory.GetDepositItemCountByResource(man, VOG25);
  }
  BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(man.FindComponent(BaseWeaponManagerComponent));
  if (!weapons) return n;
  array<IEntity> list = {};
  weapons.GetWeaponsList(list);
  foreach (IEntity item : list)
  {
   BaseWeaponComponent weapon = BaseWeaponComponent.Cast(item.FindComponent(BaseWeaponComponent));
   if (!weapon) continue;
   array<BaseMuzzleComponent> muzzles = {};
   weapon.GetMuzzlesList(muzzles);
   foreach (BaseMuzzleComponent muzzle : muzzles)
    if (BBB_GLArming.FuzeM(muzzle.GetMagazine()) > 0) n += muzzle.GetAmmoCount();
  }
  return n;
 }

 // A drop in a man's 40 mm since the last tick is a shot: note how far he was from the vehicle.
 protected void TrackShots()
 {
  if (m_iStartCount < 0 || !m_Vehicle) return;
  foreach (int i, IEntity man : m_aGL)
  {
   if (i >= m_aLast.Count()) break;
   int now = Count40Of(man);
   int shots = m_aLast[i] - now;
   m_aLast[i] = now;
   if (shots <= 0 || !man) continue;
   float distance = vector.DistanceXZ(man.GetOrigin(), m_Vehicle.GetOrigin());
   m_fNearestShot = Math.Min(m_fNearestShot, distance);
   if (distance < CLOSE_M) m_iClose += shots;
   PrintFormat("[BBB] GLARMTEST shot: %1 x 40 mm from %2 m", shots, Math.Round(distance));
  }
 }

 protected void StartCount()
 {
  m_aLast.Clear();
  foreach (IEntity man : m_aGL) m_aLast.Insert(Count40Of(man));
  m_iStartCount = Count40();
  m_iHeldAtStart = BBB_GLArming.s_iHeld;
  m_iRefusedAtStart = BBB_GLArming.s_iRefused;
  m_iClose = 0;
  m_fNearestShot = 1000;
 }

 override protected ResourceName PickGLMan(ResourceName chosen)
 {
  if (s_bM79) return M79_MAN;
  return chosen;
 }

 override protected ResourceName PickVehicle(ResourceName chosen)
 {
  if (s_bM79) return UAZ;
  return chosen;
 }

 // What the first grenadier's picker holds now, once per change.
 protected void NotePick()
 {
  if (m_aGL.IsEmpty() || !m_aGL[0]) return;
  SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(m_aGL[0].FindComponent(SCR_AICombatComponent));
  if (!combat || !combat.BBBSelectedWeapon()) return;
  BaseWeaponComponent weapon = combat.BBBSelectedWeapon();
  array<BaseMuzzleComponent> muzzles = {};
  weapon.GetMuzzlesList(muzzles);
  int m = combat.BBBSelectedMuzzle();
  string muzzle = "?";
  string round = "none";
  if (m >= 0 && m < muzzles.Count())
  {
   muzzle = typename.EnumToString(EMuzzleType, muzzles[m].GetMuzzleType());
   if (muzzles[m].GetMagazine() && muzzles[m].GetMagazine().GetOwner().GetPrefabData()) round = muzzles[m].GetMagazine().GetOwner().GetPrefabData().GetPrefabName();
  }
  string target = "none";
  if (combat.GetCurrentTarget()) target = Math.Round(combat.GetCurrentTarget().GetDistance()).ToString() + " m";
  string pick = string.Format("%1 %2 muzzle %3 (%4) round %5 min %6 max %7 target %8", BBB_Armour.Short(BBB_Armour.PrefabKey(weapon.GetOwner())), typename.EnumToString(EWeaponType, weapon.GetWeaponType()), m, muzzle, BBB_Armour.Short(round), combat.GetSelectedWeaponMinDist(), combat.BBBMaxDist(), target);
  if (pick == m_sPick) return;
  m_sPick = pick;
  PrintFormat("[BBB] GLARMTEST pick: %1", pick);
 }

 // M79 run: the fixture character also carries an M16A2; FIA's grenadier has only his M79.
 protected void StripRifles()
 {
  foreach (IEntity man : m_aGL)
  {
   if (!man) continue;
   BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(man.FindComponent(BaseWeaponManagerComponent));
   SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(man.FindComponent(SCR_InventoryStorageManagerComponent));
   if (!weapons || !inventory) continue;
   array<IEntity> list = {};
   weapons.GetWeaponsList(list);
   foreach (IEntity item : list)
   {
    BaseWeaponComponent weapon = BaseWeaponComponent.Cast(item.FindComponent(BaseWeaponComponent));
    if (!weapon || weapon.GetWeaponType() == EWeaponType.WT_GRENADELAUNCHER) continue;
    if (weapon.GetWeaponType() == EWeaponType.WT_FRAGGRENADE || weapon.GetWeaponType() == EWeaponType.WT_SMOKEGRENADE) continue;
    inventory.TryRemoveItemFromInventory(item);
    SCR_EntityHelper.DeleteEntityAndChildren(item);
   }
  }
 }

 override protected bool StartRound(int round)
 {
  m_sPick = "";
  BBB_GLArming.s_bTestOff = round == 0;
  m_iStartCount = -1;
  if (!super.StartRound(round)) return false;
  // Before the AI's first pick: deleting a gun he already holds breaks vanilla's attack node.
  if (s_bM79) StripRifles();
  // Counted from the spawn: the first shots can come before the base test's hook.
  StartCount();
  m_fRoundEnd = Now() + ARM_ROUND_S;
  PrintFormat("[BBB] GLARMTEST round %1: %2 m, guard %3", round, m_aRange[round], !BBB_GLArming.s_bTestOff);
  return true;
 }

 override protected void Tick()
 {
  if (!m_aLanes.IsEmpty()) TrackShots();
  super.Tick();
  NotePick();
 }

 override protected void EndRound()
 {
  int fired = -1;
  if (m_iStartCount >= 0) fired = m_iStartCount - Count40();
  int refused = BBB_GLArming.s_iRefused - m_iRefusedAtStart;
  m_aFired.Insert(fired);
  m_aRefused.Insert(refused);
  m_aEnemy.Insert(m_iMaxEnemy);
  m_aHeld.Insert(BBB_GLArming.s_iHeld - m_iHeldAtStart);
  TrackShots();
  m_aClose.Insert(m_iClose);
  PrintFormat("[BBB] GLARMTEST round %1: 40 mm fired from inside %2 m: %3 (nearest shot %4 m)", m_aFired.Count() - 1, CLOSE_M, m_iClose, Math.Round(m_fNearestShot));
  int shots;
  if (!m_aLanes.IsEmpty()) shots = m_aLanes[0].m_A.m_iShots;
  PrintFormat("[BBB] GLARMTEST round %1 at %2 m: 40mm fired %3 (of %4), refused picks %5, switched %6, held %7, rifle rounds %8, picks GL %9", m_aFired.Count() - 1, m_aRange[m_aFired.Count() - 1], fired, m_iStartCount, refused, BBB_GLArming.s_iSwitched, BBB_GLArming.s_iHeld, shots, m_iSamplesGL);
  if (!m_aGL.IsEmpty() && m_aGL[0] && m_Vehicle) PrintFormat("[BBB] GLARMTEST round %1: first grenadier ends %2 m from the vehicle", m_aFired.Count() - 1, Math.Round(vector.DistanceXZ(m_aGL[0].GetOrigin(), m_Vehicle.GetOrigin())));
  super.EndRound();
 }

 override protected void Finish()
 {
  GetGame().GetCallqueue().Remove(Tick);
  BBB_GLArming.s_bTestOff = false;
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers) observers.RemoveObserverSP(OBSERVER_KEY);
  if (m_aFired.Count() == 3)
  {
   string control = string.Format("inside=%1 fired=%2 (the engine's own pick inside arming range + 5 m)", m_aClose[0], m_aFired[0]);
   if (s_bM79) ArmCheck("control-near-fires", m_aClose[0] >= 1, control);
   else PrintFormat("[BBB] GLARMTEST control (reported, not checked) %1", control);
   // He may back off and fire from beyond the line; never from inside it.
   ArmCheck("guard-near-none-inside", m_aClose[1] == 0 && m_aEnemy[1] > 0, string.Format("inside=%1 fired=%2 enemies=%3", m_aClose[1], m_aFired[1], m_aEnemy[1]));
   ArmCheck("guard-near-refused", m_aRefused[1] > 0, "refused=" + m_aRefused[1]);
   // M79 only: nothing else to fire, so he keeps the launcher with its minimum range raised.
   if (s_bM79) ArmCheck("guard-near-held", m_aHeld[1] > 0, "held=" + m_aHeld[1]);
   ArmCheck("guard-80m-fires", m_aFired[2] >= 1, "fired=" + m_aFired[2]);
   ArmCheck("guard-80m-untouched", m_aRefused[2] == 0, "refused=" + m_aRefused[2]);
  }
  else ArmCheck("rounds", false, "completed=" + m_aFired.Count());
  string verdict = "FAIL";
  int expected = 10;
  string name = "bbbglarming";
  if (s_bM79) { expected = 12; name = "bbbglarmingm79"; }
  if (m_iArmFailures == 0 && m_iArmChecks == expected) verdict = "PASS";
  PrintFormat("[ACR] VERDICT %1 test=%2 passed=%3 failed=%4 expected=%5", verdict, name, m_iArmChecks - m_iArmFailures, m_iArmFailures, expected);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_GLArmingTest.MaybeStartArm();
 }
}
