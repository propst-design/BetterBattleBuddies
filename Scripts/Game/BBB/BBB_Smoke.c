// Better Battle Buddies (BBB): Tactical smoke deployment to screen casualty
// drags (BBB_Drag), bounding maneuver advances (BBB_FireManeuver), and retrograde
// break-contact withdrawals (BBB_Morale).
//
// Standalone tactical behavior for autonomous infantry squads:
// 1. Tactical screening: when squads bound under fire, drag a wounded buddy, or
//    peel back in a retrograde withdrawal, an overwatch or support member with
//    a smoke grenade (EWeaponType.WT_SMOKEGRENADE) deploys smoke toward the perceived
//    threat sector centroid.
// 2. Realistic threat constraint: smoke is ONLY thrown toward real perceived/detected
//    enemy positions (from perception/combat components). Positions are never invented.
// 3. Occlusion tracking: active smoke clouds are tracked with bloom (4 s) and
//    effective duration (30 s) for spherical line-of-sight occlusion (IsOccluded).
// 4. Strict safety: never throws if thrower is in vehicle, player-controlled,
//    unconscious, reloading, changing item, or blocked by explicit player/GM orders
//    (BBB_OrderIntent.Blocks). Cooldowns prevent grenade spam. Budget <= 0.1 ms/tick.

class BBB_SmokeCloud : Managed
{
	vector m_vPosition;
	float m_fCreatedMs;
	float m_fBloomMs;
	float m_fExpireMs;
	float m_fRadius;
	string m_sReason;
}

class BBB_Smoke
{
	static int s_iRead;
	static bool s_bOn = true;
	static bool s_bDrag = true;
	static bool s_bManeuver = true;
	static bool s_bMorale = true;

	// Tunable parameters
	static float s_fCooldownSquadS = 30.0;
	static float s_fCooldownTargetS = 45.0;
	static float s_fThrowDistanceM = 20.0;
	static const float MAX_THROW_REACH_M = 38.0;
	static const float SMOKE_BLOOM_S = 4.0;
	static const float SMOKE_DURATION_S = 30.0;
	static const float SMOKE_RADIUS = 9.0;
	static const float RECENT_THREAT_MAX_S = 20.0;

	// Active smoke clouds
	protected static ref array<ref BBB_SmokeCloud> s_aClouds = new array<ref BBB_SmokeCloud>();

	// Squad cooldown tracking: Group -> world time in ms of last smoke throw
	protected static ref map<SCR_AIGroup, float> s_mGroupCooldown = new map<SCR_AIGroup, float>();

	// Performance and test counters
	static int s_iSmokeThrown;
	static int s_iDragSmoke;
	static int s_iManeuverSmoke;
	static int s_iMoraleSmoke;

	// ---------------------------------------------------------------- configuration

	static void Read()
	{
		if (!BBB_Settings.Due(s_iRead)) return;

		string value;
		if (BBB_Settings.Get("bbbSmoke", value))
		{
			s_bOn = value == "1";
		}

		if (BBB_Settings.Get("bbbSmokeDrag", value))
		{
			s_bDrag = value != "0";
		}

		if (BBB_Settings.Get("bbbSmokeManeuver", value))
		{
			s_bManeuver = value != "0";
		}

		if (BBB_Settings.Get("bbbSmokeMorale", value))
		{
			s_bMorale = value != "0";
		}

		if (BBB_Settings.Get("bbbSmokeCooldown", value))
		{
			float cd = value.ToFloat();
			if (cd >= 5.0 && cd <= 180.0) s_fCooldownSquadS = cd;
		}

		if (BBB_Settings.Get("bbbSmokeDist", value))
		{
			float dist = value.ToFloat();
			if (dist >= 8.0 && dist <= 35.0) s_fThrowDistanceM = dist;
		}

		if (BBB_Settings.Get("bbbSmokeTargetCooldown", value)) s_fCooldownTargetS = Math.Clamp(value.ToFloat(), 10.0, 180.0);

		PrintFormat("[BBB] SMOKE enabled=%1 drag=%2 maneuver=%3 morale=%4 cooldown=%5s",
			s_bOn, s_bDrag, s_bManeuver, s_bMorale, s_fCooldownSquadS);
	}

	static void Stop()
	{
		s_aClouds.Clear();
		s_mGroupCooldown.Clear();
		s_iSmokeThrown = 0;
		s_iDragSmoke = 0;
		s_iManeuverSmoke = 0;
		s_iMoraleSmoke = 0;
	}

