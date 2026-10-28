// Tactical suppression geometry, recovery tuning, and 343 m/s acoustic explosion
// reflex (16 Oct 2026; ported from bCombat lib/suppression.sqf, LAMBS_Suppression
// CfgBrains, and LAMBS_eventhandlers fnc_delayExplosionEH / fnc_explosionEH).
//
// Vanilla SCR_AIThreatSystem & SCR_AIDangerReaction_Explosion:
//  1. Decays m_fThreatSuppression by 10%/s (0.0001/ms), so a pinned soldier
//     drops below THREATENED (0.66) in under 2 s after fire pauses.
//  2. Treats every bullet flyby identically (0.10 per round) regardless of
//     whether the shooter holds high ground, fires into an exposed flank/rear,
//     or springs an ambush on a calm / relaxed-marching squad.
//  3. Caps close explosion suppression at 0.20 (below ALERTED 0.33) and does not
//     force standing infantry onto the ground when a shockwave arrives.
//
// BBB_Threat:
//  - Slows suppression decay (default x0.55 rate, ~1.8x longer suppression hold).
//  - Scales flyby suppression by shooter elevation advantage (deltaY / dist,
//    up to 2.0x from high ground, down to 0.65x when firing uphill at high ground).
//  - Scales flyby suppression by relative bearing (up to +25% at 90 deg flank,
//    +50% at 180 deg rear).
//  - Adds a one-time ambush shock (+0.20, 45 s cooldown) when direct fire hits
//    a calm (SAFE, no target) or relaxed-marching soldier.
//  - Scales explosion suppression impulse (default x2.2, so a close blast within
//    12 m adds 0.44 suppression -> immediate ALERTED).
//  - 343 m/s acoustic explosion reflex (-bbbExplosionReflex 0 to disable):
//    when a hostile/unaffiliated explosion occurs within 75 m, foot soldiers
//    react after physical sound propagation delay (0.16 s + dist / 343 m/s + jitter),
//    diving PRONE within 45 m or ducking to CROUCH at 45-75 m for 3.5-6.0 s.
//    Sheltered indoor units (>12 m with roof overhead and no blast LOS) and
//    units under explicit player/GM movement orders are excluded.
//
// Host options:
//  -bbbThreat 0               all BBB_Threat adjustments off (vanilla)
//  -bbbSuppressionDecay <x>   suppression decay rate factor (default 0.55; 1 = vanilla)
//  -bbbExplosionThreat <x>    explosion suppression factor (default 2.2; 1 = vanilla)
//  -bbbExplosionReflex 0      disable 343 m/s acoustic explosion stance reflex
//  -bbbElevationThreat <x>    elevation slope factor (default 1.0; 0 = off)
//  -bbbFlankThreat <x>        max rear-flank bonus (default 0.50; 0 = off)
//  -bbbAmbushThreat <x>       opening ambush shock bonus (default 0.20; 0 = off)
class BBB_ExplosionReflexJob : Managed
{
	IEntity m_Soldier;
	SCR_AIUtilityComponent m_Utility;
	ref SCR_AICharacterStanceSetting m_StanceSetting;
	ECharacterStance m_eTargetStance;
	float m_fFireAtMs;
	float m_fEndMs;
	float m_fDistance;
	bool m_bApplied;
}

class BBB_Threat
{
	static int s_iRead;
	static bool s_bOn = true;
	static bool s_bGeometry = true;
	static bool s_bExplosionReflex = true;
	static float s_fDecayScale = 0.55;
	static float s_fExplosionScale = 2.2;
	static float s_fElevationScale = 1.0;
	static float s_fFlankBonus = 0.50;
	static float s_fAmbushBonus = 0.20;

	static const float VANILLA_SUPPRESSION_DROP_MS = 0.0001;
	static const float VANILLA_FLYBY_INCREMENT = 0.10;
	static const float ELEVATION_MIN_MULT = 0.65;
	static const float ELEVATION_MAX_MULT = 2.0;
	static const float AMBUSH_MAX_SUPPRESSION = 0.05;
	static const float AMBUSH_COOLDOWN_S = 45.0;
	static const float MIN_GEOMETRY_DIST_M = 5.0;
	static const float MAX_STRUCTURE_AGL_M = 35.0;

