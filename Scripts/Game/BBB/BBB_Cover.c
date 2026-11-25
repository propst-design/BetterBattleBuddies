// Cover that faces the enemy (BBB #27, 1 Nov 2026). Vanilla's m_bInCover says "I reached my
// cover point", not "something solid is between me and the enemy": a man posted on the enemy
// side of a sandbag wall counted as covered, so he stood there, hid there when pinned, and
// ExposedFire, Engagement and the Think panel all believed it. BBB now checks the flag against
// the soldier's own known threat (his current target's last seen position, never a live enemy
// position): a short ray at hidden-torso height towards it must hit solid geometry within
// REACH. No known threat, or a man under a roof (BBB_Buildings sets the flag itself indoors):
// vanilla's word stands.
// Behaviour: a man flagged in cover but exposed is handled by vanilla's own "cover is useless"
// branch (run backwards from the threat to native cover within 20 m that faces it, i.e. the
// friendly side of the wall); if that finds nothing, BBB releases the false cover so vanilla
// treats him as in the open (goes low, fights back, moves on its usual timer).
// Yields to: player/GM orders (BBB_OrderIntent), a fire and manoeuvre job, claimed men.
// Cost: one cached ray per soldier at most every RECHECK_MS, only while he is flagged in cover
// (fidelity tiers: up to 5 rays every 250 ms, BBB_Fidelity).
// Host option: -bbbTrueCover 0 (off: vanilla's flag is trusted as before).
// What counts as cover is BBB_SolidCover's rule (glass, nets and greenhouses don't, 19 Nov 2026).
class BBB_Cover
{
 static bool s_bOn = true;
 protected static int s_iRead;
 static const float REACH = 3;          // cover must be this close to the man along the threat line (m)
 static const float TORSO = 0.6;        // hidden torso height crouched or standing (m)
 static const float TORSO_PRONE = 0.3;  // hidden torso height prone (m)
 static const float THREAT_HEIGHT = 1;  // aim point above the threat's last seen ground (m)
 static const float SHOULDERS = 0.5;    // width the extra rays spread over (m)
 static const float RECHECK_MS = 1000;  // cached verdict lifetime
 static const float RELEASE_MS = 8000;  // still in false cover this soon after leaving it: release it
 static int s_iExposed, s_iLeft, s_iReleased; // tests read these

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  if (BBB_Settings.Get("bbbTrueCover", value)) s_bOn = value == "1";
  PrintFormat("[BBB] TRUE COVER on=%1", s_bOn);
 }

 static bool Geometry(notnull IEntity entity) { return ChimeraCharacter.Cast(entity) == null; }

 // Does something solid stand between this spot and the threat, within REACH of the man? As many
 // rays as the fidelity tier gives (BBB_Fidelity.COVER_RAYS).
 static bool Shields(vector position, ECharacterStance stance, vector threat)
 {
  return Shielded(position, stance, threat, BBB_Fidelity.I(BBB_Fidelity.COVER_RAYS));
 }

 // One ray (Standard): the torso's centre. More (High 3, Uncapped 5): one through the middle of each
 // equal strip of SHOULDERS, square to the threat line, and most must hit cover: more than half his
 // shoulders shielded, so a man behind a post that hides his middle but not his shoulders isn't
 // counted as covered (exposure-aware, BBB #24). More rays: a closer estimate of the same rule.
 static bool Shielded(vector position, ECharacterStance stance, vector threat, int rays)
 {
  float height = TORSO;
  if (stance == ECharacterStance.PRONE) height = TORSO_PRONE;
  vector from = position + Vector(0, height, 0);
  vector to = BBB.Ground(threat) + Vector(0, THREAT_HEIGHT, 0);
  vector line = to - from;
  float length = line.Length();
  if (length < 0.5) return false;
  if (length > REACH) to = from + line * (REACH / length);
  if (rays <= 1) return BBB_SolidCover.Ray(from, to) < 1;
  vector side = Vector(line[2], 0, -line[0]);
  if (side.Length() < 0.01) side = Vector(1, 0, 0);
  side.Normalize();
  int hits;
  for (int i = 0; i < rays; i++)
  {
   float offset = SHOULDERS * ((i + 0.5) / rays - 0.5); // the middle of each of rays equal strips
   if (BBB_SolidCover.Ray(from + side * offset, to + side * offset) < 1) hits++;
  }
  return hits * 2 > rays;
 }

 // The soldier's own known threat: his current target's last seen position, or when shot at
 // by an unseen enemy, his major threat sector's estimated position.
 static bool ThreatOf(SCR_AIUtilityComponent utility, out vector threat)
 {
  if (!utility || !utility.m_CombatComponent) return false;
  BaseTarget target = utility.m_CombatComponent.GetCurrentTarget();
  if (target)
  {
   threat = target.GetLastSeenPosition();
   if (threat != vector.Zero) return true;
  }
  if (!utility.m_SectorThreatFilter) return false;
  int major, minor;
  utility.m_SectorThreatFilter.GetActiveSectors(major, minor);
  if (!BBB_Combat.ValidSector(major) || !utility.m_SectorThreatFilter.IsSectorActive(major)) return false;
  int flags = utility.m_SectorThreatFilter.GetSectorFlags(major);
  if ((flags & (SCR_EAIThreatSectorFlags.DIRECTED_AT_ME | SCR_EAIThreatSectorFlags.CAUSED_DAMAGE)) == 0) return false;
  threat = utility.m_SectorThreatFilter.GetSectorPos(major);
  return threat != vector.Zero;
 }

 // Vanilla's in-cover flag, checked against the soldier's known threat. Every BBB reader of
 // "is he in cover" uses this instead of m_bInCover.
 static bool InCover(SCR_AIUtilityComponent utility)
 {
  if (!utility || !utility.m_CombatMoveState || !utility.m_CombatMoveState.m_bInCover) return false;
  vector threat;
  if (!ThreatOf(utility, threat)) return true;
  return Faces(utility, threat);
 }

 // Flagged in cover and the cover is really between him and this threat (or BBB can't judge).
 static bool Faces(SCR_AIUtilityComponent utility, vector threat)
 {
  Read();
  if (!utility || !utility.m_CombatMoveState || !utility.m_CombatMoveState.m_bInCover) return false;
  if (!s_bOn || threat == vector.Zero) return true;
  ChimeraCharacter body = ChimeraCharacter.Cast(utility.m_OwnerEntity);
  if (!body || !body.GetCharacterController()) return true;
  SCR_AICombatMoveState state = utility.m_CombatMoveState;
  float now = GetGame().GetWorld().GetWorldTime();
  vector here = body.GetOrigin();
  if (now < state.m_fBBBCoverUntil && vector.Distance(here, state.m_vBBBCoverAt) < 0.5 && vector.Distance(threat, state.m_vBBBCoverThreat) < 3)
   return state.m_bBBBCoverFaces;
  int started;
  if (BBB_Perf.s_bOn) started = System.GetTickCount();
  bool faces = BBB_Buildings.BuildingAt(utility) != null || Shields(here, body.GetCharacterController().GetStance(), threat);
  if (BBB_Perf.s_bOn) BBB_Perf.Add("ev.trueCover", System.GetTickCount() - started);
  state.m_fBBBCoverUntil = now + BBB_Fidelity.F(BBB_Fidelity.COVER_RECHECK_MS);
  state.m_vBBBCoverAt = here;
  state.m_vBBBCoverThreat = threat;
  if (!faces && state.m_bBBBCoverFaces) s_iExposed++;
  state.m_bBBBCoverFaces = faces;
  if (!faces) BBB_Think.NoteUtility(utility, "TrueCover", "his cover isn't between him and the enemy");
  return faces;
 }

 // Combat-move decision for a man flagged in cover: true = leave it now (vanilla's "cover is
 // useless" move). A second false verdict soon after leaving releases the cover instead, so
 // vanilla handles him as in the open rather than re-asking every update.
 static bool Leave(SCR_AIUtilityComponent utility, SCR_AICombatMoveState state, vector threat)
 {
  if (!utility || !state || !state.m_bInCover || state.IsExecutingRequest()) return false;
  if (Faces(utility, threat)) return false;
  if (utility.m_BBBFireJob || BBB_Claims.Committed(utility.m_OwnerEntity) || BBB_OrderIntent.Blocks(utility)) return false;
  float now = GetGame().GetWorld().GetWorldTime();
  if (state.m_fBBBCoverLeftAt > 0 && now - state.m_fBBBCoverLeftAt < RELEASE_MS)
  {
   state.ReleaseCover();
   state.m_fBBBCoverLeftAt = 0;
   s_iReleased++;
   BBB_Think.NoteUtility(utility, "TrueCover", "no cover facing the enemy nearby: fights from the open");
   return false;
  }
  state.m_fBBBCoverLeftAt = now;
  s_iLeft++;
  BBB_Think.NoteUtility(utility, "TrueCover", "moves to cover that faces the enemy");
  return true;
 }
}

