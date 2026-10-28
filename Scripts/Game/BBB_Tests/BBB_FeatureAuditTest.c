// -bbbFeatureAuditTest 1 (or -acrBbbFeatureAuditTest 1 from ACA's test launcher), on
// BBB_TestArland. Behaviour checks for the opt-in 19-20 Oct features that need a real
// outcome, not just a dispatch counter (Codex's validation report, 20 Oct 2026):
// - smoke (BBB_Smoke and medic smoke) and frag grenades actually leave the thrower's
//   inventory; a frag is vetoed when friendlies are inside the blast;
// - a static weapon gunner walks to the seat (no teleport), the job only counts as
//   crewed once he is seated, and it is released (native get-out) for host policy and
//   player orders; a pending boarding is cancelled cleanly;
// - a withdrawal counts only ground actually gained and never reports a frozen squad
//   as having broken contact.
// Fixture: one US fireteam, a USSR team 60 m north (hold fire, blind), an armed M1025
// 20 m east of the fireteam facing north. It does not judge perception or firefights.

class BBB_AuditStatic : BBB_StaticWeapons
{
	static int Jobs() { return s_aJobs.Count(); }
	static bool Mounted()
	{
		bool mounted = false;
		foreach (BBB_StaticGunnerJob job : s_aJobs)
		{
			if (job && job.m_bMounted) mounted = true;
		}
		return mounted;
	}
}

class BBB_AuditMorale : BBB_Morale
{
	static void BeginWithdrawal(BBB_GroupMoraleState state, vector threat, float now) { StartWithdrawal(state, threat, now); }
	static void Advance(BBB_GroupMoraleState state, float now) { UpdateWithdrawal(state, now); }
}

// Wraps whatever hooks are running and can veto static weapons for one test step.
class BBB_AuditHooks : BBB_Hooks
{
	ref BBB_Hooks m_Inner;
	bool m_bVetoStatic;
	IEntity m_LeaveAlone;
	override void GetGroups(notnull array<SCR_AIGroup> groups) { m_Inner.GetGroups(groups); }
	override void GetMembers(SCR_AIGroup group, notnull array<AIAgent> agents) { m_Inner.GetMembers(group, agents); }
	override string NameOf(SCR_AIGroup group) { return m_Inner.NameOf(group); }
	override IEntity AnchorOf(SCR_AIGroup group) { return m_Inner.AnchorOf(group); }
	override IEntity OwnVehicleOf(SCR_AIGroup group) { return m_Inner.OwnVehicleOf(group); }
	override bool LeaveAlone(IEntity vehicle)
	{
		bool mine = m_LeaveAlone && vehicle == m_LeaveAlone;
		return mine || m_Inner.LeaveAlone(vehicle);
	}
	override bool AllowFireManeuver(SCR_AIGroup group) { return m_Inner.AllowFireManeuver(group); }
	override bool AllowRelaxedMarch(SCR_AIGroup group) { return m_Inner.AllowRelaxedMarch(group); }
	override bool AllowMorale(SCR_AIGroup group) { return m_Inner.AllowMorale(group); }
	override bool AllowSmoke(SCR_AIGroup group) { return m_Inner.AllowSmoke(group); }
	override bool AllowStaticWeapons(SCR_AIGroup group)
	{
		return !m_bVetoStatic && m_Inner.AllowStaticWeapons(group);
	}
	override bool AllowGrenades(SCR_AIGroup group) { return m_Inner.AllowGrenades(group); }
	override bool ManagesVehicleDriving(SCR_AIGroup group) { return m_Inner.ManagesVehicleDriving(group); }
	override void OnLitter(IEntity item) { m_Inner.OnLitter(item); }
}

class BBB_FeatureAuditTest : BBB_AimTest
{
	static ref BBB_FeatureAuditTest s_Audit;
	static ref BBB_AuditHooks s_AuditHooks;
	static ref BBB_Hooks s_SavedHooks;
	SCR_AIGroup m_Group;
	SCR_AIGroup m_Enemy;
	IEntity m_Weapon;
	BaseCompartmentSlot m_Seat;
	ChimeraCharacter m_Gunner;
	vector m_vGunnerStart;
	int m_iGrenadesBefore;
	bool m_bSavedFireManeuver;
	int m_Checks;
	int m_Failures;
	int m_iStep;
	float m_fStepStart;
	vector m_vCenterStart;
	vector m_vEnemyDir;
	ref BBB_GroupMoraleState m_Withdraw;