	// Acoustic explosion reflex constants (LAMBS_eventhandlers: 343 m/s + 0.16 s human reaction)
	static const float SOUND_SPEED_MS = 343.0;
	static const float REFLEX_BASE_DELAY_S = 0.16;
	static const float REFLEX_MAX_JITTER_S = 0.30;
	static const float REFLEX_PRONE_RADIUS_M = 45.0;
	static const float REFLEX_CROUCH_RADIUS_M = 75.0;
	static const float REFLEX_CLOSE_BLAST_M = 12.0;
	static const float REFLEX_MIN_HOLD_S = 3.5;
	static const float REFLEX_MAX_HOLD_S = 6.0;
	static const int REFLEX_WATCH_MS = 100;

	static int s_iFlybysScaled;
	static int s_iAmbushShocks;
	static int s_iExplosionsScaled;
	static int s_iExplosionReflexScheduled;
	static int s_iExplosionReflexApplied;

	protected static ref array<ref BBB_ExplosionReflexJob> s_aReflexJobs = {};
	protected static bool s_bReflexWatching;

	static void Read()
	{
		if (!BBB_Settings.Due(s_iRead)) return;
		string value;
		s_bOn = !(BBB_Settings.Get("bbbThreat", value) && value == "0");
		s_bGeometry = !(BBB_Settings.Get("bbbThreatGeometry", value) && value == "0");

		if (BBB_Settings.Get("bbbThreatDecay", value))
		{
			if (value == "0") s_fDecayScale = 1.0;
			else s_fDecayScale = Math.Clamp(value.ToFloat(), 0.05, 5.0);
		}
		if (BBB_Settings.Get("bbbSuppressionDecay", value))
			s_fDecayScale = Math.Clamp(value.ToFloat(), 0.05, 5.0);

		s_bExplosionReflex = !(BBB_Settings.Get("bbbExplosionReflex", value) && value == "0");
		// Legacy -bbbThreatExplosion 0 also turns the reflex off.
		if (BBB_Settings.Get("bbbThreatExplosion", value))
		{
			if (value == "0")
			{
				s_bExplosionReflex = false;
				s_fExplosionScale = 1.0;
			}
			else
			{
				s_fExplosionScale = Math.Clamp(value.ToFloat(), 0.1, 5.0);
			}
		}
		if (BBB_Settings.Get("bbbExplosionThreat", value)) s_fExplosionScale = Math.Clamp(value.ToFloat(), 0.1, 5.0);
		if (BBB_Settings.Get("bbbElevationThreat", value)) s_fElevationScale = Math.Clamp(value.ToFloat(), 0.0, 3.0);
		if (BBB_Settings.Get("bbbFlankThreat", value)) s_fFlankBonus = Math.Clamp(value.ToFloat(), 0.0, 2.0);
		if (BBB_Settings.Get("bbbAmbushThreat", value)) s_fAmbushBonus = Math.Clamp(value.ToFloat(), 0.0, 1.0);
		PrintFormat("[BBB] THREAT on=%1 geom=%2 decay=%3 explosion=%4 reflex=%5 elevation=%6 flank=%7 ambush=%8", s_bOn, s_bGeometry, s_fDecayScale, s_fExplosionScale, s_bExplosionReflex, s_fElevationScale, s_fFlankBonus, s_fAmbushBonus);
	}

	static bool On(IEntity soldier = null)
	{
		Read();
		if (!s_bOn) return false;
		if (soldier && BBB_Aim.s_aVanilla && BBB_Aim.s_aVanilla.Contains(soldier)) return false;
		return true;
	}

	static bool ReflexOn(IEntity soldier = null)
	{
		return On(soldier) && s_bExplosionReflex;
	}