// Glass isn't cover (Jacob, 19 Nov 2026). BBB's cover rays traced the walk-collision layer, where a
// vanilla greenhouse is a shell of 4 cm boxes authored as steel: a man beside one counted as covered,
// stayed there when pinned, and vanilla kept picking the cover points around it. Cover now means
// "would stop a round": rays trace what bullets hit (the Projectile layer) and pass through a hit
// that wouldn't stop one:
// - glass: a collider named *glass* (vanilla's window panes and greenhouse panels; armoured glass
//   excepted) or *foil*;
// - near-weightless material (density under FLIMSY_DENSITY: nets, mesh, camo netting);
// - a prefab-name backstop for structures whose frame is authored as solid: greenhouses,
//   glasshouses, hothouses, polytunnels (vanilla Greenhouse_01's frame mesh is "steel").
// An object with no Projectile-layer geometry (some wire-mesh fences) lets rounds through, so it
// isn't cover. Measured on Arland (BBB_GlassCoverTest): sandbags, concrete, tin and wooden fences
// stay cover; window glass, greenhouses and net fences don't.
// The same rule filters vanilla's cover-point picks (SCR_AIFindCover, below), BBB's firing positions
// (BBB_FiringCover) and covering-fire sight lines (BBB_CoverFire), and a greenhouse roof doesn't put
// a man "in a building" (BBB_Buildings.RoofAt). Concealment follows: what a round passes through
// here, eyes do too, so nothing BBB treats as hiding a man is see-through.
// Cost: a passed-through hit costs one more ray (at most PIERCES); a cover-point pick one walk ray
// and one cover ray per candidate, at most TRIES candidates. Off with -bbbTrueCover 0.
class BBB_SolidCover
{
 static const float FLIMSY_DENSITY = 0.1; // g/cm3: nets 0.001; wood 0.65, sandbag 1.63, glass 2.6, steel 7.87
 static const int PIERCES = 3;            // see-through hits one ray passes (more: not cover)
 static const float POINT_REACH = 1.5;    // a cover point's geometry is looked for this far (m), or just
 static const float POINT_REACH_MAX = 4;  // past its tallest point, up to this
 static const int TRIES = 4;              // cover-point candidates tried per pick
 static int s_iPierced, s_iRejected;      // tests read these

