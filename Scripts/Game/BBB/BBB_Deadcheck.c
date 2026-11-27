// Better Battle Buddies (BBB): post-contact deadchecking and AI prisoner-taking (-bbbDeadcheck).
// After a firefight cools down, an autonomous squad with a nearby unverified downed enemy
// (from BBB_KillConfirm) or an unsecured surrendered enemy (BBB_Morale / ACE Captives) sends
// one soldier to move up, inspect, and resolve it:
//  - Dead: marks the body verified for the faction so no other squad re-checks it.
//  - Gone (dragged away via ACE Carrying or crawled off): clears the spot and returns.
//  - Surrendered: disarms and secures them (zip-cuffing via ACE Captives when loaded, or
//    locking them into BBB_SurrenderPose without ACE; neutralizes BBB_Morale's FATE_RUSE).
//  - Unconscious (alive): resolves by policy (-bbbDeadcheckPolicy capture|mixed|lethal, or
//    BBB_Hooks.PrisonerPolicy). Mixed defaults to mostly capture with occasional close-quarters
//    lethal deadchecks. Against unconscious players, AI always prefer ACE Captives capture
//    when ACE Captives is loaded (unless policy is explicitly lethal), giving a 3 s telegraph.
// Hard rules:
//  - Perception-honest: moves to the last-seen position recorded when the contact dropped.
//  - Priority 72 move / 74 deadcheck fire: strictly below ATTACK_SELECTED (90) and HEAL (111),
//    so any live threat or friendly rescue immediately preempts it.
//  - Respects BBB_Claims (CLEAR = 25), BBB_OrderIntent, GameMasterOrder, IsBinding, HostLeash,
//    BBB_Hooks.AllowDeadcheck, and BBB_Zombies (never captures zombies).
class BBB_AIClearMove : SCR_AIMoveIndividuallyBehavior
{
	override float CustomEvaluate()
	{
		return BBB_Deadcheck.MOVE_PRIORITY;
	}

	override int GetCause()
	{
		return SCR_EAIBehaviorCause.COMBAT;
	}
}

class BBB_AIDeadcheckFire : SCR_AISuppressBehavior
{
	protected float m_fEndMs;

	void BBB_AIDeadcheckFire(SCR_AIUtilityComponent utility, SCR_AIActivityBase groupActivity, SCR_AISuppressionVolumeBase suppressionVolume, float suppressionDuration, float fireRate, float priorityLevel = PRIORITY_LEVEL_NORMAL)
	{
		m_fEndMs = GetGame().GetWorld().GetWorldTime() + suppressionDuration * 1000.0;
		SetIsUniqueInActionQueue(true);
	}

	override float CustomEvaluate()
	{
		if (GetGame().GetWorld().GetWorldTime() > m_fEndMs) { Complete(); return 0; }
		if (!m_Utility || !m_Utility.m_CombatComponent || !m_Utility.m_AIInfo) { Complete(); return 0; }
		if (!BBB.IsConscious(m_Utility.m_OwnerEntity) || BBB.PlayerOf(m_Utility.m_OwnerEntity) != 0) { Complete(); return 0; }
		if (m_Utility.m_CombatComponent.GetCurrentTarget()) { Complete(); return 0; }
		if (m_Utility.m_CombatComponent.GetCombatMode() == EAIGroupCombatMode.HOLD_FIRE) { Complete(); return 0; }
		if (m_Utility.m_AIInfo.HasUnitState(EUnitState.IN_VEHICLE) || m_Utility.m_AIInfo.HasUnitState(EUnitState.UNCONSCIOUS)) { Complete(); return 0; }
		if (BBB_OrderIntent.Held(m_Utility)) { Complete(); return 0; }
		return BBB_Deadcheck.FIRE_PRIORITY;
	}
}

class BBB_ClearJob : Managed
{
	SCR_AIGroup m_Group;
	IEntity m_Checker;
	IEntity m_Target;
	BBB_DroppedContact m_Drop;      // non-null when checking a BBB_KillConfirm drop
	vector m_vGoal;
	bool m_bCaptureOnly;            // true for surrendered targets (always capture, never lethal)
	int m_iStage;                   // STAGE_APPROACH / STAGE_INSPECT / STAGE_FINISH
	float m_fStartedAt;
	float m_fStageAt;
	float m_fDeadline;
	ref BBB_AIClearMove m_Move;
	ref BBB_AIDeadcheckFire m_Fire;
}