	static bool GroundOrRoofSource(vector shooterPos)
	{
		if (!BBB_SuppressionSafety.s_bOn) return true;
		BaseWorld world = GetGame().GetWorld();
		if (!world) return true;
		float surfaceY = world.GetSurfaceY(shooterPos[0], shooterPos[2]);
		return shooterPos[1] - surfaceY <= MAX_STRUCTURE_AGL_M;
	}

	// Elevation multiplier (bCombat bcombat_allow_lowerground_penalty):
	// shooter above receiver (deltaY > 0) increases suppression up to 2.0x;
	// receiver on high ground (deltaY < 0) reduces suppression down to 0.65x.
	static float ElevationMultiplier(vector receiverPos, vector shooterPos)
	{
		if (!s_bGeometry || s_fElevationScale <= 0) return 1.0;
		float dist = vector.Distance(receiverPos, shooterPos);
		if (dist < MIN_GEOMETRY_DIST_M) return 1.0;
		float deltaY = shooterPos[1] - receiverPos[1];
		float bonus = (deltaY / dist) * s_fElevationScale;
		return Math.Clamp(1.0 + bonus, ELEVATION_MIN_MULT, ELEVATION_MAX_MULT);
	}

	// Flanking multiplier (bCombat bcombat_penalty_flanking):
	// 1.0 dead ahead (0 deg), 1.0 + 0.5 * s_fFlankBonus at 90 deg flank,
	// 1.0 + s_fFlankBonus at 180 deg rear.
	static float FlankMultiplier(vector receiverPos, vector receiverForward, vector shooterPos)
	{
		if (!s_bGeometry || s_fFlankBonus <= 0) return 1.0;
		vector toShooter = shooterPos - receiverPos;
		toShooter[1] = 0;
		float lenSq = toShooter.LengthSq();
		if (lenSq < MIN_GEOMETRY_DIST_M * MIN_GEOMETRY_DIST_M) return 1.0;
		toShooter.Normalize();
		vector fwd = receiverForward;
		fwd[1] = 0;
		if (fwd.LengthSq() <= 0.0001) return 1.0;
		fwd.Normalize();
		float dot = Math.Clamp(vector.Dot(fwd, toShooter), -1.0, 1.0);
		float offAxis = (1.0 - dot) * 0.5;
		return 1.0 + s_fFlankBonus * offAxis;
	}

	// Pure calculation of resulting suppression after a flyby event, used both live and in tests.
	static float ComputeFlybySuppression(float suppressionBefore, int count, vector receiverPos, vector receiverForward, vector shooterPos, bool ambushed)
	{
		if (count <= 0) return Math.Clamp(suppressionBefore, 0, 1);
		float baseImpulse = count * VANILLA_FLYBY_INCREMENT;
		if (!s_bOn) return Math.Clamp(suppressionBefore + baseImpulse, 0, 1);
		float elevMult = ElevationMultiplier(receiverPos, shooterPos);
		float flankMult = FlankMultiplier(receiverPos, receiverForward, shooterPos);
		float impulse = baseImpulse * elevMult * flankMult;
		if (s_bGeometry && ambushed && s_fAmbushBonus > 0) impulse += s_fAmbushBonus;
		return Math.Clamp(suppressionBefore + impulse, 0, 1);
	}

	// Pure calculation of suppression after a timeSlice (ms) decay step.
	static float ComputeDecayedSuppression(float suppressionBefore, float timeSliceMs)
	{
		if (suppressionBefore <= 0 || timeSliceMs <= 0) return Math.Clamp(suppressionBefore, 0, 1);
		float rate = VANILLA_SUPPRESSION_DROP_MS;
		if (s_bOn) rate = rate * s_fDecayScale;
		float drop = Math.Clamp(rate * timeSliceMs, 0, 1);
		return Math.Clamp(suppressionBefore * (1.0 - drop), 0, 1);
	}

