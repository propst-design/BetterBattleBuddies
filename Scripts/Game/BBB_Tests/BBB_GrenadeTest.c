// Better Battle Buddies (BBB): Standalone hand grenade employment assertions.
// Ends with a live throw: the queued throw is called off when a friendly steps into the landing
// area while it waits (BBB #40), and the same throw with nobody near lands a real grenade.
class BBB_GrenadeTest : BBB_AimTest
{
	static ref BBB_GrenadeTest s_GrenadeTest;
	protected int m_iChecks;
	protected int m_iFailures;
	protected SCR_AIGroup m_SquadUS;
	protected AIWaypoint m_Order;
	protected ChimeraCharacter m_Walker;
	protected vector m_vWalkerHome;
	protected vector m_vSafeSpot;
	protected int m_iGrenadesBefore;
	protected int m_iVetoesBefore;
	protected float m_fCooldownWas;
	static const ResourceName WP_MOVE = "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et";
	static const int EXPECTED_CHECKS = 24;

	static void MaybeStartGrenade()
	{
		if (!Replication.IsServer() || !System.IsCLIParam("bbbGrenadeTest")) return;
		s_GrenadeTest = new BBB_GrenadeTest();
		GetGame().GetCallqueue().CallLater(s_GrenadeTest.BeginTest, 12000);
	}

	protected void Check(string label, bool ok)
	{
		m_iChecks++;
		string res = "FAIL";
		if (ok)
		{
			res = "PASS";
		}
		else
		{
			m_iFailures++;
		}
		PrintFormat("[BBB] GRENADETEST CHECK %1 %2", label, res);
	}

	protected void BeginTest()
	{
		BBB_Grenades.Read();

		// Check 1: Feature flags read and parameters
		Check("flag-read", BBB_Grenades.s_iRead > 0);
		Check("defaults-valid", BBB_Grenades.s_fMinThrowDistM > 0 && BBB_Grenades.s_fMaxThrowDistM > BBB_Grenades.s_fMinThrowDistM && BBB_Grenades.s_fFriendlySafetyDistM > 0);

		// Check 2: Disabled baseline
		BBB_Grenades.s_bOn = false;
		bool disabledThrow = BBB_Grenades.TryThrowGrenadeAt(null, "1000 0 1000", "test");
		Check("disabled-baseline", !disabledThrow);

		// Check 3: Line-sphere intersection mathematics (hit)
		vector from = Vector(1000, 50, 1000);
		vector to = Vector(1000, 50, 1030);
		vector sphereCenter = Vector(1000, 50, 1015);
		float radius = 2.5;
		bool hit = BBB_Grenades.LineIntersectsSphere(from, to, sphereCenter, radius);
		Check("line-intersects-sphere-hit", hit);

		// Check 4: Line-sphere intersection mathematics (miss)
		vector sphereMissCenter = Vector(1010, 50, 1015);
		bool miss = BBB_Grenades.LineIntersectsSphere(from, to, sphereMissCenter, radius);
		Check("line-intersects-sphere-miss", !miss);

		// Check 5: Blast clearance geometry: safe distance (20 m > 14 m)
		vector safeTarget = Vector(1000, 0, 1025);
		vector friendlyPos = Vector(1000, 0, 1000); // 25 m separation
		bool safeDistOk = vector.DistanceXZ(friendlyPos, safeTarget) >= BBB_Grenades.s_fFriendlySafetyDistM;
		Check("blast-clearance-safe-geometry", safeDistOk);

		// Check 6: Blast clearance geometry: danger zone (8 m < 14 m)
		vector dangerTarget = Vector(1000, 0, 1008);
		bool dangerDistVeto = vector.DistanceXZ(friendlyPos, dangerTarget) < BBB_Grenades.s_fFriendlySafetyDistM;
		Check("blast-clearance-danger-geometry", dangerDistVeto);

		// Check 7: Corridor clearance geometry: safe lateral separation (10 m > 2.5 m)
		vector lateralSafeFriendly = Vector(1010, 50, 1015);
		bool corridorSafe = !BBB_Grenades.LineIntersectsSphere(from, to, lateralSafeFriendly, 2.5);
		Check("corridor-clearance-safe-geometry", corridorSafe);

		// Check 8: Corridor clearance geometry: obstruction in path (0.5 m < 2.5 m)
		vector corridorObstruction = Vector(1000.5, 50, 1015);
		bool corridorVeto = BBB_Grenades.LineIntersectsSphere(from, to, corridorObstruction, 2.5);
		Check("corridor-clearance-veto-geometry", corridorVeto);

		// Check 9: Spawning squad
		vector spawnPos = BBB.Ground(Vector(1340, 0, 2770));
		vector facing = Vector(0, 0, 1);
		m_SquadUS = SCR_AIGroup.Cast(Spawn(TEAM_US, spawnPos, facing));
		if (!m_SquadUS)
		{
			Check("squad-spawn", false);
			FinishTest();
			return;
		}
		Check("squad-spawn", true);

		GetGame().GetCallqueue().CallLater(EvaluateSpawnedSquad, 4000);
	}