class BBB_Deadcheck
{
	static int s_iRead;
	static bool s_bOn = true;
	static int s_iPolicy = 1;       // 0 capture, 1 mixed, 2 lethal

	static const int POLICY_CAPTURE = 0;
	static const int POLICY_MIXED = 1;
	static const int POLICY_LETHAL = 2;

	static const int STAGE_APPROACH = 0;
	static const int STAGE_INSPECT = 1;
	static const int STAGE_FINISH = 2;

	static const float REACH_M = 45.0;          // max distance from squad to deadcheck a dropped enemy
	static const float CAPTURE_REACH_M = 35.0;  // max distance from squad to capture a surrendered enemy
	static const float ARRIVE_M = 3.2;          // close enough to inspect / cuff
	static const float PRESENT_M = 4.5;         // target still at the spot (else dragged/crawled away)
	static const float DANGER_ENEMY_M = 60.0;   // abort if a live visible enemy is this close
	static const float MAX_SUPPRESSION = 0.33;  // don't send checkers while pinned
	static const float INSPECT_AI_S = 2.0;      // pause on arrival before deadchecking/cuffing AI
	static const float INSPECT_PLAYER_S = 3.0;  // extra telegraph pause when target is a player
	static const float FINISH_FIRE_S = 1.4;     // wait for lethal deadcheck burst to complete
	static const float JOB_TIMEOUT_S = 25.0;
	static const float SQUAD_COOLDOWN_S = 6.0;
	static const int MAX_GLOBAL_JOBS = 6;
	static const float MOVE_PRIORITY = 72.0;    // above observe (69), below ATTACK_SELECTED (90)
	static const float FIRE_PRIORITY = 74.0;    // below ATTACK_SELECTED (90) and HEAL (111)

	// Counters for tests and diagnostics.
	static int s_iStarted;
	static int s_iVerifiedDead;
	static int s_iEmptySpots;
	static int s_iCaptured;
	static int s_iLethalChecks;

	protected static ref array<ref BBB_ClearJob> s_aJobs;
	protected static ref map<SCR_AIGroup, float> s_mSquadCooldown;

	// Called by BBB_StaticInit at game start (static initializer budget).
	static void BBB_InitStatics()
	{
		s_aJobs = {};
		s_mSquadCooldown = new map<SCR_AIGroup, float>();
	}

	static void Read()
	{
		if (!BBB_Settings.Due(s_iRead)) return;
		string value;
		s_bOn = !(BBB_Settings.Get("bbbDeadcheck", value) && value == "0");
		s_iPolicy = POLICY_MIXED;
		if (BBB_Settings.Get("bbbDeadcheckPolicy", value))
		{
			if (value == "capture") s_iPolicy = POLICY_CAPTURE;
			else if (value == "lethal") s_iPolicy = POLICY_LETHAL;
			else s_iPolicy = POLICY_MIXED;
		}
		PrintFormat("[BBB] DEADCHECK on=%1 policy=%2 ace=%3", s_bOn, s_iPolicy, BBB_SurrenderPose.AceLoaded());
	}

	static bool On()
	{
		Read();
		return s_bOn;
	}

	static void Stop()
	{
		if (s_aJobs)
		{
			for (int i = s_aJobs.Count() - 1; i >= 0; i--)
				EndJob(i, "BBB stopped");
			s_aJobs.Clear();
		}
		BBB_Claims.UnmarkAll(BBB_Claims.JOB_CLEAR);
		if (s_mSquadCooldown) s_mSquadCooldown.Clear();
		s_iStarted = 0;
		s_iVerifiedDead = 0;
		s_iEmptySpots = 0;
		s_iCaptured = 0;
		s_iLethalChecks = 0;
	}

	protected static float Now()
	{
		return GetGame().GetWorld().GetWorldTime() / 1000.0;
	}

	static bool Busy(IEntity soldier)
	{
		return BBB_Claims.Busy(soldier, BBB_Claims.JOB_CLEAR);
	}

	protected static bool TargetClaimed(IEntity target)
	{
		if (!target || !s_aJobs) return false;
		foreach (BBB_ClearJob job : s_aJobs)
			if (job && job.m_Target == target) return true;
		return false;
	}

