class BBB_RearmWeaponTestHooks : BBB_Hooks
{
 ref array<SCR_AIGroup> m_Groups = {};
 override void GetGroups(notnull array<SCR_AIGroup> groups) { groups.Copy(m_Groups); }
}

// Weapon recovery (-bbbRearmWeaponTest): one Soviet fire team, nobody else about.
// A: a rifleman drops his rifle (the engine's own drop) and it is put 6 m away; he must walk to
//    it and pick up that same rifle (his own) on his own.
// B: a second man loses his rifle for good; a rifleman killed 6 m off still has his. He must take
//    that rifle from the body, with magazines for it.
// C: a third man loses his rifle with no gun lying about: he stays unarmed, holds the rearm
//    claim (so a fire-and-manoeuvre bound can't take him) and doesn't wander off.
// Morale, fire and manoeuvre, buildings and throws are off so only Rearm moves anyone.
class BBB_RearmWeaponTest
{
 static ref BBB_RearmWeaponTest s_Test;
 static const int EXPECTED = 13;
 static const int TRIP_LIMIT_S = 35;
 static const string TEAM = "{657590C1EC9E27D3}Prefabs/Groups/OPFOR/Group_USSR_LightFireTeam.et";
 static const string RIFLEMAN = "{DCB41B3746FDD1BE}Prefabs/Characters/Factions/OPFOR/USSR_Army/Character_USSR_Rifleman.et";
 ref BBB_RearmWeaponTestHooks m_Hooks;
 ref BBB_Hooks m_SavedHooks;
 SCR_AIGroup m_Team;
 ChimeraCharacter m_ManA;
 ChimeraCharacter m_ManB;
 ChimeraCharacter m_ManC;
 IEntity m_Dropped;
 IEntity m_Body;
 IEntity m_BodyGun;
 vector m_vBase;
 vector m_vStart;
 float m_fMoved;
 int m_Checks;
 int m_Failures;
 int m_Wait;

 static void Start()
 {
  if (!Replication.IsServer() || !System.IsCLIParam("bbbRearmWeaponTest")) return;
  s_Test = new BBB_RearmWeaponTest();
  GetGame().GetCallqueue().CallLater(s_Test.Setup, 15000);
 }

 void Check(string name, bool ok, string detail = "")
 {
  m_Checks++;
  if (!ok) m_Failures++;
  PrintFormat("[BBB] WEAPONTEST %1 ok=%2 %3", name, ok, detail);
 }