	static void Start()
	{
		if (!Replication.IsServer()) return;
		if (!System.IsCLIParam("bbbFeatureAuditTest") && !System.IsCLIParam("acrBbbFeatureAuditTest")) return;
		s_Audit = new BBB_FeatureAuditTest();
		GetGame().GetCallqueue().CallLater(s_Audit.BeginAudit, 15000);
	}

	void Audit(string label, bool passed, string detail = "")
	{
		m_Checks++;
		if (!passed) m_Failures++;
		PrintFormat("[AUDIT] CHECK %1 ok=%2 %3", label, passed, detail);
	}

	float NowMs() { return GetGame().GetWorld().GetWorldTime(); }

	int GroupGrenades()
	{
		int total = 0;
		array<AIAgent> agents = {};
		m_Group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			IEntity body = agent.GetControlledEntity();
			if (!body) continue;
			InventoryStorageManagerComponent inv = InventoryStorageManagerComponent.Cast(body.FindComponent(InventoryStorageManagerComponent));
			if (inv) total = total + inv.GetGrenadesCount();
		}
		return total;
	}

	string GrenadeBreakdown()
	{
		string text = "";
		array<AIAgent> agents = {};
		m_Group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			IEntity body = agent.GetControlledEntity();
			if (!body) continue;
			InventoryStorageManagerComponent inv = InventoryStorageManagerComponent.Cast(body.FindComponent(InventoryStorageManagerComponent));
			if (inv) text = text + string.Format("%1 ", inv.GetGrenadesCount());
		}
		return text;
	}

	ChimeraCharacter Member(int index)
	{
		array<AIAgent> agents = {};
		m_Group.GetAgents(agents);
		if (index >= agents.Count()) return null;
		return ChimeraCharacter.Cast(agents[index].GetControlledEntity());
	}

	SCR_AIUtilityComponent UtilityAt(int index)
	{
		array<AIAgent> agents = {};
		m_Group.GetAgents(agents);
		if (index >= agents.Count()) return null;
		return SCR_AIUtilityComponent.Cast(agents[index].FindComponent(SCR_AIUtilityComponent));
	}

	void SetGroupSpeed(float fraction)
	{
		array<AIAgent> agents = {};
		m_Group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
			if (body) body.GetCharacterController().OverrideMaxSpeed(fraction);
		}
	}

	void UseAuditHooks(bool vetoStatic, IEntity leaveAlone)
	{
		if (!s_AuditHooks)
		{
			s_SavedHooks = BBB.s_Hooks;
			s_AuditHooks = new BBB_AuditHooks();
			s_AuditHooks.m_Inner = s_SavedHooks;
			BBB.s_Hooks = s_AuditHooks;
		}
		s_AuditHooks.m_bVetoStatic = vetoStatic;
		s_AuditHooks.m_LeaveAlone = leaveAlone;
	}

	void RestoreHooks()
	{
		if (s_SavedHooks) BBB.s_Hooks = s_SavedHooks;
		s_AuditHooks = null;
		s_SavedHooks = null;
	}

	// ---------------------------------------------------------------- fixture

	void BeginAudit()
	{
		BBB_FireManeuver.Read();
		m_bSavedFireManeuver = BBB_FireManeuver.s_bOn;
		BBB_FireManeuver.s_bOn = false;
		BBB_FireManeuver.Stop(); // keep the boarding squad inside the real static-weapon tether
		BBB_Morale.Read(); BBB_Smoke.Read(); BBB_StaticWeapons.Read(); BBB_Grenades.Read(); BBB_Medic.Read();
		BBB_Morale.s_bOn = false; BBB_StaticWeapons.s_bOn = false; BBB_Smoke.s_bOn = false; BBB_Grenades.s_bOn = false; BBB_Buildings.s_bOn = false; // indoor moves break the fixed outdoor throw geometry
		vector at = BBB.Ground(Vector(3050, 0, 1700));
		NavmeshWorldComponent navmesh = Navmesh();
		if (navmesh)
		{
			for (int tx = -1; tx <= 1; tx++)
			{
				for (int tz = -1; tz <= 1; tz++) navmesh.LoadTileIn(at + Vector(tx * 60, 0, tz * 60));
			}
		}
		ObserversSystem observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
		if (observers) observers.InsertObserverSP(22121, at[0], at[2], null);
		// Put the enemy where the fireteam has a clear line of sight (fields and hedges vary)
		array<vector> dirs = {Vector(0, 0, 1), Vector(1, 0, 0), Vector(0, 0, -1), Vector(-1, 0, 0), Vector(0.707, 0, 0.707), Vector(-0.707, 0, 0.707), Vector(0.707, 0, -0.707), Vector(-0.707, 0, -0.707)};
		m_vEnemyDir = dirs[0];
		bool clear = false;
		foreach (vector dir : dirs)
		{
			if (Visible(at, at + dir * 45))
			{
				m_vEnemyDir = dir;
				clear = true;
				break;
			}
		}
		PrintFormat("[AUDIT] SITE enemy direction %1 clear line of sight=%2", m_vEnemyDir, clear);
		vector right = Vector(m_vEnemyDir[2], 0, -m_vEnemyDir[0]);
		m_Group = SCR_AIGroup.Cast(Spawn(TEAM_US, at, m_vEnemyDir));
		m_Enemy = SCR_AIGroup.Cast(Spawn(TEAM_USSR, at + m_vEnemyDir * 45, m_vEnemyDir * -1));
		m_Weapon = Spawn("{3EA6F47D95867114}Prefabs/Vehicles/Wheeled/M998/M1025_armed_M2HB.et", at + right * 20 + m_vEnemyDir * 2, m_vEnemyDir);
		Audit("fixtures-spawn", m_Group && m_Enemy && m_Weapon);
		if (!m_Group || !m_Enemy || !m_Weapon) { FinishAudit(); return; }
		GetGame().GetCallqueue().CallLater(Prepare, 5000);
	}

	void Prepare()
	{
		PinDetail(m_Group); PinDetail(m_Enemy);
		m_Group.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.HOLD_FIRE); m_Group.GetGroupUtilityComponent().EvaluateCombatMode();
		m_Enemy.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.HOLD_FIRE); m_Enemy.GetGroupUtilityComponent().EvaluateCombatMode();
		array<AIAgent> agents = {};
		m_Group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			PinDetail(agent);
			ChimeraCharacter body = ChimeraCharacter.Cast(agent.GetControlledEntity());
			if (body) body.GetDamageManager().EnableDamageHandling(false);
		}
		agents.Clear(); m_Enemy.GetAgents(agents);
		foreach (AIAgent enemy : agents)
		{
			PinDetail(enemy);
			ChimeraCharacter enemyBody = ChimeraCharacter.Cast(enemy.GetControlledEntity());
			SCR_AIUtilityComponent enemyUtility = SCR_AIUtilityComponent.Cast(enemy.FindComponent(SCR_AIUtilityComponent));
			if (enemyBody) { enemyBody.GetCharacterController().OverrideMaxSpeed(0); enemyBody.GetDamageManager().EnableDamageHandling(false); }
			if (enemyUtility && enemyUtility.m_CombatComponent) enemyUtility.m_CombatComponent.SetPerceptionFactor(0);
		}
		agents.Clear(); m_Group.GetAgents(agents);
		Audit("fireteam-populated", agents.Count() >= 4, string.Format("members=%1", agents.Count()));
		if (agents.Count() < 4) { FinishAudit(); return; }
		GetGame().GetCallqueue().CallLater(SmokeThrow, 3000);
	}

	// ---------------------------------------------------------------- throws

	void SmokeThrow()
	{
		m_iGrenadesBefore = GroupGrenades();
		BBB_Smoke.s_bOn = true;
		vector leader = m_Group.GetLeaderEntity().GetOrigin();
		bool dispatched = BBB_Smoke.TryDeploySmoke(m_Group, leader, leader + m_vEnemyDir * 45, "maneuver");
		BBB_Smoke.s_bOn = false;
		Audit("smoke-dispatched", dispatched, string.Format("grenades before=%1 [%2]", m_iGrenadesBefore, GrenadeBreakdown()));
		GetGame().GetCallqueue().CallLater(SmokeLanded, 9000);
	}

	static int s_iSmokeEntities;
	static bool CountSmoke(IEntity e)
	{
		EntityPrefabData data = e.GetPrefabData();
		if (!data) return true;
		string name = data.GetPrefabName();
		name.ToLower();
		if (name.Contains("smoke") && name.Contains("grenade")) s_iSmokeEntities++;
		return true;
	}

	void SmokeLanded()
	{
		s_iSmokeEntities = 0;
		GetGame().GetWorld().QueryEntitiesBySphere(m_Group.GetLeaderEntity().GetOrigin(), 45, CountSmoke);
		PrintFormat("[AUDIT] SMOKE grenade entities within 45 m: %1", s_iSmokeEntities);
		int after = GroupGrenades();
		Audit("smoke-actually-thrown", after < m_iGrenadesBefore, string.Format("grenades %1 -> %2 [%3]", m_iGrenadesBefore, after, GrenadeBreakdown()));
		m_iGrenadesBefore = after;
		bool medicSmokeWas = BBB_Medic.s_bSmokeOn;
		BBB_Medic.s_bSmokeOn = true;
		bool dispatched = BBB_Medic.TryDeploySmoke(Member(1), Member(2));
		BBB_Medic.s_bSmokeOn = medicSmokeWas;
		Audit("medic-smoke-dispatched", dispatched);
		GetGame().GetCallqueue().CallLater(MedicSmokeLanded, 9000);
	}

	void MedicSmokeLanded()
	{
		int after = GroupGrenades();
		Audit("medic-smoke-actually-thrown", after < m_iGrenadesBefore, string.Format("grenades %1 -> %2 [%3]", m_iGrenadesBefore, after, GrenadeBreakdown()));
		m_iGrenadesBefore = after;
		BBB_Grenades.s_bOn = true;
		vector leader = m_Group.GetLeaderEntity().GetOrigin();
		bool vetoed = !BBB_Grenades.TryThrowGrenadeAt(m_Group, leader + m_vEnemyDir * 5, "audit friendly blast");
		Audit("frag-vetoed-with-friendlies-in-blast", vetoed);
		bool dispatched = BBB_Grenades.TryThrowGrenadeAt(m_Group, leader + m_vEnemyDir * 24, "audit clear");
		BBB_Grenades.s_bOn = false;
		Audit("frag-dispatched-clear-spot", dispatched);
		GetGame().GetCallqueue().CallLater(FragLanded, 9000);
	}

	void FragLanded()
	{
		int after = GroupGrenades();
		Audit("frag-actually-thrown", after < m_iGrenadesBefore, string.Format("grenades %1 -> %2 [%3]", m_iGrenadesBefore, after, GrenadeBreakdown()));
		StaticBegin();
	}

	// ---------------------------------------------------------------- static weapon

	void StaticBegin()
	{
		bool valid = BBB_StaticWeapons.IsStaticWeapon(m_Weapon, m_Seat);
		Audit("weapon-eligible", valid && m_Seat);
		if (!valid || !m_Seat) { MoraleBegin(); return; }
		m_Gunner = BBB_StaticWeapons.FindBestCandidate(m_Group, m_Weapon);
		Audit("gunner-candidate", m_Gunner != null);
		if (!m_Gunner) { MoraleBegin(); return; }

		UseAuditHooks(true, null);
		Audit("host-veto-blocks-crewing", !BBB_StaticWeapons.CrewWeapon(m_Group, m_Gunner, m_Weapon, m_Seat));
		UseAuditHooks(false, m_Weapon);
		Audit("leave-alone-blocks-crewing", !BBB_StaticWeapons.CrewWeapon(m_Group, m_Gunner, m_Weapon, m_Seat));
		UseAuditHooks(false, null);

		// A pending boarding overtaken by host policy is cancelled and frees the seat
		bool requested = BBB_StaticWeapons.CrewWeapon(m_Group, m_Gunner, m_Weapon, m_Seat);
		Audit("pending-request", requested && BBB_AuditStatic.Jobs() == 1 && !BBB_AuditStatic.Mounted() && m_Seat.IsReservedBy(m_Gunner));
		UseAuditHooks(true, null);
		GetGame().GetCallqueue().CallLater(PendingCancelled, 3000);
	}

	void PendingCancelled()
	{
		Audit("pending-cancelled-by-host-policy", BBB_AuditStatic.Jobs() == 0 && !m_Seat.IsReserved(), string.Format("jobs=%1 reserved=%2", BBB_AuditStatic.Jobs(), m_Seat.IsReserved()));
		UseAuditHooks(false, null);
		GetGame().GetCallqueue().CallLater(CrewForReal, 3000);
	}

	void CrewForReal()
	{
		Audit("gunner-not-seated-after-cancel", m_Seat.GetOccupant() != m_Gunner);
		m_vGunnerStart = m_Gunner.GetOrigin();
		float dist = vector.DistanceXZ(m_vGunnerStart, m_Weapon.GetOrigin());
		bool requested = BBB_StaticWeapons.CrewWeapon(m_Group, m_Gunner, m_Weapon, m_Seat);
		Audit("boarding-request", requested, string.Format("gunner %1 m from weapon", Math.Round(dist)));
		GetGame().GetCallqueue().CallLater(NoTeleport, 1500);
	}

	void NoTeleport()
	{
		float startDist = vector.DistanceXZ(m_vGunnerStart, m_Weapon.GetOrigin());
		bool seated = m_Seat.GetOccupant() == m_Gunner;
		Audit("no-teleport-into-seat", startDist < 4 || !seated, string.Format("start %1 m, seated after 1.5 s=%2", Math.Round(startDist), seated));
		Audit("job-pending-while-walking", BBB_AuditStatic.Jobs() == 1 && (seated || !BBB_AuditStatic.Mounted()));
		m_fStepStart = NowMs();
		GetGame().GetCallqueue().CallLater(WaitSeated, 500);
	}

	void WaitSeated()
	{
		bool seated = m_Seat.GetOccupant() == m_Gunner;
		if (!(seated && BBB_AuditStatic.Mounted()) && NowMs() - m_fStepStart < 32000) { GetGame().GetCallqueue().CallLater(WaitSeated, 500); return; }
		Audit("gunner-walked-and-seated", seated, string.Format("after %1 s, walked %2 m", Math.Round((NowMs() - m_fStepStart) / 1000.0), Math.Round(vector.DistanceXZ(m_vGunnerStart, m_Gunner.GetOrigin()))));
		Audit("job-mounted-once-seated", BBB_AuditStatic.Mounted());
		if (!seated) { BBB_StaticWeapons.Stop(); MoraleBegin(); return; }
		UseAuditHooks(true, null);
		m_fStepStart = NowMs();
		GetGame().GetCallqueue().CallLater(WaitHostRelease, 500);
	}

	void WaitHostRelease()
	{
		bool isOut = m_Seat.GetOccupant() != m_Gunner;
		if (!(isOut && BBB_AuditStatic.Jobs() == 0) && NowMs() - m_fStepStart < 12000) { GetGame().GetCallqueue().CallLater(WaitHostRelease, 500); return; }
		Audit("host-policy-releases-gunner", BBB_AuditStatic.Jobs() == 0);
		Audit("released-gunner-gets-out", isOut, string.Format("after %1 s", Math.Round((NowMs() - m_fStepStart) / 1000.0)));
		UseAuditHooks(false, null);
		GetGame().GetCallqueue().CallLater(CrewAgain, 2000);
	}

	void CrewAgain()
	{
		bool requested = BBB_StaticWeapons.CrewWeapon(m_Group, m_Gunner, m_Weapon, m_Seat);
		Audit("re-crew-request", requested);
		m_fStepStart = NowMs();
		GetGame().GetCallqueue().CallLater(WaitSeatedAgain, 500);
	}

	void WaitSeatedAgain()
	{
		bool seated = m_Seat.GetOccupant() == m_Gunner;
		if (!(seated && BBB_AuditStatic.Mounted()) && NowMs() - m_fStepStart < 20000) { GetGame().GetCallqueue().CallLater(WaitSeatedAgain, 500); return; }
		Audit("gunner-seated-again", seated && BBB_AuditStatic.Mounted());
		if (!seated) { BBB_StaticWeapons.Stop(); MoraleBegin(); return; }
		AIWaypoint move = AIWaypoint.Cast(Spawn("{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et", m_Group.GetLeaderEntity().GetOrigin() - m_vEnemyDir * 30, Vector(0, 0, 1)));
		if (move) { SCR_AIWaypoint.Cast(move).SetPriorityLevel(SCR_AIActionBase.PRIORITY_LEVEL_PLAYER); m_Group.AddWaypoint(move); }
		m_Order = move;
		m_fStepStart = NowMs();
		GetGame().GetCallqueue().CallLater(WaitOrderRelease, 500);
	}

	AIWaypoint m_Order;

	void WaitOrderRelease()
	{
		bool isOut = m_Seat.GetOccupant() != m_Gunner;
		if (!(isOut && BBB_AuditStatic.Jobs() == 0) && NowMs() - m_fStepStart < 12000) { GetGame().GetCallqueue().CallLater(WaitOrderRelease, 500); return; }
		Audit("player-order-releases-gunner", BBB_AuditStatic.Jobs() == 0);
		Audit("ordered-gunner-leaves-seat", isOut, string.Format("after %1 s", Math.Round((NowMs() - m_fStepStart) / 1000.0)));
		if (m_Order) { m_Group.RemoveWaypoint(m_Order); SCR_EntityHelper.DeleteEntityAndChildren(m_Order); }
		RestoreHooks();
		GetGame().GetCallqueue().CallLater(MoraleBegin, 4000);
	}

	// ---------------------------------------------------------------- morale

	void MoraleBegin()
	{
		RestoreHooks();
		// A frozen squad gains no ground: no contact broken, no invented distance
		array<AIAgent> agents = {};
		m_Group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			SCR_AIUtilityComponent u = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			if (u && u.m_CombatMoveState) u.m_CombatMoveState.CancelRequest();
		}
		SetGroupSpeed(0);
		m_Withdraw = new BBB_GroupMoraleState();
		m_Withdraw.m_Group = m_Group;
		float now = NowMs() / 1000.0;
		BBB_AuditMorale.BeginWithdrawal(m_Withdraw, m_Enemy.GetLeaderEntity().GetOrigin(), now);
		for (int step = 0; step < 4 && m_Withdraw.m_bWithdrawing; step++)
		{
			m_Withdraw.m_fPhaseSince = now - 15;
			BBB_AuditMorale.Advance(m_Withdraw, now);
		}
		Audit("frozen-squad-not-reported-broken", !m_Withdraw.m_bWithdrawing && !m_Withdraw.m_bContactBroken && m_Withdraw.m_fTotalWithdrawn < 3,
			string.Format("withdrawn=%1 reason=%2", m_Withdraw.m_fTotalWithdrawn, m_Withdraw.m_sEndReason));

		// A squad in contact: counted distance matches what it actually covered.
		// Combat move requests only run inside combat behaviours, so the fireteam
		// must see the (harmless, blind) enemy first.
		SetGroupSpeed(2); // > 1 restores normal max speed (< 0 leaves the override in place)
		m_Group.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.FIRE_AT_WILL); m_Group.GetGroupUtilityComponent().EvaluateCombatMode();
		foreach (AIAgent fighter : agents)
		{
			SCR_AIUtilityComponent fu = SCR_AIUtilityComponent.Cast(fighter.FindComponent(SCR_AIUtilityComponent));
			if (fu && fu.m_CombatComponent) fu.m_CombatComponent.SetPerceptionFactor(10);
		}
		m_fStepStart = NowMs();
		GetGame().GetCallqueue().CallLater(WaitContact, 500);
	}

	void WaitContact()
	{
		vector threat;
		bool contact = BBB_Smoke.GetThreatCentroid(m_Group, threat);
		if (!contact && NowMs() - m_fStepStart < 25000) { GetGame().GetCallqueue().CallLater(WaitContact, 500); return; }
		if (!contact)
		{
			array<AIAgent> agents = {};
			m_Group.GetAgents(agents);
			foreach (AIAgent agent : agents)
			{
				SCR_AIUtilityComponent u = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
				if (!u || !u.m_PerceptionComponent) continue;
				array<BaseTarget> detected = {};
				array<BaseTarget> enemies = {};
				u.m_PerceptionComponent.GetTargetsList(detected, ETargetCategory.DETECTED);
				u.m_PerceptionComponent.GetTargetsList(enemies, ETargetCategory.ENEMY);
				PrintFormat("[AUDIT] PERCEPTION detected=%1 enemies=%2 toEnemy=%3 m", detected.Count(), enemies.Count(), Math.Round(vector.Distance(u.m_OwnerEntity.GetOrigin(), m_Enemy.GetLeaderEntity().GetOrigin())));
			}
		}
		Audit("fireteam-perceives-enemy", contact, string.Format("after %1 s", Math.Round((NowMs() - m_fStepStart) / 1000.0)));
		if (!contact) { FinishAudit(); return; }
		GetGame().GetCallqueue().CallLater(StartRealWithdrawal, 3000);
	}

	void StartRealWithdrawal()
	{
		vector threat;
		BBB_Smoke.GetThreatCentroid(m_Group, threat);
		BBB_Morale.SquadCenter(m_Group, m_vCenterStart);
		m_vThreatStart = threat;
		m_Withdraw = new BBB_GroupMoraleState();
		m_Withdraw.m_Group = m_Group;
		BBB_AuditMorale.BeginWithdrawal(m_Withdraw, threat, NowMs() / 1000.0);
		Audit("withdrawal-started", m_Withdraw.m_bWithdrawing && m_Withdraw.m_TeamMove.Count() > 0, string.Format("threat %1 m away", Math.Round(vector.DistanceXZ(m_vCenterStart, threat))));
		GetGame().GetCallqueue().CallLater(MoraleBound, 14500);
	}

	vector m_vThreatStart;

	void MoraleBound()
	{
		BBB_AuditMorale.Advance(m_Withdraw, NowMs() / 1000.0);
		vector center;
		BBB_Morale.SquadCenter(m_Group, center);
		float gained = BBB_Morale.GainAway(m_vCenterStart, center, m_vThreatStart);
		// How far the half-squad gets in one bound varies run to run (1.6-5.7 m of squad centre seen);
		// this checks the accounting, the movement itself is printed for the record.
		PrintFormat("[AUDIT] WITHDRAWAL squad centre moved %1 m away from the threat in one bound", gained);
		Audit("withdrawal-counts-observed-ground", Math.AbsFloat(m_Withdraw.m_fTotalWithdrawn - gained) < 1,
			string.Format("counted=%1 observed=%2 broken=%3 reason=%4", m_Withdraw.m_fTotalWithdrawn, gained, m_Withdraw.m_bContactBroken, m_Withdraw.m_sEndReason));
		array<AIAgent> agents = {};
		m_Group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			SCR_AIUtilityComponent u = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			if (u && u.m_CombatMoveState) u.m_CombatMoveState.CancelRequest();
		}
		FinishAudit();
	}

	void FinishAudit()
	{
		RestoreHooks();
		BBB_FireManeuver.s_bOn = m_bSavedFireManeuver;
		if (m_Group) PrintFormat("[AUDIT] GRENADES at end [%1]", GrenadeBreakdown());
		BBB_StaticWeapons.Stop(); BBB_Morale.Stop(); BBB_Smoke.Stop(); BBB_Grenades.Stop();
		string verdict = "PASS";
		if (m_Failures > 0) verdict = "FAIL";
		PrintFormat("[ACR] VERDICT %1 test=bbb-feature-audit checks=%2 failed=%3", verdict, m_Checks, m_Failures);
		Print("[ACR] TEST COMPLETE");
	}
}

modded class SCR_BaseGameMode
{
	protected override void OnGameStart()
	{
		super.OnGameStart();
		BBB_FeatureAuditTest.Start();
	}
}