	// ---------------------------------------------------------------- inventory and readiness

	// Check if soldier carries a smoke grenade in inventory
	static bool HasSmokeGrenade(IEntity soldier)
	{
		if (!soldier) return false;
		SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(soldier.FindComponent(SCR_AICombatComponent));
		if (!combat) return false;
		return combat.FindWeaponOfType(EWeaponType.WT_SMOKEGRENADE) != null;
	}

	// Check if soldier is fit to execute a smoke throw
	static bool IsFitThrower(IEntity soldier, SCR_AIUtilityComponent utility)
	{
		if (!soldier || !utility) return false;
		if (!BBB.IsAlive(soldier)) return false;

		ChimeraCharacter character = ChimeraCharacter.Cast(soldier);
		if (!character) return false;

		CharacterControllerComponent controller = character.GetCharacterController();
		if (!controller || controller.IsDead() || controller.IsUnconscious() || controller.IsPlayerControlled()) return false;
		if (controller.IsReloading() || controller.IsChangingItem() || controller.IsClimbing() || controller.IsSwimming() || controller.GetDisableWeaponControls()) return false;

		if (character.IsInVehicle()) return false;
		if (utility.m_AIInfo && (utility.m_AIInfo.HasUnitState(EUnitState.IN_VEHICLE) || utility.m_AIInfo.HasUnitState(EUnitState.UNCONSCIOUS))) return false;

		if (BBB_OrderIntent.Blocks(utility)) return false;
		if (BBB_Drag.Busy(soldier) || BBB_Claims.Committed(soldier)) return false;

		return HasSmokeGrenade(soldier);
	}

	// ---------------------------------------------------------------- threat centroid evaluation

	// Find the perceived threat centroid from group perception (no map knowledge or invented positions)
	static bool GetThreatCentroid(SCR_AIGroup group, out vector outThreatCenter)
	{
		outThreatCenter = vector.Zero;
		if (!group) return false;
		// The squad's main front, not the average of every report (BBB_Squad).
		if (BBB_Squad.On(group)) return BBB_Squad.Threat(group, RECENT_THREAT_MAX_S, outThreatCenter);

		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(group, agents);
		int count = 0;
		vector sum = vector.Zero;

		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			if (!utility) continue;

			if (utility.m_CombatComponent)
			{
				BaseTarget current = utility.m_CombatComponent.GetCurrentTarget();
				if (current && current.GetTargetCategory() == ETargetCategory.ENEMY)
				{
					sum = sum + current.GetLastDetectedPosition();
					count++;
				}
			}

			if (utility.m_PerceptionComponent)
			{
				array<BaseTarget> targets = {};
				utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
				foreach (BaseTarget target : targets)
				{
					if (!target || target.GetTargetCategory() != ETargetCategory.ENEMY) continue;
					float timeSince = target.GetTimeSinceDetected();
					if (timeSince >= 0 && timeSince <= RECENT_THREAT_MAX_S)
					{
						sum = sum + target.GetLastDetectedPosition();
						count++;
					}
				}
			}
		}