	// Acoustic shockwave propagation delay in ms (LAMBS_eventhandlers):
	// t_ms = (0.16 + dist / 343.0 + jitter) * 1000.
	static float ComputeExplosionDelayMs(float distanceM, float jitter01 = 0.0)
	{
		float d = Math.Max(0.0, distanceM);
		float j = Math.Clamp(jitter01, 0.0, 1.0) * REFLEX_MAX_JITTER_S;
		return (REFLEX_BASE_DELAY_S + (d / SOUND_SPEED_MS) + j) * 1000.0;
	}

	static float ComputeExplosionHoldMs(float distanceM)
	{
		float t = 1.0 - Math.Clamp(distanceM / REFLEX_CROUCH_RADIUS_M, 0.0, 1.0);
		return (REFLEX_MIN_HOLD_S + (REFLEX_MAX_HOLD_S - REFLEX_MIN_HOLD_S) * t) * 1000.0;
	}

	static void OnFlyby(notnull SCR_AIUtilityComponent utility, int count, vector shooterPos, EAIThreatState stateBefore, float suppressionBefore)
	{
		IEntity soldier = utility.m_OwnerEntity;
		if (!soldier || !On(soldier) || !utility.m_ThreatSystem || count <= 0) return;
		if (BBB.PlayerOf(soldier) != 0 || !BBB.IsAlive(soldier)) return;
		if (!GroundOrRoofSource(shooterPos)) return;

		float now = GetGame().GetWorld().GetWorldTime() * 0.001;
		bool hasTarget = utility.m_CombatComponent && utility.m_CombatComponent.GetCurrentTarget() != null;
		bool calmOrMarching = utility.m_bBBBMarchLowered || (stateBefore == EAIThreatState.SAFE && !hasTarget && suppressionBefore < AMBUSH_MAX_SUPPRESSION);
		bool ambushed = calmOrMarching && now >= utility.m_fBBBAmbushShockReady;
		if (ambushed)
		{
			utility.m_fBBBAmbushShockReady = now + AMBUSH_COOLDOWN_S;
			s_iAmbushShocks++;
			BBB_Perf.Count("threat.ambushShock");
		}

		vector myPos = soldier.GetOrigin();
		vector myFwd = soldier.GetTransformAxis(2);
		float targetSup = ComputeFlybySuppression(suppressionBefore, count, myPos, myFwd, shooterPos, ambushed);
		float curSup, shots, injury, endangered;
		utility.m_ThreatSystem.GetThreatValues(curSup, shots, injury, endangered);
		if (Math.AbsFloat(targetSup - curSup) > 0.0001)
		{
			utility.m_ThreatSystem.SetThreatValues(targetSup, shots, injury, endangered);
			s_iFlybysScaled++;
			BBB_Perf.Count("threat.flybyScaled");
		}
	}

	// ---------------------------------------------------------------- 343 m/s acoustic explosion reflex

	static bool EligibleForExplosionReflex(SCR_AIUtilityComponent utility, IEntity soldier)
	{
		if (!utility || !soldier || !ReflexOn(soldier)) return false;
		ChimeraCharacter character = ChimeraCharacter.Cast(soldier);
		if (!character || character.IsInVehicle()) return false;
		SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
		if (!controller || controller.IsDead() || controller.IsUnconscious() || controller.IsPlayerControlled() || BBB.PlayerOf(soldier) != 0) return false;
		if (controller.IsClimbing() || controller.IsSwimming() || controller.IsFalling()) return false;
		if (utility.m_AIInfo && (utility.m_AIInfo.HasUnitState(EUnitState.IN_TURRET) || utility.m_AIInfo.HasUnitState(EUnitState.PILOT))) return false;
		if (BBB_Drag.Busy(soldier)) return false;
		if (BBB_OrderIntent.Blocks(utility)) return false;
		return true;
	}

	// True when a roof or upper floor sits within 6 m above the soldier's head.
	static bool UnderRoof(IEntity soldier)
	{
		if (!soldier || !GetGame() || !GetGame().GetWorld()) return false;
		TraceParam trace = new TraceParam();
		trace.Start = soldier.GetOrigin() + Vector(0, 1.5, 0);
		trace.End = trace.Start + Vector(0, 6.0, 0);
		trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		trace.LayerMask = EPhysicsLayerPresets.Projectile;
		trace.Exclude = soldier;
		return GetGame().GetWorld().TraceMove(trace, null) < 1.0;
	}

