// Hot AI (BBB options, off by default). Two switches, each in the Settings tab, as a launch parameter
// and as a Game Master game setting (BBB_HotAIEditor.c):
//  -bbbHotAI 1        spotting and fire timing (played in AC: Actual's base fights, 3 Nov 2026);
//  -bbbHotVehicles 1  gunners and vehicles (newer, not played as much yet).
// Both 0 is vanilla. The tuning values below act only while their switch is on.
//
// Spotting and fire timing (-bbbHotAI). What vanilla 1.8.0.13 does (its scripts and Character_Base.et):
//  - spotting speed by threat state (SCR_AICombatComponent.UpdatePerceptionFactor): SAFE 1.0,
//    VIGILANT 2.5, ALERTED 2.5, THREATENED 0.4, times equipment and the host's factor. A man shot at
//    behind sandbags spots at 0.4 of normal: the "blind behind the bags" of the base fights.
//  - first shot (SCR_AIAttackBehavior.InitWaitTime): 0.25 s when surprised, 0.8 s when pinned, plus
//    1.4 d / (300 + d) s for distance d (0.34 s at 100 m, 0.7 s at 300 m).
//  - fire (SCR_AIGetFireTimes): bursts of about 0.75-4 rounds from a rifle and 2-7 from an MG on full
//    auto, one round per pull on semi; an aiming pause of 0.4 s times 0.1-0.7 up close, up to 1.3-2.9
//    past 280 m.
// With it on: the state factor is at least -bbbHotFloor (2.5: being shot at no longer blinds him, and
// a sentry is as sharp as an alerted man), then times -bbbHotSpot (3); the first-shot wait times
// -bbbHotWait (0.25); bursts times -bbbHotBurst (2), aiming pauses times -bbbHotPause (0.4).
// No new targets and no cheating: a soldier still only shoots what his own eyes recognise, with
// vanilla aim.
//
// Gunners and vehicles (-bbbHotVehicles):
//  - turrets (vehicle and static guns): vanilla drops a shot that isn't on target within 1 s for machine
//    guns and autocannons (AI agent config m_fBaseRejectionTime); a turret slewing onto a target often
//    misses that. A gunner in a turret gets -bbbHotTurretAim (3) times as long.
//  - vanilla re-picks a soldier's target every 0.5 s (EvaluateWeaponAndTarget); with several enemies
//    scored alike a gunner flips between them and his turret never settles. A gunner in a turret holds
//    a target he can see for -bbbHotTurretHold (6) s, unless someone else is shooting at him, he loses
//    sight of it for 2 s or his gun is empty.
//  - vanilla tells a soldier only about rounds that hit his own body or fly past him: a hit on his
//    vehicle tells the gunner nothing. With -bbbHotVehicleHit a hit on an armed vehicle reaches its AI
//    gunners as a hit on themselves (the shooter's position, as vanilla uses for body hits), so BBB's
//    gunner drill can burst back, and an AI-driven armed vehicle that is hit slows to
//    -bbbHotUnderFireKmh (20) km/h for -bbbHotUnderFireS (15) s, a steady platform for its gunner.
//  - fire control (-bbbHotGunnerFire; a test with the trace below: a BTR-70's gunner saw none of four
//    riflemen in 65 s while its commander, beside him without a gun, saw all four; an M1025's gunner
//    lost sight of his target, held the attack 10 s without a shot, then dropped it while still knowing
//    four enemies shooting at him). Every second an AI gunner in a turret with a gun who sees no target
//    of his own puts fire on, in turn: the target he lost sight of, at its last seen spot (within
//    LOST_SIGHT_S); an enemy he saw who shot at him (within KNOWN_S); what a crewmate in his vehicle
//    sees (within CREW_SEEN_S): he turns onto it first (vanilla's commander look priority) and fires
//    there if after CALLOUT_FIRE_S he still sees nothing of his own. A short series (FIRE_S) of vanilla
//    suppressive fire into a box round that spot, ended as soon as he sees a target; it outranks an
//    attack on a target he can't see. The crew call-out has its own switch and runs here with Hot AI
//    off as well; with -bbbCrewCallout on it calls out the biggest group (BBB_CrewCallout), else the
//    nearest sighting (CrewSighting, Hot AI only). The order gate is BBB_OrderIntent.GunnerBlocks.
//  - against vehicles: a gunner whose target is selected and in sight but who isn't attacking it
//    (vanilla's attack ended and nothing started a new one) gets a new attack after IDLE_S; a vehicle he
//    lost sight of that has moved more than MOVED_M since is followed by its engine noise (within
//    HEAR_M) instead of fire on where it was; a turret gun empty for EMPTY_S is reloaded with a box of
//    the same kind from the vehicle's own stores.
//  - main gun (-bbbHotMainGun): where vanilla picks a machine gun against infantry or for suppression,
//    a turret gunner uses the turret's main gun: its autocannon, else the machine gun in its lowest
//    weapon slot (the BTR-70's KPVT, not the coaxial PKT that its prefab lists first).
// Gunner trace (-bbbHotTrace, off by default, diagnostics): every 2 s, one "[BBB] HOT GUNNER" line per
// AI gunner in a vehicle turret who knows of an enemy, has a target, fired or whose vehicle was hit in
// the last 20 s: his behaviour (priority, level), target, gun, bursts, what he knows, threat, combat mode.
class BBB_HotAI
{
	static int s_iRead;
	static bool s_bOn;        // -bbbHotAI: spotting and fire timing (off by default)
	static bool s_bVehicles;  // -bbbHotVehicles: gunners and vehicles (off by default)
	// A Game Master's switch this mission (BBB_HotAIEditor): -1 the settings decide, 0 off, 1 on.
	static int s_iGMSpotting = -1;
	static int s_iGMVehicles = -1;
	static float s_fFloor = 2.5;
	static float s_fSpot = 3;
	static float s_fWait = 0.25;
	static float s_fBurst = 2;
	static float s_fPause = 0.4;
	static float s_fTurretAim = 3;
	static float s_fTurretHold = 6;
	static bool s_bVehicleHit = true;
	static float s_fUnderFireKmh = 20;
	static float s_fUnderFireS = 15;
	static const float HOLD_LOST_S = 2;       // a held target out of sight this long is let go
	static const float HOLD_ENDANGER_S = 3;   // someone who shot at him this recently may take over
	static const float HIT_EVERY_MS = 1000;   // one hit report per vehicle a second
	static int s_iVehicleHits;               // tests and the log
	static bool s_bTrace;
	static bool s_bGunnerFire = true;
	static bool s_bMainGun = true;
	static int s_iFires;                      // fire-control series started (tests and the log)
	protected static bool s_bTraceLoop;
	protected static int s_iLoop;
	static const float LOOP_MS = 1000;
	static const float TRACE_EVERY_MS = 2000;
	static const float LOST_SIGHT_S = 15;
	static const float KNOWN_S = 20;
	static const float CREW_SEEN_S = 4;
	static const float CALLOUT_FIRE_S = 2;
	static const float FIRE_S = 8;
	static const float FIRE_PRIORITY = 95;       // above an attack on a selected target (90)
	static const float FIRE_PRIORITY_HIGH = 1125; // above a high-priority attack (1120) on one he can't see
	static const float IDLE_S = 1.5;
	static const float MOVED_M = 12;
	static const float HEAR_M = 450;
	static const float EMPTY_S = 5;
	static int s_iReattacks;
	static int s_iReloads;
	static const float FIRE_MIN_M = 15;
	static const float FIRE_MAX_M = 900;
	static const float TRACE_QUIET_S = 20;    // a vehicle hit this long ago no longer counts
	// Declared bare, filled by InitStatics (the shared static-initializer budget, ACA #185).
	protected static ref map<IEntity, float> s_mHitNext;
	protected static ref map<IEntity, float> s_mSlowUntil;
	protected static ref map<IEntity, float> s_mLastHit;

