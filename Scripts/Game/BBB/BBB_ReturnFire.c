// Return fire at unseen shooters (3 Nov 2026, the user's playtest: "when an ai is getting shot at through
// a bush, can they return fire through the bush? they should be able to at least return indirect/
// suppressive fire back in the direction of the shots"). An AI aims only at a target its perception
// has, and a bush hides the shooter, so a soldier shot at from cover never gets one. Vanilla's answer
// to fire it can't place is to look and get away from it: SCR_AIObserveThreatSystemBehavior (69,
// OBSERVE_THREATS_HIGH_PRIORITY) and moving from unknown fire (68) outrank both investigating (64) and
// the group's suppressive fire on the shots' cluster (63, SUPPRESS, and only from 30 m to 800 m), so
// he never fires back.
// With the option on, a soldier on foot who has a round pass within vanilla's flyby radius (13 m,
// SCR_AIDangerReaction_WeaponFired) or is hit, by an enemy he can't see, fires a short burst
// (BURST_S) into a box round where the shots came from, as well as he can place them by ear
// (ERROR_PER_M of the distance, off the true spot by as much). Then vanilla takes over again: cover,
// looking, the group's suppression. RETURN_PRIORITY sits just above vanilla's look-and-get-away and
// below an attack on a target he can see, a grenade throw or a retreat.
// Not: under hold fire, under a player's or Game Master's order (BBB_OrderIntent), for players or
// civilians, in a vehicle (gunners: BBB's hot AI), at shots from the sky, or from closer than MIN_M.
// Once per COOLDOWN_S per soldier, CHANCE of the time (a roll seeded by the soldier and the moment).
// Host option: -bbbReturnFire 1 (off by default until it has been played).
// With fire discipline on (default, BBB_FireDiscipline, 6 Nov 2026) the burst goes through the same
// gate as contact fire instead: he gets into cover first, riflemen not past 250 m, a couple per squad,
// spare magazines kept, one cooldown for both. BBB_AIReturnFireBehavior is then unused.
class BBB_ReturnFire
{
	static int s_iRead;
	static bool s_bOn;
	static int s_iFires;		// bursts started (tests and the log)
	static int s_iShotAt;		// rounds past or hits reported to it (tests)
	protected static ref map<string, int> s_mWhyNot;	// why a report started no burst (tests); made on first use
	static const float MIN_M = 3;
	static const float MAX_M = 500;			// vanilla's audible range for a normal gunshot
	static const float BURST_S = 3.5;
	static const float COOLDOWN_S = 6;
	static const float CHANCE = 0.8;
	static const float ERROR_PER_M = 0.05;	// how far off his guess is, per metre to the shooter
	static const float SEEN_M = 10;			// an enemy seen this near the shots: vanilla's attack has him
	static const float SEEN_S = 2;
	static const float RETURN_PRIORITY = 71;	// above OBSERVE_THREATS_HIGH_PRIORITY (69), below ATTACK_SELECTED (90)
	static const float FIRE_RATE = 1.2;

	static void Read()
	{
		if (!BBB_Settings.Due(s_iRead)) return;
		string value;
		s_bOn = BBB_Settings.Get("bbbReturnFire", value) && value == "1";
		PrintFormat("[BBB] RETURN FIRE on=%1", s_bOn);
	}