	static bool ShelteredIndoors(IEntity soldier, vector explosionPos, float distance)
	{
		if (distance <= REFLEX_CLOSE_BLAST_M) return false;
		if (!UnderRoof(soldier)) return false;
		return !BBB_Medic.ClearLine(explosionPos, soldier.GetOrigin());
	}

	static void OnExplosionEvent(notnull SCR_AIUtilityComponent utility, vector explosionPos, float fixedJitter = -1.0)
	{
		IEntity soldier = utility.m_OwnerEntity;
		if (!EligibleForExplosionReflex(utility, soldier)) return;
		float distance = vector.Distance(soldier.GetOrigin(), explosionPos);
		if (distance > REFLEX_CROUCH_RADIUS_M) return;
		if (ShelteredIndoors(soldier, explosionPos, distance)) return;

		ECharacterStance desired = ECharacterStance.CROUCH;
		if (distance <= REFLEX_PRONE_RADIUS_M) desired = ECharacterStance.PRONE;

		float jitter = fixedJitter;
		if (jitter < 0) jitter = Math.RandomFloat01();
		float delayMs = ComputeExplosionDelayMs(distance, jitter);
		float holdMs = ComputeExplosionHoldMs(distance);
		float nowMs = GetGame().GetWorld().GetWorldTime();

		// Update existing job if a closer/newer blast arrives for the same soldier.
		foreach (BBB_ExplosionReflexJob existing : s_aReflexJobs)
		{
			if (existing.m_Soldier != soldier) continue;
			if (desired == ECharacterStance.PRONE) existing.m_eTargetStance = ECharacterStance.PRONE;
			if (!existing.m_bApplied && nowMs + delayMs < existing.m_fFireAtMs)
				existing.m_fFireAtMs = nowMs + delayMs;
			existing.m_fEndMs = Math.Max(existing.m_fEndMs, existing.m_fFireAtMs + holdMs);
			existing.m_fDistance = Math.Min(existing.m_fDistance, distance);
			return;
		}

		BBB_ExplosionReflexJob job = new BBB_ExplosionReflexJob();
		job.m_Soldier = soldier;
		job.m_Utility = utility;
		job.m_eTargetStance = desired;
		job.m_fFireAtMs = nowMs + delayMs;
		job.m_fEndMs = job.m_fFireAtMs + holdMs;
		job.m_fDistance = distance;
		s_aReflexJobs.Insert(job);
		s_iExplosionReflexScheduled++;
		BBB_Perf.Count("threat.explosionReflexScheduled");
		StartReflexWatch();
	}

	static bool InExplosionReflex(IEntity soldier)
	{
		if (!soldier) return false;
		foreach (BBB_ExplosionReflexJob job : s_aReflexJobs)
			if (job && job.m_Soldier == soldier && job.m_bApplied) return true;
		return false;
	}

	protected static void StartReflexWatch()
	{
		if (s_bReflexWatching || !GetGame()) return;
		s_bReflexWatching = true;
		GetGame().GetCallqueue().Remove(TickReflex);
		GetGame().GetCallqueue().CallLater(TickReflex, REFLEX_WATCH_MS, true);
	}

	static void TickReflex()
	{
		if (!s_bReflexWatching)
		{
			if (GetGame()) GetGame().GetCallqueue().Remove(TickReflex);
			return;
		}
		if (!GetGame() || !GetGame().GetWorld()) return;
		float nowMs = GetGame().GetWorld().GetWorldTime();
		StepReflexJobs(nowMs);
		if (s_aReflexJobs.IsEmpty())
		{
			s_bReflexWatching = false;
			GetGame().GetCallqueue().Remove(TickReflex);
		}
	}