	// From BBB.Start, and before first use.
	static void InitStatics()
	{
		if (!s_mHitNext) s_mHitNext = new map<IEntity, float>();
		if (!s_mSlowUntil) s_mSlowUntil = new map<IEntity, float>();
		if (!s_mLastHit) s_mLastHit = new map<IEntity, float>();
	}

	static void Read()
	{
		if (!BBB_Settings.Due(s_iRead)) return;
		InitStatics();
		// A new mission: the settings decide again.
		s_iGMSpotting = -1;
		s_iGMVehicles = -1;
		s_bOn = false;
		s_bVehicles = false;
		string value;
		if (BBB_Settings.Get("bbbHotAI", value)) s_bOn = value == "1";
		if (BBB_Settings.Get("bbbHotVehicles", value)) s_bVehicles = value == "1";
		if (BBB_Settings.Get("bbbHotFloor", value)) s_fFloor = Math.Max(0, value.ToFloat());
		if (BBB_Settings.Get("bbbHotSpot", value)) s_fSpot = Math.Max(0.1, value.ToFloat());
		if (BBB_Settings.Get("bbbHotWait", value)) s_fWait = Math.Max(0, value.ToFloat());
		if (BBB_Settings.Get("bbbHotBurst", value)) s_fBurst = Math.Max(0.1, value.ToFloat());
		if (BBB_Settings.Get("bbbHotPause", value)) s_fPause = Math.Max(0.05, value.ToFloat());
		if (BBB_Settings.Get("bbbHotTurretAim", value)) s_fTurretAim = Math.Max(1, value.ToFloat());
		if (BBB_Settings.Get("bbbHotTurretHold", value)) s_fTurretHold = Math.Max(0, value.ToFloat());
		if (BBB_Settings.Get("bbbHotVehicleHit", value)) s_bVehicleHit = value == "1";
		if (BBB_Settings.Get("bbbHotUnderFireKmh", value)) s_fUnderFireKmh = Math.Max(0, value.ToFloat());
		if (BBB_Settings.Get("bbbHotUnderFireS", value)) s_fUnderFireS = Math.Max(1, value.ToFloat());
		if (BBB_Settings.Get("bbbHotTrace", value)) s_bTrace = value == "1";
		if (BBB_Settings.Get("bbbHotGunnerFire", value)) s_bGunnerFire = value == "1";
		if (BBB_Settings.Get("bbbHotMainGun", value)) s_bMainGun = value == "1";
		if (!s_bTraceLoop && Replication.IsServer())
		{
			s_bTraceLoop = true;
			GetGame().GetCallqueue().CallLater(Loop, LOOP_MS, true);
		}
		PrintFormat("[BBB] HOT AI spotting and fire timing=%1 (floor=%2 spot=%3 wait=%4 burst=%5 pause=%6)", s_bOn, s_fFloor, s_fSpot, s_fWait, s_fBurst, s_fPause);
		PrintFormat("[BBB] HOT AI gunners and vehicles=%1 (turretAim=%2 hold=%3 s vehicleHit=%4 underFire=%5 km/h for %6 s fire control=%7 main gun=%8)", s_bVehicles, s_fTurretAim, s_fTurretHold, s_bVehicleHit, s_fUnderFireKmh, s_fUnderFireS, s_bGunnerFire, s_bMainGun);
		PrintFormat("[BBB] HOT AI gunner trace=%1", s_bTrace);
	}

	// A Game Master switches one of them for the rest of this mission (BBB_HotAIEditor; server).
	static void SetByGameMaster(bool vehicles, bool on)
	{
		Read();
		int value = 0;
		if (on) value = 1;
		if (vehicles)
		{
			s_iGMVehicles = value;
			s_bVehicles = on;
		}
		else
		{
			s_iGMSpotting = value;
			s_bOn = on;
		}
		PrintFormat("[BBB] HOT AI a Game Master set spotting and fire timing=%1, gunners and vehicles=%2", s_bOn, s_bVehicles);
	}

	// A hit on a vehicle (from BBB's damage hook below): its AI gunners feel it as a hit on themselves, and
	// when it has one, an AI driver slows down for a while.
	static void OnVehicleHit(IEntity vehicle, BaseDamageContext context)
	{
		Read();
		if (!s_bVehicles || !s_bVehicleHit || !vehicle || !context || !context.instigator) return;
		if (context.damageType != EDamageType.KINETIC && context.damageType != EDamageType.EXPLOSIVE) return;
		IEntity shooter = context.instigator.GetInstigatorEntity();
		if (!shooter || shooter == vehicle || BBB_Transport.VehicleOf(shooter) == vehicle) return;
		float now = GetGame().GetWorld().GetWorldTime();
		InitStatics();
		s_mLastHit.Set(vehicle, now);
		float next;
		if (s_mHitNext.Find(vehicle, next) && now < next) return;
		s_mHitNext.Set(vehicle, now + HIT_EVERY_MS);
		array<BaseCompartmentSlot> turrets = {};
		BBB_VehicleHelpers.GetVehicleCompartments(vehicle, turrets, ECompartmentType.TURRET);
		vector from = shooter.GetOrigin();
		int told = 0;
		foreach (BaseCompartmentSlot slot : turrets)
		{
			ChimeraCharacter gunner = ChimeraCharacter.Cast(slot.GetOccupant());
			if (!BBB_VehiclePosture.Controlled(gunner)) continue;
			SCR_AIUtilityComponent utility = BBB_VehiclePosture.Utility(gunner);
			if (!utility || !utility.m_SectorThreatFilter) continue;
			utility.m_SectorThreatFilter.OnDamageTaken(from);
			// Still being hit: BBB's drill (bursts where the fire came from) may go again at once.
			utility.m_fBBBDrillReady = Math.Min(utility.m_fBBBDrillReady, now / 1000 + 1);
			told++;
		}
		if (told == 0) return;
		s_iVehicleHits++;
		SlowDown(vehicle, now);
	}

