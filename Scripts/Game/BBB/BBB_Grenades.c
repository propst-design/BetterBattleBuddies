// Better Battle Buddies (BBB): Tactical hand grenade employment with friendly
// safety clearance and cover checks.
//
// Standalone tactical behavior for autonomous infantry squads:
// 1. Offensive grenade employment: when squads engage enemies in cover, trenches,
//    or rooms within throwing distance (12 to 38 m), soldiers carrying fragmentation
//    grenades (EWeaponType.WT_FRAGGRENADE) deploy them to flush out fortified defenders.
// 2. Strict friendly clearance checks:
//    - Blast clearance: no friendly unit may be within 14 m of the target blast point.
//    - Corridor clearance: no friendly unit may stand within the throwing corridor (2.5 m radius).
//    - Immediate obstruction check: ensures the throw trajectory is unobstructed within
//      3 m in front of the thrower (no close wall/window frame bounce-backs).
// 3. Realistic target constraints: only thrown at real perceived/detected enemy targets
//    with recent detection (<= 12 s) and partial/full cover. Never blind fires or invents
//    enemy positions.
// 4. Strict safety constraints: thrower must not be in vehicle, player-controlled,
//    unconscious, reloading, changing weapons, climbing, swimming, or blocked by explicit
//    player/GM orders (BBB_OrderIntent.Blocks). Enforces 25 s squad cooldown. Budget <= 0.1 ms/tick.

class BBB_Grenades
{
	static int s_iRead;
	static bool s_bOn = true;

	// Tunable parameters
	static float s_fMinThrowDistM = 12.0;
	static float s_fMaxThrowDistM = 38.0;
	static float s_fFriendlySafetyDistM = 14.0;
	static float s_fThrowCorridorRadiusM = 2.5;
	static float s_fSquadCooldownS = 25.0;
	static const float RECENT_TARGET_MAX_S = 12.0;
	static const float CHECK_INTERVAL_MS = 2000.0;

	// Squad cooldown tracking: Group -> world time in ms of last grenade throw
	protected static ref map<SCR_AIGroup, float> s_mGroupCooldown = new map<SCR_AIGroup, float>();

	// Per-group check throttle: Group -> next allowed evaluation time in ms
	protected static ref map<SCR_AIGroup, float> s_mNextCheck = new map<SCR_AIGroup, float>();

	// Scratch query buffer
	protected static ref array<IEntity> s_aNearbyEntities = new array<IEntity>();

	// Performance and test counters
	static int s_iGrenadesThrown;
	static int s_iSafetyVetoes;
	static int s_iCorridorVetoes;
	static int s_iArcVetoes;

	// ---------------------------------------------------------------- configuration

	static void Read()
	{
		if (!BBB_Settings.Due(s_iRead)) return;

		string value;
		if (BBB_Settings.Get("bbbGrenades", value))
		{
			s_bOn = value == "1";
		}

		if (BBB_Settings.Get("bbbGrenadesMinDist", value))
		{
			float minDist = value.ToFloat();
			if (minDist >= 8.0 && minDist <= 25.0) s_fMinThrowDistM = minDist;
		}

		if (BBB_Settings.Get("bbbGrenadesMaxDist", value))
		{
			float maxDist = value.ToFloat();
			if (maxDist >= 20.0 && maxDist <= 45.0) s_fMaxThrowDistM = maxDist;
		}

		if (BBB_Settings.Get("bbbGrenadesFriendlyDist", value))
		{
			float safeDist = value.ToFloat();
			if (safeDist >= 8.0 && safeDist <= 25.0) s_fFriendlySafetyDistM = safeDist;
		}

		if (BBB_Settings.Get("bbbGrenadesCooldown", value))
		{
			float cd = value.ToFloat();
			if (cd >= 5.0 && cd <= 120.0) s_fSquadCooldownS = cd;
		}

		if (BBB_Settings.Get("bbbGrenadesCorridor", value)) s_fThrowCorridorRadiusM = Math.Clamp(value.ToFloat(), 1.0, 5.0);

		PrintFormat("[BBB] GRENADES enabled=%1 range=%2..%3m safeDist=%4m cooldown=%5s",
			s_bOn, s_fMinThrowDistM, s_fMaxThrowDistM, s_fFriendlySafetyDistM, s_fSquadCooldownS);
	}

	static void Stop()
	{
		s_mGroupCooldown.Clear();
		s_mNextCheck.Clear();
		s_aNearbyEntities.Clear();
		s_iGrenadesThrown = 0;
		s_iSafetyVetoes = 0;
		s_iCorridorVetoes = 0;
	}

	// ---------------------------------------------------------------- inventory and readiness