	static void StepReflexJobs(float nowMs)
	{
		for (int i = s_aReflexJobs.Count() - 1; i >= 0; i--)
		{
			BBB_ExplosionReflexJob job = s_aReflexJobs[i];
			if (!job || !EligibleForExplosionReflex(job.m_Utility, job.m_Soldier) || nowMs >= job.m_fEndMs)
			{
				ReleaseJob(job);
				s_aReflexJobs.RemoveOrdered(i);
				continue;
			}
			if (!job.m_bApplied && nowMs >= job.m_fFireAtMs)
				ApplyJob(job);
		}
	}

	protected static void ApplyJob(notnull BBB_ExplosionReflexJob job)
	{
		job.m_bApplied = true;
		ChimeraCharacter character = ChimeraCharacter.Cast(job.m_Soldier);
		if (!character) return;
		SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
		if (!controller) return;
		ECharacterStance current = controller.GetStance();
		ECharacterStance target = job.m_eTargetStance;
		if (current == ECharacterStance.PRONE) target = ECharacterStance.PRONE;
		SCR_AICharacterSettingsComponent settings = job.m_Utility.m_SettingsComponent;
		if (settings)
		{
			job.m_StanceSetting = SCR_AICharacterStanceSetting.Create(SCR_EAISettingOrigin.BEHAVIOR, SCR_EAIBehaviorCause.ALWAYS, target);
			settings.AddCharacterSetting(job.m_StanceSetting, false);
		}
		SCR_AIStanceHandling.SetStance(controller, target);
		s_iExplosionReflexApplied++;
		BBB_Perf.Count("threat.explosionReflexApplied");
	}

	protected static void ReleaseJob(BBB_ExplosionReflexJob job)
	{
		if (!job) return;
		if (job.m_StanceSetting && job.m_Utility && job.m_Utility.m_SettingsComponent)
			job.m_Utility.m_SettingsComponent.RemoveSetting(job.m_StanceSetting);
		job.m_StanceSetting = null;
	}

	static void Stop()
	{
		s_bReflexWatching = false;
		if (GetGame()) GetGame().GetCallqueue().Remove(TickReflex);
		foreach (BBB_ExplosionReflexJob job : s_aReflexJobs)
			ReleaseJob(job);
		s_aReflexJobs.Clear();
		s_iFlybysScaled = 0;
		s_iAmbushShocks = 0;
		s_iExplosionsScaled = 0;
		s_iExplosionReflexScheduled = 0;
		s_iExplosionReflexApplied = 0;
	}
}

modded class SCR_AIUtilityComponent
{
	float m_fBBBAmbushShockReady;
}

modded class SCR_AIThreatSystem
{
	SCR_AIUtilityComponent m_BBBUtility;
	float m_fBBBFlybySupBefore;
	EAIThreatState m_eBBBFlybyStateBefore;
	bool m_bBBBPendingFlyby;
	bool m_bBBBPendingExplosion;

	void SCR_AIThreatSystem(SCR_AIUtilityComponent utility)
	{
		m_BBBUtility = utility;
	}

	override void Update(SCR_AIUtilityComponent utility, float timeSlice)
	{
		m_BBBUtility = utility;
		IEntity soldier = null;
		if (utility) soldier = utility.m_OwnerEntity;
		if (BBB_Threat.On(soldier) && timeSlice > 0 && BBB_Threat.s_fDecayScale != 1.0)
		{
			float supBefore, shots, injury, endangered;
			GetThreatValues(supBefore, shots, injury, endangered);
			if (supBefore > 0)
			{
				float vanillaDrop = Math.Clamp(BBB_Threat.VANILLA_SUPPRESSION_DROP_MS * timeSlice, 0, 0.95);
				float desiredAfter = BBB_Threat.ComputeDecayedSuppression(supBefore, timeSlice);
				float preCompensated = desiredAfter / (1.0 - vanillaDrop);
				SetThreatValues(preCompensated, shots, injury, endangered);
			}
		}
		super.Update(utility, timeSlice);
	}

	override void ThreatProjectileFlyby(int count)
	{
		float supBefore, shots, injury, endangered;
		GetThreatValues(supBefore, shots, injury, endangered);
		m_fBBBFlybySupBefore = supBefore;
		m_eBBBFlybyStateBefore = GetState();
		m_bBBBPendingFlyby = true;
		if (m_BBBUtility && m_BBBUtility.m_SectorThreatFilter)
			m_BBBUtility.m_SectorThreatFilter.m_BBBUtility = m_BBBUtility;
		super.ThreatProjectileFlyby(count);
	}

	override void ThreatExplosion(float distance)
	{
		float supBefore, shots, injury, endangered;
		GetThreatValues(supBefore, shots, injury, endangered);
		if (m_BBBUtility && m_BBBUtility.m_SectorThreatFilter)
			m_BBBUtility.m_SectorThreatFilter.m_BBBUtility = m_BBBUtility;
		m_bBBBPendingExplosion = true;
		super.ThreatExplosion(distance);
		IEntity soldier = null;
		if (m_BBBUtility) soldier = m_BBBUtility.m_OwnerEntity;
		if (!BBB_Threat.On(soldier)) return;
		float supAfter;
		GetThreatValues(supAfter, shots, injury, endangered);
		float delta = supAfter - supBefore;
		if (delta <= 0) return;
		float scaled = Math.Clamp(supBefore + delta * BBB_Threat.s_fExplosionScale, 0, 1);
		SetThreatValues(scaled, shots, injury, endangered);
		BBB_Threat.s_iExplosionsScaled++;
		BBB_Perf.Count("threat.explosionScaled");
	}
}

