// Do AI soldiers fire AT launchers at infantry? (BBB #14) -bbbLauncherInfantryTest 1
// (Test-BBB.ps1 -Preset launcherinf; -Preset launcherinfrhs also loads RHS: Status Quo and
// passes -bbbLauncherInfantryRhs 1, which makes the RHS cases required).
// Two launcher men on the aim lane, 150 m from an enemy rifle team in the open, or (control:
// the set-up lets them fire their launchers at all) an enemy vehicle with only a hold-fire
// driver aboard. Shooters and the rifle team can't be hurt, so every round runs its full
// length. RHS soldiers are put in the vanilla US/USSR factions for the round.
// Cases: vanilla USSR AT (RPG-7, PG-7VM) v US team, and v an M151A2 (control);
//        RHS USMC AT (SMAW, HEDP loaded) v USSR team, and v a BTR-70 (control);
//        RHS RF SOF AT (RPO-A) v US team; RHS RF SOF AAT (RPO-D smoke) v US team.
// Per round: launcher rounds fired, split by what the shooter was targeting at the time;
// sampled picks (launcher or another weapon, at infantry or a vehicle); the rounds the AI
// had selected; rifle shots. Logs "[BBB] LAUNCHTEST ...".
// Expectations (BBB_LauncherUse, BBB #14), with -bbbLauncherInfantry 1 (default) / 0:
//  RPG-7 PG-7VM (HEAT) v the team in the open: 0 rockets either way (HEAT only at men in buildings);
//  SMAW HEDP v the team: at least 1 rocket at infantry when on, 0 when off (vanilla);
//  RPO-A v the team: at least 1 either way (vanilla already allows it); controls: 1+ at the vehicle.
//  RPO-D smoke is recorded only (vanilla fires it at people; BBB never picks smoke).
//  SMAW v BTR-70: with -bbbLauncherSwap on his first rocket at it is HEAA (anti-armour), not HEDP
//  (each man carries one HEAA; HEDP after it is all he has left).
//  Loaded in contact (-bbbLauncherCombatLoad): a SMAW pair alone for 20 s must end with empty tubes
//  and every rocket still carried; then the enemy team appears and at least 1 rocket hits the air.
// What it does not establish: infantry in cover or in buildings, HEAT at a building, an
// RPG-7 with an OG-7V/TBG-7V loaded (no soldier carries one), other ranges.
class BBB_LauncherInfantryTest : BBB_AimTest
{
 static ref BBB_LauncherInfantryTest s_Launch;
 static const ResourceName AT_USSR = "{1C78331E156A3D65}Prefabs/Characters/Factions/OPFOR/USSR_Army/Character_USSR_AT.et";
 static const ResourceName AT_USMC = "{FA4771514D0D700A}Prefabs/Characters/Factions/BLUFOR/RHS_USAF/RHS_USAF_USMC_MEF/Character_RHS_USAF_USMC_AT.et";
 static const ResourceName AT_SOF = "{58BA009D212B8E8E}Prefabs/Characters/Factions/OPFOR/RHS_AFRF/VV/RG/Atacs/Character_RHS_RF_SOF_AT.et";
 static const ResourceName AAT_SOF = "{FFF764BBA021A8C0}Prefabs/Characters/Factions/OPFOR/RHS_AFRF/VV/RG/Atacs/Character_RHS_RF_SOF_AAT.et";
 static const ResourceName JEEP = "{F649585ABB3706C4}Prefabs/Vehicles/Wheeled/M151A2/M151A2.et";
 static const ResourceName BTR = "{C012BB3488BEA0C2}Prefabs/Vehicles/Wheeled/BTR70/BTR70.et";
 static const ResourceName GROUP_USSR = "{8DE0C0830FE0C33D}Prefabs/Groups/OPFOR/Group_USSR_Base.et";
 static const ResourceName RIFLE_USSR = "{DCB41B3746FDD1BE}Prefabs/Characters/Factions/OPFOR/USSR_Army/Character_USSR_Rifleman.et";
 static const float RANGE_M = 150;
 static const float ROUND_S = 60;
 static bool s_bRhs;

