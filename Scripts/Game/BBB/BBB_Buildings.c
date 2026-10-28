// Contact-driven indoor cover. Native cover scoring uses this soldier's pathfinding;
// native MoveIndividually handles doors and traversal. No authored room coordinates.
class BBB_BuildingJob : Managed
{
 SCR_AIGroup m_Group;
 ref BBB_OrderLease m_Lease;
 SCR_AIUtilityComponent m_Utility;
 IEntity m_Building;
 vector m_Position;
 vector m_Threat;
 vector m_LastPosition;
 ref SCR_AICoverLock m_Cover;
 ref SCR_AIBehaviorBase m_Action;
 float m_Started;
 float m_Progress;
 float m_Arrived;
 int m_Steps;
}

// Re-evaluated by the native utility before selection, including between BBB ticks.
class BBB_IndoorMove : SCR_AIMoveIndividuallyBehavior
{
 ref BBB_OrderLease m_OrderLease;
 override float CustomEvaluate()
 {
  if (!BBB_Buildings.Allowed(m_Utility, false) || !BBB_OrderLeash.Allows(BBB_Buildings.GroupOf(m_Utility), m_Utility.m_OwnerEntity.GetOrigin(), m_OrderLease)) return 0;
  return GetPriority();
 }
}

// Where a fighting soldier is: the building over his head, re-traced only when he moves.
class BBB_IndoorSpot : Managed
{
 vector m_At;
 IEntity m_Building;
 float m_Until;
 vector m_Target;
 IEntity m_TargetBuilding;
}

class BBB_Buildings
{
 static int s_iRead;
 static bool s_bOn;
 static bool s_bHold = true;
 static int s_iHolds;
 static int s_iStance;
 static int s_iShelters;
 static int s_iRises;
 static int s_iWindowAsks;
 static int s_iWindowJobs;
 static int s_iWindowNone;
 static const float HOLD_SEEN_S = 10;   // a target seen this recently is being fought from here
 static const float HOLD_CQB_M = 10;    // closer than this, vanilla close combat decides
 static const float WINDOW_WAIT_MS = 4000;
 static const float NO_WINDOW_MS = 20000;
 protected static ref map<SCR_AIUtilityComponent, ref BBB_IndoorSpot> s_Indoor = new map<SCR_AIUtilityComponent, ref BBB_IndoorSpot>();
 protected static ref map<SCR_AIUtilityComponent, vector> s_WindowThreat = new map<SCR_AIUtilityComponent, vector>();
 protected static ref map<SCR_AIUtilityComponent, float> s_WindowAsked = new map<SCR_AIUtilityComponent, float>();
 protected static ref map<SCR_AIUtilityComponent, float> s_NoWindow = new map<SCR_AIUtilityComponent, float>();
 static int s_iStarted;
 static int s_iArrived;
 static int s_iFailed;
 static int s_iQueries;
 static int s_iClearingSteps;
 protected static int s_iQueryBudget;
 static ref array<ref BBB_BuildingJob> s_Jobs = {};
 protected static ref map<SCR_AIUtilityComponent, float> s_Retry = new map<SCR_AIUtilityComponent, float>();
 protected static int s_iCursor;
 protected static bool s_bInTick;
 protected static ref array<IEntity> s_Buildings = {};