 static bool Geometry(notnull IEntity entity) { return ChimeraCharacter.Cast(entity) == null; }

 // Prefab-name backstop (any map, any mod that names its prefab plainly).
 static bool FlimsyName(string name)
 {
  name.ToLower();
  name.Replace("_", "");
  name.Replace(" ", "");
  return name.Contains("greenhouse") || name.Contains("glasshouse") || name.Contains("hothouse") || name.Contains("polytunnel") || name.Contains("foiltunnel");
 }

 // A greenhouse or the like (the entity or the structure it belongs to).
 static bool Flimsy(IEntity entity)
 {
  if (!entity) return false;
  EntityPrefabData data = entity.GetPrefabData();
  if (data && FlimsyName(data.GetPrefabName())) return true;
  IEntity root = entity.GetRootParent();
  if (!root || root == entity) return false;
  data = root.GetPrefabData();
  return data && FlimsyName(data.GetPrefabName());
 }

 // Would the surface this trace hit stop a round?
 static bool Stops(TraceParam trace)
 {
  string collider = trace.ColliderName;
  collider.ToLower();
  if (collider.Contains("glass") && !collider.Contains("armor") && !collider.Contains("armour")) return false;
  if (collider.Contains("foil")) return false;
  GameMaterial material = trace.SurfaceProps;
  if (material)
  {
   BallisticInfo ballistic = material.GetBallisticInfo();
   if (ballistic && ballistic.GetDensity() < FLIMSY_DENSITY) return false;
  }
  return !Flimsy(trace.TraceEnt);
 }

 // How far along from -> to (0..1) the first thing that stops a round is; 1: nothing does.
 static float Ray(vector from, vector to)
 {
  vector line = to - from;
  float length = line.Length();
  if (length < 0.01) return 1;
  vector dir = line * (1 / length);
  float done = 0;
  BaseWorld world = GetGame().GetWorld();
  for (int i = 0; i <= PIERCES; i++)
  {
   TraceParam trace = new TraceParam();
   trace.Start = from + dir * done;
   trace.End = to;
   trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
   trace.LayerMask = EPhysicsLayerPresets.Projectile;
   float fraction = world.TraceMove(trace, BBB_SolidCover.Geometry);
   if (fraction >= 1) return 1;
   float hit = done + (length - done) * fraction;
   if (Stops(trace)) return hit / length;
   s_iPierced++;
   done = hit + 0.05;
   if (done >= length) return 1;
  }
  return 1;
 }