	// A round passed close to the soldier 'utility' runs (or hit him), fired from 'from'.
	static void OnShotAt(SCR_AIUtilityComponent utility, vector from, string how)
	{
		Read();
		if (!s_bOn) return;
		s_iShotAt++;
		if (!BBB.IsStarted() || !utility || !Replication.IsServer()) { WhyNot("not running"); return; }
		IEntity man = utility.m_OwnerEntity;
		if (!man || !BBB.IsAlive(man) || BBB.PlayerOf(man) != 0 || !utility.IsMilitary()) { WhyNot("not an AI soldier"); return; }
		if (!utility.m_AIInfo || !utility.m_CombatComponent || !utility.m_PerceptionComponent) { WhyNot("no AI parts"); return; }
		if (utility.m_AIInfo.HasUnitState(EUnitState.IN_VEHICLE) || utility.m_AIInfo.HasUnitState(EUnitState.IN_TURRET) || utility.m_AIInfo.HasUnitState(EUnitState.UNCONSCIOUS)) { WhyNot("in a vehicle or down"); return; }
		if (utility.m_CombatComponent.GetCombatMode() == EAIGroupCombatMode.HOLD_FIRE) { WhyNot("hold fire"); return; }
		float now = GetGame().GetWorld().GetWorldTime();
		if (now < utility.m_fBBBReturnFireNext) { WhyNot("cooling down"); return; }
		vector me = man.GetOrigin();
		float distance = vector.Distance(me, from);
		if (distance < MIN_M || distance > MAX_M) { WhyNot("out of range"); return; }
		if (!BBB_SuppressionSafety.GroundSector(from)) { WhyNot("from the sky"); return; }
		// He can see the shooter, or another enemy right there: vanilla's aimed fire has him.
		BaseTarget current = utility.m_CombatComponent.GetCurrentTarget();
		if (current && utility.m_CombatComponent.IsTargetVisible(current)) { WhyNot("sees his target"); return; }
		if (SeenNear(utility, from)) { WhyNot("sees an enemy there"); return; }
		if (BBB_OrderIntent.Blocks(utility)) { WhyNot("under an order"); return; }
		utility.m_fBBBReturnFireNext = now + COOLDOWN_S * 1000;
		RandomGenerator roll = new RandomGenerator();
		roll.SetSeed((int)(me[0] * 10) * 7919 + (int)(me[2] * 10) * 31 + (int)(now / 1000));
		if (roll.RandFloat01() >= CHANCE)
		{
			WhyNot("rolled to keep his head down");
			BBB_Think.NoteUtility(utility, "ReturnFire", "shot at by an unseen enemy: keeps his head down this time");
			return;
		}
		// Where he puts it: round the shots' origin, off by how well he can place them by ear.
		float error = Math.Clamp(distance * ERROR_PER_M, 1, 15);
		vector aim = BBB.Ground(from + Vector(roll.RandFloatXY(-error, error), 0, roll.RandFloatXY(-error, error)));
		float half = Math.Max(1.5, error);
		// Fire discipline: the same gate as contact fire (one cooldown, cover first, range, squad, ammo).
		if (BBB_FireDiscipline.On())
		{
			BaseWeaponComponent weapon = utility.m_CombatComponent.GetCurrentWeapon();
			if (!weapon || !weapon.GetCurrentMagazine() || weapon.GetCurrentMagazine().GetAmmoCount() <= 0) { WhyNot("no loaded weapon"); return; }
			if (!BBB_FireDiscipline.StartBlindFire(utility, weapon, aim, half, distance, "return fire")) { WhyNot("fire discipline"); return; }
			s_iFires++;
			PrintFormat("[BBB] RETURN FIRE %1: %2 from an unseen enemy %3 m away, fires back at %4 once in cover", man, how, Math.Round(distance), aim);
			return;
		}
		SCR_AISuppressionVolumeBox volume = new SCR_AISuppressionVolumeBox(Vector(aim[0] - half, aim[1] - 0.3, aim[2] - half), Vector(aim[0] + half, aim[1] + 1.8, aim[2] + half));
		BBB_AIReturnFireBehavior fire = new BBB_AIReturnFireBehavior(utility, null, volume, BURST_S, FIRE_RATE);
		utility.AddAction(fire);
		s_iFires++;
		PrintFormat("[BBB] RETURN FIRE %1: %2 from an unseen enemy %3 m away, fires back at %4", man, how, Math.Round(distance), aim);
		BBB_Think.NoteUtility(utility, "ReturnFire", "fires back at an unseen shooter, " + Math.Round(distance) + " m");
	}

	protected static void WhyNot(string why)
	{
		if (!s_mWhyNot) s_mWhyNot = new map<string, int>();
		s_mWhyNot.Set(why, s_mWhyNot.Get(why) + 1);
	}

	// Why reports started no burst, as "reason n, ..." (tests).
	static string WhyNotSummary()
	{
		string text;
		if (!s_mWhyNot) return text;
		foreach (string why, int n : s_mWhyNot) text += string.Format("%1 %2, ", why, n);
		return text;
	}

