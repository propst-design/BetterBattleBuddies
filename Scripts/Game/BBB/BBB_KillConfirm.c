// Better Battle Buddies (BBB): kill uncertainty & follow-up fire (-bbbKillConfirm, default on).
// Vanilla perception drops a dead enemy on the next update and immediately sets IsDisarmed(true)
// when an enemy falls unconscious, so AI at any range instantly stop engaging and snap away.
// With this on:
//  1. When a shooter's selected infantry target drops (dead or unconscious), BBB records the
//     last-seen position (never the live entity position) for the squad.
//  2. If the shooter has no other live target and the drop was at range (>= FOLLOWUP_MIN_M) or
//     obscured, he keeps watching the spot (3-5 s) and may fire a short, disciplined follow-up
//     burst (1.8-2.5 s) into a tight box around last-seen, respecting FireDiscipline range,
//     reserve magazines and squad blind-fire limits. Any live target immediately preempts it.
//  3. BBB_Squad keeps recently-dropped contacts in the squad picture as unconfirmed for 4-12 s
//     (scaled by distance) so the squad maintains security instead of relaxing immediately.
//  4. Unverified drops are retained per squad (up to DROP_TTL_S) for post-contact clearing /
//     deadchecking (Stage 2).
class BBB_DroppedContact : Managed
{
	IEntity m_Enemy;
	vector m_vPos;        // last-seen position at drop (never updated from live entity coords)
	float m_fDroppedAt;   // world time (s)
	float m_fDistance;    // distance (m) from observer at drop
	bool m_bObscured;     // partial LOS or range >= OBSCURED_M
	bool m_bChecked;      // verified by a deadcheck / capture pass
}

class BBB_KillConfirm
{
	static int s_iRead;
	static bool s_bOn = true;

	static const float FOLLOWUP_MIN_M = 25.0;      // closer in the open: shooter can tell he dropped
	static const float OBSCURED_M = 40.0;          // past this range a drop is treated as uncertain
	static const float OBSCURED_TRACE = 0.85;      // below this trace fraction counts as obscured
	static const float RECENT_SEEN_S = 5.0;        // only react to a target actually seen recently
	static const float COOLDOWN_S = 14.0;          // per-shooter cooldown between follow-up bursts
	static const float BURST_MIN_S = 1.8;
	static const float BURST_MAX_S = 2.5;
	static const float OVERWATCH_MIN_S = 3.0;
	static const float OVERWATCH_MAX_S = 5.0;
	static const float HOLD_NEAR_S = 4.0;          // squad-picture hold at <= 35 m
	static const float HOLD_FAR_S = 12.0;          // squad-picture hold at >= 120 m
	static const float HOLD_NEAR_M = 35.0;
	static const float HOLD_FAR_M = 120.0;
	static const float DROP_TTL_S = 60.0;          // unverified drop memory for deadchecking
	static const int MAX_PER_SQUAD = 8;
	static const float PRIORITY = 69.5;            // below ATTACK_SELECTED (90) and HEAL (111)
	static const float FIRE_RATE = 1.0;

	// Counters for tests and diagnostics.
	static int s_iDropsRecorded;
	static int s_iFollowUps;
	static int s_iOverwatches;

	protected static ref map<SCR_AIGroup, ref array<ref BBB_DroppedContact>> s_mDrops;
	protected static ref map<string, ref set<IEntity>> s_mVerified;

	// Called by BBB_StaticInit at game start (static initializer budget).
	static void BBB_InitStatics()
	{
		s_mDrops = new map<SCR_AIGroup, ref array<ref BBB_DroppedContact>>();
		s_mVerified = new map<string, ref set<IEntity>>();
	}

	static void Read()
	{
		if (!BBB_Settings.Due(s_iRead)) return;
		string value;
		s_bOn = !(BBB_Settings.Get("bbbKillConfirm", value) && value == "0");
		PrintFormat("[BBB] KILL CONFIRM on=%1", s_bOn);
	}

	static bool On()
	{
		Read();
		return s_bOn;
	}

	static void Stop()
	{
		if (s_mDrops) s_mDrops.Clear();
		if (s_mVerified) s_mVerified.Clear();
		s_iDropsRecorded = 0;
		s_iFollowUps = 0;
		s_iOverwatches = 0;
	}

	protected static float Now()
	{
		return GetGame().GetWorld().GetWorldTime() / 1000.0;
	}