	protected void EvaluateSpawnedSquad()
	{
		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(m_SquadUS, agents);

		// Check 10: Squad members populated
		Check("squad-members-populated", agents.Count() >= 4);

		// Check 11: Inventory frag grenade detection (light fire team riflemen carry M67s)
		bool foundGrenadier = false;
		ChimeraCharacter firstChar = null;
		SCR_AIUtilityComponent firstUtil = null;

		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			IEntity controlled = agent.GetControlledEntity();
			if (!controlled) continue;

			ChimeraCharacter ch = ChimeraCharacter.Cast(controlled);
			if (!ch) continue;

			if (!firstChar)
			{
				firstChar = ch;
				firstUtil = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			}

			if (BBB_Grenades.HasFragGrenade(controlled))
			{
				foundGrenadier = true;
				break;
			}
		}
		Check("frag-grenade-inventory-check", foundGrenadier);

		// Check 12a: a squad on hold fire doesn't throw (a fit thrower is refused once the squad holds fire)
		ChimeraCharacter fitChar = null;
		SCR_AIUtilityComponent fitUtil = null;
		foreach (AIAgent fitAgent : agents)
		{
			if (!fitAgent) continue;
			ChimeraCharacter candidate = ChimeraCharacter.Cast(fitAgent.GetControlledEntity());
			SCR_AIUtilityComponent candidateUtil = SCR_AIUtilityComponent.Cast(fitAgent.FindComponent(SCR_AIUtilityComponent));
			if (candidate && candidateUtil && BBB_Grenades.IsFitThrower(candidate, candidateUtil)) { fitChar = candidate; fitUtil = candidateUtil; break; }
		}
		SCR_AIGroupUtilityComponent squadUtil = SCR_AIGroupUtilityComponent.Cast(m_SquadUS.FindComponent(SCR_AIGroupUtilityComponent));
		if (fitChar && squadUtil)
		{
			squadUtil.SetCombatMode(EAIGroupCombatMode.HOLD_FIRE);
			squadUtil.EvaluateCombatMode(); // applied on the group's next update otherwise
			bool heldBack = !BBB_Grenades.IsFitThrower(fitChar, fitUtil);
			squadUtil.SetCombatMode(EAIGroupCombatMode.FIRE_AT_WILL);
			squadUtil.EvaluateCombatMode(); // applied on the group's next update otherwise
			bool freedAgain = BBB_Grenades.IsFitThrower(fitChar, fitUtil);
			PrintFormat("[BBB] GRENADETEST hold fire: refused %1, fit again after fire at will %2", heldBack, freedAgain);
			Check("hold-fire-blocks", heldBack && freedAgain);
		}
		else
		{
			PrintFormat("[BBB] GRENADETEST hold fire: no fit thrower (%1) or group utility (%2)", fitChar != null, squadUtil != null);
			Check("hold-fire-blocks", false);
		}

