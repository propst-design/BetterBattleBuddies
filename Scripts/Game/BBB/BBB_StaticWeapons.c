// Better Battle Buddies (BBB): Autonomous static weapon crewing during contact.
//
// Standalone tactical behavior for idle or defending autonomous infantry squads:
// 1. Static weapon discovery: when an autonomous infantry squad detects enemy contact,
//    foot soldiers scan nearby positions (<= 35 m) for operational, loaded, friendly
//    static weapons (or stationary defensible turrets).
// 2. Traverse arc check: the static weapon is ONLY crewed if a known perceived enemy
//    falls within its traverse arc (default <= 60 deg from weapon centerline) and
//    within viable engagement range (5 to 500 m). Weapons facing away from threats
//    are ignored.
// 3. Crew assignment: assigns the closest fit squadmate on foot (preferring non-leaders),
//    who walks to the weapon with vanilla's own get-in behaviour (no teleport). The job
//    stays pending until he is actually seated; a boarding that fails, times out (30 s)
//    or is overtaken by orders is cancelled and the seat reservation freed.
// 4. Release when ordered on: the gunner dismounts and rejoins the squad on foot when
//    the squad receives an explicit move order (BBB_OrderIntent.Blocks), moves away
//    (> 45 m tether), the target sector stays quiet for a sustained period (15 s),
//    ammunition is exhausted, or the weapon is destroyed.
// 5. Constraints: never overrides player/GM orders or player control; respects the host
//    hooks AllowStaticWeapons(group) and LeaveAlone(weapon); never mounts enemy-faction
//    weapons; never switches on AI that something else switched off; budget <= 0.1 ms/tick.

class BBB_StaticGunnerJob : Managed
{
	SCR_AIGroup m_Group;
	ChimeraCharacter m_Gunner;
	IEntity m_StaticWeapon;
	BaseCompartmentSlot m_Slot;
	SCR_AIUtilityComponent m_Utility;
	SCR_AIGetInVehicle m_GetIn;      // native get-in behaviour (owned by the utility)
	float m_fRequestedMs;
	float m_fCrewedSince;
	float m_fLastThreatInArcMs;
	bool m_bMounted;                 // false while walking to / climbing into the seat
}

class BBB_StaticWeapons
{
	static int s_iRead;
	static bool s_bOn = true;

	// Tunable parameters
	static float s_fSearchRadiusM = 35.0;
	static float s_fMaxArcDeg = 60.0;
	static float s_fQuietTimeoutS = 15.0;
	static const float CHECK_INTERVAL_MS = 2000.0;
	static const float MAX_TETHER_DIST_M = 45.0;
	static const float MAX_ENGAGE_DIST_M = 500.0;
	static const float MIN_ENGAGE_DIST_M = 5.0;
	static const float BOARDING_TIMEOUT_MS = 30000.0;
	static const float WEAPON_RETRY_MS = 60000.0;

	// Active crewed jobs
	protected static ref array<ref BBB_StaticGunnerJob> s_aJobs = new array<ref BBB_StaticGunnerJob>();

	// Per-group evaluation throttle: Group -> next allowed check time in ms
	protected static ref map<SCR_AIGroup, float> s_mNextCheck = new map<SCR_AIGroup, float>();

	// Weapon -> earliest time it may be crewed again after a release or failed boarding
	protected static ref map<IEntity, float> s_mWeaponRetry = new map<IEntity, float>();

	// Scratch query buffer
	protected static ref array<IEntity> s_aQueriedEntities = new array<IEntity>();

	// Performance and test counters
	static int s_iBoardingRequests;
	static int s_iBoardingsCancelled;
	static int s_iWeaponsCrewed;
	static int s_iWeaponsReleased;
	static int s_iScanTicks;

	// ---------------------------------------------------------------- configuration