	protected static bool GroupBusy(SCR_AIGroup group)
	{
		if (!group || !s_aJobs) return false;
		foreach (BBB_ClearJob job : s_aJobs)
			if (job && job.m_Group == group) return true;
		return false;
	}

	// Main 1 Hz tick called from BBB.Tick().
	static void Tick()
	{
		if (!On() || !BBB.IsStarted() || !Replication.IsServer()) return;
		float now = Now();

		// 1. Update running jobs first.
		for (int i = s_aJobs.Count() - 1; i >= 0; i--)
			UpdateJob(i, now);

		if (s_aJobs.Count() >= MAX_GLOBAL_JOBS) return;

		// 2. Look for squads ready to deadcheck a dropped enemy or secure a surrendered prisoner.
		array<SCR_AIGroup> groups = {};
		BBB.Groups(groups);
		foreach (SCR_AIGroup group : groups)
		{
			if (s_aJobs.Count() >= MAX_GLOBAL_JOBS) break;
			if (!EligibleGroup(group, now)) continue;
			TryStartForGroup(group, now);
		}

		// Prune stale cooldown entries.
		for (int c = s_mSquadCooldown.Count() - 1; c >= 0; c--)
		{
			if (!s_mSquadCooldown.GetKey(c) || s_mSquadCooldown.GetElement(c) <= now)
				s_mSquadCooldown.RemoveElement(c);
		}
	}

	protected static bool EligibleGroup(SCR_AIGroup group, float now)
	{
		if (!group || GroupBusy(group)) return false;
		float nextAt;
		if (s_mSquadCooldown.Find(group, nextAt) && now < nextAt) return false;
		if (BBB.HasPlayer(group) || BBB_Transport.CrewedVehicle(group) || BBB_Morale.IsWithdrawing(group)) return false;
		if (!BBB.s_Hooks.AllowDeadcheck(group) || BBB.s_Hooks.IsBinding(group) || BBB_OrderIntent.GameMasterOrder(group)) return false;

		vector intentCentre;
		float intentRadius;
		BBB_EIntent intent = BBB.s_Hooks.Intent(group, intentCentre, intentRadius);
		if (intent == BBB_EIntent.NONE || intent == BBB_EIntent.TRANSIT || intent == BBB_EIntent.WITHDRAW) return false;

		BBB_SquadPicture pic = BBB_Squad.Get(group);
		if (pic && pic.m_fAvgSuppression >= MAX_SUPPRESSION) return false;
		return !HasCloseLiveEnemy(group);
	}

