// Armour learning per round (BBB #5): -bbbArmourRoundsTest 1 (Test-BBB.ps1 -Preset armourrounds).
// Rounds are spawned and launched by script at an empty BTR-70 from 30 m, side on, a fresh
// BTR and fresh learning for each set, no AI involved. Per set: rounds BBB counted, how many
// got through, its verdict, and the health the BTR actually lost in its hit-zone hierarchy.
// - 12 x 25 mm APDS-T, 300 ms apart (the M242's rate): wrecks wheels; must not be "useless".
// - 3 x M72A3: each rocket must count as one round (its ~70 spall hits are not rounds), and
//   not "useless" against a BTR it damages.
// - 12 x 5.45 mm ball at the upper hull (control): does nothing; must be learned "useless".
// What it does not establish: AI soldiers' picks in a real fight, or two shooters at once.
class BBB_ArmourRoundsTest : BBB_AimTest
{
 static ref BBB_ArmourRoundsTest s_Rounds;
 static const ResourceName BTR = "{C012BB3488BEA0C2}Prefabs/Vehicles/Wheeled/BTR70/BTR70.et";
 static const ResourceName APDS = "{A8835B5EA92F1CE1}Prefabs/Weapons/Ammo/Ammo_25x137/Ammo_25x137_APDST_M791.et";
 static const ResourceName M72 = "{79FA751EEBE25DDE}Prefabs/Weapons/Ammo/Ammo_Rocket_M72A3.et";
 static const ResourceName BALL = "{1D9DDE1632F33A9E}Prefabs/Weapons/Ammo/Ammo_545x39_Ball_7N6.et";
 static const float RANGE_M = 30;

 protected IEntity m_Btr;
 protected vector m_vAt;
 protected vector m_vDir;
 protected int m_iSet = -1;
 protected int m_iFired;
 protected float m_fHealthStart;
 protected int m_iChecks;
 protected int m_iFailures;

 static void Start()
 {
  if (s_Rounds || !Replication.IsServer() || !System.IsCLIParam("bbbArmourRoundsTest")) return;
  s_Rounds = new BBB_ArmourRoundsTest();
  GetGame().GetCallqueue().CallLater(s_Rounds.Setup, 15000);
 }

 protected void Assert(string label, bool passed, string detail)
 {
  m_iChecks++;
  if (!passed) m_iFailures++;
  PrintFormat("[BBB] ARMOURROUNDS CHECK %1 ok=%2 %3", label, passed, detail);
 }

 protected void Setup()
 {
  BBB_Armour.Read();
  if (!FindSite())
  {
   Assert("site", false, "no clear lane");
   FinishRounds();
   return;
  }
  m_vDir = m_aSiteDir[0];
  m_vAt = BBB.Ground(m_aSiteFrom[0] + m_vDir * 60);
  NextSet();
 }

 protected ResourceName SetAmmo()
 {
  if (m_iSet == 0) return APDS;
  if (m_iSet == 1) return M72;
  return BALL;
 }

 protected int SetRounds()
 {
  if (m_iSet == 1) return 3;
  return 12;
 }

 protected int SetGapMs()
 {
  if (m_iSet == 1) return 2000;
  if (m_iSet == 0) return 300;
  return 400;
 }

 protected string SetName()
 {
  if (m_iSet == 0) return "apds";
  if (m_iSet == 1) return "m72";
  return "ball";
 }

 protected void NextSet()
 {
  if (m_Btr) SCR_EntityHelper.DeleteEntityAndChildren(m_Btr);
  m_iSet++;
  if (m_iSet > 2) { FinishRounds(); return; }
  BBB_Armour.s_Stats.Clear();
  m_Btr = Spawn(BTR, m_vAt, Vector(m_vDir[2], 0, -m_vDir[0])); // side on to the line of fire
  if (!m_Btr) { Assert("spawn", false, "no BTR-70"); FinishRounds(); return; }
  m_iFired = 0;
  GetGame().GetCallqueue().CallLater(BeginSet, 3000); // let it settle on its wheels
 }