 protected ref array<string> m_aName = {};
 protected ref array<ResourceName> m_aShooter = {};
 protected ref array<bool> m_aShooterUS = {};
 protected ref array<ResourceName> m_aVehicle = {};
 protected ref array<bool> m_aFromRhs = {};
 // Rockets expected at infantry: -1 not checked, 0 none, 1 at least one, 2 at least one with BBB's rule on.
 protected ref array<int> m_aExpect = {};
 protected ref array<float> m_aDelay = {};
 protected float m_fEnemyAt;
 protected int m_iRocketsAtStart;
 protected int m_iCase;
 protected IEntity m_Vehicle;
 protected ref map<IEntity, int> m_mLoaded = new map<IEntity, int>();
 // The round last seen in each tube, and the rounds that left them, in order.
 protected ref map<IEntity, string> m_mTubeRound = new map<IEntity, string>();
 protected int m_iUnloadedSeen;
 protected ref map<IEntity, IEntity> m_mTubeMagazine = new map<IEntity, IEntity>();
 protected ref array<string> m_aFired = {};
 protected int m_iAtInfantry;
 protected int m_iAtVehicle;
 protected int m_iAtNothing;
 protected int m_iPickLauncherInfantry;
 protected int m_iPickLauncherVehicle;
 protected int m_iPickOtherInfantry;
 protected int m_iPickOtherVehicle;
 protected int m_iPickNone;
 protected string m_sRounds;
 protected float m_fNextSample;
 protected int m_iChecks;
 protected int m_iFailures;
 protected int m_iRhsRun;
 protected ResourceName m_sEnemyTeam;
 protected float m_fHookTargetsAt;

 static void MaybeStartLaunch()
 {
  if (s_Launch || !Replication.IsServer() || !System.IsCLIParam("bbbLauncherInfantryTest")) return;
  s_bRhs = System.IsCLIParam("bbbLauncherInfantryRhs");
  s_Launch = new BBB_LauncherInfantryTest();
  GetGame().GetCallqueue().CallLater(s_Launch.Begin, 10000);
 }

 protected void Case(string name, ResourceName shooter, bool shooterUS, ResourceName vehicle, bool fromRhs, int expect, float delay = 0)
 {
  m_aDelay.Insert(delay);
  m_aExpect.Insert(expect);
  m_aName.Insert(name);
  m_aShooter.Insert(shooter);
  m_aShooterUS.Insert(shooterUS);
  m_aVehicle.Insert(vehicle);
  m_aFromRhs.Insert(fromRhs);
 }

 protected void Assert(string label, bool passed, string detail)
 {
  m_iChecks++;
  if (!passed) m_iFailures++;
  PrintFormat("[BBB] LAUNCHTEST CHECK %1 ok=%2 %3", label, passed, detail);
 }