 IEntity Spawn(ResourceName prefab, vector at)
 {
  Resource resource = Resource.Load(prefab);
  if (!resource.IsValid()) return null;
  EntitySpawnParams parameters = new EntitySpawnParams();
  parameters.TransformMode = ETransformMode.WORLD;
  parameters.Transform[3] = BBB.Ground(at);
  return GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), parameters);
 }

 // The engine's drop (as surrender uses): out of his hands onto the ground.
 IEntity Drop(ChimeraCharacter man)
 {
  IEntity gun = BBB_Rearm.PrimaryOf(man);
  if (!gun) return null;
  BaseWeaponManagerComponent wm = BaseWeaponManagerComponent.Cast(man.FindComponent(BaseWeaponManagerComponent));
  array<WeaponSlotComponent> slots = {};
  wm.GetWeaponsSlots(slots);
  foreach (WeaponSlotComponent slot : slots)
  {
   if (slot && slot.GetWeaponEntity() == gun) man.GetCharacterController().DropWeapon(slot);
  }
  return gun;
 }

 // Gone for good: out of his slots and deleted. Recovery is paused until it is gone, so he
 // can't pick his own gun back up meanwhile (StartB / StrayC switch it back on).
 void Lose(ChimeraCharacter man)
 {
  BBB_Rearm.s_bWeaponOn = false;
  IEntity gun = BBB_Rearm.PrimaryOf(man);
  if (!gun) return;
  SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(man.FindComponent(SCR_InventoryStorageManagerComponent));
  if (inv) inv.TryRemoveItemFromInventory(gun);
  if (BBB_Rearm.PrimaryOf(man) == gun)
  {
   Drop(man);
   return; // deleted once the drop lands (LoseLanded)
  }
  SCR_EntityHelper.DeleteEntityAndChildren(gun);
 }

 bool Loose(IEntity gun)
 {
  if (!gun) return false;
  InventoryItemComponent item = InventoryItemComponent.Cast(gun.FindComponent(InventoryItemComponent));
  return item && !item.GetParentSlot();
 }

 void Setup()
 {
  BBB_Rearm.Read();
  Check("default-on", BBB_Rearm.s_bOn && BBB_Rearm.s_bWeaponOn);
  BBB_FireManeuver.s_bOn = false;
  BBB_Morale.s_bOn = false;
  BBB_Grenades.s_bOn = false;
  BBB_Smoke.s_bOn = false;
  BBB_Flares.s_bOn = false;
  BBB_StaticWeapons.s_bOn = false;
  m_SavedHooks = BBB.s_Hooks;
  m_Hooks = new BBB_RearmWeaponTestHooks();
  BBB.s_Hooks = m_Hooks;
  m_vBase = BBB.Ground("1340 0 2770");
  ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
  if (observers) observers.InsertObserverSP(22150, m_vBase[0], m_vBase[2], null);
  m_Team = SCR_AIGroup.Cast(Spawn(TEAM, m_vBase));
  if (m_Team) m_Hooks.m_Groups.Insert(m_Team);
  GetGame().GetCallqueue().CallLater(PickMen, 4000);
 }

 void PickMen()
 {
  array<AIAgent> agents = {};
  if (m_Team) m_Team.GetAgents(agents);
  array<ChimeraCharacter> men = {};
  foreach (AIAgent agent : agents)
  {
   ChimeraCharacter man = ChimeraCharacter.Cast(agent.GetControlledEntity());
   // Not the leader: the group's own moves start from him.
   if (man && agents.Find(agent) > 0 && BBB_Rearm.PrimaryOf(man)) men.Insert(man);
  }
  Check("team", men.Count() >= 3, "armed men=" + men.Count());
  if (men.Count() < 3) { Finish(); return; }
  m_ManA = men[0];
  m_ManB = men[1];
  m_ManC = men[2];
  // Rearm has seen all three armed (it remembers each man's gun on its rounds).
  bool seen = !BBB_Rearm.LostPrimary(m_ManA) && !BBB_Rearm.LostPrimary(m_ManB) && !BBB_Rearm.LostPrimary(m_ManC);
  Check("seen-armed", seen && BBB_Rearm.LastPrimary(m_ManA) == BBB_Rearm.PrimaryOf(m_ManA));
  m_Dropped = Drop(m_ManA);
  m_Wait = 0;
  GetGame().GetCallqueue().CallLater(PlaceDropped, 1000, true);
 }

 // A: once his rifle is on the ground, put it 6 m off so he has to walk for it.
 void PlaceDropped()
 {
  m_Wait++;
  if (!Loose(m_Dropped) && m_Wait < 6) return;
  GetGame().GetCallqueue().Remove(PlaceDropped);
  bool dropped = Loose(m_Dropped) && !BBB_Rearm.PrimaryOf(m_ManA);
  Check("a-dropped", dropped, "loose=" + Loose(m_Dropped));
  if (!dropped) { CaseB(); return; }
  vector away = BBB.Ground(m_ManA.GetOrigin() + Vector(6, 0, 0));
  m_Dropped.SetOrigin(away + Vector(0, 0.15, 0));
  m_Dropped.Update();
  Physics physics = m_Dropped.GetPhysics();
  if (physics) physics.SetVelocity(vector.Zero);
  m_vStart = m_ManA.GetOrigin();
  m_fMoved = 0;
  m_Wait = 0;
  GetGame().GetCallqueue().CallLater(WatchA, 1000, true);
 }

 void WatchA()
 {
  m_Wait++;
  if (m_ManA) m_fMoved = Math.Max(m_fMoved, vector.DistanceXZ(m_vStart, m_ManA.GetOrigin()));
  bool back = m_ManA && BBB_Rearm.PrimaryOf(m_ManA) == m_Dropped;
  if (!back && m_Wait < TRIP_LIMIT_S) return;
  GetGame().GetCallqueue().Remove(WatchA);
  Check("a-own-rifle-back", back, "after=" + m_Wait + "s");
  Check("a-walked", m_fMoved >= 3.0, "moved=" + m_fMoved);
  Check("a-counted-own", BBB_Rearm.s_iOwnWeaponsRecovered >= 1, "own=" + BBB_Rearm.s_iOwnWeaponsRecovered);
  CaseB();
 }

 // B: no gun of his own any more; a dead rifleman 6 m off still has his.
 void CaseB()
 {
  m_Body = Spawn(RIFLEMAN, m_ManB.GetOrigin() + Vector(-6, 0, 0));
  SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.GetDamageManager(m_Body);
  if (dmg) dmg.Kill(Instigator.CreateInstigator(null));
  Lose(m_ManB);
  m_Wait = 0;
  GetGame().GetCallqueue().CallLater(StartB, 2000, false);
 }

 void StartB()
 {
  // A drop that refused inventory removal lands on the ground: delete it there.
  IEntity stray = BBB_Rearm.LastPrimary(m_ManB);
  if (stray && Loose(stray)) SCR_EntityHelper.DeleteEntityAndChildren(stray);
  BBB_Rearm.s_bWeaponOn = true;
  m_BodyGun = null;
  InventoryStorageManagerComponent bodyInv = null;
  if (m_Body) bodyInv = InventoryStorageManagerComponent.Cast(m_Body.FindComponent(InventoryStorageManagerComponent));
  array<IEntity> items = {};
  if (bodyInv) bodyInv.GetItems(items);
  foreach (IEntity item : items)
  {
   if (BBB_Rearm.PrimaryComponent(item)) m_BodyGun = item;
  }
  // The engine might also drop a dead man's gun beside him: then that loose gun is the one.
  string where = "on-body";
  if (!m_BodyGun)
  {
   IEntity source;
   m_BodyGun = BBB_Rearm.FindWeapon(m_ManB, 15, source);
   where = "loose";
  }
  Check("b-unarmed", !BBB_Rearm.PrimaryOf(m_ManB) && m_BodyGun != null, "bodyGun=" + where);
  m_vStart = m_ManB.GetOrigin();
  m_fMoved = 0;
  m_Wait = 0;
  GetGame().GetCallqueue().CallLater(WatchB, 1000, true);
 }

 void WatchB()
 {
  m_Wait++;
  if (m_ManB) m_fMoved = Math.Max(m_fMoved, vector.DistanceXZ(m_vStart, m_ManB.GetOrigin()));
  bool armed = m_ManB && m_BodyGun && BBB_Rearm.PrimaryOf(m_ManB) == m_BodyGun;
  if (!armed && m_Wait < TRIP_LIMIT_S) return;
  GetGame().GetCallqueue().Remove(WatchB);
  Check("b-took-body-rifle", armed, "after=" + m_Wait + "s moved=" + m_fMoved);
  int mags = 0;
  BaseMuzzleComponent muzzle = BBB_Rearm.WellMuzzle(BBB_Rearm.PrimaryComponent(m_BodyGun));
  SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(m_ManB.FindComponent(SCR_InventoryStorageManagerComponent));
  if (inv && muzzle) mags = inv.GetMagazineCountByMuzzle(m_ManB, muzzle);
  Check("b-has-mags", armed && mags >= 1, "mags=" + mags);
  CaseC();
 }

 // C: nothing to pick up. He stays unarmed, held out of fire jobs, and stays put.
 void CaseC()
 {
  if (m_Body) SCR_EntityHelper.DeleteEntityAndChildren(m_Body);
  Lose(m_ManC);
  m_vStart = m_ManC.GetOrigin();
  GetGame().GetCallqueue().CallLater(StrayC, 2000, false);
 }

 void StrayC()
 {
  IEntity stray = BBB_Rearm.LastPrimary(m_ManC);
  if (stray && Loose(stray)) SCR_EntityHelper.DeleteEntityAndChildren(stray);
  BBB_Rearm.s_bWeaponOn = true;
  GetGame().GetCallqueue().CallLater(CheckC, 6000, false);
 }

 void CheckC()
 {
  bool unarmed = !BBB_Rearm.PrimaryOf(m_ManC);
  Check("c-unarmed", unarmed);
  Check("c-held-out", BBB_Claims.Role(m_ManC) == BBB_Claims.REARM && !BBB_Claims.Can(m_ManC, BBB_Claims.FIRE), "role=" + BBB_Claims.Name(BBB_Claims.Role(m_ManC)));
  float moved = vector.DistanceXZ(m_vStart, m_ManC.GetOrigin());
  Check("c-no-trip", !BBB_Rearm.InRearmAction(m_ManC) && moved < 4.0, "moved=" + moved);
  Finish();
 }

 void Finish()
 {
  if (m_SavedHooks) BBB.s_Hooks = m_SavedHooks;
  if (m_Team) SCR_EntityHelper.DeleteEntityAndChildren(m_Team);
  string verdict = "FAIL";
  if (m_Failures == 0 && m_Checks == EXPECTED) verdict = "PASS";
  PrintFormat("[BBB] WEAPONTEST recovered=%1 own=%2", BBB_Rearm.s_iWeaponsRecovered, BBB_Rearm.s_iOwnWeaponsRecovered);
  PrintFormat("[ACR] VERDICT %1 test=bbbrearmweapon passed=%2 failed=%3 expected=%4", verdict, m_Checks - m_Failures, m_Failures, EXPECTED);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 override protected void OnGameStart()
 {
  super.OnGameStart();
  BBB_RearmWeaponTest.Start();
 }
}