		if (count == 0) return false;
		outThreatCenter = sum * (1.0 / count);
		return true;
	}

	// ---------------------------------------------------------------- thrower selection

	// Select the best fit thrower from candidate utilities or group members within reach
	static IEntity FindBestThrower(SCR_AIGroup group, vector smokePos, array<SCR_AIUtilityComponent> preferred = null, IEntity exclude = null)
	{
		if (!group) return null;

		IEntity bestCandidate;
		float bestDist = MAX_THROW_REACH_M;

		// 1. Search preferred utilities (e.g., covering/overwatch element)
		if (preferred)
		{
			foreach (SCR_AIUtilityComponent prefUtil : preferred)
			{
				if (!prefUtil) continue;
				IEntity ent = prefUtil.m_OwnerEntity;
				if (!ent || ent == exclude) continue;
				if (!IsFitThrower(ent, prefUtil)) continue;

				float d = vector.DistanceXZ(ent.GetOrigin(), smokePos);
				if (d <= bestDist)
				{
					bestDist = d;
					bestCandidate = ent;
				}
			}
			if (bestCandidate) return bestCandidate;
		}

		// 2. Search all group members
		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(group, agents);
		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			IEntity member = agent.GetControlledEntity();
			if (!member || member == exclude) continue;

			SCR_AIUtilityComponent util = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			if (!util || !IsFitThrower(member, util)) continue;

			float dMem = vector.DistanceXZ(member.GetOrigin(), smokePos);
			if (dMem <= bestDist)
			{
				bestDist = dMem;
				bestCandidate = member;
			}
		}

		return bestCandidate;
	}

	// ---------------------------------------------------------------- cooldown and cloud deduplication

	protected static bool IsGroupOnCooldown(SCR_AIGroup group, float nowMs)
	{
		if (!group) return true;
		if (s_mGroupCooldown.Contains(group))
		{
			float last = s_mGroupCooldown.Get(group);
			if (nowMs - last < (s_fCooldownSquadS * 1000.0)) return true;
		}
		return false;
	}

	// Check if an existing blooming/active smoke screen already covers the target area
	protected static bool IsSmokeAlreadyPresent(vector smokePos, float nowMs)
	{
		for (int i = s_aClouds.Count() - 1; i >= 0; i--)
		{
			BBB_SmokeCloud cloud = s_aClouds[i];
			if (!cloud) continue;
			// Still smoking, or thrown there less than the spot cooldown ago (-bbbSmokeTargetCooldown)
			if (nowMs <= cloud.m_fExpireMs || nowMs - cloud.m_fCreatedMs < s_fCooldownTargetS * 1000.0)
			{
				if (vector.DistanceXZ(smokePos, cloud.m_vPosition) < (cloud.m_fRadius * 1.5))
				{
					return true;
				}
			}
		}
		return false;
	}

	// ---------------------------------------------------------------- deployment execution

	// Primary smoke deployment dispatch
	static bool TryDeploySmoke(SCR_AIGroup group, vector throwOrigin, vector threatCentroid, string reason, array<SCR_AIUtilityComponent> preferred = null, IEntity exclude = null)
	{
		Read();
		if (!s_bOn || !group) return false;

		// Check host hook
		if (BBB.s_Hooks && !BBB.s_Hooks.AllowSmoke(group)) return false;

		// Check player squads
		if (BBB.HasPlayer(group)) return false;

		float nowMs = GetGame().GetWorld().GetWorldTime();

		// Check squad cooldown
		if (IsGroupOnCooldown(group, nowMs)) return false;

		// Calculate vector toward threat
		vector toThreat = threatCentroid - throwOrigin;
		toThreat[1] = 0;
		float threatDist = toThreat.Length();
		if (threatDist < 2.0) return false;
		toThreat = toThreat.Normalized();

		// Throw distance: 10 to 22 meters ahead toward threat
		float throwDist = Math.Clamp(threatDist * 0.35, 10.0, 22.0);
		vector smokePos = throwOrigin + toThreat * throwDist;
		smokePos[1] = GetGame().GetWorld().GetSurfaceY(smokePos[0], smokePos[2]);

		// Check if active smoke already covers this spot
		if (IsSmokeAlreadyPresent(smokePos, nowMs)) return false;

		// Find best thrower
		IEntity thrower = FindBestThrower(group, smokePos, preferred, exclude);
		if (!thrower) return false;

		AIControlComponent control = AIControlComponent.Cast(thrower.FindComponent(AIControlComponent));
		if (!control || !control.GetAIAgent()) return false;
		SCR_AIUtilityComponent throwerUtil = SCR_AIUtilityComponent.Cast(control.GetAIAgent().FindComponent(SCR_AIUtilityComponent));
		if (!throwerUtil) return false;

		// Execute throw via native behavior
		SCR_AIThrowGrenadeToBehavior behavior = new SCR_AIThrowGrenadeToBehavior(
			throwerUtil, null, smokePos, EWeaponType.WT_SMOKEGRENADE, 0.1, SCR_AIActionBase.PRIORITY_BEHAVIOR_THROW_GRENADE
		);
		throwerUtil.AddAction(behavior);

		// Register active cloud
		BBB_SmokeCloud cloud = new BBB_SmokeCloud();
		cloud.m_vPosition = smokePos;
		cloud.m_fCreatedMs = nowMs;
		cloud.m_fBloomMs = nowMs + (SMOKE_BLOOM_S * 1000.0);
		cloud.m_fExpireMs = nowMs + ((SMOKE_BLOOM_S + SMOKE_DURATION_S) * 1000.0);
		cloud.m_fRadius = SMOKE_RADIUS;
		cloud.m_sReason = reason;
		s_aClouds.Insert(cloud);

		// Record cooldown
		s_mGroupCooldown.Set(group, nowMs);

		s_iSmokeThrown++;
		if (reason == "drag") s_iDragSmoke++;
		else if (reason == "maneuver") s_iManeuverSmoke++;
		else if (reason == "morale") s_iMoraleSmoke++;

		PrintFormat("[BBB] SMOKE: %1 (%2) deployed smoke to %3 (reason=%4, threatDist=%5 m)",
			thrower, BBB.s_Hooks.NameOf(group), smokePos, reason, Math.Round(threatDist));
			BBB_Think.Note(thrower, "Smoke", "throws smoke (" + reason + ", threat " + Math.Round(threatDist) + " m)");
		return true;
	}

	// ---------------------------------------------------------------- domain triggers

	// Deploy smoke to screen a casualty drag
	static bool DeployForDrag(IEntity healer, IEntity casualty, vector destination)
	{
		Read();
		if (!s_bOn || !s_bDrag || !healer || !casualty) return false;

		SCR_AIGroup group = BBB_Medic.GroupOf(healer);
		if (!group) return false;

		vector threatCentroid;
		if (!GetThreatCentroid(group, threatCentroid)) return false;

		// Screen between casualty and enemy threat centroid, excluding the healer (who is carrying)
		return TryDeploySmoke(group, casualty.GetOrigin(), threatCentroid, "drag", null, healer);
	}

	// Deploy smoke to screen a bounding fireteam advance
	static bool DeployForBound(SCR_AIGroup group, vector boundTarget, vector threatCenter, array<SCR_AIUtilityComponent> coveringMembers)
	{
		Read();
		if (!s_bOn || !s_bManeuver || !group) return false;

		// Covering fireteam deploys smoke between bound target and threat
		return TryDeploySmoke(group, boundTarget, threatCenter, "maneuver", coveringMembers);
	}

	// Deploy smoke to screen a squad retrograde break-contact withdrawal
	static bool DeployForRetrograde(SCR_AIGroup group, vector threatCenter, array<SCR_AIUtilityComponent> coveringMembers)
	{
		Read();
		if (!s_bOn || !s_bMorale || !group) return false;

		IEntity anchor = BBB.AnchorOf(group);
		if (!anchor) return false;

		// Covering overwatch element deploys smoke toward threat to break sightlines
		return TryDeploySmoke(group, anchor.GetOrigin(), threatCenter, "morale", coveringMembers);
	}

	// ---------------------------------------------------------------- line-of-sight occlusion

	// Mathematical line-sphere intersection test
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

	// Check if line of sight between two positions is occluded by tactical smoke clouds
	static bool IsOccludedSelf(vector from, vector to)
	{
		Read();
		if (!s_bOn) return false;

		float nowMs = GetGame().GetWorld().GetWorldTime();

		for (int i = s_aClouds.Count() - 1; i >= 0; i--)
		{
			BBB_SmokeCloud cloud = s_aClouds[i];
			if (!cloud) continue;

			if (nowMs >= cloud.m_fBloomMs && nowMs <= cloud.m_fExpireMs)
			{
				if (LineIntersectsSphere(from, to, cloud.m_vPosition, cloud.m_fRadius))
				{
					return true;
				}
			}
		}

		return false;
	}

	// Check if line of sight between two positions is occluded by either tactical smoke or medic smoke
	static bool IsOccluded(vector from, vector to)
	{
		if (IsOccludedSelf(from, to)) return true;
		if (BBB_Medic.IsOccludedBySmoke(from, to)) return true;
		return false;
	}

	// ---------------------------------------------------------------- lifecycle and maintenance

	// Periodic cleanup of expired clouds and stale group cooldowns (<= 0.05 ms)
	static void Tick()
	{
		Read();
		if (!s_bOn) return;

		float nowMs = GetGame().GetWorld().GetWorldTime();

		// Clean up expired clouds
		for (int i = s_aClouds.Count() - 1; i >= 0; i--)
		{
			BBB_SmokeCloud cloud = s_aClouds[i];
			// Kept past its smoke for the spot cooldown; the line-of-sight check reads only live smoke.
			if (!cloud || (nowMs > cloud.m_fExpireMs && nowMs - cloud.m_fCreatedMs >= s_fCooldownTargetS * 1000.0))
			{
				s_aClouds.RemoveOrdered(i);
			}
		}

		// Clean up stale squad cooldowns (> 120 s old)
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