	protected static bool HasCloseLiveEnemy(SCR_AIGroup group)
	{
		array<AIAgent> agents = {};
		BBB.Members(group, agents);
		array<BaseTarget> targets = {};
		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			if (!utility || !utility.m_PerceptionComponent || !utility.m_CombatComponent) continue;
			targets.Clear();
			utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
			foreach (BaseTarget target : targets)
			{
				if (!target || target.IsDisarmed() || target.GetDistance() > DANGER_ENEMY_M) continue;
				if (utility.m_CombatComponent.IsTargetVisible(target)) return true;
			}
		}
		return false;
	}

	protected static void TryStartForGroup(SCR_AIGroup group, float now)
	{
		IEntity anchor = BBB.AnchorOf(group);
		if (!anchor) return;
		vector centre = anchor.GetOrigin();
		Faction side = group.GetFaction();
		if (!side) return;

		vector leashCentre;
		float leashRadius;
		bool hasLeash = BBB.s_Hooks.HostLeash(group, leashCentre, leashRadius) && leashRadius > 0;

		IEntity bestTarget = null;
		BBB_DroppedContact bestDrop = null;
		vector bestGoal = vector.Zero;
		bool captureOnly = false;
		float bestDist = 99999.0;

		// Priority 1: nearby unsecured surrendered prisoners (BBB_Morale or ACE-surrendered).
		array<ChimeraCharacter> prisoners = {};
		BBB_Morale.UnsecuredPrisonersNear(centre, CAPTURE_REACH_M, side, prisoners);
		foreach (ChimeraCharacter p : prisoners)
		{
			if (!p || p.IsInVehicle() || TargetClaimed(p) || BBB_SurrenderPose.AceIsCaptive(p)) continue;
			vector pos = p.GetOrigin();
			if (hasLeash && vector.DistanceXZ(leashCentre, pos) > leashRadius) continue;
			float d = vector.DistanceXZ(centre, pos);
			if (d < bestDist)
			{
				bestDist = d;
				bestTarget = p;
				bestGoal = pos;
				bestDrop = null;
				captureOnly = true;
			}
		}

		// Also check for ACE-surrendered players/AI not in BBB_Morale (e.g. a player who voluntarily surrendered).
		if (!bestTarget && BBB_SurrenderPose.AceLoaded())
		{
			ChimeraCharacter aceSurr = FindAceSurrenderedNear(centre, CAPTURE_REACH_M, side);
			if (aceSurr && !TargetClaimed(aceSurr))
			{
				vector pos = aceSurr.GetOrigin();
				if (!hasLeash || vector.DistanceXZ(leashCentre, pos) <= leashRadius)
				{
					bestTarget = aceSurr;
					bestGoal = pos;
					bestDrop = null;
					captureOnly = true;
				}
			}
		}

		// Priority 2: unverified dropped contacts from BBB_KillConfirm (deadcheck / unconscious capture).
		if (!bestTarget)
		{
			array<BBB_DroppedContact> drops = {};
			BBB_KillConfirm.GetDrops(group, drops);
			foreach (BBB_DroppedContact drop : drops)
			{
				if (!drop || !drop.m_Enemy || drop.m_bChecked) continue;
				if (BBB_KillConfirm.IsVerified(side, drop.m_Enemy) || TargetClaimed(drop.m_Enemy)) continue;
				if (BBB_Zombies.Is(drop.m_Enemy)) continue;
				ChimeraCharacter ch = ChimeraCharacter.Cast(drop.m_Enemy);
				if (!ch || ch.IsInVehicle()) continue;
				// Wait at least 3 s after the drop before walking up.
				if (now - drop.m_fDroppedAt < 3.0) continue;
				if (hasLeash && vector.DistanceXZ(leashCentre, drop.m_vPos) > leashRadius) continue;
				float d = vector.DistanceXZ(centre, drop.m_vPos);
				if (d > REACH_M) continue;
				if (d < bestDist)
				{
					bestDist = d;
					bestTarget = drop.m_Enemy;
					bestGoal = drop.m_vPos;
					bestDrop = drop;
					captureOnly = false;
				}
			}
		}

		if (!bestTarget) return;

		// Pick the nearest available conscious foot rifleman in the group.
		IEntity checker = PickChecker(group, bestGoal);
		if (!checker) return;

		SCR_AIUtilityComponent utility = BBB_VehiclePosture.Utility(checker);
		if (!utility) return;

		BBB_ClearJob job = new BBB_ClearJob();
		job.m_Group = group;
		job.m_Checker = checker;
		job.m_Target = bestTarget;
		job.m_Drop = bestDrop;
		job.m_vGoal = BBB.Ground(bestGoal);
		job.m_bCaptureOnly = captureOnly;
		job.m_iStage = STAGE_APPROACH;
		job.m_fStartedAt = now;
		job.m_fStageAt = now;
		job.m_fDeadline = now + JOB_TIMEOUT_S;

		job.m_Move = new BBB_AIClearMove(utility, null, job.m_vGoal, MOVE_PRIORITY, SCR_AIActionBase.PRIORITY_LEVEL_NORMAL, null, 2.0);
		utility.AddAction(job.m_Move);
		BBB_Claims.Take(checker, BBB_Claims.CLEAR);
		BBB_Claims.Mark(checker, BBB_Claims.JOB_CLEAR);
		s_aJobs.Insert(job);
		s_iStarted++;
		BBB_Perf.Count("deadcheck.started");

		string kind = "deadcheck";
		if (captureOnly) kind = "secure prisoner";
		PrintFormat("[BBB] DEADCHECK %1: %2 moves %3 m to %4", BBB.s_Hooks.NameOf(group), checker, Math.Round(vector.DistanceXZ(checker.GetOrigin(), job.m_vGoal)), kind);
		BBB_Think.Note(checker, "Deadcheck", "moving to " + kind + " (" + Math.Round(vector.DistanceXZ(checker.GetOrigin(), job.m_vGoal)) + " m)");
	}

	protected static IEntity PickChecker(SCR_AIGroup group, vector goal)
	{
		array<AIAgent> agents = {};
		BBB.Members(group, agents);
		IEntity best = null;
		float bestDist = 99999.0;
		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			ChimeraCharacter ch = ChimeraCharacter.Cast(agent.GetControlledEntity());
			if (!ch || !BBB.IsConscious(ch) || ch.IsInVehicle() || BBB.PlayerOf(ch) != 0) continue;
			if (BBB_Drag.Busy(ch) || BBB_Morale.IsPrisoner(ch)) continue;
			if (BBB_Claims.Committed(ch) || !BBB_Claims.Can(ch, BBB_Claims.CLEAR)) continue;
			SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			if (!utility || BBB_OrderIntent.Blocks(utility)) continue;
			float d = vector.DistanceXZ(ch.GetOrigin(), goal);
			if (d < bestDist)
			{
				bestDist = d;
				best = ch;
			}
		}
		return best;
	}

	protected static Faction s_AceQuerySide;
	protected static ChimeraCharacter s_AceQueryBest;
	protected static float s_fAceQueryBest;
	protected static vector s_vAceQueryFrom;

	protected static ChimeraCharacter FindAceSurrenderedNear(vector origin, float range, Faction side)
	{
		s_AceQuerySide = side;
		s_AceQueryBest = null;
		s_fAceQueryBest = range;
		s_vAceQueryFrom = origin;
		GetGame().GetWorld().QueryEntitiesBySphere(origin, range, ConsiderAceSurrendered, null, EQueryEntitiesFlags.DYNAMIC);
		ChimeraCharacter result = s_AceQueryBest;
		s_AceQueryBest = null;
		return result;
	}

	protected static bool ConsiderAceSurrendered(IEntity entity)
	{
		ChimeraCharacter ch = ChimeraCharacter.Cast(entity);
		if (!ch || !BBB.IsConscious(ch) || ch.IsInVehicle()) return true;
		if (!BBB_SurrenderPose.AceHasSurrendered(ch) || BBB_SurrenderPose.AceIsCaptive(ch)) return true;
		// ACE switches surrendered units to CIV; check players directly or via BBB_Morale.
		// Non-player AI already in BBB_Morale are handled by UnsecuredPrisonersNear.
		if (BBB.PlayerOf(ch) == 0 && BBB_Morale.IsPrisoner(ch)) return true;
		if (BBB.PlayerOf(ch) > 0)
		{
			// Players who surrender via ACE: only capture if their original/current faction is not our own.
			Faction f = SCR_AIFactionHandling.GetEntityPerceivedFaction(ch);
			if (f && f == s_AceQuerySide) return true;
		}
		else
		{
			Faction f = SCR_AIFactionHandling.GetEntityPerceivedFaction(ch);
			if (!f || !BBB_Combat.IsHostile(s_AceQuerySide, f)) return true;
		}
		float d = vector.DistanceXZ(s_vAceQueryFrom, ch.GetOrigin());
		if (d < s_fAceQueryBest)
		{
			s_fAceQueryBest = d;
			s_AceQueryBest = ch;
		}
		return true;
	}

	protected static void UpdateJob(int index, float now)
	{
		BBB_ClearJob job = s_aJobs[index];
		if (!job || !job.m_Checker || !BBB.IsConscious(job.m_Checker) || BBB.PlayerOf(job.m_Checker) != 0)
		{
			EndJob(index, "checker unavailable");
			return;
		}
		ChimeraCharacter checkerChar = ChimeraCharacter.Cast(job.m_Checker);
		if (!checkerChar || checkerChar.IsInVehicle() || !BBB_Claims.Holds(job.m_Checker, BBB_Claims.CLEAR))
		{
			EndJob(index, "claim lost or mounted");
			return;
		}
		if (now > job.m_fDeadline || !job.m_Group || BBB_Morale.IsWithdrawing(job.m_Group))
		{
			EndJob(index, "timeout or withdrawing");
			return;
		}
		SCR_AIUtilityComponent utility = BBB_VehiclePosture.Utility(job.m_Checker);
		if (!utility || BBB_OrderIntent.Blocks(utility))
		{
			EndJob(index, "order override");
			return;
		}
		// Abort immediately if a live visible enemy is close.
		if (utility.m_CombatComponent)
		{
			BaseTarget live = utility.m_CombatComponent.GetCurrentTarget();
			if (live && !live.IsDisarmed() && live.GetDistance() <= DANGER_ENEMY_M && utility.m_CombatComponent.IsTargetVisible(live))
			{
				EndJob(index, "live enemy nearby");
				return;
			}
		}

		BBB_Claims.Take(job.m_Checker, BBB_Claims.CLEAR);

		if (job.m_iStage == STAGE_APPROACH)
		{
			float dist = vector.DistanceXZ(job.m_Checker.GetOrigin(), job.m_vGoal);
			if (dist <= ARRIVE_M)
			{
				if (job.m_Move) { job.m_Move.Complete(); job.m_Move = null; }
				if (utility.m_LookAction)
					utility.m_LookAction.LookAt(job.m_vGoal + Vector(0, 0.3, 0), SCR_AILookAction.PRIO_COMMANDER, 3.5);
				job.m_iStage = STAGE_INSPECT;
				job.m_fStageAt = now;
				BBB_Think.Note(job.m_Checker, "Deadcheck", "inspecting contact");
			}
			return;
		}

		if (job.m_iStage == STAGE_INSPECT)
		{
			float pause = INSPECT_AI_S;
			if (job.m_Target && BBB.PlayerOf(job.m_Target) > 0) pause = INSPECT_PLAYER_S;
			if (now - job.m_fStageAt < pause) return;
			ResolveAtSpot(index, job, utility, now);
			return;
		}

		if (job.m_iStage == STAGE_FINISH)
		{
			if (now - job.m_fStageAt >= FINISH_FIRE_S)
			{
				if (job.m_Group && job.m_Target)
					BBB_KillConfirm.MarkVerified(job.m_Group.GetFaction(), job.m_Target);
				if (job.m_Drop) job.m_Drop.m_bChecked = true;
				EndJob(index, "lethal deadcheck complete");
			}
		}
	}

	protected static void ResolveAtSpot(int index, BBB_ClearJob job, SCR_AIUtilityComponent utility, float now)
	{
		Faction side;
		if (job.m_Group) side = job.m_Group.GetFaction();

		// Check if the body/target is actually still here (perception-honest: maybe dragged away or crawled off).
		if (!job.m_Target || vector.DistanceXZ(job.m_Target.GetOrigin(), job.m_vGoal) > PRESENT_M)
		{
			if (job.m_Drop) job.m_Drop.m_bChecked = true;
			s_iEmptySpots++;
			BBB_Think.Note(job.m_Checker, "Deadcheck", "contact gone from spot");
			EndJob(index, "spot empty");
			return;
		}

		// Already dead: mark verified so no other friendly squad checks this corpse again.
		if (!BBB.IsAlive(job.m_Target))
		{
			BBB_KillConfirm.MarkVerified(side, job.m_Target);
			if (job.m_Drop) job.m_Drop.m_bChecked = true;
			s_iVerifiedDead++;
			BBB_Perf.Count("deadcheck.verifiedDead");
			BBB_Think.Note(job.m_Checker, "Deadcheck", "confirmed KIA");
			EndJob(index, "verified dead");
			return;
		}

		ChimeraCharacter targetChar = ChimeraCharacter.Cast(job.m_Target);
		if (!targetChar)
		{
			EndJob(index, "invalid target");
			return;
		}

		// Surrendered target: always capture/secure, never lethal.
		if (job.m_bCaptureOnly || BBB_Morale.IsPrisoner(targetChar) || BBB_SurrenderPose.AceHasSurrendered(targetChar))
		{
			CaptureTarget(index, job, targetChar, side);
			return;
		}

		// Target woke up before we reached them and isn't surrendered: let normal combat handle them.
		if (BBB.IsConscious(targetChar))
		{
			if (job.m_Drop) job.m_Drop.m_bChecked = true;
			EndJob(index, "target awake");
			return;
		}

		// Target is alive and unconscious: decide Capture vs. Lethal deadcheck by ROE policy.
		int policy = ResolvePolicy(job.m_Group, targetChar);
		bool capture = DecideCapture(policy, targetChar, now);
		if (capture)
		{
			CaptureTarget(index, job, targetChar, side);
			return;
		}

		// Lethal deadcheck ("double tap"): short, tight burst onto the downed body.
		if (utility.m_CombatComponent && utility.m_CombatComponent.GetCombatMode() != EAIGroupCombatMode.HOLD_FIRE)
		{
			vector bodyPos = targetChar.GetOrigin();
			SCR_AISuppressionVolumeBox vol = new SCR_AISuppressionVolumeBox(
				Vector(bodyPos[0] - 0.35, bodyPos[1] - 0.2, bodyPos[2] - 0.35),
				Vector(bodyPos[0] + 0.35, bodyPos[1] + 0.6, bodyPos[2] + 0.35)
			);
			job.m_Fire = new BBB_AIDeadcheckFire(utility, null, vol, 1.1, 1.0);
			utility.AddAction(job.m_Fire);
			job.m_iStage = STAGE_FINISH;
			job.m_fStageAt = now;
			s_iLethalChecks++;
			BBB_Perf.Count("deadcheck.lethal");
			PrintFormat("[BBB] DEADCHECK %1: %2 performs lethal deadcheck on unconscious %3", BBB.s_Hooks.NameOf(job.m_Group), job.m_Checker, targetChar);
			BBB_Think.Note(job.m_Checker, "Deadcheck", "lethal deadcheck on unconscious enemy");
			return;
		}
		// Under HOLD_FIRE, fall back to capturing instead of shooting.
		CaptureTarget(index, job, targetChar, side);
	}

	protected static int ResolvePolicy(SCR_AIGroup group, IEntity target)
	{
		int hostPolicy = BBB.s_Hooks.PrisonerPolicy(group, target);
		if (hostPolicy >= POLICY_CAPTURE && hostPolicy <= POLICY_LETHAL) return hostPolicy;
		return s_iPolicy;
	}

	protected static bool DecideCapture(int policy, ChimeraCharacter target, float now)
	{
		if (policy == POLICY_CAPTURE) return true;
		if (policy == POLICY_LETHAL) return false;
		// Mixed ROE: always prefer capturing unconscious players when ACE Captives is loaded.
		if (BBB.PlayerOf(target) > 0 && BBB_SurrenderPose.AceLoaded()) return true;
		vector pos = target.GetOrigin();
		RandomGenerator roll = new RandomGenerator();
		roll.SetSeed((int)(pos[0] * 10) * 6151 + (int)(pos[2] * 10) * 53 + (int)(now * 10));
		float captureChance = 0.50;
		if (BBB_SurrenderPose.AceLoaded()) captureChance = 0.65;
		return roll.RandFloat01() < captureChance;
	}

	protected static void CaptureTarget(int index, BBB_ClearJob job, ChimeraCharacter target, Faction side)
	{
		bool ok = BBB_Morale.SecurePrisoner(target);
		BBB_KillConfirm.MarkVerified(side, target);
		if (job.m_Drop) job.m_Drop.m_bChecked = true;
		if (ok)
		{
			s_iCaptured++;
			BBB_Perf.Count("deadcheck.captured");
			BBB.s_Hooks.OnPrisonerCaptured(job.m_Group, target);
			PrintFormat("[BBB] DEADCHECK %1: %2 captured prisoner %3 (ace=%4)", BBB.s_Hooks.NameOf(job.m_Group), job.m_Checker, target, BBB_SurrenderPose.AceLoaded());
			BBB_Think.Note(job.m_Checker, "Deadcheck", "captured enemy prisoner");
		}
		EndJob(index, "captured");
	}

	protected static void EndJob(int index, string reason)
	{
		if (index < 0 || index >= s_aJobs.Count()) return;
		BBB_ClearJob job = s_aJobs[index];
		s_aJobs.RemoveOrdered(index);
		if (!job) return;
		if (job.m_Move) { job.m_Move.Fail(); job.m_Move = null; }
		if (job.m_Fire) { job.m_Fire.Complete(); job.m_Fire = null; }
		if (job.m_Checker)
		{
			BBB_Claims.Release(job.m_Checker, BBB_Claims.CLEAR);
			BBB_Claims.Unmark(job.m_Checker, BBB_Claims.JOB_CLEAR);
		}
		if (job.m_Group) s_mSquadCooldown.Set(job.m_Group, Now() + SQUAD_COOLDOWN_S);
	}
}