 // Is the geometry a vanilla cover point leans on solid? A cover point's direction runs from where
 // the man stands to the top of what hides him (tallest), sometimes 3 m off. Nothing physical found there: BBB can't
 // judge, and the point stands.
 static bool PointSolid(vector position, vector tallest, vector threat)
 {
  vector dir = tallest - position;
  dir[1] = 0;
  float reach = Math.Clamp(dir.Length() + 0.5, POINT_REACH, POINT_REACH_MAX);
  if (dir.Length() < 0.05) { dir = threat - position; dir[1] = 0; }
  if (dir.Length() < 0.05) return true;
  dir.Normalize();
  float height = Math.Clamp((tallest[1] - position[1]) * 0.5, 0.25, BBB_Cover.TORSO);
  vector from = position + Vector(0, height, 0);
  vector to = from + dir * reach;
  TraceParam trace = new TraceParam();
  trace.Start = from;
  trace.End = to;
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  if (GetGame().GetWorld().TraceMove(trace, BBB_SolidCover.Geometry) >= 1) return true;
  if (Ray(from, to) < 1) return true;
  s_iRejected++;
  return false;
 }

 // The cover manager's best cover, passing over points whose cover is glass or the like (each held
 // occupied while the next is asked for, then freed).
 static bool BestCover(notnull ChimeraCoverManagerComponent manager, notnull AIPathfindingComponent path, notnull CoverQueryProperties props, out vector position, out vector tallest, out int tileX, out int tileY, out int coverId)
 {
  BBB_Cover.Read();
  array<ref SCR_AICoverLock> skipped = {};
  bool found;
  for (int attempt = 0; attempt < TRIES; attempt++)
  {
   if (!manager.GetBestCover("Soldiers", path, props, position, tallest, tileX, tileY, coverId)) break;
   if (!BBB_Cover.s_bOn || PointSolid(position, tallest, props.m_vThreatPos)) { found = true; break; }
   skipped.Insert(new SCR_AICoverLock(tileX, tileY, coverId, position, tallest));
  }
  foreach (SCR_AICoverLock lock : skipped) lock.Release();
  return found;
 }
}

// Vanilla's one cover pick (combat moves, defend, retreat): the same query through BestCover, so
// glass cover points are passed over. Otherwise vanilla's node as it is (1.8).
modded class SCR_AIFindCover
{
 override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
 {
  IEntity ownerEntity = owner.GetControlledEntity();
  if (!ownerEntity || !m_State || !m_CoverMgr || !m_PathfindingComp) return ENodeResult.FAIL;
  CoverQueryProperties queryProps;
  GetVariableIn(PORT_COVER_QUERY_PROPERTIES, queryProps);
  if (!queryProps) return ENodeResult.FAIL;
  vector coverPos, coverTallestPos;
  int tileX, tileY, coverId;
  if (!BBB_SolidCover.BestCover(m_CoverMgr, m_PathfindingComp, queryProps, coverPos, coverTallestPos, tileX, tileY, coverId))
  {
   // As vanilla: the previous cover stays held when no new one is found.
   ClearVariable(PORT_COVER_LOCK);
   return ENodeResult.FAIL;
  }
  m_State.AssignCover(new SCR_AICoverLock(tileX, tileY, coverId, coverPos, coverTallestPos));
  SetVariableOut(PORT_COVER_LOCK, m_State.GetAssignedCover());
  return ENodeResult.SUCCESS;
 }
}

modded class SCR_AICombatMoveState
{
 float m_fBBBCoverUntil, m_fBBBCoverLeftAt;
 vector m_vBBBCoverAt, m_vBBBCoverThreat;
 bool m_bBBBCoverFaces = true;
}

// Attack and suppressive fire. Vanilla checks "pinned in cover: hide" before "cover is useless":
// a pinned man in false cover must not hide there, so both see the checked flag.
modded class SCR_AICombatMoveLogicBase
{
 protected override bool SuppressedInCoverCondition()
 {
  return super.SuppressedInCoverCondition() && BBB_Cover.Faces(m_Utility, GetTargetPosition());
 }

 protected override bool CurrentCoverUselessCondition()
 {
  return super.CurrentCoverUselessCondition() || BBB_Cover.Leave(m_Utility, m_State, GetTargetPosition());
 }
}