	static void Read()
	{
		if (!BBB_Settings.Due(s_iRead)) return;

		string value;
		if (BBB_Settings.Get("bbbStaticWeapons", value))
		{
			s_bOn = value == "1";
		}

		if (BBB_Settings.Get("bbbStaticReach", value))
		{
			float reach = value.ToFloat();
			if (reach >= 10.0 && reach <= 80.0) s_fSearchRadiusM = reach;
		}

		if (BBB_Settings.Get("bbbStaticArc", value))
		{
			float arc = value.ToFloat();
			if (arc >= 15.0 && arc <= 180.0) s_fMaxArcDeg = arc;
		}

		if (BBB_Settings.Get("bbbStaticQuiet", value))
		{
			float quiet = value.ToFloat();
			if (quiet >= 5.0 && quiet <= 120.0) s_fQuietTimeoutS = quiet;
		}

		PrintFormat("[BBB] STATIC WEAPONS enabled=%1 reach=%2m arc=%3deg quiet=%4s",
			s_bOn, s_fSearchRadiusM, s_fMaxArcDeg, s_fQuietTimeoutS);
	}

	static void Stop()
	{
		// Dismount all active gunners cleanly upon BBB stop
		for (int i = s_aJobs.Count() - 1; i >= 0; i--)
		{
			BBB_StaticGunnerJob job = s_aJobs[i];
			if (!job) continue;
			if (job.m_bMounted) DismountGunner(job, "BBB stopped");
			else CancelBoarding(job, "BBB stopped");
			BBB_Claims.Release(job.m_Gunner, BBB_Claims.STATIC);
		}
		s_aJobs.Clear();
		s_mNextCheck.Clear();
		s_mWeaponRetry.Clear();
		s_aQueriedEntities.Clear();
		s_iBoardingRequests = 0;
		s_iBoardingsCancelled = 0;
		s_iWeaponsCrewed = 0;
		s_iWeaponsReleased = 0;
		s_iScanTicks = 0;
	}

	// ---------------------------------------------------------------- geometry and bearing

	// Signed bearing from forward vector to target direction in degrees [-180, 180]
	static float Bearing(vector forward, vector direction)
	{
		forward[1] = 0;
		direction[1] = 0;
		forward.Normalize();
		direction.Normalize();
		return Math.Atan2(forward[2] * direction[0] - forward[0] * direction[2], vector.Dot(forward, direction)) * Math.RAD2DEG;
	}

	// Check if a target position falls within the weapon forward traverse arc
	static bool IsInTraverseArc(IEntity weapon, vector targetPos, float maxArcDeg)
	{
		if (!weapon) return false;
		vector forward = weapon.GetTransformAxis(2);
		forward[1] = 0;
		if (forward.LengthSq() < 0.001) forward = "0 0 1";
		else forward.Normalize();

		vector toTarget = targetPos - weapon.GetOrigin();
		toTarget[1] = 0;
		float dist = toTarget.Length();
		if (dist < MIN_ENGAGE_DIST_M || dist > MAX_ENGAGE_DIST_M) return false;

		toTarget.Normalize();
		float angle = Math.AbsFloat(Bearing(forward, toTarget));
		return angle <= maxArcDeg;
	}

	// Check if any perceived threat known to the squad is within the weapon traverse arc
	static bool HasThreatInArc(SCR_AIGroup group, IEntity weapon, float maxArcDeg)
	{
		if (!group || !weapon) return false;
		vector forward = weapon.GetTransformAxis(2);
		forward[1] = 0;
		if (forward.LengthSq() < 0.001) forward = "0 0 1";
		else forward.Normalize();

		vector weaponPos = weapon.GetOrigin();
		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(group, agents);

		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			SCR_AIUtilityComponent u = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			if (!u) continue;

			// 1. Current selected target
			if (u.m_CombatComponent)
			{
				BaseTarget cur = u.m_CombatComponent.GetCurrentTarget();
				if (cur && cur.GetTargetCategory() == ETargetCategory.ENEMY)
				{
					vector curPos = cur.GetLastDetectedPosition();
					vector toCur = curPos - weaponPos;
					toCur[1] = 0;
					float dCur = toCur.Length();
					if (dCur >= MIN_ENGAGE_DIST_M && dCur <= MAX_ENGAGE_DIST_M)
					{
						float bCur = Math.AbsFloat(Bearing(forward, toCur));
						if (bCur <= maxArcDeg) return true;
					}
				}
			}

			// 2. Perceived enemy target list
			if (u.m_PerceptionComponent)
			{
				array<BaseTarget> targets = {};
				u.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
				foreach (BaseTarget tgt : targets)
				{
					if (!tgt || tgt.GetTargetCategory() != ETargetCategory.ENEMY) continue;
					if (tgt.GetTimeSinceDetected() > 20.0) continue;

					vector tPos = tgt.GetLastDetectedPosition();
					vector toT = tPos - weaponPos;
					toT[1] = 0;
					float dist = toT.Length();
					if (dist >= MIN_ENGAGE_DIST_M && dist <= MAX_ENGAGE_DIST_M)
					{
						float b = Math.AbsFloat(Bearing(forward, toT));
						if (b <= maxArcDeg) return true;
					}
				}
			}
		}