	protected static void SlowDown(IEntity vehicle, float now)
	{
		if (s_fUnderFireKmh <= 0) return;
		ChimeraCharacter driver = BBB.DriverOf(vehicle);
		if (!driver || !BBB_VehiclePosture.Controlled(driver)) return;
		AICarMovementComponent movement = AICarMovementComponent.Cast(vehicle.FindComponent(AICarMovementComponent));
		if (!movement) return;
		InitStatics();
		bool fresh = !s_mSlowUntil.Contains(vehicle);
		s_mSlowUntil.Set(vehicle, now + s_fUnderFireS * 1000);
		movement.SetCruiseSpeed(s_fUnderFireKmh);
		if (!fresh) return;
		GetGame().GetCallqueue().CallLater(EndSlow, s_fUnderFireS * 1000 + 250, false, vehicle);
		PrintFormat("[BBB] HOT AI %1 hit: its gunners told, slows to %2 km/h for %3 s", vehicle, s_fUnderFireKmh, s_fUnderFireS);
	}

	protected static void EndSlow(IEntity vehicle)
	{
		if (!s_mSlowUntil || !vehicle) return;
		float until;
		if (!s_mSlowUntil.Find(vehicle, until)) return;
		float now = GetGame().GetWorld().GetWorldTime();
		if (now < until)
		{
			GetGame().GetCallqueue().CallLater(EndSlow, until - now + 250, false, vehicle);
			return;
		}
		s_mSlowUntil.Remove(vehicle);
		AICarMovementComponent movement = AICarMovementComponent.Cast(vehicle.FindComponent(AICarMovementComponent));
		if (movement) movement.ResetCruiseSpeed();
		PrintFormat("[BBB] HOT AI %1: quiet for %2 s, road speed again", vehicle, s_fUnderFireS);
	}

	// Every second: gunner fire control; every other second, the trace.
	static void Loop()
	{
		s_iLoop++;
		// The crew call-out (BBB_CrewCallout) runs in fire control on its own switch, Hot AI or not.
		if ((s_bVehicles && s_bGunnerFire) || BBB_CrewCallout.On())
		{
			int t = System.GetTickCount();
			FireControlTick();
			BBB_Perf.Add("tick.gunnerFire", System.GetTickCount() - t);
		}
		if ((s_iLoop % 2) == 0) TraceTick();
	}

	static void FireControlTick()
	{
		if (!BBB.s_Hooks) return;
		BaseWorld world = GetGame().GetWorld();
		if (!world) return;
		float now = world.GetWorldTime();
		array<SCR_AIGroup> groups = {};
		BBB.s_Hooks.GetGroups(groups);
		array<AIAgent> agents = {};
		array<BaseTarget> targets = {};
		foreach (SCR_AIGroup group : groups)
		{
			if (!group) continue;
			agents.Clear();
			group.GetAgents(agents);
			foreach (AIAgent agent : agents)
			{
				if (!agent) continue;
				SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
				IEntity man = agent.GetControlledEntity();
				if (utility && man) FireControl(utility, man, now, targets);
			}
		}
	}

	protected static void FireControl(SCR_AIUtilityComponent utility, IEntity man, float now, array<BaseTarget> targets)
	{
		if (!utility.m_AIInfo || !utility.m_AIInfo.HasUnitState(EUnitState.IN_TURRET) || utility.m_AIInfo.HasUnitState(EUnitState.UNCONSCIOUS)) return;
		SCR_AICombatComponent combat = utility.m_CombatComponent;
		if (!combat || combat.GetCombatMode() == EAIGroupCombatMode.HOLD_FIRE) return;
		BaseWeaponComponent weapon = combat.GetCurrentWeapon();
		if (!weapon || weapon.GetWeaponType() == EWeaponType.WT_NONE) return; // a commander's seat: no gun
		bool hot = s_bVehicles && s_bGunnerFire; // otherwise only the crew call-out runs
		if (hot) ReloadCheck(utility, man, weapon, now);
		BaseTarget current = combat.GetCurrentTarget();
		if (current && combat.IsTargetVisible(current))
		{
			utility.m_fBBBCalloutAt = 0;
			if (hot) ReattackCheck(utility, current, now);
			return; // aimed fire: vanilla's
		}
		utility.m_fBBBIdleSince = 0;
		if (now < utility.m_fBBBHotFireUntil) return; // a series is running
		if (BBB.PlayerOf(man)) return;
		IEntity vehicle = BBB_Transport.VehicleOf(man);
		if (vehicle && BBB.s_Hooks.LeaveAlone(vehicle)) return;
		if (BBB.HasPlayer(BBB_HeldGuns.GroupOf(man)) || BBB_OrderIntent.GunnerBlocks(utility)) return;

		vector aim;
		string source;
		int men;       // the crew call-out's group size (BBB_CrewCallout), 0 for the older call-out
		float spread;  // and how far its men are from aim
		bool callout;
		if (hot && current && current.GetTargetCategory() == ETargetCategory.ENEMY && current.GetTimeSinceSeen() <= LOST_SIGHT_S)
		{
			aim = current.GetLastSeenPosition();
			source = "the target he lost sight of";
			// A vehicle on the move: he hears where it is now; fire on where it was would be wasted.
			IEntity seen = current.GetTargetEntity();
			IEntity moving = seen;
			if (!Vehicle.Cast(moving)) moving = BBB_Transport.VehicleOf(seen);
			if (moving && utility.m_LookAction)
			{
				vector now3 = moving.GetOrigin();
				if (vector.DistanceXZ(now3, aim) > MOVED_M && vector.DistanceXZ(now3, man.GetOrigin()) < HEAR_M)
				{
					utility.m_LookAction.LookAt(now3 + Vector(0, 1.5, 0), SCR_AILookAction.PRIO_ENEMY_TARGET, 1.2);
					return;
				}
			}
		}
		else if (hot && KnownShooter(utility, targets, aim))
		{
			source = "an enemy who shot at him";
		}
		else if (vehicle && BBB_CrewCallout.On() && BBB_CrewCallout.Pick(vehicle, man, targets, aim, men, spread))
		{
			callout = true;
		}
		else if (vehicle && hot && CrewSighting(vehicle, man, targets, aim))
		{
			callout = true;
		}
		else return;
		if (callout)
		{
			// The commander's call-out: turn onto it, and fire if he still sees nothing of his own.
			if (utility.m_fBBBCalloutAt <= 0 || now - utility.m_fBBBCalloutAt > 10000) utility.m_fBBBCalloutAt = now;
			if (utility.m_LookAction) utility.m_LookAction.LookAt(aim + Vector(0, 1, 0), SCR_AILookAction.PRIO_COMMANDER, 1.5);
			if (now - utility.m_fBBBCalloutAt < CALLOUT_FIRE_S * 1000) return;
			source = "a crewmate's call-out";
			if (men > 0) source = "a crewmate's call-out (" + men.ToString() + " men)";
		}

		vector me = man.GetOrigin();
		float distance = vector.DistanceXZ(me, aim);
		if (distance < FIRE_MIN_M || distance > FIRE_MAX_M || !BBB_SuppressionSafety.GroundSector(aim)) return;
		aim = BBB.Ground(aim);
		float half = Math.Clamp(distance * 0.025, 2.5, 10);
		// A called-out group: the box covers its men (aim is one of them).
		if (men > 0) half = Math.Clamp(Math.Max(half, spread + 2), 2.5, BBB_CrewCallout.GROUP_M);
		SCR_AISuppressionVolumeBox volume = new SCR_AISuppressionVolumeBox(Vector(aim[0] - half, aim[1] - 0.5, aim[2] - half), Vector(aim[0] + half, aim[1] + 2, aim[2] + half));
		BBB_AIHotFireBehavior fire = new BBB_AIHotFireBehavior(utility, null, volume, FIRE_S, 1.3);
		utility.AddAction(fire);
		utility.m_fBBBHotFireUntil = now + FIRE_S * 1000;
		s_iFires++;
		if (men > 0) BBB_CrewCallout.s_iCallouts++;
		PrintFormat("[BBB] HOT GUNNER fire control: %1 m at %2, on %3", Math.Round(distance), aim, source);
		BBB_Think.NoteUtility(utility, "Gunner", "puts fire on " + source + ", " + Math.Round(distance) + " m");
	}

