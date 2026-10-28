// Better Battle Buddies (BBB): squad morale, tactical retrograde break-contact
// withdrawal, and isolated out-of-ammo surrender.
//
// Standalone tactical reaction for autonomous infantry squads under heavy attrition:
// 1. Morale evaluation (per-group, <= 0.5 Hz): monitors casualty ratio (>= 50%),
//    squad leader loss, sustained suppression (> 0.60), and ammunition depletion.
// 2. Retrograde withdrawal: when morale breaks under active contact, the squad
//    conducts a leap-frogging peel-back (retrograde bounding overwatch) directly
//    away from the enemy threat centroid to rear cover. One element covers while
//    the other bounds back, alternating until contact is broken (> 180 m, or the
//    enemy is no longer perceived after real movement), the squad stalls (two bounds
//    gaining < 3 m) or 90 m of observed ground is gained. Distance counts only what
//    the squad actually moved away from the threat. 60 s before another withdrawal.
// 3. Isolated surrender: a lone, out-of-ammo survivor cornered by close enemies
//    (<= 15 m) with direct sight halts fire, lowers weapon, disables weapon controls
//    and crouches in place rather than suicide-charging.
// 4. Hard safety constraints: never overrides explicit player/GM orders
//    (BBB_OrderIntent.Blocks), never affects player-controlled squads or vehicles,
//    and respects host hooks (BBB.s_Hooks.AllowMorale). Never invents positions
//    or references ACA classes.

class BBB_GroupMoraleState : Managed
{
	SCR_AIGroup m_Group;
	int m_iInitialCount;
	float m_fMorale;
	float m_fBrokenSince;
	float m_fNextCheck;
	float m_fPhaseSince;
	bool m_bWithdrawing;
	vector m_vThreatCenter;
	vector m_vAwayFrom;             // point the bounds back away from: the threat, or BBB_Squad's way out
	vector m_vEscapeDir;            // BBB_Squad way out, kept for the whole withdrawal while it stays clear
	vector m_vRetrogradeDir;
	vector m_vWithdrawPos;
	ref array<SCR_AIUtilityComponent> m_TeamCover = {};
	ref array<SCR_AIUtilityComponent> m_TeamMove = {};
	int m_iCoverTeam;
	int m_iPhase;
	float m_fTotalWithdrawn;        // observed travel away from the threat, not requested distance
	bool m_bContactBroken;
	vector m_vBoundStart;           // squad centre when the current bound began
	int m_iStalledBounds;           // consecutive bounds that gained almost no ground
	float m_fNextWithdrawAllowed;   // no new withdrawal before this time (s)
	string m_sEndReason;
	ref array<ref BBB_AIWithdrawMove> m_aMoves = {}; // the moving half's run to m_vWithdrawPos
	IEntity m_Leader;               // last leader seen up
	float m_fLeaderLostAt;          // when he went down (s); counts as leader loss while the squad reorganises
}

// The moving half's run back. Above attacking (90) like Buildings' indoor move and Rearm's
// out-of-spares run, so it happens under fire (vanilla combat-move requests only run inside a
// combat behaviour and stalled here); grenades, healing, danger and player orders still outrank it.
class BBB_AIWithdrawMove : SCR_AIMoveIndividuallyBehavior
{
	override float CustomEvaluate()
	{
		return BBB_Morale.MOVE_PRIORITY;
	}

	override int GetCause()
	{
		return SCR_EAIBehaviorCause.COMBAT;
	}
}

class BBB_SurrenderState : Managed
{
	ChimeraCharacter m_Soldier;
	SCR_AIUtilityComponent m_Utility;
	float m_fSurrenderSince;
	bool m_bDisarmed;
	bool m_bWasActivated;
}

class BBB_Morale
{
	static int s_iRead;
	static bool s_bOn = true;
	static bool s_bSurrenderOn = true;

	// Tunable thresholds
	static float s_fCasualtyThreshold = 0.50;
	static float s_fSuppressionThreshold = 0.60;
	static float s_fBreakScore = 0.70; // tests may lower it
	static float s_fRetrogradeBoundDist = 25.0;
	static float s_fMaxWithdrawDist = 90.0;

