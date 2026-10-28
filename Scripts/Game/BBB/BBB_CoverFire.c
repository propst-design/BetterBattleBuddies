// Cover fire (squad brain stage 4, Jacob 24 Oct): where to put covering rounds. An enemy who was
// seen is most likely behind the solid thing nearest where he was seen, so aim there (LAMBS aims
// at building positions and bushes within 8-12 m of the believed position):
//   only from a real report (callers pass one at most 8 s old), 45-300 m from the shooter;
//   the nearest tree, rock, wall or house within 10 m of the report that the shooter can see
//   (aim at its near side), else the report itself as before;
//   refused when a friendly stands within 15 m of the aim point.
// Readers: FireManeuver's commanded cover fire, and BBB_Tactic's HOLD base of fire
// (BBB_AISuppressCoverBehavior, the same contract: priority 90.5, ends on a visible enemy).
//  -bbbCoverFire 0   off: the box on the report, as before (the friendly check stays)
class BBB_CoverFire
{
 static const float NEAR_M = 10;
 static const float FRIENDLY_M = 15;
 static const float MIN_M = 45;
 static const float MAX_M = 300;
 static const float PRIORITY = 90.5;  // just above attacking a selected target (90), as FireManeuver's orders
 static bool s_bOn = true;
 protected static int s_iRead;
 static int s_iOnCover, s_iOnReport, s_iRefused; // tests read these
 protected static ref array<IEntity> s_aFound = {};
 protected static float s_fGround;
 protected static Faction s_Side;
 protected static bool s_bFriendly;
 protected static IEntity s_Shooter;

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbCoverFire", value) && value == "0");
  PrintFormat("[BBB] COVER FIRE enabled=%1 near=%2m friendly=%3m", s_bOn, NEAR_M, FRIENDLY_M);
 }

 protected static bool Collect(IEntity entity)
 {
  if (s_aFound.Count() >= 16) return false;
  if (ChimeraCharacter.Cast(entity) || Vehicle.Cast(entity) || !entity.GetPhysics()) return true;
  vector mins, maxs;
  entity.GetWorldBounds(mins, maxs);
  // Something a man can hide behind: at least 1 m above the ground, not a sprawling terrain piece.
  if (maxs[1] - s_fGround < 1.0 || maxs[0] - mins[0] > 60 || maxs[2] - mins[2] > 60) return true;
  s_aFound.Insert(entity);
  return true;
 }

 protected static bool CollectFriend(IEntity entity)
 {
  ChimeraCharacter man = ChimeraCharacter.Cast(entity);
  if (!man || man == s_Shooter || !BBB.IsAlive(man)) return true;
  Faction faction = SCR_AIFactionHandling.GetEntityPerceivedFaction(man);
  if (faction && s_Side && !s_Side.IsFactionEnemy(faction)) { s_bFriendly = true; return false; }
  return true;
 }

 protected static bool NotCharacter(notnull IEntity entity) { return ChimeraCharacter.Cast(entity) == null; }

 // Can the shooter see this point (or the object it sits on)?
 protected static bool Sees(vector eye, vector point, IEntity object)
 {
  TraceParam trace = new TraceParam();
  trace.Start = eye;
  trace.End = point;
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  float fraction = GetGame().GetWorld().TraceMove(trace, NotCharacter);
  if (fraction >= 0.99) return true;
  IEntity hit = trace.TraceEnt;
  if (hit && (hit == object || hit.GetRootParent() == object)) return true;
  float length = vector.Distance(eye, point);
  return length > 0 && (1 - fraction) * length < 1.5;
 }

 // The suppression box for covering fire on this report, or null (out of range, friendlies near).
 static SCR_AISuppressionVolumeBox Volume(SCR_AIUtilityComponent shooter, vector report, out bool onCover)
 {
  Read();
  onCover = false;
  if (!shooter || !shooter.m_OwnerEntity) return null;
  vector from = shooter.m_OwnerEntity.GetOrigin();
  float distance = vector.DistanceXZ(from, report);
  if (distance < MIN_M || distance > MAX_M) return null;
  vector ground = BBB.Ground(report);
  vector aim = ground;
  if (s_bOn)
  {
   s_aFound.Clear();
   s_fGround = ground[1];
   GetGame().GetWorld().QueryEntitiesBySphere(ground, NEAR_M, Collect, null, EQueryEntitiesFlags.ALL);
   vector eye = from + Vector(0, 1.5, 0);
   float best = NEAR_M + 1;
   foreach (IEntity object : s_aFound)
   {
    vector mins, maxs;
    object.GetWorldBounds(mins, maxs);
    // Its side nearest the report, at chest height.
    vector side = Vector(Math.Clamp(ground[0], mins[0], maxs[0]), 0, Math.Clamp(ground[2], mins[2], maxs[2]));
    side[1] = GetGame().GetWorld().GetSurfaceY(side[0], side[2]) + 1.0;
    float off = vector.DistanceXZ(side, ground);
    if (off >= best || !Sees(eye, side, object)) continue;
    best = off;
    aim = side;
    onCover = true;
   }
   s_aFound.Clear();
  }
  s_Shooter = shooter.m_OwnerEntity;
  s_Side = SCR_AIFactionHandling.GetEntityPerceivedFaction(shooter.m_OwnerEntity);
  s_bFriendly = false;
  GetGame().GetWorld().QueryEntitiesBySphere(aim, FRIENDLY_M, CollectFriend, null, EQueryEntitiesFlags.DYNAMIC);
  s_Shooter = null;
  if (s_bFriendly) { s_iRefused++; return null; }
  float half = Math.Clamp(distance * 0.04, 4, 10);
  if (onCover) { half = 2.5; s_iOnCover++; }
  else s_iOnReport++;
  return new SCR_AISuppressionVolumeBox(Vector(aim[0] - half, aim[1] - 1.5, aim[2] - half), Vector(aim[0] + half, aim[1] + 1.5, aim[2] + half));
 }
}