 protected static bool CollectBuilding(IEntity entity)
 {
  if (BaseBuilding.Cast(entity) && s_Buildings.Count() < 24) s_Buildings.Insert(entity);
  return true;
 }

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbBuildings", value) && value == "0");
  s_bHold = !(BBB_Settings.Get("bbbIndoorHold", value) && value == "0");
  PrintFormat("[BBB] BUILDINGS enabled=%1 indoorHold=%2", s_bOn, s_bHold);
 }

 // ---------------------------------------------------------------- fighting from a building
 // Vanilla combat movement (SCR_AICombatMoveLogic_Attack/_Suppressive) sends a soldier
 // who has stood 3-6 s out of a registered cover to a cover 10-30 m towards his target,
 // or that far in a straight line when none is found. Inside a building that is out of
 // the door into the open: garrisons and men BBB moved indoors empty out within seconds
 // of the first shot. A soldier fighting from a building at an enemy outside it keeps his
 // place while he can see that enemy; when he can't, he is offered a spot in the same
 // building that sees it, and only if there is none does vanilla move him on.

 static IEntity BuildingAt(SCR_AIUtilityComponent utility)
 {
  // A window request can outlive its soldier (deleted while queued: ACA Kolguyev soak 28 Oct,
  // NULL m_OwnerEntity from ServeWindows).
  if (!utility || !utility.m_OwnerEntity) return null;
  float now = GetGame().GetWorld().GetWorldTime();
  vector here = utility.m_OwnerEntity.GetOrigin();
  BBB_IndoorSpot spot = s_Indoor.Get(utility);
  if (spot && now < spot.m_Until && vector.Distance(spot.m_At, here) < 0.75) return spot.m_Building;
  if (!spot) { spot = new BBB_IndoorSpot(); s_Indoor.Set(utility, spot); }
  spot.m_At = here;
  spot.m_Until = now + 2000;
  spot.m_Building = RoofAt(here);
  return spot.m_Building;
 }

 // stayBlind: with no window to be had he stays anyway (covering fire), as defenders do.
 static bool HoldsIndoors(SCR_AIUtilityComponent utility, vector target, bool seen, bool stayBlind = false)
 {
  Read();
  if (!s_bHold || !BBB.IsStarted() || !utility || !utility.m_OwnerEntity || utility.m_BBBFireJob) return false;
  int started;
  if (BBB_Perf.s_bOn) started = System.GetTickCount();
  bool hold = DecideHold(utility, target, seen, stayBlind);
  if (utility.m_CombatMoveState)
  {
   if (hold) utility.m_CombatMoveState.m_BBBIndoorOwner = utility;
   else utility.m_CombatMoveState.m_BBBIndoorOwner = null;
   utility.m_CombatMoveState.m_fBBBIndoorAt = GetGame().GetWorld().GetWorldTime();
  }
  if (BBB_Perf.s_bOn) BBB_Perf.Add("ev.indoorHold", System.GetTickCount() - started);
  return hold;
 }

 // The building this soldier fights from at an enemy outside it, or null: then vanilla decides.
 protected static IEntity FightsFrom(SCR_AIUtilityComponent utility, vector target, out bool defend)
 {
  // Cheapest first: most fighting men are outdoors (one cached trace per 2 s).
  IEntity building = BuildingAt(utility);
  vector here = utility.m_OwnerEntity.GetOrigin();
  if (!building || vector.DistanceXZ(here, target) < HOLD_CQB_M) return null;
  SCR_AIGroup group = GroupOf(utility);
  if (!group || !BBB.s_Hooks.AllowBuildings(group) || BBB_Morale.IsWithdrawing(group)) return null;
  if (BBB_Tactic.TacticOf(group) == BBB_Tactic.ASSAULT) return null;
  // Holding a building is what a defend order asks for, whoever gave it; any other
  // order keeps BBB's usual leash and yields to direct player/GM commands.
  defend = SCR_DefendWaypoint.Cast(group.GetCurrentWaypoint()) != null;
  if (!defend && (!BBB_OrderLeash.Allows(group, here) || BBB_OrderIntent.Blocks(utility))) return null;
  ChimeraCharacter body = ChimeraCharacter.Cast(utility.m_OwnerEntity);
  if (!body || body.IsInVehicle() || BBB.PlayerOf(body) || BBB.HasPlayer(group)) return null;
  // An enemy inside our own building is a room fight: vanilla closes with him.
  BBB_IndoorSpot spot = s_Indoor.Get(utility);
  if (vector.Distance(spot.m_Target, target) > 1) { spot.m_Target = target; spot.m_TargetBuilding = RoofAt(target); }
  if (spot.m_TargetBuilding == building) return null;
  return building;
 }

 // Vanilla's gunfire reaction (HideFromThreatSystem) sprints a man shot at from beyond
 // 40 m up to 25 m in a random direction to find cover: out of a building. The walls are
 // his cover; he ducks where he is (vanilla's own stance handling) instead.
 static bool SheltersIndoors(SCR_AIUtilityComponent utility, vector threat)
 {
  Read();
  if (!s_bHold || !BBB.IsStarted() || !utility || !utility.m_OwnerEntity || utility.m_BBBFireJob) return false;
  bool defend;
  if (!FightsFrom(utility, threat, defend)) return false;
  s_iShelters++;
  BBB_Think.NoteUtility(utility, "Buildings", "shot at from outside: ducking inside the building");
  return true;
 }

 protected static bool DecideHold(SCR_AIUtilityComponent utility, vector target, bool seen, bool stayBlind)
 {
  bool defend;
  if (!FightsFrom(utility, target, defend)) return false;
  defend = defend || stayBlind;
  float now = GetGame().GetWorld().GetWorldTime();
  if (seen)
  {
   s_iHolds++;
   BBB_Think.NoteUtility(utility, "Buildings", "fighting from the building: enemy outside in view");
   return true;
  }
  // Blind inside: a BBB move to a window is already under way, or ask for one. With
  // none to be had, defenders stay (that is their order); anyone else goes on with vanilla.
  foreach (BBB_BuildingJob job : s_Jobs)
   if (job.m_Utility == utility) return true;
  if (!s_bOn || now < s_NoWindow.Get(utility) || now < s_Retry.Get(utility)) return defend;
  if (!s_WindowAsked.Contains(utility))
  {
   s_WindowAsked.Set(utility, now);
   s_WindowThreat.Set(utility, target);
   s_iWindowAsks++;
  }
  else if (now - s_WindowAsked.Get(utility) > WINDOW_WAIT_MS)
  {
   s_WindowAsked.Remove(utility);
   s_NoWindow.Set(utility, now + NO_WINDOW_MS);
   return defend;
  }
  BBB_Think.NoteUtility(utility, "Buildings", "enemy out of view: looking for a window in this building");
  return true;
 }

 // Indoors, prone or crouched below the sill is blind. Pick the lowest stance at or
 // above the one vanilla asked for that still sees the target; prone never indoors.
 static ECharacterStance IndoorStance(SCR_AIUtilityComponent utility, ECharacterStance stance, vector target)
 {
  if (!s_bHold || !utility || !utility.m_OwnerEntity || stance == ECharacterStance.STAND) return stance;
  vector here = utility.m_OwnerEntity.GetOrigin();
  vector aim = target + Vector(0, 1.3, 0);
  if (stance == ECharacterStance.PRONE) stance = ECharacterStance.CROUCH;
  if (ViewClear(here + Vector(0, 1.0, 0), aim)) return stance;
  if (ViewClear(here + Vector(0, 1.6, 0), aim)) return ECharacterStance.STAND;
  return stance;
 }

 protected static bool IgnoreCharacters(notnull IEntity entity)
 {
  return !ChimeraCharacter.Cast(entity);
 }

 // Window glass is seen (and shot) through; frames, walls and furniture block.
 static bool ViewClear(vector from, vector to)
 {
  vector start = from;
  for (int pane = 0; pane < 3; pane++)
  {
   TraceParam trace = new TraceParam();
   trace.Start = start;
   trace.End = to;
   trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
   float fraction = GetGame().GetWorld().TraceMove(trace, IgnoreCharacters);
   if (fraction >= 0.98) return true;
   string collider = trace.ColliderName;
   collider.ToLower();
   if (!collider.Contains("glass")) return false;
   vector dir = (to - start).Normalized();
   start = start + (to - start) * fraction + dir * 0.05;
   if (vector.Distance(start, to) < 0.5) return true;
  }
  return false;
 }

 // One asked-for window per tick, inside the building he is already in.
 protected static void ServeWindows(float now)
 {
  for (int i = s_WindowAsked.Count() - 1; i >= 0; i--)
  {
   SCR_AIUtilityComponent utility = s_WindowAsked.GetKey(i);
   if (!utility || now - s_WindowAsked.GetElement(i) > WINDOW_WAIT_MS * 2) { s_WindowAsked.RemoveElement(i); if (utility) s_WindowThreat.Remove(utility); }
  }
  for (int n = s_NoWindow.Count() - 1; n >= 0; n--)
   if (!s_NoWindow.GetKey(n) || now >= s_NoWindow.GetElement(n)) s_NoWindow.RemoveElement(n);
  for (int p = s_Indoor.Count() - 1; p >= 0; p--)
   if (!s_Indoor.GetKey(p) || now - s_Indoor.GetElement(p).m_Until > 10000) s_Indoor.RemoveElement(p);
  if (!s_bOn) return;
  foreach (SCR_AIUtilityComponent asker, float asked : s_WindowAsked)
  {
   if (!asker || !s_WindowThreat.Contains(asker) || s_iQueryBudget <= 0) continue;
   vector threat = s_WindowThreat.Get(asker);
   s_WindowThreat.Remove(asker);
   IEntity inside = BuildingAt(asker);
   if (inside && Begin(asker, threat, true, inside)) { s_iWindowJobs++; s_WindowAsked.Remove(asker); }
   else { s_iWindowNone++; s_WindowAsked.Remove(asker); s_NoWindow.Set(asker, now + NO_WINDOW_MS); }
   return;
  }
 }

 static SCR_AIGroup GroupOf(SCR_AIUtilityComponent utility)
 {
  if (!utility || !utility.GetAIAgent()) return null;
  return SCR_AIGroup.Cast(utility.GetAIAgent().GetParentGroup());
 }

 static bool Allowed(SCR_AIUtilityComponent utility, bool inspectGroup = true)
 {
  if (!s_bOn || !BBB.IsStarted() || !utility || !utility.m_OwnerEntity) return false;
  SCR_AIGroup group = GroupOf(utility);
  if (!group || !BBB_OrderLeash.Allows(group, utility.m_OwnerEntity.GetOrigin()) || (inspectGroup && BBB.HasPlayer(group)) || !BBB.s_Hooks.AllowBuildings(group)) return false;
  if (BBB_OrderIntent.Blocks(utility) || !utility.IsMilitary()) return false;
  ChimeraCharacter body = ChimeraCharacter.Cast(utility.m_OwnerEntity);
  if (!body || !BBB.IsAlive(body) || body.IsInVehicle() || BBB.PlayerOf(body) || BBB_Drag.Busy(body)) return false;
  CharacterControllerComponent cc = body.GetCharacterController();
  if (!cc || cc.IsUnconscious() || cc.IsPlayerControlled() || cc.IsClimbing() || cc.IsSwimming()) return false;
  AIControlComponent control = AIControlComponent.Cast(body.FindComponent(AIControlComponent));
  if (!control || !control.IsAIActivated()) return false;
  if (BBB_Morale.IsWithdrawing(group) || utility.m_BBBFireJob) return false;
  // Any other BBB job outranks moving indoors (and takes a man already inside).
  if (!BBB_Claims.Can(body, BBB_Claims.BUILDINGS)) return false;
  return true;
 }

 static bool RecentContact(SCR_AIUtilityComponent utility, out vector known)
 {
  if (!utility || !utility.m_PerceptionComponent) return false;
  array<BaseTarget> targets = {};
  utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
  foreach (BaseTarget target : targets)
  {
   if (!target || target.GetUnitType() != EAIUnitType.UnitType_Infantry || target.IsDisarmed()) continue;
   float age = target.GetTimeSinceDetected();
   if (age < 0 || age > 20) continue;
   vector point = target.GetLastDetectedPosition();
   float distance = vector.Distance(utility.m_OwnerEntity.GetOrigin(), point);
   if (distance < 8 || distance > 100) continue;
   known = point;
   return true;
  }
  return false;
 }

 // A roof belonging to a real building, rather than trees or outdoor cover.
 static IEntity RoofAt(vector position)
 {
  TraceParam trace = new TraceParam();
  trace.Start = position + Vector(0, 1.8, 0);
  trace.End = position + Vector(0, 8, 0);
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  if (GetGame().GetWorld().TraceMove(trace, null) >= 1) return null;
  IEntity hit = trace.TraceEnt;
  if (!hit) return null;
  if (BaseBuilding.Cast(hit)) return hit;
  if (BaseBuilding.Cast(hit.GetRootParent())) return hit.GetRootParent();
  return null;
 }

 // mustSee: only covers the native query finds a view of the threat from (a window,
 // a doorway). Otherwise those come first and a hidden indoor cover is the fallback.
 static bool FindIndoorCover(SCR_AIUtilityComponent utility, vector threat, out vector position, out IEntity building, out SCR_AICoverLock cover, bool mustSee = false, IEntity within = null)
 {
  if (!utility || !utility.m_OwnerEntity || !GetGame().GetAIWorld()) return false;
  if (!s_bInTick) s_iQueryBudget = 4;
  AIPathfindingComponent path = AIPathfindingComponent.Cast(utility.m_OwnerEntity.FindComponent(AIPathfindingComponent));
  ChimeraCoverManagerComponent manager = ChimeraCoverManagerComponent.Cast(GetGame().GetAIWorld().FindComponent(CoverManagerComponent));
  if (!path || !manager || !path.GetNavmeshComponent()) return false;
  vector origin = utility.m_OwnerEntity.GetOrigin();
  if (!path.GetNavmeshComponent().IsTileLoaded(origin)) return false;
  s_Buildings.Clear();
  GetGame().GetWorld().QueryEntitiesBySphere(origin, 32, CollectBuilding);
  IEntity selected = within;
  float nearest = float.MAX;
  if (!selected) foreach (IEntity candidate : s_Buildings)
  {
   vector boundsMin, boundsMax;
   candidate.GetBounds(boundsMin, boundsMax);
   vector size = boundsMax - boundsMin;
   // Tiny closed sheds and other non-room structures are poor garrison targets.
   if (size[0] < 5 || size[2] < 5 || size[1] < 2.5) continue;
   float dist = vector.Distance(origin, candidate.GetOrigin());
   if (dist < nearest) { selected = candidate; nearest = dist; }
  }
  if (!selected) return false;
  CoverQueryProperties props = new CoverQueryProperties();
  vector towards = threat - origin;
  towards[1] = 0;
  towards.Normalize();
  props.m_vSectorPos = selected.GetOrigin();
  props.m_vSectorDir = towards;
  props.m_vThreatPos = threat + Vector(0, 1, 0);
  props.m_vNearestPolyHalfExtend = "1 2 1";
  vector mins, maxs;
  selected.GetBounds(mins, maxs);
  props.m_fSectorDistMax = Math.Clamp((maxs - mins).Length() * 0.5, 5, 18);
  props.m_fCoverHeightMin = 0;
  props.m_fCoverToThreatAngleCosMin = -1;
  props.m_fNmAreaCostScale = 1.0 / 3.0;
  props.m_iMaxCoversToCheck = 12;
  props.m_bCheckVisibility = true;
  props.m_bSelectHighestScore = false;
  vector tallest;
  int tileX, tileY, coverId;
  // Temporarily exclude rejected outdoor covers so an exterior wall cannot
  // permanently hide the usable indoor positions of this building.
  array<ref SCR_AICoverLock> rejected = {};
  bool found = false;
  for (int attempt = 0; attempt < 4; attempt++)
  {
   if (s_iQueryBudget <= 0) break;
   if (!mustSee && attempt == 2) props.m_bCheckVisibility = false;
   s_iQueryBudget--;
   s_iQueries++;
   bool coverFound = manager.GetBestCover("Soldiers", path, props, position, tallest, tileX, tileY, coverId);
   if (!coverFound && props.m_bCheckVisibility && !mustSee && attempt < 2) { attempt = 1; continue; }
   if (!coverFound) break;
   SCR_AICoverLock candidateCover = new SCR_AICoverLock(tileX, tileY, coverId, position, tallest);
   building = RoofAt(position);
   vector snapped;
   if (building == selected && vector.Distance(origin, position) <= 32 && vector.Distance(origin, position) >= 2 && path.GetClosestPositionOnNavmesh(position, "0.3 0.3 0.3", snapped) && vector.Distance(position, snapped) <= 0.4)
   {
    cover = candidateCover;
    found = true;
    break;
   }
   rejected.Insert(candidateCover);
  }
  foreach (SCR_AICoverLock rejectedCover : rejected) rejectedCover.Release();
  return found;
 }

 static bool Begin(SCR_AIUtilityComponent utility, vector threat, bool mustSee = false, IEntity within = null)
 {
  if (!Allowed(utility)) return false;
  foreach (BBB_BuildingJob existing : s_Jobs)
   if (existing.m_Utility == utility) return false;
  vector position;
  IEntity building;
  SCR_AICoverLock cover;
  if (!FindIndoorCover(utility, threat, position, building, cover, mustSee, within)) return false;
  if (!BBB_OrderLeash.Allows(GroupOf(utility), position)) { cover.Release(); return false; }
  BBB_BuildingJob job = new BBB_BuildingJob();
  job.m_Group = GroupOf(utility);
  job.m_Lease = BBB_OrderLeash.Capture(job.m_Group);
  job.m_Utility = utility;
  job.m_Position = position;
  job.m_Threat = threat;
  job.m_Building = building;
  job.m_Cover = cover;
  job.m_Started = GetGame().GetWorld().GetWorldTime();
  job.m_Progress = job.m_Started;
  job.m_LastPosition = utility.m_OwnerEntity.GetOrigin();
  // Above ordinary attack selection, below native aid/grenade/retreat/danger.
  job.m_Action = new BBB_IndoorMove(utility, null, position, 91, SCR_AIActionBase.PRIORITY_LEVEL_NORMAL, null, 0.3);
  BBB_IndoorMove.Cast(job.m_Action).m_OrderLease = job.m_Lease;
  utility.AddAction(job.m_Action);
  BBB_Claims.Take(utility.m_OwnerEntity, BBB_Claims.BUILDINGS);
  s_Jobs.Insert(job);
  s_iStarted++;
  return true;
 }

 static void Release(BBB_BuildingJob job)
 {
  if (job.m_Action && job.m_Action.GetActionState() != EAIActionState.COMPLETED && job.m_Action.GetActionState() != EAIActionState.FAILED) job.m_Action.Fail();
  if (job.m_Utility && job.m_Utility.m_CombatMoveState && job.m_Utility.m_CombatMoveState.GetAssignedCover() == job.m_Cover) job.m_Utility.m_CombatMoveState.ReleaseCover();
  else if (job.m_Cover) job.m_Cover.Release();
  if (job.m_Utility) s_Retry.Set(job.m_Utility, GetGame().GetWorld().GetWorldTime() + 30000);
  if (job.m_Utility) BBB_Claims.Release(job.m_Utility.m_OwnerEntity, BBB_Claims.BUILDINGS);
 }

 static void Stop()
 {
  foreach (BBB_BuildingJob job : s_Jobs) Release(job);
  s_Jobs.Clear();
  s_Retry.Clear();
  foreach (SCR_AIUtilityComponent utility, BBB_IndoorSpot spot : s_Indoor)
   if (utility && utility.m_CombatMoveState) utility.m_CombatMoveState.m_BBBIndoorOwner = null;
  s_Indoor.Clear();
  s_WindowAsked.Clear();
  s_WindowThreat.Clear();
  s_NoWindow.Clear();
 }

 static void Tick()
 {
  s_bInTick = true;
  TickJobs();
  s_bInTick = false;
 }

 protected static void TickJobs()
 {
  Read();
  if (!s_bOn) { Stop(); return; }
  s_iQueryBudget = 4;
  float now = GetGame().GetWorld().GetWorldTime();
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  for (int i = s_Jobs.Count() - 1; i >= 0; i--)
  {
   BBB_BuildingJob job = s_Jobs[i];
   vector known;
   bool valid = BBB_OrderLeash.Allows(job.m_Group, job.m_Position, job.m_Lease) && groups.Contains(job.m_Group) && Allowed(job.m_Utility) && job.m_Building && job.m_Cover && job.m_Cover.IsValid();
   if (valid) valid = RecentContact(job.m_Utility, known) && vector.Distance(known, job.m_Threat) < 25;
   if (valid)
   {
    BBB_Claims.Take(job.m_Utility.m_OwnerEntity, BBB_Claims.BUILDINGS);
    vector here = job.m_Utility.m_OwnerEntity.GetOrigin();
    if (vector.Distance(here, job.m_LastPosition) > 0.75) { job.m_LastPosition = here; job.m_Progress = now; }
    if (job.m_Arrived == 0 && vector.Distance(here, job.m_Position) <= 1.3 && RoofAt(here) == job.m_Building)
    {
     job.m_Arrived = now;
     s_iArrived++;
     if (job.m_Action.GetActionState() != EAIActionState.COMPLETED) job.m_Action.Fail();
     if (job.m_Utility.m_CombatMoveState)
     {
      job.m_Utility.m_CombatMoveState.AssignCover(job.m_Cover);
      job.m_Utility.m_CombatMoveState.m_bInCover = true;
      job.m_Utility.m_CombatMoveState.m_bExposedInCover = true;
     }
     // No follow-up behaviour: a move-and-investigate here walked men round the house
     // and out of the door (indoorhold test). The indoor hold on vanilla combat
     // movement keeps him at this cover while he fights from it.
     job.m_Action = null;
    }
    if (job.m_Arrived > 0 && now - job.m_Arrived >= 6000 && job.m_Steps < 2 && RoofAt(known) == job.m_Building && s_iQueryBudget > 0)
    {
     vector nextPosition;
     IEntity nextBuilding;
     SCR_AICoverLock nextCover;
     if (FindIndoorCover(job.m_Utility, known, nextPosition, nextBuilding, nextCover))
     {
      if (BBB_OrderLeash.Allows(job.m_Group, nextPosition, job.m_Lease) && nextBuilding == job.m_Building && vector.Distance(nextPosition, job.m_Position) >= 3)
      {
       if (job.m_Action) job.m_Action.Fail();
       if (job.m_Utility.m_CombatMoveState.GetAssignedCover() == job.m_Cover) job.m_Utility.m_CombatMoveState.ReleaseCover();
       else job.m_Cover.Release();
       job.m_Cover = nextCover;
       job.m_Position = nextPosition;
       job.m_Arrived = 0;
       job.m_Progress = now;
       job.m_Started = now;
       job.m_Steps++;
       s_iClearingSteps++;
       job.m_Action = new BBB_IndoorMove(job.m_Utility, null, nextPosition, 91, 0, null, 0.3);
       BBB_IndoorMove.Cast(job.m_Action).m_OrderLease = job.m_Lease;
       job.m_Utility.AddAction(job.m_Action);
      }
      else nextCover.Release();
     }
    }
    if (job.m_Arrived > 0) valid = now - job.m_Arrived < 12000 && vector.Distance(here, job.m_Position) < 4;
    else valid = (job.m_Action.GetActionState() != EAIActionState.RUNNING || now - job.m_Progress < 8000) && now - job.m_Started < 30000 && job.m_Action.GetActionState() != EAIActionState.FAILED;
   }
   if (!valid)
   {
    if (job.m_Arrived == 0) s_iFailed++;
    Release(job);
    s_Jobs.RemoveOrdered(i);
   }
  }
  for (int r = s_Retry.Count() - 1; r >= 0; r--)
   if (!s_Retry.GetKey(r) || now >= s_Retry.GetElement(r)) s_Retry.RemoveElement(r);
  ServeWindows(now);
  if (groups.IsEmpty()) return;
  // One group's discovery per second, at most four bounded cover queries. No frame scans.
  s_iCursor = (s_iCursor + 1) % groups.Count();
  SCR_AIGroup group = groups[s_iCursor];
  int working;
  foreach (BBB_BuildingJob occupied : s_Jobs)
   if (occupied.m_Group == group) working++;
  if (working >= 2 || s_Jobs.Count() >= 16 || s_iQueryBudget <= 0) return;
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(group, agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   if (!Allowed(utility) || now < s_Retry.Get(utility)) continue;
   bool busy;
   foreach (BBB_BuildingJob activeJob : s_Jobs)
    if (activeJob.m_Utility == utility) busy = true;
   if (busy) continue;
   vector threat;
   if (!RecentContact(utility, threat)) continue;
   s_Retry.Set(utility, now + 10000);
   Begin(utility, threat);
   break;
  }
 }
}