	// Internal timing constants
	static const float CHECK_INTERVAL_MS = 1500;
	static const float BOUND_TIMEOUT_S = 14.0;
	static const float SURRENDER_DIST_MAX = 15.0;
	static const float SURRENDER_THREAT_MIN = 0.60;
	static const float RECOVERY_CALM_TIME_S = 20.0;
	static const float BREAK_CONTACT_DIST_M = 180.0;
	static const float MIN_BOUND_GAIN_M = 3.0;      // less than this counts as a stalled bound
	static const int MAX_STALLED_BOUNDS = 2;
	static const float WITHDRAW_COOLDOWN_S = 60.0;  // after a withdrawal ends, before another may start
	static const float MOVE_PRIORITY = 91;
	static const float LEADER_REORG_S = 45.0;      // a fallen leader still counts this long after a new one is named

	// State tracking
	protected static ref array<ref BBB_GroupMoraleState> s_States = {};
	protected static ref array<ref BBB_SurrenderState> s_Surrendered = {};

	// Performance counters
	static int s_iWithdrawalsStarted;
	static int s_iWithdrawalBounds;
	static int s_iContactsBroken;
	static int s_iSurrenders;

	static void Read()
	{
		if (!BBB_Settings.Due(s_iRead)) return;

		string value;
		if (BBB_Settings.Get("bbbMorale", value))
		{
			s_bOn = value == "1";
		}

		if (BBB_Settings.Get("bbbMoraleSurrender", value))
		{
			s_bSurrenderOn = value != "0";
		}

		if (BBB_Settings.Get("bbbMoraleCasualties", value))
		{
			float c = value.ToFloat();
			if (c > 0.1 && c <= 0.9) s_fCasualtyThreshold = c;
		}

		if (BBB_Settings.Get("bbbMoraleSuppression", value))
		{
			float s = value.ToFloat();
			if (s > 0.1 && s <= 1.0) s_fSuppressionThreshold = s;
		}

		if (BBB_Settings.Get("bbbMoraleDistance", value))
		{
			float d = value.ToFloat();
			if (d >= 10.0 && d <= 50.0) s_fRetrogradeBoundDist = d;
		}

		if (BBB_Settings.Get("bbbMoraleBreak", value)) s_fBreakScore = Math.Clamp(value.ToFloat(), 0.3, 1.0);
		if (BBB_Settings.Get("bbbMoraleWithdraw", value)) s_fMaxWithdrawDist = Math.Clamp(value.ToFloat(), 30.0, 250.0);

		PrintFormat("[BBB] MORALE enabled=%1 surrender=%2 casualtyThreshold=%3", s_bOn, s_bSurrenderOn, s_fCasualtyThreshold);
	}

	static bool IsOn()
	{
		Read();
		return s_bOn;
	}

	// Host or other modules can query if a group is currently breaking contact.
	static bool IsWithdrawing(SCR_AIGroup group)
	{
		if (!group || !s_bOn) return false;
		foreach (BBB_GroupMoraleState state : s_States)
		{
			if (state && state.m_Group == group && state.m_bWithdrawing) return true;
		}
		return false;
	}

	// The squad's current (or last) withdrawal: the threat it backs away from and its bound end.
	static bool WithdrawOrder(SCR_AIGroup group, out vector threat, out vector destination)
	{
		foreach (BBB_GroupMoraleState state : s_States)
		{
			if (!state || state.m_Group != group || state.m_fBrokenSince <= 0) continue;
			threat = state.m_vThreatCenter;
			destination = state.m_vWithdrawPos;
			return true;
		}
		return false;
	}

	// ---------------------------------------------------------------- threat queries

	// Centroid of currently perceived enemy contacts, using native perception memory only.
	static bool GetThreatCentroid(SCR_AIGroup group, out vector outThreatCenter)
	{
		outThreatCenter = vector.Zero;
		if (!group) return false;
		// The squad's main front, not the average of every report (BBB_Squad).
		if (BBB_Squad.On(group)) return BBB_Squad.Threat(group, 15.0, outThreatCenter);

		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(group, agents);
		int count = 0;
		vector sum = vector.Zero;

		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			if (!utility || !utility.m_PerceptionComponent) continue;

			if (utility.m_CombatComponent)
			{
				BaseTarget current = utility.m_CombatComponent.GetCurrentTarget();
				if (current && current.GetTargetCategory() == ETargetCategory.ENEMY)
				{
					sum = sum + current.GetLastDetectedPosition();
					count++;
				}
			}

			array<BaseTarget> targets = {};
			utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
			foreach (BaseTarget target : targets)
			{
				if (!target) continue;
				float timeSince = target.GetTimeSinceDetected();
				if (timeSince >= 0 && timeSince <= 15.0)
				{
					sum = sum + target.GetLastDetectedPosition();
					count++;
				}
			}
		}