	// How long BBB_Squad keeps a dropped contact as unconfirmed in the squad picture.
	static float HoldWindow(float distance)
	{
		float t = Math.Clamp((distance - HOLD_NEAR_M) / (HOLD_FAR_M - HOLD_NEAR_M), 0.0, 1.0);
		return HOLD_NEAR_S + (HOLD_FAR_S - HOLD_NEAR_S) * t;
	}

	// Mark an enemy entity as verified dead/secured for this faction so other squads don't re-check.
	static void MarkVerified(Faction faction, IEntity enemy)
	{
		if (!faction || !enemy || !s_mVerified) return;
		string key = faction.GetFactionKey();
		if (key.IsEmpty()) return;
		set<IEntity> known = s_mVerified.Get(key);
		if (!known)
		{
			known = new set<IEntity>();
			s_mVerified.Set(key, known);
		}
		known.Insert(enemy);
	}

	static bool IsVerified(Faction faction, IEntity enemy)
	{
		if (!faction || !enemy || !s_mVerified) return false;
		set<IEntity> known = s_mVerified.Get(faction.GetFactionKey());
		return known && known.Contains(enemy);
	}

	// Record an unconfirmed drop for a squad using only the last-seen position.
	static void RecordDrop(SCR_AIGroup group, IEntity enemy, vector lastSeenPos, float distance, bool obscured)
	{
		if (!group || !enemy || !s_mDrops || lastSeenPos == vector.Zero) return;
		Faction side = group.GetFaction();
		if (side && IsVerified(side, enemy)) return;
		float now = Now();
		array<ref BBB_DroppedContact> list = s_mDrops.Get(group);
		if (!list)
		{
			list = {};
			s_mDrops.Set(group, list);
		}
		foreach (BBB_DroppedContact existing : list)
		{
			if (existing && existing.m_Enemy == enemy)
			{
				// Keep the earliest drop timestamp and freshest observed last-seen position.
				existing.m_vPos = lastSeenPos;
				if (distance < existing.m_fDistance) existing.m_fDistance = distance;
				return;
			}
		}
		if (list.Count() >= MAX_PER_SQUAD) list.RemoveOrdered(0);
		BBB_DroppedContact drop = new BBB_DroppedContact();
		drop.m_Enemy = enemy;
		drop.m_vPos = lastSeenPos;
		drop.m_fDroppedAt = now;
		drop.m_fDistance = distance;
		drop.m_bObscured = obscured;
		list.Insert(drop);
		s_iDropsRecorded++;
	}

	// Returns unverified drops recorded for this group (used by Stage 2 deadchecking).
	static void GetDrops(SCR_AIGroup group, notnull array<BBB_DroppedContact> outDrops)
	{
		outDrops.Clear();
		if (!group || !s_mDrops) return;
		array<ref BBB_DroppedContact> list = s_mDrops.Get(group);
		if (!list) return;
		foreach (BBB_DroppedContact drop : list)
		{
			if (drop && !drop.m_bChecked) outDrops.Insert(drop);
		}
	}