	// An enemy he saw in the last SEEN_S, within SEEN_M of where the shots came from.
	protected static bool SeenNear(SCR_AIUtilityComponent utility, vector from)
	{
		array<BaseTarget> targets = {};
		utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
		foreach (BaseTarget target : targets)
		{
			if (!target || target.GetTimeSinceSeen() > SEEN_S) continue;
			if (vector.DistanceXZ(target.GetLastSeenPosition(), from) < SEEN_M) return true;
		}
		return false;
	}

	// The shooter is an enemy of his (a hit can come from a friend).
	static bool Enemy(SCR_AIUtilityComponent utility, IEntity shooter)
	{
		if (!shooter) return false;
		SCR_ChimeraAIAgent agent = SCR_ChimeraAIAgent.Cast(utility.GetOwner());
		Faction faction = SCR_AIFactionHandling.GetEntityPerceivedFaction(shooter);
		return agent && faction && agent.IsEnemy(faction);
	}
}

modded class SCR_AIUtilityComponent
{
	float m_fBBBReturnFireNext;	// world time (ms) he may return fire at an unseen shooter again
}

// A burst of return fire: vanilla's suppressive fire into a box, given up as soon as he sees a target.
class BBB_AIReturnFireBehavior : SCR_AISuppressBehavior
{
	protected float m_fEndMs;

	// Same leading parameters as vanilla's (Enforce passes them to the parent constructor).
	void BBB_AIReturnFireBehavior(SCR_AIUtilityComponent utility, SCR_AIActivityBase groupActivity, SCR_AISuppressionVolumeBase suppressionVolume, float suppressionDuration, float fireRate, float priorityLevel = PRIORITY_LEVEL_NORMAL)
	{
		m_fEndMs = GetGame().GetWorld().GetWorldTime() + suppressionDuration * 1000;
		SetIsUniqueInActionQueue(true);
	}

	override float CustomEvaluate()
	{
		if (GetGame().GetWorld().GetWorldTime() > m_fEndMs) { Complete(); return 0; }
		if (!m_Utility || !m_Utility.m_CombatComponent || !m_Utility.m_AIInfo) { Complete(); return 0; }
		if (m_Utility.m_AIInfo.HasUnitState(EUnitState.IN_VEHICLE) || m_Utility.m_AIInfo.HasUnitState(EUnitState.UNCONSCIOUS)) { Complete(); return 0; }
		if (BBB.PlayerOf(m_Utility.m_OwnerEntity) || BBB_OrderIntent.Held(m_Utility)) { Complete(); return 0; }
		if (m_Utility.m_CombatComponent.GetCombatMode() == EAIGroupCombatMode.HOLD_FIRE) { Complete(); return 0; }
		BaseTarget target = m_Utility.m_CombatComponent.GetCurrentTarget();
		if (target && m_Utility.m_CombatComponent.IsTargetVisible(target)) { Complete(); return 0; } // aimed fire takes over
		return BBB_ReturnFire.RETURN_PRIORITY;
	}
}

// A round past him (vanilla's flyby: within 13 m of its line), from an enemy (vanilla checked).
// Built from the AI config, so it repeats vanilla's attribute: without it the config can't build the
// reaction and the soldier loses it altogether (srv:returnfire, 3 Nov: no gunshot or damage reaction).
[BaseContainerProps()]
modded class SCR_AIDangerReaction_WeaponFired
{
	override void OnProjectileFlyby(notnull SCR_AIUtilityComponent utility, int dangerEventCount, vector pos)
	{
		super.OnProjectileFlyby(utility, dangerEventCount, pos);
		BBB_ReturnFire.OnShotAt(utility, pos, "a round past him");
	}
}

// A hit on him, from wherever the shooter stands. Vanilla's attribute repeated, as above.
[BaseContainerProps()]
modded class SCR_AIDangerReaction_DamageTaken
{
	override bool PerformReaction(notnull SCR_AIUtilityComponent utility, notnull SCR_AIThreatSystem threatSystem, AIDangerEvent dangerEvent, int dangerEventCount)
	{
		bool reacted = super.PerformReaction(utility, threatSystem, dangerEvent, dangerEventCount);
		IEntity shooter = dangerEvent.GetObject();
		if (reacted && BBB_ReturnFire.Enemy(utility, shooter)) BBB_ReturnFire.OnShotAt(utility, shooter.GetOrigin(), "a hit");
		return reacted;
	}
}