	// Vanilla's attack ended with the target still selected and in sight (nothing new to react to): a new
	// attack, as vanilla starts one when the target changes, once he has only looked at it for IDLE_S.
	protected static void ReattackCheck(SCR_AIUtilityComponent utility, BaseTarget current, float now)
	{
		SCR_AIBehaviorBase doing = utility.GetCurrentBehavior();
		bool idle = current.GetTargetCategory() == ETargetCategory.ENEMY && !current.IsDisarmed() && doing && !SCR_AIAttackBehavior.Cast(doing);
		if (idle) idle = doing.GetPriority() < SCR_AIActionBase.PRIORITY_BEHAVIOR_ATTACK_NOT_SELECTED && doing.EvaluatePriorityLevel() < SCR_AIActionBase.PRIORITY_LEVEL_PLAYER;
		if (!idle)
		{
			utility.m_fBBBIdleSince = 0;
			return;
		}
		if (utility.m_fBBBIdleSince <= 0)
		{
			utility.m_fBBBIdleSince = now;
			return;
		}
		if (now - utility.m_fBBBIdleSince < IDLE_S * 1000) return;
		utility.m_fBBBIdleSince = 0;
		utility.SetStateAllActionsOfType(SCR_AIAttackBehavior, EAIActionState.COMPLETED, true);
		utility.AddAction(new SCR_AIAttackBehavior(utility, null, current, null));
		s_iReattacks++;
		string kind = "man";
		if (Vehicle.Cast(current.GetTargetEntity())) kind = "vehicle";
		PrintFormat("[BBB] HOT GUNNER re-attack: a %1 %2 m away in sight, he was only %3", kind, Math.Round(current.GetDistance()), doing.Type().ToString());
	}

	// A turret gun empty for EMPTY_S: a box of the same kind from the vehicle's own stores.
	protected static void ReloadCheck(SCR_AIUtilityComponent utility, IEntity man, BaseWeaponComponent weapon, float now)
	{
		BaseMagazineComponent magazine = weapon.GetCurrentMagazine();
		if (magazine && magazine.GetAmmoCount() > 0)
		{
			utility.m_fBBBEmptySince = 0;
			return;
		}
		if (utility.m_fBBBEmptySince <= 0)
		{
			utility.m_fBBBEmptySince = now;
			return;
		}
		if (now - utility.m_fBBBEmptySince < EMPTY_S * 1000) return;
		utility.m_fBBBEmptySince = now; // again in EMPTY_S if this one doesn't take
		ChimeraCharacter character = ChimeraCharacter.Cast(man);
		if (!character || !character.GetCompartmentAccessComponent()) return;
		TurretCompartmentSlot slot = TurretCompartmentSlot.Cast(character.GetCompartmentAccessComponent().GetCompartment());
		if (!slot) return;
		TurretControllerComponent controller = TurretControllerComponent.Cast(slot.GetController());
		BaseMuzzleComponent muzzle = weapon.GetCurrentMuzzle();
		if (!controller || !muzzle || !muzzle.GetMagazineWell()) return;
		IEntity root = slot.GetVehicle();
		if (!root) return;
		IEntity spare = SpareMagazine(root, muzzle.GetMagazineWell(), magazine, 0);
		if (!spare) return;
		bool started = controller.DoReloadWeaponWith(spare);
		s_iReloads++;
		string name = "gun";
		if (weapon.GetOwner() && weapon.GetOwner().GetPrefabData()) name = FilePath.StripExtension(FilePath.StripPath(weapon.GetOwner().GetPrefabData().GetPrefabName()));
		PrintFormat("[BBB] HOT GUNNER reload: %1 empty for %2 s, a box from the vehicle's stores (%3)", name, EMPTY_S, started);
	}

	// A magazine for this well with rounds in it, anywhere in the vehicle (its stores are its children).
	protected static IEntity SpareMagazine(IEntity parent, BaseMagazineWell well, BaseMagazineComponent current, int depth)
	{
		IEntity child = parent.GetChildren();
		while (child)
		{
			BaseMagazineComponent magazine = BaseMagazineComponent.Cast(child.FindComponent(BaseMagazineComponent));
			if (magazine && magazine != current && magazine.GetAmmoCount() > 0 && magazine.GetMagazineWell() && magazine.GetMagazineWell().Type() == well.Type()) return child;
			if (depth < 5)
			{
				IEntity found = SpareMagazine(child, well, current, depth + 1);
				if (found) return found;
			}
			child = child.GetSibling();
		}
		return null;
	}