		return false;
	}

	// ---------------------------------------------------------------- weapon eligibility

	// Identify static weapons and stationary turrets with operational ammo
	static bool IsStaticWeapon(IEntity entity, out BaseCompartmentSlot outTurretSlot)
	{
		outTurretSlot = null;
		if (!entity || BBB.Health(entity) <= 0) return false;

		SCR_BaseCompartmentManagerComponent compMgr = SCR_BaseCompartmentManagerComponent.Cast(entity.FindComponent(SCR_BaseCompartmentManagerComponent));
		if (!compMgr) return false;

		array<BaseCompartmentSlot> slots = {};
		BBB_VehicleHelpers.GetVehicleCompartments(entity, slots);

		BaseCompartmentSlot turretSlot = null;
		bool hasPilot = false;

		foreach (BaseCompartmentSlot s : slots)
		{
			if (!s) continue;
			if (s.GetType() == ECompartmentType.PILOT)
			{
				hasPilot = true;
			}
			else if (s.GetType() == ECompartmentType.TURRET && !turretSlot)
			{
				turretSlot = s;
			}
		}

		if (!turretSlot) return false;

		// Static weapons have a turret seat and no driver seat
		// If it has a pilot seat, it must be completely stationary and unpowered
		if (hasPilot)
		{
			CarControllerComponent car = CarControllerComponent.Cast(entity.FindComponent(CarControllerComponent));
			if (car && car.IsEngineOn()) return false;
			IEntity driver = BBB.DriverOf(entity);
			if (driver) return false;
		}

		// Must have live ammunition
		if (!BBB_VehicleHelpers.TurretHasAmmo(turretSlot)) return false;

		// Seat must be unoccupied or occupant incapacitated
		IEntity occupant = turretSlot.GetOccupant();
		if (occupant)
		{
			if (BBB.PlayerOf(occupant) || BBB_VehicleHelpers.IsAble(occupant)) return false;
		}

		outTurretSlot = turretSlot;
		return true;
	}

	// Check if a weapon's faction is compatible (friendly or neutral; never enemy)
	static bool IsFactionCompatible(SCR_AIGroup group, IEntity weapon)
	{
		if (!group || !weapon) return false;
		Faction squadFac = group.GetFaction();
		if (!squadFac) return true;

		FactionAffiliationComponent aff = FactionAffiliationComponent.Cast(weapon.FindComponent(FactionAffiliationComponent));
		if (!aff) return true;

		Faction weaponFac = aff.GetAffiliatedFaction();
		if (!weaponFac) weaponFac = aff.GetDefaultAffiliatedFaction();
		if (!weaponFac) return true;

		// If weapon belongs to an enemy faction, do not crew it
		if (squadFac.IsFactionEnemy(weaponFac)) return false;

		return true;
	}

	// ---------------------------------------------------------------- candidate gunner selection

	// Check if a soldier is fit to crew a static weapon
	static bool IsFitGunner(ChimeraCharacter soldier, SCR_AIUtilityComponent utility)
	{
		if (!soldier || !utility) return false;
		if (!BBB.IsAlive(soldier)) return false;

		CharacterControllerComponent controller = soldier.GetCharacterController();
		if (!controller || controller.IsDead() || controller.IsUnconscious() || controller.IsPlayerControlled()) return false;
		if (controller.IsReloading() || controller.IsChangingItem() || controller.IsClimbing() || controller.IsSwimming() || controller.GetDisableWeaponControls()) return false;

		if (soldier.IsInVehicle()) return false;
		if (utility.m_AIInfo && (utility.m_AIInfo.HasUnitState(EUnitState.IN_VEHICLE) || utility.m_AIInfo.HasUnitState(EUnitState.UNCONSCIOUS))) return false;

		if (BBB_OrderIntent.Blocks(utility)) return false;
		if (BBB_Drag.Busy(soldier)) return false;
		// Already bounding in a fire-and-maneuver: one BBB task at a time.
		if (utility.m_BBBFireJob) return false;
		if (!BBB_Claims.Can(soldier, BBB_Claims.STATIC)) return false;

		return true;
	}

	// Find best squadmate on foot to crew the weapon (prefer non-leaders close to weapon)
	static ChimeraCharacter FindBestCandidate(SCR_AIGroup group, IEntity weapon)
	{
		if (!group || !weapon) return null;

		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(group, agents);
		IEntity leaderEnt = group.GetLeaderEntity();

		ChimeraCharacter bestCandidate = null;
		float bestDist = s_fSearchRadiusM;
		ChimeraCharacter leaderCandidate = null;
		float leaderDist = s_fSearchRadiusM;

		vector weaponPos = weapon.GetOrigin();

		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			IEntity controlled = agent.GetControlledEntity();
			if (!controlled) continue;

			ChimeraCharacter character = ChimeraCharacter.Cast(controlled);
			if (!character) continue;

			SCR_AIUtilityComponent util = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			if (!util || !IsFitGunner(character, util)) continue;

			float d = vector.DistanceXZ(character.GetOrigin(), weaponPos);
			if (d > s_fSearchRadiusM) continue;

			if (controlled == leaderEnt)
			{
				if (d < leaderDist)
				{
					leaderDist = d;
					leaderCandidate = character;
				}
			}
			else
			{
				if (d < bestDist)
				{
					bestDist = d;
					bestCandidate = character;
				}
			}
		}

		if (bestCandidate) return bestCandidate;
		return leaderCandidate;
	}

	// ---------------------------------------------------------------- mounting & dismounting

	// Resolve a soldier's AI utility. It lives on the AI agent, not on the body.
	static SCR_AIUtilityComponent UtilityOf(IEntity soldier)
	{
		if (!soldier) return null;
		AIControlComponent control = AIControlComponent.Cast(soldier.FindComponent(AIControlComponent));
		if (!control || !control.GetControlAIAgent()) return null;
		return SCR_AIUtilityComponent.Cast(control.GetControlAIAgent().FindComponent(SCR_AIUtilityComponent));
	}

	// True while a job (pending or mounted) already claims this weapon
	static bool IsClaimed(IEntity weapon)
	{
		foreach (BBB_StaticGunnerJob job : s_aJobs)
		{
			if (job && job.m_StaticWeapon == weapon) return true;
		}
		return false;
	}

	// Send the soldier to the weapon on foot with vanilla's own get-in behaviour
	// (the same one vanilla gunners use to return to a turret). Nothing teleports:
	// the job stays pending until he is actually seated.
	static bool CrewWeapon(SCR_AIGroup group, ChimeraCharacter gunner, IEntity weapon, BaseCompartmentSlot slot)
	{
		if (!group || !gunner || !weapon || !slot) return false;
		if (!BBB.s_Hooks.AllowStaticWeapons(group) || BBB.s_Hooks.LeaveAlone(weapon)) return false;
		if (IsClaimed(weapon)) return false;
		if (slot.IsReserved() && !slot.IsReservedBy(gunner)) return false;

		IEntity occ = slot.GetOccupant();
		if (occ && occ != gunner && BBB_VehicleHelpers.IsAble(occ)) return false;

		SCR_AIUtilityComponent utility = UtilityOf(gunner);
		if (!utility || BBB_OrderIntent.Blocks(utility)) return false;

		IEntity vehicle = slot.GetVehicle();
		if (!vehicle) vehicle = weapon;

		// Remove an incapacitated occupant so the seat is free when he arrives
		if (occ && occ != gunner)
		{
			ChimeraCharacter occCh = ChimeraCharacter.Cast(occ);
			if (occCh)
			{
				CompartmentAccessComponent occAcc = occCh.GetCompartmentAccessComponent();
				if (occAcc) occAcc.GetOutVehicle(EGetOutType.TELEPORT, -1, ECloseDoorAfterActions.LEAVE_OPEN, false);
			}
		}

		slot.SetReserved(gunner);
		SCR_AIGetInVehicle getIn = new SCR_AIGetInVehicle(utility, null, vehicle, slot, EAICompartmentType.Turret,
			SCR_AIActionBase.PRIORITY_BEHAVIOR_GET_IN_VEHICLE, SCR_AIActionBase.PRIORITY_LEVEL_NORMAL);
		utility.AddAction(getIn);

		BBB_StaticGunnerJob job = new BBB_StaticGunnerJob();
		job.m_Group = group;
		job.m_Gunner = gunner;
		job.m_StaticWeapon = weapon;
		job.m_Slot = slot;
		job.m_Utility = utility;
		job.m_GetIn = getIn;
		float now = GetGame().GetWorld().GetWorldTime();
		job.m_fRequestedMs = now;
		job.m_fCrewedSince = now;
		job.m_fLastThreatInArcMs = now;
		job.m_bMounted = false;
		s_aJobs.Insert(job);
		BBB_Claims.Take(gunner, BBB_Claims.STATIC);

		s_iBoardingRequests++;
		PrintFormat("[BBB] STATIC: %1 (%2) heading for static weapon %3 (%4 m)",
			gunner, BBB.s_Hooks.NameOf(group), weapon, Math.Round(vector.Distance(gunner.GetOrigin(), weapon.GetOrigin())));
			BBB_Think.Note(gunner, "Static", "heads for a static weapon " + Math.Round(vector.Distance(gunner.GetOrigin(), weapon.GetOrigin())) + " m away");
		return true;
	}

	// Stop a boarding that has not finished: cancel the native get-in and free the seat
	protected static void CancelBoarding(BBB_StaticGunnerJob job, string reason)
	{
		if (!job) return;
		if (job.m_GetIn)
		{
			EAIActionState state = job.m_GetIn.GetActionState();
			if (state != EAIActionState.COMPLETED && state != EAIActionState.FAILED) job.m_GetIn.Fail();
		}
		if (job.m_Slot && job.m_Gunner && job.m_Slot.IsReservedBy(job.m_Gunner)) job.m_Slot.SetReserved(null);

		// A boarding that got as far as the seat is a release, not a cancel
		if (job.m_Gunner && job.m_Slot && job.m_Slot.GetOccupant() == job.m_Gunner)
		{
			DismountGunner(job, reason);
			return;
		}
		s_iBoardingsCancelled++;
		if (job.m_StaticWeapon) s_mWeaponRetry.Set(job.m_StaticWeapon, GetGame().GetWorld().GetWorldTime() + WEAPON_RETRY_MS);
		PrintFormat("[BBB] STATIC: %1 boarding of %2 cancelled (reason=%3)", job.m_Gunner, job.m_StaticWeapon, reason);
		BBB_Think.Note(job.m_Gunner, "Static", "boarding cancelled: " + reason);
	}

	// Release a seated gunner back to his squad with vanilla's get-out behaviour.
	// A player who has taken him over is left alone.
	protected static void DismountGunner(BBB_StaticGunnerJob job, string reason)
	{
		if (!job || !job.m_Gunner) return;
		s_iWeaponsReleased++;
		if (job.m_StaticWeapon) s_mWeaponRetry.Set(job.m_StaticWeapon, GetGame().GetWorld().GetWorldTime() + WEAPON_RETRY_MS);

		CompartmentAccessComponent acc = job.m_Gunner.GetCompartmentAccessComponent();
		bool seated = acc && job.m_Slot && acc.GetCompartment() == job.m_Slot;
		if (seated && BBB.IsAlive(job.m_Gunner) && !BBB.PlayerOf(job.m_Gunner))
		{
			SCR_AIUtilityComponent utility = UtilityOf(job.m_Gunner);
			IEntity vehicle = job.m_Slot.GetVehicle();
			if (!vehicle) vehicle = job.m_StaticWeapon;
			if (utility && !utility.FindActionOfType(SCR_AIGetOutVehicle))
			{
				utility.AddAction(new SCR_AIGetOutVehicle(utility, null, vehicle, 0,
					SCR_AIActionBase.PRIORITY_BEHAVIOR_GET_OUT_VEHICLE_HIGH_PRIORITY, SCR_AIActionBase.PRIORITY_LEVEL_NORMAL));
			}
			else if (!utility)
			{
				acc.GetOutVehicle(EGetOutType.TELEPORT, -1, ECloseDoorAfterActions.LEAVE_OPEN, false);
			}
			// Backstop in case the native get-out cannot run
			GetGame().GetCallqueue().CallLater(ForceOutIfStillSeated, 8000, false, job.m_Gunner, job.m_Slot);
		}

		PrintFormat("[BBB] STATIC: %1 released static weapon %2 (reason=%3)",
			job.m_Gunner, job.m_StaticWeapon, reason);
		BBB_Think.Note(job.m_Gunner, "Static", "released the static weapon: " + reason);
	}

	protected static void ForceOutIfStillSeated(ChimeraCharacter gunner, BaseCompartmentSlot slot)
	{
		if (!gunner || !slot || !BBB.IsAlive(gunner) || BBB.PlayerOf(gunner)) return;
		if (slot.GetOccupant() != gunner) return;
		foreach (BBB_StaticGunnerJob job : s_aJobs)
		{
			if (job && job.m_Gunner == gunner) return; // crewed again since
		}
		CompartmentAccessComponent acc = gunner.GetCompartmentAccessComponent();
		if (acc && !acc.IsGettingOut()) acc.GetOutVehicle(EGetOutType.TELEPORT, -1, ECloseDoorAfterActions.LEAVE_OPEN, false);
	}

	// ---------------------------------------------------------------- sphere query filter

	protected static bool CollectStaticWeapon(IEntity entity)
	{
		if (!entity || BBB.Health(entity) <= 0) return true;
		if (!entity.FindComponent(SCR_BaseCompartmentManagerComponent)) return true;
		s_aQueriedEntities.Insert(entity);
		return true;
	}

	// ---------------------------------------------------------------- main tick

	// Drop a job and free its gunner for other BBB jobs.
	protected static void Forget(int index)
	{
		BBB_StaticGunnerJob job = s_aJobs[index];
		if (job) BBB_Claims.Release(job.m_Gunner, BBB_Claims.STATIC);
		s_aJobs.RemoveOrdered(index);
	}

	// Why a job must end now, or "" to keep it
	protected static string ReleaseReason(BBB_StaticGunnerJob job, float nowMs)
	{
		if (!job.m_StaticWeapon || BBB.Health(job.m_StaticWeapon) <= 0) return "weapon destroyed";
		if (BBB.PlayerOf(job.m_Gunner)) return "player took over";
		if (!job.m_Group || BBB.HasPlayer(job.m_Group)) return "player squad";
		if (BBB_HeldGuns.GroupOf(job.m_Gunner) != job.m_Group) return "changed squad"; // its player and host policy apply
		if (!BBB.s_Hooks.AllowStaticWeapons(job.m_Group) || BBB.s_Hooks.LeaveAlone(job.m_StaticWeapon)) return "host policy";

		SCR_AIUtilityComponent utility = UtilityOf(job.m_Gunner);
		if (utility && BBB_OrderIntent.Blocks(utility)) return "player/GM order";

		IEntity anchor = BBB.AnchorOf(job.m_Group);
		if (anchor && anchor != job.m_Gunner && vector.DistanceXZ(anchor.GetOrigin(), job.m_StaticWeapon.GetOrigin()) > MAX_TETHER_DIST_M) return "squad moved on";

		if (!job.m_bMounted) return "";

		if (!BBB_VehicleHelpers.TurretHasAmmo(job.m_Slot)) return "out of ammo";

		if (HasThreatInArc(job.m_Group, job.m_StaticWeapon, s_fMaxArcDeg)) job.m_fLastThreatInArcMs = nowMs;
		// A gun the group holds where it stands stays manned through quiet spells (BBB_HeldGuns).
		else if (BBB_HeldGuns.Holds(job.m_Gunner, job.m_StaticWeapon)) job.m_fLastThreatInArcMs = nowMs;
		else if (nowMs - job.m_fLastThreatInArcMs >= s_fQuietTimeoutS * 1000.0) return "quiet timeout / target cleared";
		return "";
	}

	static void Tick()
	{
		Read();
		if (!s_bOn && s_aJobs.IsEmpty()) return;

		float nowMs = GetGame().GetWorld().GetWorldTime();
		s_iScanTicks++;

		// 1. Maintain pending boardings and crewed weapons
		for (int j = s_aJobs.Count() - 1; j >= 0; j--)
		{
			BBB_StaticGunnerJob job = s_aJobs[j];
			if (!job)
			{
				Forget(j);
				continue;
			}

			// Dead or incapacitated: nothing to hand back, just free the seat
			if (!job.m_Gunner || !BBB.IsAlive(job.m_Gunner) || !BBB_VehicleHelpers.IsAble(job.m_Gunner))
			{
				if (job.m_Slot && job.m_Gunner && job.m_Slot.IsReservedBy(job.m_Gunner)) job.m_Slot.SetReserved(null);
				Forget(j);
				continue;
			}

			CompartmentAccessComponent gAcc = job.m_Gunner.GetCompartmentAccessComponent();
			bool seated = gAcc && job.m_Slot && gAcc.GetCompartment() == job.m_Slot;

			if (!job.m_bMounted)
			{
				// Pending: mounted only once he is actually in the seat
				if (seated && !gAcc.IsGettingIn())
				{
					job.m_bMounted = true;
					job.m_fCrewedSince = nowMs;
					job.m_fLastThreatInArcMs = nowMs;
					s_iWeaponsCrewed++;
					PrintFormat("[BBB] STATIC: %1 (%2) crewing static weapon %3 after %4 s",
						job.m_Gunner, BBB.s_Hooks.NameOf(job.m_Group), job.m_StaticWeapon, Math.Round((nowMs - job.m_fRequestedMs) / 1000.0));
						BBB_Think.Note(job.m_Gunner, "Static", "crewing a static weapon");
				}
				else
				{
					string pendingReason = ReleaseReason(job, nowMs);
					// A rescue (drag, medic) outranks walking to the gun: he goes there instead.
					if (pendingReason == "" && !BBB_Claims.Take(job.m_Gunner, BBB_Claims.STATIC)) pendingReason = "taken for " + BBB_Claims.Name(BBB_Claims.Role(job.m_Gunner));
					if (pendingReason == "")
					{
						bool behaviourOver = !job.m_GetIn;
						if (job.m_GetIn)
						{
							EAIActionState getInState = job.m_GetIn.GetActionState();
							behaviourOver = getInState == EAIActionState.COMPLETED || getInState == EAIActionState.FAILED;
						}
						bool transition = gAcc && (gAcc.IsGettingIn() || gAcc.IsGettingOut());
						if (behaviourOver && !transition) pendingReason = "could not board";
						else if (nowMs - job.m_fRequestedMs > BOARDING_TIMEOUT_MS) pendingReason = "boarding timed out";
					}
					if (pendingReason != "")
					{
						CancelBoarding(job, pendingReason);
						Forget(j);
					}
					continue;
				}
			}

			// Mounted: he left the seat by himself (vanilla turret dismount to investigate
			// sends him back with its own get-in, so a transition or get-in keeps the job)
			if (!seated)
			{
				bool returning = gAcc && (gAcc.IsGettingIn() || gAcc.IsGettingOut());
				SCR_AIUtilityComponent gunnerUtility = UtilityOf(job.m_Gunner);
				if (!returning && gunnerUtility && gunnerUtility.FindActionOfType(SCR_AIGetInVehicle)) returning = true;
				if (!returning)
				{
					s_iWeaponsReleased++;
					PrintFormat("[BBB] STATIC: %1 left static weapon %2 on his own", job.m_Gunner, job.m_StaticWeapon);
					BBB_Think.Note(job.m_Gunner, "Static", "left the static weapon on his own");
					Forget(j);
				}
				continue;
			}

			string reason = ReleaseReason(job, nowMs);
			if (reason != "")
			{
				DismountGunner(job, reason);
				Forget(j);
			}
		}

		// Seated gunners stay claimed while the job runs (pending ones renew above).
		foreach (BBB_StaticGunnerJob held : s_aJobs)
			if (held && held.m_bMounted) BBB_Claims.Take(held.m_Gunner, BBB_Claims.STATIC);

		if (!s_bOn) return;

		// 2. Discover and crew nearby static weapons for autonomous squads in contact
		array<SCR_AIGroup> groups = {};
		BBB.s_Hooks.GetGroups(groups);

		foreach (SCR_AIGroup group : groups)
		{
			if (!group || BBB.HasPlayer(group)) continue;
			if (!BBB.s_Hooks.AllowStaticWeapons(group)) continue;
			// A withdrawing squad doesn't stop to man a gun (one already crewed stays until tethered off).
			if (BBB_Morale.IsWithdrawing(group)) continue;

			// One weapon per squad at a time
			bool alreadyCrewing = false;
			foreach (BBB_StaticGunnerJob activeJob : s_aJobs)
			{
				if (activeJob && activeJob.m_Group == group)
				{
					alreadyCrewing = true;
					break;
				}
			}
			if (alreadyCrewing) continue;

			// Throttled per-group check
			if (s_mNextCheck.Contains(group))
			{
				if (nowMs < s_mNextCheck.Get(group)) continue;
			}
			s_mNextCheck.Set(group, nowMs + CHECK_INTERVAL_MS);

			IEntity groupAnchor = BBB.AnchorOf(group);
			if (!groupAnchor) continue;

			// Only squads that perceive the enemy (own perception, no shared knowledge)
			vector threatCentroid;
			if (!BBB_Smoke.GetThreatCentroid(group, threatCentroid)) continue;

			s_aQueriedEntities.Clear();
			GetGame().GetWorld().QueryEntitiesBySphere(groupAnchor.GetOrigin(), s_fSearchRadiusM, CollectStaticWeapon);

			foreach (IEntity candidate : s_aQueriedEntities)
			{
				if (BBB.s_Hooks.LeaveAlone(candidate) || IsClaimed(candidate)) continue;
				if (s_mWeaponRetry.Contains(candidate) && nowMs < s_mWeaponRetry.Get(candidate)) continue;

				BaseCompartmentSlot turretSlot;
				if (!IsStaticWeapon(candidate, turretSlot)) continue;
				if (turretSlot.IsReserved()) continue;
				if (!IsFactionCompatible(group, candidate)) continue;
				if (!IsInTraverseArc(candidate, threatCentroid, s_fMaxArcDeg)) continue;

				ChimeraCharacter gunnerCandidate = FindBestCandidate(group, candidate);
				if (!gunnerCandidate) continue;

				if (CrewWeapon(group, gunnerCandidate, candidate, turretSlot)) break;
			}
			s_aQueriedEntities.Clear();
		}
	}
}
