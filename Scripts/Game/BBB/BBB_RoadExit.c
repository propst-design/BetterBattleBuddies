// Off the road in a firefight (1 Nov 2026, Jacob): "infantry can use roads but they gotta get
// off em". Vanilla's first combat move looks for cover within 30 m and, finding none (asphalt
// rarely offers any), stays where it is and lies down: two squads meeting on a road lie on the
// same open strip and trade shots until one side is gone. A soldier on foot who stops on a road
// in a fight (attack or suppressive fire, not in cover, enemy beyond close range) and is still
// there STOPPED_S after vanilla's own cover search now steps off it to the side he is nearer, into cover on that side if the native cover search finds
// one, else onto the verge 7 m past the edge. Vanilla still picks the cover, the stance and when
// to move next; BBB only adds the "move now" and points the move off the road.
// Not on a bridge or an overpass: the verge there is a drop into a river or a ravine, so he
// stays to vanilla's own cover search; a verge spot in water (river, pond, sea) is refused.
// Yields to: player/GM orders (BBB_OrderIntent), a fire and manoeuvre job (it moves him itself),
// claimed men (rescue, static gun, rearm), close-range fights. One road check per soldier every
// CHECK_S at most, only while he is stopped in the open.
// Host option: -bbbRoadExit 0 (off).
class BBB_RoadExit
{
 static bool s_bOn = true;
 protected static int s_iRead;
 static const float ON_ROAD = 2;       // the open shoulder past the edge counts as the road (m)
 static const float STEP_OFF = 7;      // verge spot this far past the road edge (m)
 static const float CLEAR_SPOT = 2;    // the verge spot must be this clear of any road (junctions)
 static const float CHECK_S = 2;       // road check per soldier at most this often
 static const float STOPPED_S = 3;     // vanilla has had its own go at finding cover first
 static const float RETRY_S = 6;       // after a road exit, the next one no sooner
 static const float DECK = 1.5;        // standing this far above the ground under him: on a bridge or overpass
 static int s_iRequests, s_iStarted, s_iCover, s_iArrived, s_iNoSpot; // tests read these

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  if (BBB_Settings.Get("bbbRoadExit", value)) s_bOn = value == "1";
  PrintFormat("[BBB] ROAD EXIT on=%1", s_bOn);
 }

 // Called from vanilla's "move to the next position" decision: true when this soldier is
 // stopped on a road in a fight and has a spot off it (kept for PushRequestMove).
 static bool Wants(SCR_AIUtilityComponent utility, SCR_AICombatMoveState state, float targetDistance, vector threat)
 {
  Read();
  if (!s_bOn || !utility || !state || !utility.m_AIInfo) return false;
  utility.m_bBBBRoadExitPending = false;
  float now = GetGame().GetWorld().GetWorldTime() / 1000;
  if (now < utility.m_fBBBRoadExitNext || state.IsExecutingRequest() || state.m_bInCover || state.m_fTimerStopped_s < STOPPED_S) return false;
  if (targetDistance < SCR_AICombatMoveUtils.CLOSE_RANGE_COMBAT_DIST) return false;
  if (utility.m_AIInfo.HasUnitState(EUnitState.IN_VEHICLE) || utility.m_AIInfo.HasUnitState(EUnitState.UNCONSCIOUS)) return false;
  if (utility.m_BBBFireJob || BBB_Claims.Committed(utility.m_OwnerEntity) || BBB_OrderIntent.Blocks(utility)) return false;
  if (BBB.PlayerOf(utility.m_OwnerEntity) != 0) return false;
  int started;
  if (BBB_Perf.s_bOn) started = System.GetTickCount();
  utility.m_fBBBRoadExitNext = now + CHECK_S;
  vector spot;
  int found = OffRoadSpot(utility.m_OwnerEntity.GetOrigin(), threat, spot);
  if (BBB_Perf.s_bOn) BBB_Perf.Add("ev.roadExit", System.GetTickCount() - started);
  if (found < 0) s_iNoSpot++;
  if (found <= 0) return false;
  utility.m_vBBBRoadExitSpot = spot;
  utility.m_bBBBRoadExitPending = true;
  return true;
 }

 // Points the move vanilla just built at the verge spot: cover on that side if any, else the spot.
 static void Configure(SCR_AIUtilityComponent utility, SCR_AICombatMoveRequest_Move request)
 {
  if (!utility || !utility.m_bBBBRoadExitPending) return;
  utility.m_bBBBRoadExitPending = false;
  if (!request || request.m_eReason != SCR_EAICombatMoveReason.STANDARD) return;
  vector at = utility.m_OwnerEntity.GetOrigin();
  request.m_vMovePos = utility.m_vBBBRoadExitSpot;
  request.m_eDirection = SCR_EAICombatMoveDirection.CUSTOM_POS;
  request.m_vAvoidStraightPathDir = vector.Zero;
  request.m_bTryFindCover = true;
  request.m_bUseCoverSearchDirectivity = true;
  request.m_bCheckCoverVisibility = true;
  request.m_bFailIfNoCover = false;
  request.m_fCoverSearchDistMin = 0;
  request.m_fCoverSearchDistMax = Math.Max(12, vector.DistanceXZ(at, request.m_vMovePos) + 8);
  request.m_fCoverSearchSectorHalfAngleRad = 0.4 * Math.PI;
  request.m_eMovementType = EMovementType.RUN;
  request.m_eStanceMoving = ECharacterStance.CROUCH;
  request.m_bAimAtTarget = SCR_AICombatMoveUtils.IsAimingAndMovementPossible(request.m_eStanceMoving, request.m_eMovementType);
  request.m_bAimAtTargetEnd = true;
  request.m_fMoveDuration_s = 6;
  request.GetOnMovementStarted().Insert(MoveStarted);
  request.GetOnCompleted().Insert(Arrived);
  utility.m_fBBBRoadExitNext = GetGame().GetWorld().GetWorldTime() / 1000 + RETRY_S;
  s_iRequests++;
 }

 protected static void MoveStarted(SCR_AIUtilityComponent utility, SCR_AICombatMoveRequest_Move request, vector position, bool cover)
 {
  if (!utility || !utility.m_OwnerEntity) return;
  s_iStarted++;
  if (cover) s_iCover++;
  BBB_Perf.Count("roadExit.moves");
  string where = "the verge";
  if (cover) where = "cover";
  BBB_Think.NoteUtility(utility, "RoadExit", "off the road under fire: " + Math.Round(vector.DistanceXZ(utility.m_OwnerEntity.GetOrigin(), position)) + " m to " + where);
 }

 protected static void Arrived(SCR_AIUtilityComponent utility, SCR_AICombatMoveRequestBase request)
 {
  s_iArrived++;
 }

 // 1 and a spot off the road when 'at' is on one, 0 when it isn't, -1 on a road with no usable spot.
 static int OffRoadSpot(vector at, vector threat, out vector spot)
 {
  ChimeraAIWorld aiWorld = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
  if (!aiWorld || !aiWorld.GetRoadNetworkManager()) return 0;
  BaseRoad road;
  float distance;
  aiWorld.GetRoadNetworkManager().GetClosestRoad(at, road, distance, true);
  if (!road) return 0;
  float half = road.GetWidth() * 0.5;
  if (half <= 0) half = 3;
  if (distance - half > ON_ROAD) return 0;
  if (at[1] > GetGame().GetWorld().GetSurfaceY(at[0], at[2]) + DECK) return 0;
  // The road's direction here: the nearest of its segments.
  array<vector> points = {};
  road.GetPoints(points);
  if (points.Count() < 2) return -1;
  vector closest, tangent;
  float best = float.MAX;
  for (int i = 1; i < points.Count(); i++)
  {
   vector a = points[i - 1];
   vector b = points[i];
   float abx = b[0] - a[0];
   float abz = b[2] - a[2];
   float lengthSq = abx * abx + abz * abz;
   if (lengthSq < 0.01) continue;
   float t = Math.Clamp(((at[0] - a[0]) * abx + (at[2] - a[2]) * abz) / lengthSq, 0, 1);
   vector c = Vector(a[0] + abx * t, at[1], a[2] + abz * t);
   float d = vector.DistanceXZ(c, at);
   if (d >= best) continue;
   best = d;
   closest = c;
   tangent = Vector(abx, 0, abz);
  }
  if (best == float.MAX) return -1;
  tangent.Normalize();
  vector side = Vector(-tangent[2], 0, tangent[0]);
  // The side he is already on; from the crown, the side away from the enemy.
  float lateral = vector.Dot(at - closest, side);
  if (Math.AbsFloat(lateral) < 1)
  {
   vector toThreat = threat - at;
   toThreat[1] = 0;
   lateral = -vector.Dot(toThreat, side);
  }
  if (lateral < 0) side = -side;
  for (int pass = 0; pass < 2; pass++)
  {
   vector candidate = BBB.Ground(closest + side * (half + STEP_OFF));
   if (BBB_Terrain.WaterDepthAt(candidate) <= 0 && Clearance(aiWorld, candidate) >= CLEAR_SPOT)
   {
    spot = candidate;
    return 1;
   }
   side = -side;
  }
  return -1;
 }

 protected static float Clearance(ChimeraAIWorld aiWorld, vector p)
 {
  BaseRoad road;
  float distance;
  aiWorld.GetRoadNetworkManager().GetClosestRoad(p, road, distance, true);
  if (!road) return float.MAX;
  float half = road.GetWidth() * 0.5;
  if (half <= 0) half = 3;
  return distance - half;
 }
}