	// An enemy he saw who shot at him lately: the one seen last.
	protected static bool KnownShooter(SCR_AIUtilityComponent utility, array<BaseTarget> targets, out vector aim)
	{
		if (!utility.m_PerceptionComponent) return false;
		targets.Clear();
		utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
		float best = KNOWN_S;
		bool found;
		foreach (BaseTarget target : targets)
		{
			if (!target || target.IsDisarmed() || target.GetTimeSinceEndangered() > KNOWN_S) continue;
			float seen = target.GetTimeSinceSeen();
			if (seen > best || BBB_SuppressionSafety.AircraftSource(target.GetTargetEntity())) continue;
			best = seen;
			aim = target.GetLastSeenPosition();
			found = true;
		}
		targets.Clear();
		return found;
	}

	// What an AI crewmate in the same vehicle has seen lately: one shooting at them first, else the nearest.
	protected static bool CrewSighting(IEntity vehicle, IEntity gunner, array<BaseTarget> targets, out vector aim)
	{
		BaseCompartmentManagerComponent compartments = BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(BaseCompartmentManagerComponent));
		if (!compartments) return false;
		array<BaseCompartmentSlot> slots = {};
		compartments.GetCompartments(slots);
		vector at = vehicle.GetOrigin();
		float bestScore = float.MAX;
		bool found;
		foreach (BaseCompartmentSlot slot : slots)
		{
			IEntity mate = slot.GetOccupant();
			if (!mate || mate == gunner || BBB.PlayerOf(mate) || !BBB.IsAlive(mate)) continue;
			PerceptionComponent eyes = PerceptionComponent.Cast(mate.FindComponent(PerceptionComponent));
			if (!eyes) continue;
			targets.Clear();
			eyes.GetTargetsList(targets, ETargetCategory.ENEMY);
			foreach (BaseTarget target : targets)
			{
				if (!target || target.IsDisarmed() || target.GetTimeSinceSeen() > CREW_SEEN_S) continue;
				if (BBB_SuppressionSafety.AircraftSource(target.GetTargetEntity())) continue;
				vector spot = target.GetLastSeenPosition();
				float score = vector.DistanceXZ(at, spot);
				if (target.GetTimeSinceEndangered() < 10) score -= 1000;
				if (score >= bestScore) continue;
				bestScore = score;
				aim = spot;
				found = true;
			}
		}
		targets.Clear();
		return found;
	}

	// The gunner trace (server): every AI gunner in a vehicle turret with something going on.
	static void TraceTick()
	{
		if (!s_bTrace || !BBB.s_Hooks) return;
		BaseWorld world = GetGame().GetWorld();
		if (!world) return;
		float now = world.GetWorldTime();
		array<SCR_AIGroup> groups = {};
		BBB.s_Hooks.GetGroups(groups);
		array<AIAgent> agents = {};
		array<BaseTarget> targets = {};
		foreach (SCR_AIGroup group : groups)
		{
			if (!group) continue;
			agents.Clear();
			group.GetAgents(agents);
			foreach (AIAgent agent : agents)
				TraceGunner(group, agent, now, targets);
		}
	}

	protected static void TraceGunner(SCR_AIGroup group, AIAgent agent, float now, array<BaseTarget> targets)
	{
		if (!agent) return;
		IEntity man = agent.GetControlledEntity();
		SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
		if (!man || !utility || !utility.m_AIInfo || !utility.m_AIInfo.HasUnitState(EUnitState.IN_TURRET)) return;
		IEntity vehicle = BBB_Transport.VehicleOf(man);
		if (!Vehicle.Cast(vehicle)) return; // static guns are Turret entities: vehicles only
		int enemies, detected;
		if (utility.m_PerceptionComponent)
		{
			targets.Clear();
			utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
			enemies = targets.Count();
			targets.Clear();
			utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.DETECTED);
			detected = targets.Count();
			targets.Clear();
		}
		SCR_AICombatComponent combat = utility.m_CombatComponent;
		BaseTarget target;
		if (combat) target = combat.GetCurrentTarget();
		float hitAgo = -1;
		float lastHit;
		if (s_mLastHit && s_mLastHit.Find(vehicle, lastHit)) hitAgo = (now - lastHit) / 1000;
		bool recentHit = hitAgo >= 0 && hitAgo <= TRACE_QUIET_S;
		int bursts = utility.m_iBBBHotBursts - utility.m_iBBBHotBurstsTraced;
		utility.m_iBBBHotBurstsTraced = utility.m_iBBBHotBursts;
		if (enemies == 0 && detected == 0 && !target && bursts == 0 && !recentHit) return;

		string doing = "nothing";
		SCR_AIBehaviorBase current = utility.GetCurrentBehavior();
		if (current) doing = string.Format("%1 (priority %2, level %3)", current.Type().ToString(), Math.Round(current.GetPriority() * 10) / 10, Math.Round(current.EvaluatePriorityLevel()));
		AIActionBase executed = utility.GetExecutedAction();
		if (executed && executed != current) doing = doing + ", executing " + executed.Type().ToString();
		string aim = "none";
		if (target)
		{
			string kind = "man";
			if (Vehicle.Cast(target.GetTargetEntity())) kind = "vehicle";
			aim = string.Format("%1 %2 m, seen %3 s ago, visible %4, shooting at him %5", kind, Math.Round(target.GetDistance()), Math.Round(target.GetTimeSinceSeen() * 10) / 10, combat.IsTargetVisible(target), target.GetTimeSinceEndangered() < 3);
		}
		string gun = "none";
		BaseWeaponComponent weapon;
		if (combat) weapon = combat.GetCurrentWeapon();
		string mainName = "none";
		BaseMagazineComponent mainMagazine;
		BaseWeaponComponent main;
		if (combat) main = combat.BBBHotMainGun(mainMagazine);
		if (main && main.GetOwner() && main.GetOwner().GetPrefabData()) mainName = FilePath.StripExtension(FilePath.StripPath(main.GetOwner().GetPrefabData().GetPrefabName()));
		if (weapon)
		{
			int ammo = -1;
			if (weapon.GetCurrentMagazine()) ammo = weapon.GetCurrentMagazine().GetAmmoCount();
			string weaponName = "?";
			if (weapon.GetOwner() && weapon.GetOwner().GetPrefabData()) weaponName = FilePath.StripExtension(FilePath.StripPath(weapon.GetOwner().GetPrefabData().GetPrefabName()));
			gun = string.Format("%1 %2 (%3 rounds, main gun %4)", weaponName, typename.EnumToString(EWeaponType, weapon.GetWeaponType()), ammo, mainName);
		}
		float kmh;
		if (vehicle.GetPhysics()) kmh = vehicle.GetPhysics().GetVelocity().Length() * 3.6;
		string threat = "?";
		if (utility.m_ThreatSystem) threat = typename.EnumToString(EAIThreatState, utility.m_ThreatSystem.GetState());
		string mode = "?";
		if (combat) mode = typename.EnumToString(EAIGroupCombatMode, combat.GetCombatMode());
		string name = "vehicle";
		if (vehicle.GetPrefabData()) name = FilePath.StripExtension(FilePath.StripPath(vehicle.GetPrefabData().GetPrefabName()));
		string head = string.Format("[BBB] HOT GUNNER group %1 %2 at %3 km/h: doing %4; target %5; gun %6", group.GetGroupID(), name, Math.Round(kmh), doing, aim, gun);
		string tail = string.Format("; bursts %1; knows %2 enemies, %3 detected; threat %4; mode %5; vehicle hit %6 s ago", bursts, enemies, detected, threat, mode, Math.Round(hitAgo));
		Print(head + tail);
	}
}