		if (count == 0) return false;
		outThreatCenter = sum * (1.0 / count);
		return true;
	}

	// ---------------------------------------------------------------- ammunition check

	// Check if soldier is critically low or completely empty of ammunition.
	static bool IsOutOfAmmo(ChimeraCharacter soldier)
	{
		if (!soldier) return false;
		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(soldier.FindComponent(SCR_InventoryStorageManagerComponent));
		BaseWeaponManagerComponent wm = BaseWeaponManagerComponent.Cast(soldier.FindComponent(BaseWeaponManagerComponent));
		if (!inv || !wm) return false;

		BaseWeaponComponent weapon = wm.GetCurrentWeapon();
		if (!weapon)
		{
			array<IEntity> weapons = {};
			wm.GetWeaponsList(weapons);
			foreach (IEntity wEnt : weapons)
			{
				BaseWeaponComponent w = BaseWeaponComponent.Cast(wEnt.FindComponent(BaseWeaponComponent));
				if (w && (w.GetWeaponType() == EWeaponType.WT_RIFLE || w.GetWeaponType() == EWeaponType.WT_MACHINEGUN || w.GetWeaponType() == EWeaponType.WT_SNIPERRIFLE))
				{
					weapon = w;
					break;
				}
			}
		}
		if (!weapon) return true;

		array<BaseMuzzleComponent> muzzles = {};
		weapon.GetMuzzlesList(muzzles);
		BaseMuzzleComponent primaryMuzzle;
		foreach (BaseMuzzleComponent m : muzzles)
		{
			if (m && m.GetMagazineWell())
			{
				primaryMuzzle = m;
				break;
			}
		}
		if (!primaryMuzzle) return false;

		int spareMags = inv.GetMagazineCountByMuzzle(soldier, primaryMuzzle);
		int inGun = 0;
		BaseMagazineComponent curMag = primaryMuzzle.GetMagazine();
		if (curMag) inGun = curMag.GetAmmoCount();

		return spareMags == 0 && inGun == 0;
	}

	// ---------------------------------------------------------------- group state helper

	protected static BBB_GroupMoraleState GetOrCreateState(SCR_AIGroup group, int currentCount)
	{
		foreach (BBB_GroupMoraleState state : s_States)
		{
			if (state && state.m_Group == group) return state;
		}

		if (s_States.Count() >= 64) return null;

		BBB_GroupMoraleState newState = new BBB_GroupMoraleState();
		newState.m_Group = group;
		newState.m_iInitialCount = currentCount;
		newState.m_fMorale = 0;
		newState.m_fNextCheck = 0;
		s_States.Insert(newState);
		return newState;
	}

	// ---------------------------------------------------------------- morale evaluation

	protected static bool Down(IEntity soldier)
	{
		if (!soldier || !BBB.IsAlive(soldier)) return true;
		ChimeraCharacter character = ChimeraCharacter.Cast(soldier);
		return character && character.GetCharacterController() && character.GetCharacterController().IsUnconscious();
	}

	static bool Running(BBB_AIWithdrawMove move)
	{
		return move && move.GetActionState() != EAIActionState.COMPLETED && move.GetActionState() != EAIActionState.FAILED;
	}

	// Call off the moving half's runs (bound over, withdrawal ended, squad gone, Stop).
	protected static void CancelMoves(BBB_GroupMoraleState state)
	{
		SetCovering(state, false);
		foreach (int i, BBB_AIWithdrawMove move : state.m_aMoves)
		{
			if (i < state.m_TeamMove.Count() && state.m_TeamMove[i] && state.m_TeamMove[i].m_BBBWithdrawRun == move) state.m_TeamMove[i].m_BBBWithdrawRun = null;
			if (Running(move)) move.Fail();
		}
		state.m_aMoves.Clear();
	}

	static float CalculateGroupMorale(BBB_GroupMoraleState state, array<AIAgent> agents, out int aliveCount, out int outOfAmmoCount)
	{
		aliveCount = 0;
		outOfAmmoCount = 0;
		float totalThreat = 0;
		bool leaderAlive = false;

		IEntity leaderEntity = state.m_Group.GetLeaderEntity();

		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			IEntity controlled = agent.GetControlledEntity();
			if (!controlled || !BBB.IsAlive(controlled)) continue;

			ChimeraCharacter character = ChimeraCharacter.Cast(controlled);
			if (!character) continue;

			CharacterControllerComponent controller = character.GetCharacterController();
			if (!controller || controller.IsDead() || controller.IsUnconscious()) continue;

			aliveCount++;
			if (controlled == leaderEntity) leaderAlive = true;

			SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			if (utility && utility.m_ThreatSystem)
			{
				totalThreat = totalThreat + utility.m_ThreatSystem.GetThreatMeasure();
			}

			if (IsOutOfAmmo(character)) outOfAmmoCount++;
		}

		if (state.m_iInitialCount < aliveCount) state.m_iInitialCount = aliveCount;
		if (state.m_iInitialCount <= 0) return 0;

		// The group names a new leader the moment he falls, so "no leader alive" almost never
		// happened. The squad still counts him lost while it reorganises.
		float now = GetGame().GetWorld().GetWorldTime() / 1000.0;
		if (state.m_Leader && state.m_Leader != leaderEntity && Down(state.m_Leader)) state.m_fLeaderLostAt = now;
		if (leaderAlive) state.m_Leader = leaderEntity;
		if (state.m_fLeaderLostAt > 0 && now - state.m_fLeaderLostAt < LEADER_REORG_S) leaderAlive = false;

		float score = 0;

		// 1. Casualty ratio
		float casualtyRatio = (state.m_iInitialCount - aliveCount) / state.m_iInitialCount;
		if (casualtyRatio >= s_fCasualtyThreshold) score = score + 0.40;
		if (casualtyRatio >= 0.75) score = score + 0.25;

		// 2. Leader loss penalty
		if (!leaderAlive && state.m_iInitialCount > 1) score = score + 0.25;

		// 3. Average threat / suppression
		if (aliveCount > 0)
		{
			float avgThreat = totalThreat / aliveCount;
			if (avgThreat >= s_fSuppressionThreshold) score = score + 0.25;
			if (avgThreat >= 0.85) score = score + 0.15;
		}

		// 4. Ammunition status
		if (aliveCount > 0)
		{
			float ammoEmptyRatio = outOfAmmoCount / aliveCount;
			if (ammoEmptyRatio >= 0.50) score = score + 0.20;
		}

		// 5. Critical lone survivor
		if (aliveCount == 1 && state.m_iInitialCount > 1) score = score + 0.30;

		return score;
	}

	// ---------------------------------------------------------------- retrograde destination

	static vector CalculateRetrogradeDestination(vector squadCenter, vector threatCenter, float distance)
	{
		vector away = squadCenter - threatCenter;
		away[1] = 0;
		if (away.LengthSq() < 0.01)
		{
			away = Vector(0, 0, -1);
		}
		else
		{
			away.Normalize();
		}

		vector dest = squadCenter + away * distance;
		dest = BBB.Ground(dest);

		// If water blocks the direct retreat, check lateral 30 deg detours
		if (BBB_Terrain.IsWaterAt(dest))
		{
			vector lateral = Vector(-away[2], 0, away[0]);
			vector altDest = squadCenter + (away * 0.866 + lateral * 0.5) * distance;
			altDest = BBB.Ground(altDest);
			if (!BBB_Terrain.IsWaterAt(altDest)) return altDest;

			altDest = squadCenter + (away * 0.866 - lateral * 0.5) * distance;
			altDest = BBB.Ground(altDest);
			if (!BBB_Terrain.IsWaterAt(altDest)) return altDest;
		}

		return dest;
	}

	// ---------------------------------------------------------------- withdrawal execution

	// Centre of the squad's living, conscious members (the leader may be in the covering half)
	static bool SquadCenter(SCR_AIGroup group, out vector outCenter)
	{
		outCenter = vector.Zero;
		if (!group) return false;
		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(group, agents);
		int count = 0;
		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			ChimeraCharacter character = ChimeraCharacter.Cast(agent.GetControlledEntity());
			if (!character || !BBB.IsAlive(character)) continue;
			CharacterControllerComponent controller = character.GetCharacterController();
			if (!controller || controller.IsUnconscious()) continue;
			outCenter = outCenter + character.GetOrigin();
			count++;
		}
		if (count == 0) return false;
		outCenter = outCenter * (1.0 / count);
		return true;
	}

	// Ground actually gained away from the threat since the bound began (never negative)
	// With the squad picture: back away from the widest gap's far side, not one group (moving
	// straight away from one front can walk into the other).
	// The way out is kept for the whole withdrawal (re-picked only once an enemy is seen within
	// 45 deg of it), so the bounds don't swing as sightings come and go.
	protected static vector AwayFrom(BBB_GroupMoraleState state, vector squadCenter, vector threatCenter)
	{
		if (!BBB_Squad.On(state.m_Group)) return threatCenter;
		vector escape;
		if (state.m_vEscapeDir.LengthSq() < 0.01 || !BBB_Squad.Clear(state.m_Group, state.m_vEscapeDir, 45.0, 15.0))
		{
			if (!BBB_Squad.Escape(state.m_Group, BBB_Squad.MEMORY_S, escape)) return threatCenter;
			state.m_vEscapeDir = escape;
		}
		return squadCenter - state.m_vEscapeDir * 100.0;
	}

	// A withdrawal's covering half holds where it is (vanilla attack would advance on the enemy).
	protected static void SetCovering(BBB_GroupMoraleState state, bool hold)
	{
		foreach (SCR_AIUtilityComponent coverer : state.m_TeamCover)
		{
			if (coverer) coverer.m_bBBBHoldGround = hold;
		}
	}

	static float GainAway(vector boundStart, vector now, vector threatCenter)
	{
		vector away = boundStart - threatCenter;
		away[1] = 0;
		if (away.LengthSq() < 0.01) return 0;
		away.Normalize();
		vector moved = now - boundStart;
		moved[1] = 0;
		float gain = vector.Dot(moved, away);
		if (gain < 0) return 0;
		return gain;
	}

	protected static void EndWithdrawal(BBB_GroupMoraleState state, float now, bool contactBroken, string reason, float threatDist)
	{
		CancelMoves(state);
		state.m_bWithdrawing = false;
		state.m_bContactBroken = contactBroken;
		state.m_sEndReason = reason;
		state.m_fNextWithdrawAllowed = now + WITHDRAW_COOLDOWN_S;
		if (contactBroken) s_iContactsBroken++;
		PrintFormat("[BBB] MORALE: Squad %1 ended withdrawal (%2, contact broken=%3, withdrawn=%4 m, threat dist=%5 m)",
			BBB.s_Hooks.NameOf(state.m_Group), reason, contactBroken, Math.Round(state.m_fTotalWithdrawn), Math.Round(threatDist));
			BBB_Think.NoteGroup(state.m_Group, "Morale", "withdrawal ended: " + reason);
	}

	protected static void StartWithdrawal(BBB_GroupMoraleState state, vector threatCenter, float now)
	{
		vector squadCenter;
		if (!SquadCenter(state.m_Group, squadCenter)) return;

		state.m_vThreatCenter = threatCenter;
		state.m_vEscapeDir = vector.Zero;
		state.m_vAwayFrom = AwayFrom(state, squadCenter, threatCenter);
		state.m_vWithdrawPos = CalculateRetrogradeDestination(squadCenter, state.m_vAwayFrom, s_fRetrogradeBoundDist);
		state.m_bWithdrawing = true;
		state.m_fBrokenSince = now;
		state.m_fPhaseSince = now;
		state.m_fTotalWithdrawn = 0;
		state.m_bContactBroken = false;
		state.m_iStalledBounds = 0;
		state.m_sEndReason = "";
		state.m_iPhase = 0;
		state.m_iCoverTeam = 0;

		s_iWithdrawalsStarted++;
		PrintFormat("[BBB] MORALE: Squad %1 morale broke (score=%2, initial=%3), withdrawing away from %4 to %5",
			BBB.s_Hooks.NameOf(state.m_Group), state.m_fMorale, state.m_iInitialCount, threatCenter, state.m_vWithdrawPos);
			BBB_Think.NoteGroup(state.m_Group, "Morale", "MORALE BROKE (score " + state.m_fMorale.ToString(4, 2) + "): withdrawing");

		ExecuteBoundingPhase(state);
	}

	protected static void ExecuteBoundingPhase(BBB_GroupMoraleState state)
	{
		CancelMoves(state);
		state.m_TeamCover.Clear();
		state.m_TeamMove.Clear();

		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(state.m_Group, agents);

		array<SCR_AIUtilityComponent> consciousMembers = {};
		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			IEntity ent = agent.GetControlledEntity();
			if (!ent || !BBB.IsAlive(ent)) continue;
			ChimeraCharacter character = ChimeraCharacter.Cast(ent);
			if (!character || character.GetCharacterController().IsUnconscious()) continue;

			SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			if (!utility || !utility.m_CombatMoveState || BBB_OrderIntent.Blocks(utility)) continue;

			consciousMembers.Insert(utility);
		}

		if (consciousMembers.IsEmpty())
		{
			state.m_bWithdrawing = false;
			CancelMoves(state);
			state.m_sEndReason = "no one able to move";
			return;
		}
		vector boundStart;
		if (SquadCenter(state.m_Group, boundStart)) state.m_vBoundStart = boundStart;

		// Split into two logical elements for bounding overwatch; a lone survivor just runs
		int half = consciousMembers.Count() / 2;
		if (half == 0) half = 1;
		if (consciousMembers.Count() == 1)
		{
			state.m_TeamMove.Insert(consciousMembers[0]);
			consciousMembers.Clear();
		}

		for (int i = 0; i < consciousMembers.Count(); i++)
		{
			SCR_AIUtilityComponent u = consciousMembers[i];
			if (state.m_iCoverTeam == 0)
			{
				if (i < half) state.m_TeamCover.Insert(u);
				else state.m_TeamMove.Insert(u);
			}
			else
			{
				if (i < half) state.m_TeamMove.Insert(u);
				else state.m_TeamCover.Insert(u);
			}
		}

		// Team Move runs back to the destination (native pathing; covered by the other half)
		foreach (SCR_AIUtilityComponent mover : state.m_TeamMove)
		{
			BBB_AIWithdrawMove move = new BBB_AIWithdrawMove(mover, null, state.m_vWithdrawPos, priority: MOVE_PRIORITY, radius: 3.0);
			mover.AddAction(move);
			mover.m_BBBWithdrawRun = move;
			state.m_aMoves.Insert(move);
		}

		// Team Cover establishes overwatch and faces threat
		SetCovering(state, true);
		foreach (SCR_AIUtilityComponent coverer : state.m_TeamCover)
		{
			SCR_AICombatMoveRequest_Stop stopReq = new SCR_AICombatMoveRequest_Stop();
			stopReq.m_bAimAtTarget = true;
			stopReq.m_bAimAtTargetEnd = true;
			coverer.m_CombatMoveState.ApplyNewRequest(stopReq);
		}

		// Tactical smoke deployment to screen retrograde withdrawal
		BBB_Smoke.DeployForRetrograde(state.m_Group, state.m_vThreatCenter, state.m_TeamCover);

		s_iWithdrawalBounds++;
		PrintFormat("[BBB] MORALE: Squad %1 bound %2: %3 run to %4, %5 cover", BBB.s_Hooks.NameOf(state.m_Group), state.m_iPhase, state.m_TeamMove.Count(), state.m_vWithdrawPos, state.m_TeamCover.Count());
	}

	protected static void UpdateWithdrawal(BBB_GroupMoraleState state, float now)
	{
		float phaseElapsed = now - state.m_fPhaseSince;

		// Check if moving team arrived or phase timed out
		bool allArrived = true;
		foreach (int index, SCR_AIUtilityComponent mover : state.m_TeamMove)
		{
			if (!mover || !mover.m_OwnerEntity || !BBB.IsAlive(mover.m_OwnerEntity) || index >= state.m_aMoves.Count()) continue;
			BBB_AIWithdrawMove move = state.m_aMoves[index];
			if (Running(move) && vector.DistanceXZ(mover.m_OwnerEntity.GetOrigin(), state.m_vWithdrawPos) > 6.0)
			{
				allArrived = false;
				break;
			}
		}

		if (!allArrived && phaseElapsed < BOUND_TIMEOUT_S) return;

		// Count only the ground the squad actually gained away from the threat
		vector squadCenter;
		if (!SquadCenter(state.m_Group, squadCenter))
		{
			EndWithdrawal(state, now, false, "squad gone", 0);
			return;
		}
		float gain = GainAway(state.m_vBoundStart, squadCenter, state.m_vAwayFrom);
		PrintFormat("[BBB] MORALE: Squad %1 bound %2 over after %3 s: gained %4 m", BBB.s_Hooks.NameOf(state.m_Group), state.m_iPhase, Math.Round(phaseElapsed), Math.Round(gain));
		state.m_fTotalWithdrawn = state.m_fTotalWithdrawn + gain;
		if (gain < MIN_BOUND_GAIN_M) state.m_iStalledBounds++;
		else state.m_iStalledBounds = 0;
		state.m_iPhase++;
		state.m_fPhaseSince = now;

		vector threatCenter;
		bool hasThreat = GetThreatCentroid(state.m_Group, threatCenter);
		float currentDistToThreat = 0;
		if (hasThreat) currentDistToThreat = vector.DistanceXZ(squadCenter, threatCenter);

		// Contact is broken only by real separation or by losing the enemy after real movement
		if (hasThreat && currentDistToThreat >= BREAK_CONTACT_DIST_M)
		{
			EndWithdrawal(state, now, true, "out of range", currentDistToThreat);
			return;
		}
		if (!hasThreat)
		{
			EndWithdrawal(state, now, state.m_fTotalWithdrawn >= s_fRetrogradeBoundDist * 0.5, "enemy no longer perceived", 0);
			return;
		}
		if (state.m_iStalledBounds >= MAX_STALLED_BOUNDS)
		{
			EndWithdrawal(state, now, false, "stalled", currentDistToThreat);
			return;
		}
		if (state.m_fTotalWithdrawn >= s_fMaxWithdrawDist)
		{
			EndWithdrawal(state, now, false, "max distance", currentDistToThreat);
			return;
		}

		// Continue withdrawal: swap roles and bound again
		state.m_iCoverTeam = 1 - state.m_iCoverTeam;
		state.m_vThreatCenter = threatCenter;
		state.m_vAwayFrom = AwayFrom(state, squadCenter, threatCenter);
		state.m_vWithdrawPos = CalculateRetrogradeDestination(squadCenter, state.m_vAwayFrom, s_fRetrogradeBoundDist);

		ExecuteBoundingPhase(state);
	}

	// ---------------------------------------------------------------- surrender

	protected static void CheckSurrender(ChimeraCharacter soldier, SCR_AIUtilityComponent utility)
	{
		if (!s_bSurrenderOn || !soldier || !utility || !utility.m_CombatComponent || !utility.m_PerceptionComponent) return;
		if (BBB.PlayerOf(soldier) || BBB_OrderIntent.Blocks(utility)) return;

		CharacterControllerComponent controller = soldier.GetCharacterController();
		if (!controller || controller.IsDead() || controller.IsUnconscious() || controller.IsPlayerControlled()) return;

		// Already surrendered
		foreach (BBB_SurrenderState existing : s_Surrendered)
		{
			if (existing && existing.m_Soldier == soldier) return;
		}

		// Condition 1: Must be completely out of ammunition
		if (!IsOutOfAmmo(soldier)) return;

		// Condition 2: High threat/suppression
		if (!utility.m_ThreatSystem || utility.m_ThreatSystem.GetThreatMeasure() < SURRENDER_THREAT_MIN) return;

		// Condition 3: Close enemy with direct exposure
		array<BaseTarget> targets = {};
		utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
		bool closeEnemyFound = false;
		vector soldierPos = soldier.GetOrigin();

		foreach (BaseTarget target : targets)
		{
			if (!target || target.GetTargetCategory() != ETargetCategory.ENEMY) continue;
			if (target.GetDistance() <= SURRENDER_DIST_MAX && target.GetTraceFraction() >= 0.8)
			{
				closeEnemyFound = true;
				break;
			}
		}

		if (!closeEnemyFound) return;

		// Execute surrender
		ApplySurrender(soldier, utility, controller);
	}

	protected static void ApplySurrender(ChimeraCharacter soldier, SCR_AIUtilityComponent utility, CharacterControllerComponent controller)
	{
		if (!soldier || !utility || !controller) return;

		// 1. Cancel active combat movement
		if (utility.m_CombatMoveState)
		{
			utility.m_CombatMoveState.CancelRequest();
		}

		// 3. Lower weapon and holster if possible
		controller.SetWeaponRaised(false);
		controller.TryEquipRightHandItem(null, EEquipItemType.EEquipTypeWeapon, false);

		// 4. Stance crouch / surrender posture
		SCR_AIStanceHandling.SetStance(controller, ECharacterStance.CROUCH);

		// 5. Deactivate AI so the surrendered soldier stays in place quietly
		AIControlComponent ai = AIControlComponent.Cast(soldier.FindComponent(AIControlComponent));
		bool wasActivated = false;
		if (ai)
		{
			wasActivated = ai.IsAIActivated();
			if (wasActivated) ai.DeactivateAI();
		}

		// 6. Store state
		BBB_SurrenderState sState = new BBB_SurrenderState();
		sState.m_Soldier = soldier;
		sState.m_Utility = utility;
		sState.m_fSurrenderSince = GetGame().GetWorld().GetWorldTime();
		sState.m_bDisarmed = true;
		sState.m_bWasActivated = wasActivated;
		s_Surrendered.Insert(sState);

		s_iSurrenders++;
		PrintFormat("[BBB] MORALE: Soldier %1 surrendered (out of ammo, suppressed, enemy within %2 m)",
			soldier, SURRENDER_DIST_MAX);
			BBB_Think.Note(soldier, "Morale", "SURRENDERS: out of ammo, suppressed, enemy close");
	}

	// ---------------------------------------------------------------- main tick

	static void Tick()
	{
		Read();
		if (!s_bOn) return;

		float now = GetGame().GetWorld().GetWorldTime() / 1000.0;

		array<SCR_AIGroup> groups = {};
		BBB.s_Hooks.GetGroups(groups);

		// Clean up invalid states
		for (int i = s_States.Count() - 1; i >= 0; i--)
		{
			BBB_GroupMoraleState gs = s_States[i];
			if (!gs || !gs.m_Group || !groups.Contains(gs.m_Group))
			{
				if (gs) CancelMoves(gs);
				s_States.RemoveOrdered(i);
			}
		}

		// Clean up surrendered soldiers
		for (int sIdx = s_Surrendered.Count() - 1; sIdx >= 0; sIdx--)
		{
			BBB_SurrenderState ss = s_Surrendered[sIdx];
			if (!ss || !ss.m_Soldier || !BBB.IsAlive(ss.m_Soldier))
			{
				s_Surrendered.RemoveOrdered(sIdx);
			}
		}

		foreach (SCR_AIGroup group : groups)
		{
			if (!group || BBB.HasPlayer(group) || !BBB.s_Hooks.AllowMorale(group)) continue;

			// Don't manage mounted vehicle squads
			IEntity vehicle = BBB_Transport.CrewedVehicle(group);
			if (vehicle) continue;

			array<AIAgent> agents = {};
			BBB.s_Hooks.GetMembers(group, agents);
			if (agents.IsEmpty()) continue;

			BBB_GroupMoraleState state = GetOrCreateState(group, agents.Count());
			if (!state) continue;

			// Active withdrawal update
			if (state.m_bWithdrawing)
			{
				UpdateWithdrawal(state, now);
				continue;
			}

			// Throttled assessment
			if (now < state.m_fNextCheck) continue;
			state.m_fNextCheck = now + (CHECK_INTERVAL_MS / 1000.0);

			int aliveCount;
			int outOfAmmoCount;
			state.m_fMorale = CalculateGroupMorale(state, agents, aliveCount, outOfAmmoCount);

			// Check surrender for lone survivors
			if (aliveCount == 1 && s_bSurrenderOn)
			{
				foreach (AIAgent a : agents)
				{
					if (!a) continue;
					ChimeraCharacter ch = ChimeraCharacter.Cast(a.GetControlledEntity());
					if (ch && BBB.IsAlive(ch))
					{
						SCR_AIUtilityComponent u = SCR_AIUtilityComponent.Cast(a.FindComponent(SCR_AIUtilityComponent));
						if (u) CheckSurrender(ch, u);
					}
				}
			}

			// Check if morale broke and threat exists
			if (state.m_fMorale >= s_fBreakScore && aliveCount > 0 && now >= state.m_fNextWithdrawAllowed)
			{
				vector threatCenter;
				if (GetThreatCentroid(group, threatCenter))
				{
					StartWithdrawal(state, threatCenter, now);
				}
			}
		}
	}

	static void Stop()
	{
		// Restore AI for surrendered soldiers
		foreach (BBB_SurrenderState ss : s_Surrendered)
		{
			if (ss && ss.m_Soldier && ss.m_bWasActivated && BBB.IsAlive(ss.m_Soldier))
			{
				AIControlComponent ai = AIControlComponent.Cast(ss.m_Soldier.FindComponent(AIControlComponent));
				if (ai && !ai.IsAIActivated()) ai.ActivateAI();
			}
		}

		s_Surrendered.Clear();
		foreach (BBB_GroupMoraleState gone : s_States)
		{
			if (gone) CancelMoves(gone);
		}
		s_States.Clear();
	}
}

modded class SCR_AIUtilityComponent
{
	bool m_bBBBHoldGround; // in a withdrawal's covering half: fight from here, don't advance
	BBB_AIWithdrawMove m_BBBWithdrawRun; // his live run back (BBB_GroupMoraleState holds it)
}

// A man firing at a high-scoring target attacks above any normal-level move (1120), so the
// covering half never started its run. During his run back attack stays below it, as it
// yields to a drag, unless he must fight off a visible enemy on top of him (BBB_Drag.Defend) or
// a player or GM order holds him.
modded class SCR_AIAttackBehavior
{
	override float CustomEvaluate()
	{
		float priority = super.CustomEvaluate();
		if (priority < BBB_Morale.MOVE_PRIORITY || !m_Utility || !BBB_Morale.Running(m_Utility.m_BBBWithdrawRun) || BBB_Drag.Defend(m_Utility) || BBB_OrderIntent.Held(m_Utility)) return priority;
		return PRIORITY_BEHAVIOR_ATTACK_SELECTED;
	}
}
