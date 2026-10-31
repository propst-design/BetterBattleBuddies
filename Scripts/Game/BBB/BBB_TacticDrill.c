// How a squad carries out its tactic (squad brain stage 4, Jacob 24 Oct). BBB_Tactic decides;
// this moves and fires. The squad works in two halves that swap (LAMBS: no fixed MG role):
//   base of fire  (HOLD, REORG, FLANK) the firing half puts bursts on the cover nearest the
//                 freshest real report (<= 8 s, BBB_CoverFire); halves swap every 15 s
//   flank         the other half runs (BBB_AIWithdrawMove, 91, attack held off as in a
//                 withdrawal) to a flank point 60-80 deg off the line to the enemy at 0.6 of
//                 the range; whoever arrives covers, the rest move; done once all are near it
//   search        after contact is lost: the last known position, then up to four building
//                 sides and solid cover within 30 m of it, nearest first; half walks to each
//                 (vanilla move-and-investigate) while the other half watches, then they swap
// Positions come only from the squad's own sightings and the world's geometry.
class BBB_TacticDrill
{
 static const float SWAP_S = 15;
 static const float REPORT_S = 8;
 static const float BURST_S = 6;
 static const float FLANK_MIN_DEG = 60;
 static const float FLANK_MAX_DEG = 80;
 static const float FLANK_RANGE = 0.6;
 static const float ARRIVE_M = 15;
 static const float SEARCH_RADIUS_M = 30;
 static const float POINT_M = 5;
 static const float POINT_S = 20;     // per point, plus walking time at 1.5 m/s
 static const int SEARCH_EXTRA = 4;
 static int s_iBursts, s_iRuns, s_iLooks, s_iPointsDone; // tests read these
 static ref array<SCR_AIGroup> s_aNoFire = {}; // test only: squads that hold without a base of fire
 protected static ref array<IEntity> s_aFound = {};
 protected static float s_fGround;