modded class SCR_AIUtilityComponent
{
	int m_iBBBHotBursts;        // hot AI fire-node runs (one per burst or single shot): the gunner trace
	int m_iBBBHotBurstsTraced;
	float m_fBBBCalloutAt;      // world time (ms) a crewmate's sighting was first called out to him
	float m_fBBBHotFireUntil;   // world time (ms) his fire-control series ends
	float m_fBBBIdleSince;      // world time (ms) he began only looking at a target in sight
	float m_fBBBEmptySince;     // world time (ms) his turret gun went empty
}

// A fire-control series: vanilla suppressive fire into a box, given up as soon as he sees a target.
class BBB_AIHotFireBehavior : SCR_AISuppressBehavior
{
	protected float m_fEndMs;

	// Same leading parameters as vanilla's (Enforce passes them to the parent constructor).
	void BBB_AIHotFireBehavior(SCR_AIUtilityComponent utility, SCR_AIActivityBase groupActivity, SCR_AISuppressionVolumeBase suppressionVolume, float suppressionDuration, float fireRate, float priorityLevel = PRIORITY_LEVEL_NORMAL)
	{
		m_fEndMs = GetGame().GetWorld().GetWorldTime() + suppressionDuration * 1000;
		SetIsUniqueInActionQueue(true);
	}

	override float CustomEvaluate()
	{
		if (GetGame().GetWorld().GetWorldTime() > m_fEndMs) { Complete(); return 0; }
		if (!m_Utility || !m_Utility.m_CombatComponent || !m_Utility.m_AIInfo || !m_Utility.m_AIInfo.HasUnitState(EUnitState.IN_TURRET)) { Complete(); return 0; }
		if (BBB.PlayerOf(m_Utility.m_OwnerEntity) || BBB_OrderIntent.GunnerBlocks(m_Utility)) { Complete(); return 0; }
		if (m_Utility.m_CombatComponent.GetCombatMode() == EAIGroupCombatMode.HOLD_FIRE) { Complete(); return 0; }
		BaseTarget target = m_Utility.m_CombatComponent.GetCurrentTarget();
		if (!target) return BBB_HotAI.FIRE_PRIORITY;
		if (m_Utility.m_CombatComponent.IsTargetVisible(target)) { Complete(); return 0; } // aimed fire takes over
		return BBB_HotAI.FIRE_PRIORITY_HIGH; // his attack on a target he can't see does nothing
	}
}