modded class SCR_AIUtilityComponent
{
 float m_fBBBRoadExitNext;
 bool m_bBBBRoadExitPending;
 vector m_vBBBRoadExitSpot;
}

// The road exit sits on vanilla's "move to the next position" decision only: danger, grenades,
// friendly fire, useless cover and reloading are decided before it.
modded class SCR_AICombatMoveLogic_Attack
{
 protected override bool MoveToNextPosCondition()
 {
  if (BBB_RoadExit.Wants(m_Utility, m_State, m_fTargetDist, GetTargetPosition())) return true;
  return super.MoveToNextPosCondition();
 }
 protected override void PushRequestMove()
 {
  super.PushRequestMove();
  BBB_RoadExit.Configure(m_Utility, SCR_AICombatMoveRequest_Move.Cast(m_State.GetRequest()));
 }
}

modded class SCR_AICombatMoveLogic_Suppressive
{
 protected override bool MoveToNextPosCondition()
 {
  if (BBB_RoadExit.Wants(m_Utility, m_State, m_fTargetDist, GetTargetPosition())) return true;
  return super.MoveToNextPosCondition();
 }
 protected override void PushRequestMove()
 {
  super.PushRequestMove();
  BBB_RoadExit.Configure(m_Utility, SCR_AICombatMoveRequest_Move.Cast(m_State.GetRequest()));
 }
}