 protected void BeginSet()
 {
  m_fHealthStart = BBB_Armour.Health(m_Btr);
  Fire();
 }

 protected void Fire()
 {
  if (!m_Btr) return;
  // The hull's centre, 1.0 m up (wheels and hull), or 1.6 m up for the rifle control (upper hull).
  float up = 1.0;
  if (m_iSet == 2) up = 1.6;
  vector target = m_Btr.GetOrigin() + Vector(0, up, 0);
  vector side = Vector(m_vDir[2], 0, -m_vDir[0]);
  int column = m_iFired % 3;
  target = target + side * (0.8 * (column - 1)); // a small grid along the hull
  vector from = target - m_vDir * RANGE_M;
  Resource resource = Resource.Load(SetAmmo());
  if (resource.IsValid())
  {
   EntitySpawnParams parameters = new EntitySpawnParams();
   parameters.TransformMode = ETransformMode.WORLD;
   Math3D.DirectionAndUpMatrix(m_vDir, vector.Up, parameters.Transform);
   parameters.Transform[3] = from;
   IEntity projectile = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), parameters);
   ProjectileMoveComponent move;
   if (projectile) move = ProjectileMoveComponent.Cast(projectile.FindComponent(ProjectileMoveComponent));
   if (move) move.Launch(m_vDir, vector.Zero, 1, projectile, null, null, null, null);
  }
  m_iFired++;
  if (m_iFired < SetRounds()) GetGame().GetCallqueue().CallLater(Fire, SetGapMs());
  else GetGame().GetCallqueue().CallLater(EndSet, 3000); // every round's window has closed
 }

 protected void EndSet()
 {
  string kind = BBB_Armour.PrefabKey(m_Btr);
  string ammo = BBB_Armour.Round(BBB_Armour.Key(SetAmmo()));
  BBB_ArmourStat stat = BBB_Armour.s_Stats.Get(kind + "|" + ammo);
  float lost = m_fHealthStart - BBB_Armour.Health(m_Btr);
  int rounds, through;
  bool useless;
  if (stat) { rounds = stat.m_iHits; through = stat.m_iThrough; useless = stat.m_bUseless; }
  string detail = string.Format("fired %1, counted %2 rounds, %3 through, useless %4, BTR lost %5 health", m_iFired, rounds, through, useless, Math.Round(lost));
  PrintFormat("[BBB] ARMOURROUNDS set %1: %2", SetName(), detail);
  BBB_Armour.Dump();
  if (m_iSet == 0)
  {
   Assert("apds-wrecks", lost >= 100, detail);
   Assert("apds-counted-per-round", rounds >= 9 && rounds <= 12, detail);
   Assert("apds-not-useless", !useless && through >= 2, detail);
  }
  else if (m_iSet == 1)
  {
   Assert("m72-damages", lost >= 100, detail);
   Assert("m72-one-round-per-rocket", rounds == 3, detail);
   Assert("m72-not-useless", !useless && through >= 1, detail);
  }
  else
  {
   Assert("ball-counted-per-round", rounds >= 9 && rounds <= 12, detail);
   Assert("ball-useless", rounds >= BBB_Armour.s_iMinHits && useless && through == 0, detail);
  }
  NextSet();
 }

 protected void FinishRounds()
 {
  if (m_Btr) SCR_EntityHelper.DeleteEntityAndChildren(m_Btr);
  string verdict = "PASS";
  if (m_iFailures > 0 || m_iChecks < 8) verdict = "FAIL";
  PrintFormat("[ACR] VERDICT %1 test=bbb-armourrounds checks=%2 failed=%3", verdict, m_iChecks, m_iFailures);
  Print("[ACR] TEST COMPLETE");
 }
}

modded class SCR_BaseGameMode
{
 protected override void OnGameStart()
 {
  super.OnGameStart();
  BBB_ArmourRoundsTest.Start();
 }
}