modded class SCR_AICombatComponent
{
	float m_fBBBHotTargetSince; // world time (ms) his current target was picked: the turret hold

	protected bool BBBHotTurretHold()
	{
		if (!BBB_HotAI.s_bVehicles || BBB_HotAI.s_fTurretHold <= 0 || !m_AIInfo || !m_AIInfo.HasUnitState(EUnitState.IN_TURRET)) return false;
		BaseTarget current = GetCurrentTarget();
		if (!current || current.GetTargetCategory() != ETargetCategory.ENEMY || current.IsDisarmed()) return false;
		if (current.GetTimeSinceSeen() > BBB_HotAI.HOLD_LOST_S) return false;
		if (GetGame().GetWorld().GetWorldTime() - m_fBBBHotTargetSince > BBB_HotAI.s_fTurretHold * 1000) return false;
		BaseWeaponComponent weapon = GetCurrentWeapon();
		if (!weapon || !weapon.GetCurrentMagazine() || weapon.GetCurrentMagazine().GetAmmoCount() <= 0) return false;
		if (current.GetTimeSinceEndangered() < BBB_HotAI.HOLD_ENDANGER_S || !m_Perception) return true;
		// Someone else is shooting at him: let vanilla choose (it scores the one shooting at him higher).
		array<BaseTarget> enemies = {};
		m_Perception.GetTargetsList(enemies, ETargetCategory.ENEMY);
		foreach (BaseTarget other : enemies)
			if (other != current && other.GetTimeSinceEndangered() < BBB_HotAI.HOLD_ENDANGER_S && other.GetTimeSinceSeen() < BBB_HotAI.HOLD_LOST_S) return false;
		return true;
	}

	override void EvaluateWeaponAndTarget(out bool outWeaponEvent, out bool outSelectedTargetChanged,
		out BaseTarget outPrevTarget, out BaseTarget outCurrentTarget,
		out bool outRetreatTargetChanged, out bool outCompartmentChanged)
	{
		BBB_HotAI.Read();
		if (BBBHotTurretHold())
		{
			// As vanilla's own between its half-second evaluations.
			outWeaponEvent = false;
			outSelectedTargetChanged = false;
			return;
		}
		BaseTarget before = GetCurrentTarget();
		BaseWeaponComponent weaponBefore = m_SelectedWeaponComp;
		super.EvaluateWeaponAndTarget(outWeaponEvent, outSelectedTargetChanged, outPrevTarget, outCurrentTarget, outRetreatTargetChanged, outCompartmentChanged);
		if (GetCurrentTarget() != before) m_fBBBHotTargetSince = GetGame().GetWorld().GetWorldTime();
		BBBHotPreferMainGun(outWeaponEvent, weaponBefore);
	}

	// The turret's main gun, with rounds in it: its autocannon, else the machine gun in the lowest weapon
	// slot (prefabs list a turret's coaxial gun anywhere: the BTR-70's PKT comes first, in slot 1, and
	// its KPVT second, in slot 0).
	BaseWeaponComponent BBBHotMainGun(out BaseMagazineComponent magazine)
	{
		if (!m_CurrentTurretController || !m_CurrentTurretController.GetWeaponManager()) return null;
		array<WeaponSlotComponent> slots = {};
		m_CurrentTurretController.GetWeaponManager().GetWeaponsSlots(slots);
		if (slots.Count() < 2) return null;
		BaseWeaponComponent best;
		int bestRank;
		int bestIndex;
		foreach (WeaponSlotComponent slot : slots)
		{
			if (!slot || !slot.GetWeaponEntity()) continue;
			BaseWeaponComponent weapon = BaseWeaponComponent.Cast(slot.GetWeaponEntity().FindComponent(BaseWeaponComponent));
			if (!weapon || !weapon.GetCurrentMagazine() || weapon.GetCurrentMagazine().GetAmmoCount() <= 0) continue;
			int rank;
			if (weapon.GetWeaponType() == EWeaponType.WT_AUTOCANNON) rank = 2;
			else if (weapon.GetWeaponType() == EWeaponType.WT_MACHINEGUN) rank = 1;
			if (rank == 0) continue;
			int index = slot.GetWeaponSlotIndex();
			if (best && (rank < bestRank || (rank == bestRank && index >= bestIndex))) continue;
			best = weapon;
			bestRank = rank;
			bestIndex = index;
		}
		if (best) magazine = best.GetCurrentMagazine();
		return best;
	}

	protected bool BBBHotMainGunOn()
	{
		return BBB_HotAI.s_bVehicles && BBB_HotAI.s_bMainGun && m_AIInfo && m_AIInfo.HasUnitState(EUnitState.IN_TURRET);
	}

	// Vanilla picked a machine gun against a man: the main gun instead (held, so no switching back and forth).
	protected void BBBHotPreferMainGun(out bool weaponEvent, BaseWeaponComponent before)
	{
		if (!BBBHotMainGunOn()) return;
		BaseTarget target = GetCurrentTarget();
		if (!target || target.GetUnitType() != EAIUnitType.UnitType_Infantry) return;
		if (!m_SelectedWeaponComp || m_SelectedWeaponComp.GetWeaponType() != EWeaponType.WT_MACHINEGUN) return;
		BaseMagazineComponent magazine;
		BaseWeaponComponent main = BBBHotMainGun(magazine);
		if (!main || main == m_SelectedWeaponComp) return;
		m_SelectedWeaponComp = main;
		m_iSelectedMuzzle = 0;
		m_SelectedMagazineComp = magazine;
		array<BaseMuzzleComponent> muzzles = {};
		main.GetMuzzlesList(muzzles);
		EMuzzleType muzzleType;
		if (!muzzles.IsEmpty()) muzzleType = muzzles[0].GetMuzzleType();
		m_SelectedWeaponResource = m_ConfigComponent.GetTreeNameForWeaponType(main.GetWeaponType(), muzzleType);
		weaponEvent = main != before;
	}

	override void EvaluateSuppressionWeapon(EAIUnitType targetUnitType, float distanceToTarget, out BaseWeaponComponent outWeaponComp, out int outMuzzleId, out BaseMagazineComponent outMagazineComp)
	{
		super.EvaluateSuppressionWeapon(targetUnitType, distanceToTarget, outWeaponComp, outMuzzleId, outMagazineComp);
		BBB_HotAI.Read();
		if (!BBBHotMainGunOn() || (outWeaponComp && outWeaponComp.GetWeaponType() != EWeaponType.WT_MACHINEGUN)) return;
		BaseMagazineComponent magazine;
		BaseWeaponComponent main = BBBHotMainGun(magazine);
		if (!main) return;
		outWeaponComp = main;
		outMuzzleId = 0;
		outMagazineComp = magazine;
	}

	override void UpdatePerceptionFactor(PerceptionComponent perceptionComp, SCR_AIThreatSystem threatSystem)
	{
		super.UpdatePerceptionFactor(perceptionComp, threatSystem);
		BBB_HotAI.Read();
		if (!BBB_HotAI.s_bOn || !perceptionComp || !threatSystem) return;
		float state = PERCEPTION_FACTOR_SAFE;
		EAIThreatState threat = threatSystem.GetState();
		if (threat == EAIThreatState.VIGILANT) state = PERCEPTION_FACTOR_VIGILANT;
		else if (threat == EAIThreatState.ALERTED) state = PERCEPTION_FACTOR_ALERTED;
		else if (threat == EAIThreatState.THREATENED) state = PERCEPTION_FACTOR_THREATENED;
		if (state < BBB_HotAI.s_fFloor) state = BBB_HotAI.s_fFloor;
		perceptionComp.SetPerceptionFactor(state * m_fEquipmentPerceptionFactor * m_fPerceptionFactor * BBB_HotAI.s_fSpot);
	}
}

modded class SCR_AIAttackBehavior
{
	override protected void InitWaitTime(SCR_AIUtilityComponent utility)
	{
		super.InitWaitTime(utility);
		BBB_HotAI.Read();
		if (!BBB_HotAI.s_bOn) return;
		m_fWaitTime.m_Value = m_fWaitTime.m_Value * BBB_HotAI.s_fWait;
	}
}