	// Check if soldier carries a fragmentation hand grenade
	static bool HasFragGrenade(IEntity soldier)
	{
		if (!soldier) return false;
		SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(soldier.FindComponent(SCR_AICombatComponent));
		if (!combat) return false;
		return combat.FindWeaponOfType(EWeaponType.WT_FRAGGRENADE) != null;
	}

	// Check if soldier is fit to throw a grenade
	static bool IsFitThrower(ChimeraCharacter soldier, SCR_AIUtilityComponent utility)
	{
		if (!soldier || !utility) return false;
		if (!BBB.IsAlive(soldier)) return false;

		CharacterControllerComponent controller = soldier.GetCharacterController();
		if (!controller || controller.IsDead() || controller.IsUnconscious() || controller.IsPlayerControlled()) return false;
		if (controller.IsReloading() || controller.IsChangingItem() || controller.IsClimbing() || controller.IsSwimming() || controller.GetDisableWeaponControls()) return false;

		if (soldier.IsInVehicle()) return false;
		if (utility.m_AIInfo && (utility.m_AIInfo.HasUnitState(EUnitState.IN_VEHICLE) || utility.m_AIInfo.HasUnitState(EUnitState.UNCONSCIOUS))) return false;

		if (BBB_OrderIntent.Blocks(utility)) return false;
		if (BBB_Drag.Busy(soldier) || BBB_Claims.Committed(soldier)) return false;

		return HasFragGrenade(soldier);
	}

	// ---------------------------------------------------------------- geometry & line-sphere

	// Point-segment distance to sphere center test
	static bool LineIntersectsSphere(vector from, vector to, vector center, float radius)
	{
		vector dir = to - from;
		float lenSq = dir.LengthSq();
		if (lenSq < 0.001) return vector.DistanceSq(from, center) <= radius * radius;
		vector fromToCenter = center - from;
		float t = vector.Dot(fromToCenter, dir) / lenSq;
		t = Math.Clamp(t, 0.0, 1.0);
		vector closest = from + dir * t;
		return vector.DistanceSq(closest, center) <= radius * radius;
	}

	// ---------------------------------------------------------------- friendly clearance checks

	// Check if any friendly unit is within blast radius of the target landing position
	static bool IsBlastAreaClearOfFriendlies(vector landingPos, SCR_AIGroup throwingGroup, float safetyDist)
	{
		Faction throwerFac = throwingGroup.GetFaction();

		// 1. Check thrower's own group members
		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(throwingGroup, agents);
		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			IEntity member = agent.GetControlledEntity();
			if (!member || !BBB.IsAlive(member)) continue;

			if (vector.DistanceXZ(member.GetOrigin(), landingPos) < safetyDist)
			{
				return false;
			}
		}

		// 2. Check nearby entities within safety radius
		s_aNearbyEntities.Clear();
		GetGame().GetWorld().QueryEntitiesBySphere(landingPos, safetyDist + 2.0, CollectNearbyEntity);

		foreach (IEntity ent : s_aNearbyEntities)
		{
			ChimeraCharacter ch = ChimeraCharacter.Cast(ent);
			if (!ch || !BBB.IsAlive(ch)) continue;

			// If player-controlled, apply strict safety
			if (BBB.PlayerOf(ch))
			{
				if (vector.DistanceXZ(ch.GetOrigin(), landingPos) < safetyDist) return false;
			}

			// Check faction friendliness
			FactionAffiliationComponent aff = FactionAffiliationComponent.Cast(ch.FindComponent(FactionAffiliationComponent));
			if (aff)
			{
				Faction f = aff.GetAffiliatedFaction();
				if (!f) f = aff.GetDefaultAffiliatedFaction();
				if (f && throwerFac)
				{
					// If friendly or neutral to thrower faction
					if (!throwerFac.IsFactionEnemy(f))
					{
						if (vector.DistanceXZ(ch.GetOrigin(), landingPos) < safetyDist) return false;
					}
				}
			}
		}