// Covering fire ordered by the squad tactic (HOLD base of fire). Ends when its window is over,
// the man sees an enemy of his own, he is no longer in the firing half, or orders intervene.
class BBB_AISuppressCoverBehavior : SCR_AISuppressBehavior
{
 protected float m_fEndMs;

 void BBB_AISuppressCoverBehavior(SCR_AIUtilityComponent utility, SCR_AIActivityBase groupActivity, SCR_AISuppressionVolumeBase suppressionVolume, float suppressionDuration, float fireRate, float priorityLevel = PRIORITY_LEVEL_NORMAL)
 {
  m_fEndMs = GetGame().GetWorld().GetWorldTime() + suppressionDuration * 1000;
  SetIsUniqueInActionQueue(true);
 }

 override float CustomEvaluate()
 {
  if (GetGame().GetWorld().GetWorldTime() > m_fEndMs || !m_Utility || !m_Utility.m_CombatComponent) { Complete(); return 0; }
  if (!m_Utility.m_bBBBFiringHalf) { Complete(); return 0; }
  if (!BBB.IsConscious(m_Utility.m_OwnerEntity) || BBB.PlayerOf(m_Utility.m_OwnerEntity) || BBB_OrderIntent.Blocks(m_Utility)) { Complete(); return 0; }
  if (m_Utility.m_CombatComponent.GetCombatMode() == EAIGroupCombatMode.HOLD_FIRE) { Complete(); return 0; }
  BaseTarget target = m_Utility.m_CombatComponent.GetCurrentTarget();
  if (target && m_Utility.m_CombatComponent.IsTargetVisible(target)) { Complete(); return 0; }
  return BBB_CoverFire.PRIORITY;
 }
}

modded class SCR_AIUtilityComponent
{
 bool m_bBBBFiringHalf; // in the half of a holding or flanking squad that lays down fire now (BBB_Tactic)
}