modded class SCR_AIGetFireTimes
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		BBB_HotAI.Read();
		bool pace = BBB_FireDiscipline.Paces(m_BBBUtility); // BBB fire discipline: aimed pace at range
		if (!BBB_HotAI.s_bOn && !BBB_HotAI.s_bVehicles && !pace)
		{
			ENodeResult plain = super.EOnTaskSimulate(owner, dt);
			if (plain == ENodeResult.SUCCESS && m_BBBUtility) m_BBBUtility.m_iBBBHotBursts++; // the gunner trace
			return plain;
		}
		// From here vanilla 1.8.0.13's own body, with the BBB lines marked.

		// Get agent's character entity
		IEntity ownerEntity = owner.GetControlledEntity();
		
		// Get current weapon
		BaseWeaponComponent weapon = m_CombatComponent.GetCurrentWeapon();
		if (!weapon || !ownerEntity || !m_bValid)
			return ENodeResult.FAIL;

		// Get current muzzle and fire rate
		BaseMuzzleComponent muzzle = weapon.GetCurrentMuzzle();
		if (!muzzle)
			return ENodeResult.FAIL;
		
		BaseFireMode fireMode = muzzle.GetCurrentFireMode();
		if (!fireMode)
			return ENodeResult.FAIL;
		
		// Update config if something relevant changed
		if (weapon != m_LastWeapon || muzzle != m_LastMuzzle || fireMode != m_LastFireMode)
		{
			// Update weapon type
			m_eLastWeaponType = SCR_AIWeaponHandling.GetWeaponType(weapon, true);
			
			// Update weapon config
			m_WeaponConfig = m_ConfigComponent.GetWeaponTypeHandlingConfig(m_eLastWeaponType);
			
			// Weapon data. The short-span clamp of BBB_Drag.c's override, which this copy runs in place
			// of: a fire mode with no shot span made the single-fire burst RandomFloat(0.1, 0) (script
			// error; ACA A/B soak 7 Nov, now that fire discipline sends every soldier through here).
			m_fShotSpan = Math.Max(fireMode.GetShotSpan(), BBB_MIN_SHOT_SPAN);
			m_fBurstSize = fireMode.GetBurstSize();
			
			// Update cache
			m_LastWeapon = weapon;
			m_LastMuzzle = muzzle;
			m_LastFireMode = fireMode;
		}	
				
		// Get fire pattern
		EAIFirePattern pattern = m_iFirePattern;
		
		// Get target pos
		vector targetPos;
		IEntity targetEntity;
		if (GetVariableIn(TARGET_ENTITY_PORT, targetEntity))
			targetPos = targetEntity.GetOrigin();
		else 
			GetVariableIn(TARGET_POSITION_PORT, targetPos);
		
		// We assume mid range to target
		float distToTarget = 250;
		// Get real distance if available
		if (targetPos != vector.Zero)
			distToTarget = vector.Distance(ownerEntity.GetOrigin(), targetPos);

		// Get config values
		float stabilizationTime = m_WeaponConfig.m_fBaseStabilizationTime;
		float rejectionTime = m_WeaponConfig.m_fBaseRejectionTime;
		if (BBB_HotAI.s_bVehicles && m_BBBUtility && m_BBBUtility.m_AIInfo && m_BBBUtility.m_AIInfo.HasUnitState(EUnitState.IN_TURRET))
			rejectionTime *= BBB_HotAI.s_fTurretAim; // BBB hot AI (vehicles): a turret gets time to slew onto the target
		
		// CQC Factor
		float cqcFactor;
		if (distToTarget < SCR_AICombatComponent.CLOSE_RANGE_COMBAT_DISTANCE)
			cqcFactor = Math.Map(distToTarget, 5, SCR_AICombatComponent.CLOSE_RANGE_COMBAT_DISTANCE, 1, 0);
		
		// Use weapon burst size as default
		float burstSize = m_fBurstSize;
		
		// Burst or suppress fire
		if (pattern == EAIFirePattern.Burst || pattern == EAIFirePattern.Suppress)
		{
			// Override weapon burst size
			if (burstSize <= 1)
			{
				// It's a machine gun!
				if (m_eLastWeaponType == EWeaponType.WT_MACHINEGUN)
				{
					float range = 2.6;
					float base = 1.75;
					
					// Suppressive fire for mg
					if (pattern == EAIFirePattern.Suppress)
					{
						range = 3.5;
						base = 2.2;
					}
					
					stabilizationTime += Math.RandomGaussFloat(0.236, 0);
					burstSize = base + Math.Pow(Math.RandomFloat(0, range) - 0.4, 2);
				}
				else
				{
					float range = 1.77;
					
					// Suppressive fire for rifles
					if (pattern == EAIFirePattern.Suppress)
						range = 1.96;
					
					burstSize = 0.75 + Math.Pow(Math.RandomFloat(0, range) - 0.3, 3);
				}
			}
		}
		// SemiAuto/Single fire - default
		else
		{
			burstSize = Math.RandomFloat(0.1, m_fShotSpan * 1.9);
			stabilizationTime += Math.RandomGaussFloat(0.236, 0);
		}
		
		// Apply CQC factor to burst size
		burstSize -= Math.RandomGaussFloat(0.32, 0) * cqcFactor;
		if (BBB_HotAI.s_bOn) burstSize *= BBB_HotAI.s_fBurst; // BBB hot AI: longer bursts
		if (pace) burstSize = BBB_FireDiscipline.PacedBurst(burstSize, distToTarget, m_eLastWeaponType == EWeaponType.WT_MACHINEGUN); // BBB fire discipline: short bursts at range
		
		if (stabilizationTime > 0)
			stabilizationTime -= Math.RandomFloat(stabilizationTime / 3, stabilizationTime) * cqcFactor * 0.85;
		else
			stabilizationTime = 0;
				
		// Ger fire time
		float fireTime = m_fShotSpan * burstSize;			
		
		// Get Agent/Group fire rate factor	
		float fireRateFactor = GetFireRateFactor();
	
		// Apply fire rate
		fireTime += (fireTime / 2.5) * fireRateFactor; 
		stabilizationTime += (stabilizationTime * 1.05) * fireRateFactor;
		
		// Fire rate factor in MG stabilization
		if (m_eLastWeaponType == EWeaponType.WT_MACHINEGUN)
			stabilizationTime += Math.AbsFloat(Math.RandomGaussFloat(0.12, 0)) * fireRateFactor;
			
		// Stabilization range factor
		vector stabTimeRandRange = Vector(0.1, 0, 0.7);
		
		// Mid or long range
		if (distToTarget > SCR_AICombatComponent.CLOSE_RANGE_COMBAT_DISTANCE)
		{
			// Add factors of mid range
			stabTimeRandRange += Vector(0.5, 0, 0.7);

			// Add factors of long range
			if (distToTarget > SCR_AICombatComponent.LONG_RANGE_COMBAT_DISTANCE)
				stabTimeRandRange += Vector(0.7, 0, 1.5);
		}
			
		// Distance is main factor in stabilization time
		stabilizationTime *= Math.RandomFloat(stabTimeRandRange[0], stabTimeRandRange[2]);
		
		if (BBB_HotAI.s_bOn) stabilizationTime *= BBB_HotAI.s_fPause; // BBB hot AI: shorter aiming pauses
		if (pace) stabilizationTime *= BBB_FireDiscipline.PacedPause(distToTarget); // BBB fire discipline: aim longer at range
		// Make sure it's always some time
		stabilizationTime = Math.Max(stabilizationTime, 0.05);
		
		SetVariableOut(PORT_FIRE_TIME, fireTime);
		SetVariableOut(PORT_STABILIZATION_TIME, stabilizationTime);
		SetVariableOut(PORT_REJECT_TIME, rejectionTime);
		SetVariableOut(PORT_SHOT_SPAN, m_fShotSpan);
		if (m_BBBUtility) m_BBBUtility.m_iBBBHotBursts++; // BBB hot AI: the gunner trace's burst count

		return ENodeResult.SUCCESS;
	}
}

modded class SCR_DamageManagerComponent
{
	override bool HijackDamageHandling(notnull BaseDamageContext damageContext)
	{
		bool hijacked = super.HijackDamageHandling(damageContext);
		if (hijacked || !Replication.IsServer()) return hijacked;
		IEntity owner = GetOwner();
		if (!owner || ChimeraCharacter.Cast(owner)) return hijacked;
		IEntity root = owner.GetRootParent();
		if (Vehicle.Cast(root)) BBB_HotAI.OnVehicleHit(root, damageContext);
		return hijacked;
	}
}