	// Called from SCR_AICombatComponent.EvaluateWeaponAndTarget when a soldier's selected target changes.
	static void OnTargetLost(SCR_AIUtilityComponent utility, SCR_AICombatComponent combat, BaseTarget prevTarget, BaseTarget newTarget)
	{
		if (!On() || !BBB.IsStarted() || !Replication.IsServer()) return;
		if (!utility || !combat || !prevTarget || !utility.m_AIInfo) return;
		if (prevTarget.GetTargetCategory() != ETargetCategory.ENEMY) return;
		if (prevTarget.GetUnitType() != EAIUnitType.UnitType_Infantry) return;
		float seenAge = prevTarget.GetTimeSinceSeen();
		if (seenAge < 0 || seenAge > RECENT_SEEN_S) return;

		IEntity man = utility.m_OwnerEntity;
		if (!man || !BBB.IsConscious(man) || BBB.PlayerOf(man) != 0 || !utility.IsMilitary()) return;
		if (utility.m_AIInfo.HasUnitState(EUnitState.IN_VEHICLE) || utility.m_AIInfo.HasUnitState(EUnitState.IN_TURRET) || utility.m_AIInfo.HasUnitState(EUnitState.UNCONSCIOUS)) return;

		IEntity enemy = prevTarget.GetTargetEntity();
		if (!enemy || !ChimeraCharacter.Cast(enemy)) return;
		// Only react when the target actually went dead or unconscious (not a surrendered prisoner or live target who ran behind a wall).
		if (BBB.IsConscious(enemy) || BBB_Morale.IsPrisoner(enemy)) return;

		vector lastSeen = prevTarget.GetLastSeenPosition();
		if (lastSeen == vector.Zero) return;
		float distance = vector.DistanceXZ(man.GetOrigin(), lastSeen);
		bool obscured = distance >= OBSCURED_M || prevTarget.GetTraceFraction() < OBSCURED_TRACE;

		SCR_AIGroup group = BBB_FireDiscipline.GroupOf(utility);
		if (group) RecordDrop(group, enemy, lastSeen, distance, obscured);

		// Any live target already selected: engage it immediately, no follow-up on the dropped one.
		if (newTarget) return;
		if (distance < FOLLOWUP_MIN_M && !obscured) return;
		if (combat.GetCombatMode() == EAIGroupCombatMode.HOLD_FIRE) return;
		if (BBB_OrderIntent.Blocks(utility) || BBB_Claims.Committed(man)) return;

		float now = Now();
		float overwatchS = Math.Lerp(OVERWATCH_MIN_S, OVERWATCH_MAX_S, Math.Clamp((distance - FOLLOWUP_MIN_M) / 150.0, 0.0, 1.0));
		if (utility.m_LookAction)
		{
			utility.m_LookAction.LookAt(lastSeen + Vector(0, 0.6, 0), SCR_AILookAction.PRIO_COMMANDER, overwatchS);
			s_iOverwatches++;
		}

		// Follow-up confirmation burst: short, disciplined, capped by weapon range and squad blind-fire limit.
		if (now < utility.m_fBBBKillConfirmNext) return;
		BaseWeaponComponent weapon = combat.GetCurrentWeapon();
		if (!weapon) return;
		EWeaponType wType = weapon.GetWeaponType();
		if (wType != EWeaponType.WT_RIFLE && wType != EWeaponType.WT_MACHINEGUN && wType != EWeaponType.WT_SNIPERRIFLE) return;
		BaseMagazineComponent mag = weapon.GetCurrentMagazine();
		if (!mag || mag.GetAmmoCount() <= 0) return;

		// Respect FireDiscipline range, reserve mags, and squad blind-fire cap.
		if (BBB_FireDiscipline.On())
		{
			string refuse = BBB_FireDiscipline.Refuse(utility, weapon, distance);
			if (refuse != "")
			{
				BBB_Think.NoteUtility(utility, "KillConfirm", "watches dropped contact at " + Math.Round(distance) + " m (burst held: " + refuse + ")");
				return;
			}
		}

		// Probability scales with distance and concealment: ~35% near open, up to ~65% far/obscured.
		float chance = 0.35 + 0.25 * Math.Clamp((distance - FOLLOWUP_MIN_M) / 150.0, 0.0, 1.0);
		if (obscured) chance = Math.Min(0.70, chance + 0.15);
		vector me = man.GetOrigin();
		RandomGenerator roll = new RandomGenerator();
		roll.SetSeed((int)(me[0] * 10) * 7919 + (int)(me[2] * 10) * 31 + (int)(now * 10));
		if (roll.RandFloat01() >= chance)
		{
			BBB_Think.NoteUtility(utility, "KillConfirm", "holds overwatch on dropped contact (" + Math.Round(distance) + " m)");
			return;
		}

		vector aim = BBB.Ground(lastSeen);
		if (!BBB_SuppressionSafety.GroundSector(aim)) return;
		float half = Math.Clamp(distance * 0.015, 1.0, 3.5);
		float burstS = Math.Lerp(BURST_MIN_S, BURST_MAX_S, roll.RandFloat01());
		SCR_AISuppressionVolumeBox volume = new SCR_AISuppressionVolumeBox(
			Vector(aim[0] - half, aim[1] - 0.3, aim[2] - half),
			Vector(aim[0] + half, aim[1] + 1.4, aim[2] + half)
		);
		BBB_AIKillConfirmFireBehavior fire = new BBB_AIKillConfirmFireBehavior(utility, null, volume, burstS, FIRE_RATE);
		utility.AddAction(fire);
		utility.m_fBBBKillConfirmNext = now + burstS + COOLDOWN_S;
		utility.m_fBBBBlindUntil = now + burstS;
		s_iFollowUps++;
		BBB_Perf.Count("killConfirm.followUp");
		BBB_Think.NoteUtility(utility, "KillConfirm", "follow-up burst on dropped contact (" + Math.Round(distance) + " m, " + burstS.ToString(3, 1) + " s)");
	}