		return true;
	}

	// Check if any friendly stands in the throwing corridor from thrower to target
	static bool IsThrowCorridorClear(vector throwerPos, vector landingPos, SCR_AIGroup throwingGroup, IEntity thrower)
	{
		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(throwingGroup, agents);

		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			IEntity member = agent.GetControlledEntity();
			if (!member || member == thrower || !BBB.IsAlive(member)) continue;

			if (LineIntersectsSphere(throwerPos, landingPos, member.GetOrigin(), s_fThrowCorridorRadiusM))
			{
				return false;
			}
		}

		return true;
	}

	// Check if immediate trajectory in front of thrower (3 m) is clear of solid walls/windows
	static bool IsImmediateTrajectoryClear(IEntity thrower, vector throwDir)
	{
		vector eyePos = thrower.GetOrigin() + Vector(0, 1.5, 0);
		TraceParam trace = new TraceParam();
		trace.Start = eyePos;
		trace.End = eyePos + throwDir * 3.0;
		trace.Flags = TraceFlags.WORLD;
		float fraction = GetGame().GetWorld().TraceMove(trace, null);
		return fraction >= 0.95;
	}

	protected static bool IgnoreCharacters(notnull IEntity entity)
	{
		return !ChimeraCharacter.Cast(entity);
	}

	// A frag is lobbed, not thrown flat. Under a roof, or with a wall or ceiling across the
	// rising half of the arc, it hits that and drops at the thrower's feet (seen in play: a
	// soldier killed himself throwing at a window). No frags from under a building's roof;
	// outdoors, the first 6 m of a ~45 degree lob must be clear: what it hits there lands
	// beside him. Trees or a fence further out only shorten the throw.
	static bool IsArcClear(IEntity thrower, vector landingPos)
	{
		if (BBB_Buildings.RoofAt(thrower.GetOrigin())) return false;
		vector hand = thrower.GetOrigin() + Vector(0, 1.6, 0);
		float range = vector.DistanceXZ(hand, landingPos);
		if (range < 1) return false;
		float apex = Math.Clamp(range * 0.25, 2, 10);
		float reach = Math.Min(6 / range, 0.5);
		vector previous = hand;
		for (int i = 1; i <= 6; i++)
		{
			float t = reach * i / 6.0;
			vector point = hand + (landingPos - hand) * t;
			point[1] = hand[1] + (landingPos[1] - hand[1]) * t + 4 * apex * t * (1 - t);
			TraceParam trace = new TraceParam();
			trace.Start = previous;
			trace.End = point;
			trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
			if (GetGame().GetWorld().TraceMove(trace, IgnoreCharacters) < 1) return false;
			previous = point;
		}
		return true;
	}

	// ---------------------------------------------------------------- query callback

	protected static bool CollectNearbyEntity(IEntity entity)
	{
		if (!entity || BBB.Health(entity) <= 0) return true;
		s_aNearbyEntities.Insert(entity);
		return true;
	}

	// ---------------------------------------------------------------- thrower selection

	// Find the best squadmate to throw a grenade at the designated target position
	static ChimeraCharacter FindBestThrower(SCR_AIGroup group, vector targetPos, out SCR_AIUtilityComponent outUtility)
	{
		outUtility = null;
		if (!group) return null;

		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(group, agents);

		ChimeraCharacter bestThrower = null;
		float bestDist = s_fMaxThrowDistM;

		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			IEntity controlled = agent.GetControlledEntity();
			if (!controlled) continue;

			ChimeraCharacter character = ChimeraCharacter.Cast(controlled);
			if (!character) continue;

			SCR_AIUtilityComponent util = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			if (!util || !IsFitThrower(character, util)) continue;

			float d = vector.DistanceXZ(character.GetOrigin(), targetPos);
			if (d < s_fMinThrowDistM || d > s_fMaxThrowDistM) continue;

			// Prefer closer throwers within viable range
			if (d < bestDist)
			{
				// Check corridor and immediate obstruction
				vector toTarget = targetPos - character.GetOrigin();
				toTarget[1] = 0;
				toTarget.Normalize();
				if (!IsImmediateTrajectoryClear(character, toTarget)) continue;
				if (!IsArcClear(character, targetPos)) { s_iArcVetoes++; continue; }
				if (!IsThrowCorridorClear(character.GetOrigin(), targetPos, group, character)) continue;

				bestDist = d;
				bestThrower = character;
				outUtility = util;
			}
		}

		return bestThrower;
	}

	// ---------------------------------------------------------------- throw execution

	// Attempt to deploy a hand grenade against a target position
	static bool TryThrowGrenadeAt(SCR_AIGroup group, vector targetPos, string reason)
	{
		Read();
		if (!s_bOn || !group) return false;

		// Check host hook
		if (BBB.s_Hooks && !BBB.s_Hooks.AllowGrenades(group)) return false;

		// Check player squads
		if (BBB.HasPlayer(group)) return false;

		float nowMs = GetGame().GetWorld().GetWorldTime();

		// Check squad cooldown
		if (s_mGroupCooldown.Contains(group))
		{
			float last = s_mGroupCooldown.Get(group);
			if (nowMs - last < (s_fSquadCooldownS * 1000.0)) return false;
		}

		// Check blast clearance for friendlies
		if (!IsBlastAreaClearOfFriendlies(targetPos, group, s_fFriendlySafetyDistM))
		{
			s_iSafetyVetoes++;
			return false;
		}

		// Find best thrower
		SCR_AIUtilityComponent throwerUtil;
		ChimeraCharacter thrower = FindBestThrower(group, targetPos, throwerUtil);
		if (!thrower || !throwerUtil)
		{
			return false;
		}

		// Clamp landing position to surface height
		vector landingPos = targetPos;
		landingPos[1] = GetGame().GetWorld().GetSurfaceY(landingPos[0], landingPos[2]) + 0.1;

		// Issue throw action via native behavior
		BBB_AIThrowFrag behavior = new BBB_AIThrowFrag(
			throwerUtil, null, landingPos, EWeaponType.WT_FRAGGRENADE, 0.1, SCR_AIActionBase.PRIORITY_BEHAVIOR_THROW_GRENADE
		);
		throwerUtil.AddAction(behavior);

		// Record cooldown
		s_mGroupCooldown.Set(group, nowMs);

		s_iGrenadesThrown++;
		float throwDist = vector.DistanceXZ(thrower.GetOrigin(), landingPos);
		PrintFormat("[BBB] GRENADE: %1 (%2) threw frag grenade at %3 (dist=%4m, reason=%5)",
			thrower, BBB.s_Hooks.NameOf(group), landingPos, Math.Round(throwDist), reason);
			BBB_Think.Note(thrower, "Grenades", "throws a frag " + Math.Round(throwDist) + " m (" + reason + ")");
		return true;
	}

	// ---------------------------------------------------------------- main tick

	static void Tick()
	{
		Read();
		if (!s_bOn) return;

		float nowMs = GetGame().GetWorld().GetWorldTime();

		array<SCR_AIGroup> groups = {};
		BBB.s_Hooks.GetGroups(groups);

		foreach (SCR_AIGroup group : groups)
		{
			if (!group || BBB.HasPlayer(group)) continue;

			// Check squad cooldown
			if (s_mGroupCooldown.Contains(group))
			{
				if (nowMs - s_mGroupCooldown.Get(group) < (s_fSquadCooldownS * 1000.0)) continue;
			}

			// Throttled per-group check (every 2 s)
			if (s_mNextCheck.Contains(group))
			{
				if (nowMs < s_mNextCheck.Get(group)) continue;
			}
			s_mNextCheck.Set(group, nowMs + CHECK_INTERVAL_MS);

			// Search perceived enemy targets in cover or structures within throwing distance
			array<AIAgent> agents = {};
			BBB.s_Hooks.GetMembers(group, agents);

			bool grenadeThrown = false;

			foreach (AIAgent agent : agents)
			{
				if (grenadeThrown) break;
				if (!agent) continue;
				SCR_AIUtilityComponent u = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
				if (!u || !u.m_PerceptionComponent) continue;

				array<BaseTarget> targets = {};
				u.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);

				foreach (BaseTarget target : targets)
				{
					if (!target || target.GetTargetCategory() != ETargetCategory.ENEMY) continue;
					if (target.GetUnitType() != EAIUnitType.UnitType_Infantry) continue;

					float timeSince = target.GetTimeSinceDetected();
					if (timeSince < 0 || timeSince > RECENT_TARGET_MAX_S) continue;

					float dist = target.GetDistance();
					if (dist < s_fMinThrowDistM || dist > s_fMaxThrowDistM) continue;

					// Target qualifies if in partial cover or recently seen
					// Trace fraction < 0.7 indicates soldier is behind cover or in a trench/building
					float traceFrac = target.GetTraceFraction();
					bool inCover = traceFrac < 0.7 || target.GetTimeSinceSeen() > 1.5;

					if (inCover)
					{
						vector targetPos = target.GetLastDetectedPosition();
						if (TryThrowGrenadeAt(group, targetPos, "enemy in cover"))
						{
							grenadeThrown = true;
							break;
						}
					}
				}
			}
		}

		// Prune stale group cooldown entries (> 120 s old)
		for (int c = s_mGroupCooldown.Count() - 1; c >= 0; c--)
		{
			SCR_AIGroup grp = s_mGroupCooldown.GetKey(c);
			if (!grp || nowMs - s_mGroupCooldown.GetElement(c) > 120000.0)
			{
				s_mGroupCooldown.RemoveElement(c);
			}
		}
	}
}

// The thrower may have moved (indoors, behind a wall) between being picked and the throw:
// the arc is checked again whenever the native utility evaluates the throw.
class BBB_AIThrowFrag : SCR_AIThrowGrenadeToBehavior
{
	override float CustomEvaluate()
	{
		float priority = super.CustomEvaluate();
		// Never cancel a throw already under way: only before the animation starts.
		if (priority <= 0 || !m_Utility || !m_Utility.m_OwnerEntity || GetActionState() == EAIActionState.RUNNING) return priority;
		if (BBB_Grenades.IsArcClear(m_Utility.m_OwnerEntity, m_vTargetPosition.m_Value)) return priority;
		BBB_Grenades.s_iArcVetoes++;
		Fail();
		return 0;
	}
}