 override protected void Begin()
 {
  BBB_LauncherUse.s_bTrace = true;
  if (!FindSite())
  {
   Assert("site", false, "no clear lane");
   Finish();
   return;
  }
  LoadLane();
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers)
  {
   vector mid = m_aSiteFrom[0] + m_aSiteDir[0] * (RANGE_M / 2);
   observers.InsertObserverSP(OBSERVER_KEY, mid[0], mid[2], null);
  }
  Case("vanilla RPG-7 (PG-7VM) v infantry", AT_USSR, false, "", false, 0);
  Case("vanilla RPG-7 (PG-7VM) v M151A2 (control)", AT_USSR, false, JEEP, false, -1);
  Case("RHS SMAW (HEDP) v infantry", AT_USMC, true, "", true, 2);
  Case("RHS SMAW (HEDP) v BTR-70 (control)", AT_USMC, true, BTR, true, -1);
  Case("RHS RPO-A v infantry", AT_SOF, false, "", true, 1);
  Case("RHS RPO-D (smoke) v infantry", AAT_SOF, false, "", true, -1);
  Case("RHS SMAW loaded in contact v infantry", AT_USMC, true, "", true, 2, 20);
  m_fNextRound = Now() + 5;
  GetGame().GetCallqueue().CallLater(Tick, 500, true);
 }

 protected SCR_AIGroup EmptyGroup(ResourceName groupPrefab, ResourceName slot, int slots, vector at, vector dir, BBB_AimLane lane)
 {
  SCR_AIGroup.IgnoreSpawning(true);
  SCR_AIGroup group = SCR_AIGroup.Cast(Spawn(groupPrefab, at, dir));
  SCR_AIGroup.IgnoreSpawning(false);
  if (!group) return null;
  lane.m_aSpawned.Insert(group);
  if (!group.m_aUnitPrefabSlots) group.m_aUnitPrefabSlots = {};
  group.m_aUnitPrefabSlots.Clear();
  for (int i = 0; i < slots; i++) group.m_aUnitPrefabSlots.Insert(slot);
  return group;
 }

 protected void SetFaction(IEntity man, string key)
 {
  FactionAffiliationComponent faction = FactionAffiliationComponent.Cast(man.FindComponent(FactionAffiliationComponent));
  if (faction) faction.SetAffiliatedFactionByKey(key);
 }

 protected void Invulnerable(BBB_AimSide side)
 {
  foreach (IEntity man : side.m_aMen)
  {
   ChimeraCharacter character = ChimeraCharacter.Cast(man);
   if (character && character.GetDamageManager()) character.GetDamageManager().EnableDamageHandling(false);
  }
 }

 override protected bool StartRound(int round)
 {
  while (round < m_aName.Count() && m_aFromRhs[round] && !Resource.Load(m_aShooter[round]).IsValid())
  {
   PrintFormat("[BBB] LAUNCHTEST SKIP %1: RHS isn't loaded", m_aName[round]);
   if (s_bRhs) Assert("rhs-loaded", false, m_aName[round]);
   round++;
   m_iRound = round;
  }
  if (round >= m_aName.Count()) return false;
  m_iCase = round;
  m_aLanes.Clear();
  m_mLoaded.Clear();
  m_mTubeRound.Clear();
  m_mTubeMagazine.Clear();
  m_aFired.Clear();
  BBB_AimLane lane = new BBB_AimLane();
  lane.m_vFrom = m_aSiteFrom[0];
  lane.m_vDir = m_aSiteDir[0];
  lane.m_vTo = lane.m_vFrom + lane.m_vDir * RANGE_M;
  bool us = m_aShooterUS[round];
  string shooterFaction = "USSR";
  ResourceName shooterGroup = GROUP_USSR;
  ResourceName enemyTeam = TEAM_US;
  ResourceName enemyMan = RIFLE_US;
  ResourceName enemyGroup = GROUP_US;
  if (us)
  {
   shooterFaction = "US";
   shooterGroup = GROUP_US;
   enemyTeam = TEAM_USSR;
   enemyMan = RIFLE_USSR;
   enemyGroup = GROUP_USSR;
  }
  SCR_AIGroup team = EmptyGroup(shooterGroup, m_aShooter[round], 2, lane.m_vFrom, lane.m_vDir, lane);
  vector side = Vector(lane.m_vDir[2], 0, -lane.m_vDir[0]);
  for (int i = 0; i < 2 && team; i++)
  {
   IEntity man = Spawn(m_aShooter[round], lane.m_vFrom + side * (i * 4), lane.m_vDir);
   if (!man) continue;
   SetFaction(man, shooterFaction);
   lane.m_aSpawned.Insert(man);
   team.AddAIEntityToGroup(man);
  }
  lane.m_A.m_Group = team;
  m_Vehicle = null;
  ResourceName vehicle = m_aVehicle[round];
  m_fEnemyAt = 0;
  m_sEnemyTeam = enemyTeam;
  if (vehicle.IsEmpty() && m_aDelay[round] > 0) m_fEnemyAt = Now() + m_aDelay[round];
  else if (vehicle.IsEmpty())
  {
   lane.m_B.m_Group = SCR_AIGroup.Cast(Spawn(enemyTeam, lane.m_vTo, lane.m_vDir * -1));
   if (lane.m_B.m_Group) lane.m_aSpawned.Insert(lane.m_B.m_Group);
  }
  else
  {
   m_Vehicle = Spawn(vehicle, lane.m_vTo, side);
   if (m_Vehicle) lane.m_aSpawned.Insert(m_Vehicle);
   SCR_AIGroup crew = EmptyGroup(enemyGroup, enemyMan, 1, lane.m_vTo + lane.m_vDir * 5, lane.m_vDir, lane);
   IEntity driver;
   if (crew && m_Vehicle) driver = Spawn(enemyMan, lane.m_vTo + lane.m_vDir * 5, lane.m_vDir);
   if (driver)
   {
    lane.m_aSpawned.Insert(driver);
    crew.AddAIEntityToGroup(driver);
    ChimeraCharacter character = ChimeraCharacter.Cast(driver);
    SCR_BaseCompartmentManagerComponent compartments = SCR_BaseCompartmentManagerComponent.Cast(m_Vehicle.FindComponent(SCR_BaseCompartmentManagerComponent));
    if (character && compartments && character.GetCompartmentAccessComponent())
    {
     array<BaseCompartmentSlot> seats = {};
     compartments.GetCompartmentsOfType(seats, ECompartmentType.PILOT);
     if (!seats.IsEmpty()) character.GetCompartmentAccessComponent().GetInVehicle(m_Vehicle, seats[0], true, -1, ECloseDoorAfterActions.CLOSE_DOOR, false);
    }
    SCR_AIGroupUtilityComponent utility = SCR_AIGroupUtilityComponent.Cast(crew.FindComponent(SCR_AIGroupUtilityComponent));
    if (utility) utility.SetCombatMode(EAIGroupCombatMode.HOLD_FIRE);
   }
  }
  lane.m_fStart = Now();
  lane.m_fHookAt = Now() + 4;
  m_aLanes.Insert(lane);
  // The loaded-in-contact case also waits out a ~10 s reload, and enemies who go to ground
  // can stay unseen for half a minute: give it longer.
  float extra = 0;
  if (m_aDelay[round] > 0) extra = 30;
  m_fRoundEnd = Now() + ROUND_S + m_aDelay[round] + extra;
  m_iAtInfantry = 0;
  m_iAtVehicle = 0;
  m_iAtNothing = 0;
  m_iPickLauncherInfantry = 0;
  m_iPickLauncherVehicle = 0;
  m_iPickOtherInfantry = 0;
  m_iPickOtherVehicle = 0;
  m_iPickNone = 0;
  m_sRounds = "";
  m_fNextSample = Now() + 6;
  m_iRocketsAtStart = -1;
  if (m_aFromRhs[round]) m_iRhsRun++;
  PrintFormat("[BBB] LAUNCHTEST round %1/%2: %3 at %4 m", round + 1, m_aName.Count(), m_aName[round], RANGE_M);
  return true;
 }

 // Rockets for the shooters' reloadable launchers: in the tubes (inTube) or everywhere else on them.
 protected int Rockets(BBB_AimSide shooters, bool inTube)
 {
  int n;
  foreach (IEntity man : shooters.m_aMen)
  {
   if (!man) continue;
   BaseWeaponComponent launcher = BBB_LauncherUse.Reloadable(man);
   if (!launcher) continue;
   BaseMagazineComponent loaded = BBB_LauncherUse.InTube(launcher);
   if (inTube) { if (loaded) n++; continue; }
   array<BaseMuzzleComponent> muzzles = {};
   launcher.GetMuzzlesList(muzzles);
   InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(man.FindComponent(InventoryStorageManagerComponent));
   if (muzzles.IsEmpty() || !muzzles[0].GetMagazineWell() || !inventory) continue;
   string well = muzzles[0].GetMagazineWell().Type().ToString();
   array<IEntity> items = {};
   inventory.GetItems(items);
   foreach (IEntity item : items)
   {
    BaseMagazineComponent magazine;
    if (item) magazine = BaseMagazineComponent.Cast(item.FindComponent(BaseMagazineComponent));
    if (magazine && magazine != loaded && magazine.GetAmmoCount() > 0 && magazine.GetMagazineWell() && magazine.GetMagazineWell().Type().ToString() == well) n++;
   }
  }
  return n;
 }

 protected string TargetKind(SCR_AICombatComponent combat)
 {
  if (!combat || !combat.GetCurrentTarget()) return "none";
  IEntity what = combat.GetCurrentTarget().GetTargetEntity();
  if (!what) return "none";
  ChimeraCharacter character = ChimeraCharacter.Cast(what);
  if (character && !character.IsInVehicle()) return "infantry";
  return "vehicle";
 }

 // Every launcher round that leaves a tube, put down to what its shooter was targeting.
 protected void WatchLaunchers(BBB_AimSide shooters)
 {
  foreach (IEntity man : shooters.m_aMen)
  {
   if (!man) continue;
   BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(man.FindComponent(BaseWeaponManagerComponent));
   if (!weapons) continue;
   string kind = TargetKind(SCR_AICombatComponent.Cast(man.FindComponent(SCR_AICombatComponent)));
   array<IEntity> list = {};
   weapons.GetWeaponsList(list);
   foreach (IEntity item : list)
   {
    BaseWeaponComponent weapon = BaseWeaponComponent.Cast(item.FindComponent(BaseWeaponComponent));
    if (!weapon || weapon.GetWeaponType() != EWeaponType.WT_ROCKETLAUNCHER) continue;
    int loaded = BBB_Launchers.Loaded(item);
    int before;
    // A swap ejects the old round into his kit: that's not a shot.
    IEntity ejected = m_mTubeMagazine.Get(item);
    BaseMagazineComponent ejectedMagazine;
    if (ejected) ejectedMagazine = BaseMagazineComponent.Cast(ejected.FindComponent(BaseMagazineComponent));
    SCR_InventoryStorageManagerComponent kit = SCR_InventoryStorageManagerComponent.Cast(man.FindComponent(SCR_InventoryStorageManagerComponent));
    if (m_mLoaded.Find(item, before) && loaded < before && ejectedMagazine && ejectedMagazine.GetAmmoCount() > 0 && kit && kit.Contains(ejected) && BBB_LauncherUse.InTube(weapon) != ejectedMagazine)
    {
     PrintFormat("[BBB] LAUNCHTEST unloaded: %1 back into the kit", m_mTubeRound.Get(item));
     m_iUnloadedSeen++;
    }
    else if (m_mLoaded.Find(item, before) && loaded < before)
    {
     // One rocket: single-shot tubes, and Loaded() counts a chambered RPG round twice.
     int fired = 1;
     if (kind == "infantry") m_iAtInfantry += fired;
     else if (kind == "vehicle") m_iAtVehicle += fired;
     else m_iAtNothing += fired;
     string left = m_mTubeRound.Get(item);
     m_aFired.Insert(left);
     PrintFormat("[BBB] LAUNCHTEST shot: %1 fired %2 (%4) at %3", BBB_Launchers.WeaponName(item), fired, kind, left);
    }
    m_mLoaded.Set(item, loaded);
    BaseMagazineComponent seated = BBB_LauncherUse.InTube(weapon);
    if (seated) m_mTubeRound.Set(item, BBB_Launchers.WeaponName(seated.GetOwner()));
    if (seated) m_mTubeMagazine.Set(item, seated.GetOwner());
   }
  }
 }

 protected void SamplePicks(BBB_AimSide shooters)
 {
  foreach (IEntity man : shooters.m_aMen)
  {
   if (!man || Down(man)) continue;
   SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(man.FindComponent(SCR_AICombatComponent));
   string kind = TargetKind(combat);
   if (kind == "none") { m_iPickNone++; continue; }
   BaseWeaponComponent weapon = combat.BBBSelectedWeapon();
   bool launcher = weapon && weapon.GetWeaponType() == EWeaponType.WT_ROCKETLAUNCHER;
   if (launcher && combat.BBBSelectedMagazine())
   {
    string round = BBB_Launchers.WeaponName(combat.BBBSelectedMagazine().GetOwner());
    if (!m_sRounds.Contains(round)) m_sRounds += " " + round;
   }
   if (launcher || combat.BBBLaunchState() != "no hold") Trace(man, combat, kind);
   if (launcher && kind == "infantry") m_iPickLauncherInfantry++;
   else if (launcher) m_iPickLauncherVehicle++;
   else if (kind == "infantry") m_iPickOtherInfantry++;
   else m_iPickOtherVehicle++;
  }
 }

 // What a man with the launcher chosen is actually doing.
 protected void Trace(IEntity man, SCR_AICombatComponent combat, string kind)
 {
  string hand = "nothing";
  BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(man.FindComponent(BaseWeaponManagerComponent));
  if (weapons && weapons.GetCurrentWeapon() && weapons.GetCurrentWeapon().GetOwner()) hand = BBB_Launchers.WeaponName(weapons.GetCurrentWeapon().GetOwner());
  string tube = "empty";
  string selected = "none";
  BaseWeaponComponent weapon = combat.BBBSelectedWeapon();
  if (weapon && weapon.GetWeaponType() == EWeaponType.WT_ROCKETLAUNCHER && BBB_LauncherUse.InTube(weapon)) tube = BBB_Launchers.WeaponName(BBB_LauncherUse.InTube(weapon).GetOwner());
  if (combat.BBBSelectedMagazine()) selected = BBB_Launchers.WeaponName(combat.BBBSelectedMagazine().GetOwner());
  string action = "none";
  AIControlComponent control = AIControlComponent.Cast(man.FindComponent(AIControlComponent));
  if (control && control.GetAIAgent())
  {
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(control.GetAIAgent().FindComponent(SCR_AIUtilityComponent));
   if (utility && utility.GetCurrentAction()) action = utility.GetCurrentAction().Type().ToString();
  }
  bool reloading;
  ChimeraCharacter character = ChimeraCharacter.Cast(man);
  if (character && character.GetCharacterController()) reloading = character.GetCharacterController().IsReloading();
  PrintFormat("[BBB] LAUNCHTRACE %1: target %2, in hand %3, selected %4, tube %5, reloading %8, action %6, %7", man.GetID(), kind, hand, selected, tube, action, combat.BBBLaunchState(), reloading);
 }

 override protected void Tick()
 {
  float now = Now();
  if (m_aLanes.IsEmpty())
  {
   if (now < m_fNextRound) return;
   m_iRound++;
   if (!StartRound(m_iRound)) Finish();
   return;
  }
  foreach (BBB_AimLane lane : m_aLanes)
  {
   if (!lane.m_bHooked && now >= lane.m_fHookAt)
   {
    lane.m_bHooked = true;
    Hook(lane.m_A, false);
    Invulnerable(lane.m_A);
    m_iRocketsAtStart = Rockets(lane.m_A, false);
    if (lane.m_B.m_Group)
    {
     Hook(lane.m_B, false);
     Invulnerable(lane.m_B);
    }
    if (m_Vehicle)
    {
     PerceivableComponent perceivable = PerceivableComponent.Cast(m_Vehicle.FindComponent(PerceivableComponent));
     if (perceivable) PrintFormat("[BBB] LAUNCHTEST vehicle unit type %1", typename.EnumToString(EAIUnitType, perceivable.GetUnitType()));
    }
   }
   if (!lane.m_bHooked) continue;
   if (m_fEnemyAt > 0 && now >= m_fEnemyAt)
   {
    m_fEnemyAt = 0;
    int carried = Rockets(lane.m_A, false);
    int inTubes = Rockets(lane.m_A, true);
    PrintFormat("[BBB] LAUNCHTEST before contact: rockets at start %1, now carried %2, in tubes %3, unloaded by BBB %4", m_iRocketsAtStart, carried, inTubes, BBB_LauncherUse.s_iUnloaded);
    if (BBB_LauncherUse.s_bCombatLoad)
    {
     Assert("tubes-empty-out-of-contact", inTubes == 0, string.Format("%1 in tubes", inTubes));
     Assert("no-rocket-lost-unloading", carried == m_iRocketsAtStart && carried > 0, string.Format("%1 of %2", carried, m_iRocketsAtStart));
    }
    lane.m_B.m_Group = SCR_AIGroup.Cast(Spawn(m_sEnemyTeam, lane.m_vTo, lane.m_vDir * -1));
    if (lane.m_B.m_Group) lane.m_aSpawned.Insert(lane.m_B.m_Group);
    m_fHookTargetsAt = now + 4;
   }
   if (m_fHookTargetsAt > 0 && now >= m_fHookTargetsAt)
   {
    m_fHookTargetsAt = 0;
    Hook(lane.m_B, false);
    Invulnerable(lane.m_B);
   }
   lane.m_A.SampleAmmo();
   WatchLaunchers(lane.m_A);
  }
  if (now >= m_fNextSample)
  {
   m_fNextSample = now + 2;
   foreach (BBB_AimLane sampled : m_aLanes) SamplePicks(sampled.m_A);
  }
  if (now >= m_fRoundEnd) EndRound();
 }

 override protected void EndRound()
 {
  foreach (BBB_AimLane lane : m_aLanes)
  {
   string name = m_aName[m_iCase];
   int engaged = m_iPickLauncherInfantry + m_iPickLauncherVehicle + m_iPickOtherInfantry + m_iPickOtherVehicle;
   string detail = string.Format("launcher rounds fired at infantry %1, at a vehicle %2, with no target %3; picks: launcher v infantry %4, launcher v vehicle %5, other weapon v infantry %6, other weapon v vehicle %7, no target %8; rounds selected:%9",
    m_iAtInfantry, m_iAtVehicle, m_iAtNothing, m_iPickLauncherInfantry, m_iPickLauncherVehicle, m_iPickOtherInfantry, m_iPickOtherVehicle, m_iPickNone, m_sRounds);
   PrintFormat("[BBB] LAUNCHTEST RESULT %1: %2; rifle shots %3; shooters %4", name, detail, lane.m_A.m_iShots, lane.m_A.m_aMen.Count());
   Assert("shooters-" + m_iCase, lane.m_A.m_aMen.Count() == 2, name);
   // Without a target in hand, "no launcher shots" would say nothing about the choice.
   Assert("engaged-" + m_iCase, engaged > 0, name);
   if (m_Vehicle) Assert("control-fires-" + m_iCase, m_iAtVehicle > 0, name);
   BBB_LauncherUse.Read();
   string fired = "";
   foreach (string f : m_aFired) fired += " " + f;
   PrintFormat("[BBB] LAUNCHTEST rounds that left the tubes:%1", fired);
   if (m_aVehicle[m_iCase] == BTR && BBB_LauncherUse.s_bSwap) Assert("armour-gets-antiarmour-round-" + m_iCase, !m_aFired.IsEmpty() && m_aFired[0].Contains("HEAA"), fired);
   int expect = m_aExpect[m_iCase];
   if (expect == 2 && !BBB_LauncherUse.s_bOn) expect = 0;
   if (expect == 0) Assert("no-rockets-at-infantry-" + m_iCase, m_iAtInfantry == 0, name);
   if (expect >= 1) Assert("rockets-at-infantry-" + m_iCase, m_iAtInfantry > 0, name);
   Clear(lane);
  }
  m_aLanes.Clear();
  m_fNextRound = Now() + 4;
 }

 override protected void Finish()
 {
  GetGame().GetCallqueue().Remove(Tick);
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers) observers.RemoveObserverSP(OBSERVER_KEY);
  BBB_LauncherUse.Read();
  PrintFormat("[BBB] LAUNCHTEST BBB on infantry=%1 swap=%2 combatLoad=%3: picks %4, rockets fired %5 (anti-personnel %6), swaps %7, armour swaps %8, unloaded %9", BBB_LauncherUse.s_bOn, BBB_LauncherUse.s_bSwap, BBB_LauncherUse.s_bCombatLoad, BBB_LauncherUse.s_iPicked, BBB_LauncherUse.s_iFired, BBB_LauncherUse.s_iFiredAP, BBB_LauncherUse.s_iSwapped, BBB_LauncherUse.s_iArmourSwaps, BBB_LauncherUse.s_iUnloaded);
  if (s_bRhs) Assert("rhs-cases-ran", m_iRhsRun == 5, string.Format("%1 of 5", m_iRhsRun));
  string verdict = "PASS";
  if (m_iFailures > 0 || m_iChecks < 5) verdict = "FAIL";
  PrintFormat("[ACR] VERDICT %1 test=bbb-launcherinfantry checks=%2 failed=%3", verdict, m_iChecks, m_iFailures);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_LauncherInfantryTest.MaybeStartLaunch();
 }
}