	// Prune expired drops and stale verified entries.
	static void Tick()
	{
		if (!s_mDrops || s_mDrops.IsEmpty()) return;
		float now = Now();
		for (int i = s_mDrops.Count() - 1; i >= 0; i--)
		{
			SCR_AIGroup group = s_mDrops.GetKey(i);
			array<ref BBB_DroppedContact> list = s_mDrops.GetElement(i);
			if (!group || !list)
			{
				s_mDrops.RemoveElement(i);
				continue;
			}
			for (int j = list.Count() - 1; j >= 0; j--)
			{
				BBB_DroppedContact drop = list[j];
				if (!drop || now - drop.m_fDroppedAt > DROP_TTL_S) list.RemoveOrdered(j);
			}
			if (list.IsEmpty()) s_mDrops.RemoveElement(i);
		}
		if (s_mVerified)
		{
			for (int v = s_mVerified.Count() - 1; v >= 0; v--)
			{
				set<IEntity> known = s_mVerified.GetElement(v);
				if (!known) { s_mVerified.RemoveElement(v); continue; }
				for (int e = known.Count() - 1; e >= 0; e--)
					if (!known[e]) known.Remove(e);
			}
		}
	}
}

// Short, tight confirmation burst on a contact that just dropped at range.
// Yields immediately to any live target, order, hold fire, or incapacitation.
class BBB_AIKillConfirmFireBehavior : SCR_AISuppressBehavior
{
	protected float m_fEndMs;

	void BBB_AIKillConfirmFireBehavior(SCR_AIUtilityComponent utility, SCR_AIActivityBase groupActivity, SCR_AISuppressionVolumeBase suppressionVolume, float suppressionDuration, float fireRate, float priorityLevel = PRIORITY_LEVEL_NORMAL)
	{
		m_fEndMs = GetGame().GetWorld().GetWorldTime() + suppressionDuration * 1000.0;
		SetIsUniqueInActionQueue(true);
	}

	protected void Done()
	{
		if (m_Utility) m_Utility.m_fBBBBlindUntil = 0;
		Complete();
	}

	override float CustomEvaluate()
	{
		if (GetGame().GetWorld().GetWorldTime() > m_fEndMs) { Done(); return 0; }
		if (!m_Utility || !m_Utility.m_CombatComponent || !m_Utility.m_AIInfo) { Done(); return 0; }
		if (!BBB_KillConfirm.s_bOn || !BBB.IsConscious(m_Utility.m_OwnerEntity) || BBB.PlayerOf(m_Utility.m_OwnerEntity) != 0) { Done(); return 0; }
		if (m_Utility.m_CombatComponent.GetCurrentTarget()) { Done(); return 0; }
		if (m_Utility.m_CombatComponent.GetCombatMode() == EAIGroupCombatMode.HOLD_FIRE) { Done(); return 0; }
		if (m_Utility.m_AIInfo.HasUnitState(EUnitState.IN_VEHICLE) || m_Utility.m_AIInfo.HasUnitState(EUnitState.IN_TURRET) || m_Utility.m_AIInfo.HasUnitState(EUnitState.UNCONSCIOUS)) { Done(); return 0; }
		if (BBB_OrderIntent.Held(m_Utility)) { Done(); return 0; }
		return BBB_KillConfirm.PRIORITY;
	}
}

modded class SCR_AIUtilityComponent
{
	float m_fBBBKillConfirmNext; // world time (s) next follow-up burst is allowed
}

modded class SCR_AICombatComponent
{
	override void EvaluateWeaponAndTarget(out bool outWeaponEvent, out bool outSelectedTargetChanged,
		out BaseTarget outPrevTarget, out BaseTarget outCurrentTarget,
		out bool outRetreatTargetChanged, out bool outCompartmentChanged)
	{
		super.EvaluateWeaponAndTarget(outWeaponEvent, outSelectedTargetChanged, outPrevTarget, outCurrentTarget, outRetreatTargetChanged, outCompartmentChanged);
		if (outSelectedTargetChanged && outPrevTarget)
			BBB_KillConfirm.OnTargetLost(m_Utility, this, outPrevTarget, outCurrentTarget);
	}
}