		// Check 12: Player/GM explicit order blocks grenade throwing (a missing fixture is a failure,
		// not a pass). The order is lifted again before the live throw below.
		bool orderBlocks = false;
		if (firstUtil && firstChar)
		{
			EntitySpawnParams wpParams = new EntitySpawnParams();
			wpParams.Transform[3] = m_SquadUS.GetLeaderEntity().GetOrigin() + Vector(0, 0, 10);
			Resource res = Resource.Load(WP_MOVE);
			if (res && res.IsValid()) m_Order = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), wpParams));
			if (m_Order)
			{
				SCR_AIWaypoint scrWp = SCR_AIWaypoint.Cast(m_Order);
				if (scrWp) scrWp.SetPriorityLevel(SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
				m_SquadUS.AddWaypoint(m_Order);
				orderBlocks = BBB_OrderIntent.Blocks(firstUtil) || !BBB_Grenades.IsFitThrower(firstChar, firstUtil);
			}
		}
		Check("player-order-blocks", orderBlocks);

		// Check 13: players are judged by their side: an enemy player is a target, a friendly or
		// unreadable one is kept clear (both blast and corridor checks use NonHostileSide).
		Faction us = GetGame().GetFactionManager().GetFactionByKey("US");
		Faction ussr = GetGame().GetFactionManager().GetFactionByKey("USSR");
		bool sides = us && ussr && us.IsFactionEnemy(ussr);
		Check("player-enemy-is-target", sides && !BBB_Grenades.NonHostileSide(ussr, us, true));
		Check("player-friendly-kept-clear", sides && BBB_Grenades.NonHostileSide(us, us, true));
		Check("player-no-side-kept-clear", BBB_Grenades.NonHostileSide(null, us, true) && !BBB_Grenades.NonHostileSide(null, us, false));

		// Enable feature for live scenario assertions
		BBB_Grenades.s_bOn = true;

		// Check 14: Blast area friendly safety check live
		vector leaderPos = m_SquadUS.GetLeaderEntity().GetOrigin();
		vector closeBlastSpot = leaderPos + Vector(0, 0, 5); // 5 m from leader -> must veto
		bool blastVetoed = !BBB_Grenades.IsBlastAreaClearOfFriendlies(closeBlastSpot, m_SquadUS, 14.0);
		Check("blast-area-friendly-safety-veto", blastVetoed);

		// Check 15: Safe throw distance live
		m_vSafeSpot = leaderPos + Vector(0, 0, 22); // 22 m from squad -> clear of friendlies
		bool safeClear = BBB_Grenades.IsBlastAreaClearOfFriendlies(m_vSafeSpot, m_SquadUS, 14.0);
		Check("blast-area-safe-spot-clear", safeClear);

		// The player order above would block the live throw: lift it and let the behaviour settle.
		if (m_Order)
		{
			m_SquadUS.RemoveWaypoint(m_Order);
			SCR_EntityHelper.DeleteEntityAndChildren(m_Order);
			m_Order = null;
		}
		GetGame().GetCallqueue().CallLater(LiveThrow, 3000);
	}

	// The squad member holding a queued BBB frag throw (null: nobody has one).
	protected ChimeraCharacter QueuedThrower(array<AIAgent> agents)
	{
		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			SCR_AIUtilityComponent util = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			if (!util) continue;
			array<ref AIActionBase> actions = {};
			util.FindActionsOfInheritedType(BBB_AIThrowFrag, actions);
			foreach (AIActionBase action : actions)
			{
				if (action.GetRemoveAction() || action.GetActionState() == EAIActionState.FAILED || action.GetActionState() == EAIActionState.COMPLETED) continue;
				return ChimeraCharacter.Cast(agent.GetControlledEntity());
			}
		}
		return null;
	}

	protected int GroupGrenades()
	{
		int total = 0;
		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(m_SquadUS, agents);
		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			IEntity body = agent.GetControlledEntity();
			if (!body) continue;
			InventoryStorageManagerComponent inv = InventoryStorageManagerComponent.Cast(body.FindComponent(InventoryStorageManagerComponent));
			if (inv) total = total + inv.GetGrenadesCount();
		}
		return total;
	}

	// Checks 16-18: a dispatch queues BBB_AIThrowFrag on a squad member; a friendly who steps into
	// the landing area while it waits calls it off before any grenade leaves the inventory (#40).
	protected void LiveThrow()
	{
		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(m_SquadUS, agents);
		m_fCooldownWas = BBB_Grenades.s_fSquadCooldownS;
		BBB_Grenades.s_fSquadCooldownS = 0; // the test dispatches twice
		m_iGrenadesBefore = GroupGrenades();
		m_iVetoesBefore = BBB_Grenades.s_iSafetyVetoes;
		bool thrownOk = BBB_Grenades.TryThrowGrenadeAt(m_SquadUS, m_vSafeSpot, "test throw");
		Check("grenade-throw-dispatched", thrownOk);
		ChimeraCharacter thrower = QueuedThrower(agents);
		Check("grenade-throw-queued", thrower != null);
		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			ChimeraCharacter ch = ChimeraCharacter.Cast(agent.GetControlledEntity());
			if (ch && ch != thrower && BBB.IsAlive(ch)) { m_Walker = ch; break; }
		}
		if (m_Walker)
		{
			m_vWalkerHome = m_Walker.GetOrigin();
			m_Walker.SetOrigin(BBB.Ground(m_vSafeSpot + Vector(3, 0, 0)));
		}
		PrintFormat("[BBB] GRENADETEST live throw: dispatched %1, queued on %2, %3 steps into the blast area", thrownOk, thrower, m_Walker);
		GetGame().GetCallqueue().CallLater(LateFriendlyChecked, 6000);
	}

	protected void LateFriendlyChecked()
	{
		int after = GroupGrenades();
		int vetoes = BBB_Grenades.s_iSafetyVetoes - m_iVetoesBefore;
		PrintFormat("[BBB] GRENADETEST late friendly: grenades %1 -> %2, safety vetoes +%3", m_iGrenadesBefore, after, vetoes);
		Check("late-friendly-vetoes-queued-frag", m_Walker && after == m_iGrenadesBefore && vetoes > 0);
		if (m_Walker) m_Walker.SetOrigin(m_vWalkerHome);
		GetGame().GetCallqueue().CallLater(ClearThrow, 1000);
	}

	// Control: the same throw with nobody near lands a real grenade.
	protected void ClearThrow()
	{
		m_iGrenadesBefore = GroupGrenades();
		Check("grenade-rethrow-dispatched", BBB_Grenades.TryThrowGrenadeAt(m_SquadUS, m_vSafeSpot, "test throw clear"));
		GetGame().GetCallqueue().CallLater(FragLanded, 9000);
	}

	protected void FragLanded()
	{
		int after = GroupGrenades();
		PrintFormat("[BBB] GRENADETEST clear throw: grenades %1 -> %2", m_iGrenadesBefore, after);
		Check("frag-actually-thrown", after < m_iGrenadesBefore);
		BBB_Grenades.s_fSquadCooldownS = m_fCooldownWas;
		FinishTest();
	}

	protected void FinishTest()
	{
		string verdict = "FAIL";
		if (m_iFailures == 0 && m_iChecks >= EXPECTED_CHECKS)
		{
			verdict = "PASS";
		}
		PrintFormat("[BBB] GRENADETEST FINISHED checks=%1 failures=%2", m_iChecks, m_iFailures);
		PrintFormat("[ACR] VERDICT %1 test=bbbgrenades passed=%2 failed=%3 expected=%4", verdict, m_iChecks - m_iFailures, m_iFailures, EXPECTED_CHECKS);
		Print("[ACR] TEST COMPLETE");
	}
}

modded class SCR_BaseGameMode
{
	protected override void OnGameStart()
	{
		super.OnGameStart();
		BBB_GrenadeTest.MaybeStartGrenade();
	}
}
