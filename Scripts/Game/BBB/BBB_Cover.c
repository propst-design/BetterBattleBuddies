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
  TraceParam trace = new TraceParam();
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  BaseWorld world = GetGame().GetWorld();
  if (rays <= 1)
  {
   trace.Start = from;
   trace.End = to;
   return world.TraceMove(trace, BBB_Cover.Geometry) < 1;
  }
  vector side = Vector(line[2], 0, -line[0]);
  if (side.Length() < 0.01) side = Vector(1, 0, 0);
  side.Normalize();
  int hits;
  for (int i = 0; i < rays; i++)
  {
   float offset = SHOULDERS * ((i + 0.5) / rays - 0.5); // the middle of each of rays equal strips
   trace.Start = from + side * offset;
   trace.End = to + side * offset;
   if (world.TraceMove(trace, BBB_Cover.Geometry) < 1) hits++;
  }
  return hits * 2 > rays;
 }

 // The soldier's own known threat: his current target's last seen position.
 static bool ThreatOf(SCR_AIUtilityComponent utility, out vector threat)
 {
  if (!utility || !utility.m_CombatComponent) return false;
  BaseTarget target = utility.m_CombatComponent.GetCurrentTarget();
  if (!target) return false;
  threat = target.GetLastSeenPosition();
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