modded class SCR_AICombatMoveState
{
 SCR_AIUtilityComponent m_BBBIndoorOwner;
 float m_fBBBIndoorAt;
 override void ApplyRequestChangeStanceOutsideCover(ECharacterStance stance)
 {
  // Only right after this combat-move node decided to hold him indoors.
  SCR_AIUtilityComponent owner;
  if (GetGame().GetWorld().GetWorldTime() - m_fBBBIndoorAt < 1500) owner = m_BBBIndoorOwner;
  if (owner && owner.m_OwnerEntity && stance != ECharacterStance.STAND)
  {
   BaseTarget target;
   SCR_AICombatComponent combat = owner.m_CombatComponent;
   if (combat) target = combat.GetCurrentTarget();
   if (target)
   {
    ECharacterStance view = BBB_Buildings.IndoorStance(owner, stance, target.GetLastSeenPosition());
    if (view != stance) BBB_Buildings.s_iStance++;
    stance = view;
    ChimeraCharacter body = ChimeraCharacter.Cast(owner.m_OwnerEntity);
    // Vanilla only asks to go lower; if he already stands as low as the view allows, leave him.
    if (body && body.GetCharacterController() && body.GetCharacterController().GetStance() == stance) return;
   }
  }
  super.ApplyRequestChangeStanceOutsideCover(stance);
 }
}