modded class SCR_AISectorThreatFilter
{
	SCR_AIUtilityComponent m_BBBUtility;

	// Vanilla weighs a new event against a sector's estimated position by the ratio of their
	// distances from the soldier, dividing by the distance to the estimated position: when that sits
	// exactly where he stands (an earlier event there, e.g. a shot from his own vehicle) it threw
	// "Division by zero" (AddNewEventsToSector:436, crash.log 25 Oct). That case moves the estimate
	// 0.1 m off him first, which leaves it where it was.
	override protected void AddNewEventsToSector(int sectorId, vector eventPos, float dangerDeltaPerEvent, int eventCount, float bumpDangerTo, SCR_EAIThreatSectorFlags flagsToAdd = 0)
	{
		if (sectorId >= 0 && sectorId < SECTOR_COUNT)
		{
			SCR_AIThreatSector sector = m_aSectors[sectorId];
			if (sector && sector.m_iState != SCR_AIThreatSector.SECTOR_STATE_IDLE && vector.DistanceSq(GetMyPos(), sector.m_vEstimatedPos) < 0.0001)
				sector.m_vEstimatedPos = sector.m_vEstimatedPos + Vector(0.1, 0, 0);
		}
		super.AddNewEventsToSector(sectorId, eventPos, dangerDeltaPerEvent, eventCount, bumpDangerTo, flagsToAdd);
	}

	override void OnShotsFired(vector eventPos, int count, bool isFlyby)
	{
		super.OnShotsFired(eventPos, count, isFlyby);
		if (!isFlyby || !m_BBBUtility || !m_BBBUtility.m_ThreatSystem) return;
		if (!m_BBBUtility.m_ThreatSystem.m_bBBBPendingFlyby) return;
		m_BBBUtility.m_ThreatSystem.m_bBBBPendingFlyby = false;
		BBB_Threat.OnFlyby(m_BBBUtility, count, eventPos, m_BBBUtility.m_ThreatSystem.m_eBBBFlybyStateBefore, m_BBBUtility.m_ThreatSystem.m_fBBBFlybySupBefore);
	}

	override void OnExplosion(vector eventPos)
	{
		super.OnExplosion(eventPos);
		if (!m_BBBUtility || !m_BBBUtility.m_ThreatSystem) return;
		if (!m_BBBUtility.m_ThreatSystem.m_bBBBPendingExplosion) return;
		m_BBBUtility.m_ThreatSystem.m_bBBBPendingExplosion = false;
		BBB_Threat.OnExplosionEvent(m_BBBUtility, eventPos);
	}
}