 // Conscious AI members in a stable order; half = index % 2.
 static void Members(BBB_TacticState state, notnull array<SCR_AIUtilityComponent> members)
 {
  members.Clear();
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(state.m_Group, agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   ChimeraCharacter man = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!man || !BBB.IsConscious(man) || BBB.PlayerOf(man) || man.IsInVehicle()) continue;
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   if (utility) members.Insert(utility);
  }
 }

 // Every 15 s the halves swap.
 static void Swap(BBB_TacticState state, float now)
 {
  if (now < state.m_fSwapAt) return;
  state.m_iFireHalf = 1 - state.m_iFireHalf;
  state.m_fSwapAt = now + SWAP_S;
  CancelRuns(state);
 }

 // The firing half covers the freshest real report; the other half doesn't fire on order.
 static void BaseOfFire(BBB_TacticState state, float now, notnull array<SCR_AIUtilityComponent> members)
 {
  vector report;
  bool hasReport = BBB_Squad.FreshReport(state.m_Group, REPORT_S, report);
  if (s_aNoFire.Contains(state.m_Group)) hasReport = false;
  foreach (int i, SCR_AIUtilityComponent utility : members)
  {
   bool firing = i % 2 == state.m_iFireHalf;
   utility.m_bBBBFiringHalf = firing;
   if (!firing || !hasReport || now < utility.m_fBBBCoverOrderUntil || Running(utility.m_BBBWithdrawRun)) continue;
   if (!utility.m_CombatComponent || utility.m_CombatComponent.GetCombatMode() == EAIGroupCombatMode.HOLD_FIRE || BBB_OrderIntent.Held(utility)) continue;
   BaseTarget target = utility.m_CombatComponent.GetCurrentTarget();
   if (target && utility.m_CombatComponent.IsTargetVisible(target)) continue; // already shooting at the real thing
   bool onCover;
   SCR_AISuppressionVolumeBox volume = BBB_CoverFire.Volume(utility, report, onCover);
   if (!volume) continue;
   utility.AddAction(new BBB_AISuppressCoverBehavior(utility, null, volume, BURST_S, 1.1));
   utility.m_fBBBCoverOrderUntil = now + BURST_S;
   s_iBursts++;
   BBB_Think.NoteUtility(utility, "Tactic", "base of fire on the enemy's cover");
  }
 }

 protected static bool Running(BBB_AIWithdrawMove move) { return BBB_Morale.Running(move); }

 static void CancelRuns(BBB_TacticState state)
 {
  foreach (BBB_AIWithdrawMove run : state.m_aRuns)
   if (Running(run)) run.Fail();
  state.m_aRuns.Clear();
  foreach (SCR_AIMoveAndInvestigateBehavior look : state.m_aLooks)
   if (look && look.GetActionState() != EAIActionState.COMPLETED && look.GetActionState() != EAIActionState.FAILED) look.Fail();
  state.m_aLooks.Clear();
  state.m_aLookers.Clear();
  array<SCR_AIUtilityComponent> members = {};
  Members(state, members);
  foreach (SCR_AIUtilityComponent utility : members)
  {
   if (utility.m_BBBWithdrawRun && !Running(utility.m_BBBWithdrawRun)) utility.m_BBBWithdrawRun = null;
   if (utility.m_bBBBTacticRunner) { BBB_Claims.Release(utility.m_OwnerEntity, BBB_Claims.FIRE); utility.m_bBBBTacticRunner = false; }
  }
 }

 // Everything the drill set on the squad, undone (tactic change, drop, Stop).
 static void Clear(BBB_TacticState state)
 {
  CancelRuns(state);
  array<AIAgent> agents = {};
  if (state.m_Group) BBB.s_Hooks.GetMembers(state.m_Group, agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   if (!utility) continue;
   utility.m_bBBBFiringHalf = false;
   if (utility.m_bBBBTacticRunner) { BBB_Claims.Release(utility.m_OwnerEntity, BBB_Claims.FIRE); utility.m_bBBBTacticRunner = false; }
  }
 }

 protected static bool CollectCover(IEntity entity)
 {
  if (s_aFound.Count() >= 40) return false;
  if (ChimeraCharacter.Cast(entity) || Vehicle.Cast(entity) || !entity.GetPhysics()) return true;
  vector mins, maxs;
  entity.GetWorldBounds(mins, maxs);
  if (maxs[1] - s_fGround < 1.2 || maxs[0] - mins[0] > 60 || maxs[2] - mins[2] > 60) return true;
  s_aFound.Insert(entity);
  return true;
 }

 // A covered, dry spot inside the order leash, 60-80 deg off the line to the enemy at 0.6 of the
 // range, with no enemy seen near it; the side with more trees and walls around it wins.
 static bool FlankPoint(BBB_TacticState state, BBB_SquadPicture picture, vector threat, out vector point)
 {
  point = vector.Zero;
  vector back = picture.m_vCentre - threat;
  back[1] = 0;
  float range = back.Length();
  if (range < 1) return false;
  back.Normalize();
  int bestScore = 0;
  array<float> angles = {70, FLANK_MIN_DEG, FLANK_MAX_DEG};
  array<float> sides = {1, -1};
  foreach (float sign : sides)
  {
   foreach (float angle : angles)
   {
    float radians = sign * angle * Math.DEG2RAD;
    vector dir = Vector(back[0] * Math.Cos(radians) + back[2] * Math.Sin(radians), 0, back[2] * Math.Cos(radians) - back[0] * Math.Sin(radians));
    vector candidate = BBB.Ground(threat + dir * range * FLANK_RANGE);
    if (BBB_Terrain.IsWaterAt(candidate) || !BBB_OrderLeash.Allows(state.m_Group, candidate)) continue;
    if (EnemyNear(picture, candidate, 40)) continue;
    s_aFound.Clear();
    s_fGround = candidate[1];
    GetGame().GetWorld().QueryEntitiesBySphere(candidate, 15, CollectCover, null, EQueryEntitiesFlags.ALL);
    int score = s_aFound.Count();
    s_aFound.Clear();
    if (score <= bestScore) continue;
    bestScore = score;
    point = candidate;
   }
  }
  return bestScore > 0;
 }

 protected static bool EnemyNear(BBB_SquadPicture picture, vector point, float radius)
 {
  foreach (BBB_SquadContact contact : picture.m_Contacts)
   if (vector.DistanceXZ(contact.m_vPos, point) < radius) return true;
  return false;
 }

 // One flank step. True once the whole squad is near the flank point.
 static bool Flank(BBB_TacticState state, float now)
 {
  array<SCR_AIUtilityComponent> members = {};
  Members(state, members);
  if (members.IsEmpty()) return false;
  bool allThere = true;
  bool moversBusy;
  foreach (int i, SCR_AIUtilityComponent utility : members)
  {
   float left = vector.DistanceXZ(utility.m_OwnerEntity.GetOrigin(), state.m_vFlankPoint);
   if (left > ARRIVE_M) allThere = false;
   if (i % 2 == state.m_iFireHalf) continue;
   if (left <= ARRIVE_M) continue;
   if (Running(utility.m_BBBWithdrawRun)) { moversBusy = true; BBB_Claims.Take(utility.m_OwnerEntity, BBB_Claims.FIRE); continue; }
   if (!BBB_Claims.Take(utility.m_OwnerEntity, BBB_Claims.FIRE)) continue;
   // Spread the movers a few metres apart around the point.
   int column = i % 3;
   int row = i / 3;
   row = row % 2;
   vector spot = BBB.Ground(state.m_vFlankPoint + Vector((column - 1) * 4, 0, row * 4 - 2));
   BBB_AIWithdrawMove run = new BBB_AIWithdrawMove(utility, null, spot, priority: BBB_Morale.MOVE_PRIORITY, radius: 3.0);
   utility.AddAction(run);
   utility.m_BBBWithdrawRun = run;
   utility.m_bBBBTacticRunner = true;
   state.m_aRuns.Insert(run);
   moversBusy = true;
   s_iRuns++;
  }
  if (allThere) return true;
  // The movers made it (or can't): they cover, the others go.
  if (!moversBusy) state.m_fSwapAt = now;
  Swap(state, now);
  BaseOfFire(state, now, members);
  return false;
 }

 // Places to look, from the last known enemy position and the solid things around it.
 static void SearchPoints(BBB_TacticState state, vector lastSeen, vector squadAt, notnull array<vector> points)
 {
  points.Clear();
  vector start = BBB.Ground(lastSeen);
  if (BBB_OrderLeash.Allows(state.m_Group, start)) points.Insert(start);
  s_aFound.Clear();
  s_fGround = start[1];
  GetGame().GetWorld().QueryEntitiesBySphere(start, SEARCH_RADIUS_M, CollectCover, null, EQueryEntitiesFlags.ALL);
  array<vector> extra = {};
  foreach (IEntity object : s_aFound)
  {
   vector mins, maxs;
   object.GetWorldBounds(mins, maxs);
   vector middle = (mins + maxs) * 0.5;
   // The side away from the squad: where someone hiding from it would be.
   vector away = middle - squadAt;
   away[1] = 0;
   if (away.LengthSq() < 0.01) continue;
   away.Normalize();
   float reach = Math.Max(maxs[0] - mins[0], maxs[2] - mins[2]) * 0.5 + 2;
   vector spot = BBB.Ground(middle + away * reach);
   if (BBB_Terrain.IsWaterAt(spot) || !BBB_OrderLeash.Allows(state.m_Group, spot)) continue;
   bool close;
   foreach (vector have : extra)
    if (vector.DistanceXZ(have, spot) < 8) close = true;
   if (close || vector.DistanceXZ(spot, start) < 8) continue;
   extra.Insert(spot);
  }
  s_aFound.Clear();
  // Nearest the last known position first.
  while (!extra.IsEmpty() && points.Count() < SEARCH_EXTRA + 1)
  {
   int pick = 0;
   for (int e = 1; e < extra.Count(); e++)
    if (vector.DistanceXZ(extra[e], start) < vector.DistanceXZ(extra[pick], start)) pick = e;
   points.Insert(extra[pick]);
   extra.Remove(pick);
  }
 }

 // One search step. True when every point has been looked at.
 static bool Search(BBB_TacticState state, float now)
 {
  if (state.m_aPoints.IsEmpty()) return true;
  array<SCR_AIUtilityComponent> members = {};
  Members(state, members);
  if (members.IsEmpty()) return true;
  vector target = state.m_aPoints[0];
  bool reached;
  foreach (SCR_AIUtilityComponent utility : members)
   if (vector.DistanceXZ(utility.m_OwnerEntity.GetOrigin(), target) <= POINT_M) reached = true;
  if (reached || now > state.m_fPointDeadline)
  {
   if (reached) s_iPointsDone++;
   PrintFormat("[BBB] TACTIC %1: search point %2 %3 (%4 left)", BBB.s_Hooks.NameOf(state.m_Group), target, reached, state.m_aPoints.Count() - 1);
   state.m_aPoints.Remove(0);
   state.m_fSwapAt = now;
   Swap(state, now);
   if (state.m_aPoints.IsEmpty()) return true;
   target = state.m_aPoints[0];
   state.m_fPointDeadline = now + POINT_S + vector.DistanceXZ(members[0].m_OwnerEntity.GetOrigin(), target) / 1.5;
  }
  foreach (int i, SCR_AIUtilityComponent utility : members)
  {
   bool looking = i % 2 != state.m_iFireHalf;
   utility.BBBSetTacticHold(!looking, state.m_Group); // the watching half stays put
   if (!looking || HasLook(state, utility)) continue;
   SCR_AIMoveAndInvestigateBehavior look = new SCR_AIMoveAndInvestigateBehavior(utility, null, target, radius: POINT_M, isDangerous: true, duration: 4);
   utility.AddAction(look);
   state.m_aLooks.Insert(look);
   state.m_aLookers.Insert(utility);
   s_iLooks++;
  }
  return false;
 }

 protected static bool HasLook(BBB_TacticState state, SCR_AIUtilityComponent utility)
 {
  foreach (int i, SCR_AIMoveAndInvestigateBehavior look : state.m_aLooks)
   if (look && i < state.m_aLookers.Count() && state.m_aLookers[i] == utility && look.GetActionState() != EAIActionState.COMPLETED && look.GetActionState() != EAIActionState.FAILED) return true;
  return false;
 }
}

modded class SCR_AIUtilityComponent
{
 bool m_bBBBTacticRunner; // holds a FIRE claim for a flank run (BBB_TacticDrill)
}