// The hold sits on vanilla's "move to the next position" decision only: danger,
// grenades, friendly fire, useless cover and reloading are decided before it.
modded class SCR_AICombatMoveLogic_Attack
{
 protected override bool MoveToNextPosCondition()
 {
  if (m_Target && BBB_Buildings.HoldsIndoors(m_Utility, m_Target.GetLastSeenPosition(), m_Target.GetTimeSinceSeen() >= 0 && m_Target.GetTimeSinceSeen() < BBB_Buildings.HOLD_SEEN_S))
  {
   // Vanilla only ever lowers stance in a fight, so a man who ducked prone below a
   // window stays blind there. Back up to the lowest stance that sees out.
   if (m_eStance == ECharacterStance.PRONE && !m_State.IsExecutingRequest() && !m_State.m_bInCover)
   {
    ECharacterStance view = BBB_Buildings.IndoorStance(m_Utility, ECharacterStance.PRONE, m_Target.GetLastSeenPosition());
    if (view != ECharacterStance.PRONE) { BBB_Buildings.s_iRises++; m_State.ApplyRequestChangeStanceOutsideCover(view); }
   }
   return false;
  }
  return super.MoveToNextPosCondition();
 }
}

modded class SCR_AICombatMoveLogic_Suppressive
{
 protected override bool MoveToNextPosCondition()
 {
  if (m_SuppressionVolume && BBB_Buildings.HoldsIndoors(m_Utility, m_SuppressionVolume.GetCenterPosition(), m_bGoodVision, true)) return false;
  return super.MoveToNextPosCondition();
 }
}

modded class SCR_AICombatMoveLogic_HideFromThreatSystem
{
 override void Update()
 {
  if (!m_bPushedRequest && !m_bReachedSafety && m_iCurrentSector != -1 && m_MyEntity && m_Utility.m_SectorThreatFilter.IsSectorActive(m_iCurrentSector) && !m_State.IsMoving())
  {
   if (BBB_Buildings.SheltersIndoors(m_Utility, m_Utility.m_SectorThreatFilter.GetSectorPos(m_iCurrentSector)))
   {
    // As vanilla's own "stay put" branch: safe here, observe the sector.
    m_bReachedSafety = true;
    m_ParentBehavior.OnMovementCompleted(m_State.IsInValidCover());
   }
  }
  super.Update();
 }
}
