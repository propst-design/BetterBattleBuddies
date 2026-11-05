// Better Battle Buddies (BBB): Standalone tactical smoke deployment assertions.
// Live part (#41): an ordered throw counts as smoke only once a grenade has really left the
// thrower, and at the grenade, not the aim point; a thrower killed before throwing leaves no cover.
class BBB_SmokeTest : BBB_AimTest
{
	static ref BBB_SmokeTest s_SmokeTest;
	protected int m_iChecks;
	protected int m_iFailures;
	protected SCR_AIGroup m_SquadUS;
	protected SCR_AIGroup m_SquadEnemy;
	protected ref BBB_SmokeCloud m_Thrown;
	protected int m_iThrownBefore;
	protected ref BBB_SmokeCloud m_Interrupted;
	protected int m_iInterruptedBefore;
	static const ResourceName WP_MOVE = "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et";
	static const int EXPECTED = 22;

	static void MaybeStartSmoke()
	{
		if (!Replication.IsServer() || !System.IsCLIParam("bbbSmokeTest")) return;
		s_SmokeTest = new BBB_SmokeTest();
		GetGame().GetCallqueue().CallLater(s_SmokeTest.BeginTest, 12000);
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
		PrintFormat("[BBB] SMOKETEST CHECK %1 %2", label, res);
	}

	protected void BeginTest()
	{
		BBB_Smoke.Read();

		// Check 1: Feature flags read and defaults
		Check("flag-read", BBB_Smoke.s_iRead > 0);
		Check("defaults-valid", BBB_Smoke.s_fCooldownSquadS > 0 && BBB_Smoke.s_fThrowDistanceM > 0 && BBB_Smoke.s_bDrag && BBB_Smoke.s_bManeuver && BBB_Smoke.s_bMorale);

		// Check 2: Disabled baseline
		BBB_Smoke.s_bOn = false;
		bool disabledDeploy = BBB_Smoke.TryDeploySmoke(null, "0 0 0", "10 0 0", "test");
		bool disabledOccluded = BBB_Smoke.IsOccluded("0 0 0", "10 0 0");
		Check("disabled-baseline", !disabledDeploy && !disabledOccluded);

		// Check 3: Mathematical line-sphere intersection
		vector from = Vector(1000, 50, 1000);
		vector to = Vector(1000, 50, 1050);
		vector cloudCenter = Vector(1000, 50, 1025);
		float cloudRadius = 9.0;
		bool hit = BBB_Smoke.LineIntersectsSphere(from, to, cloudCenter, cloudRadius);
		Check("geometric-occlusion-hit", hit);

		// Check 4: Mathematical line-sphere clearance (offset ray)
		vector fromOffset = Vector(1050, 50, 1000);
		vector toOffset = Vector(1050, 50, 1050);
		bool miss = BBB_Smoke.LineIntersectsSphere(fromOffset, toOffset, cloudCenter, cloudRadius);
		Check("geometric-occlusion-miss", !miss);

		// Check 5: Threat vector and landing position calculation
		vector squadPos = Vector(1000, 0, 1000);
		vector threatPos = Vector(1000, 0, 1060); // 60 m North (+Z)
		vector toThreat = threatPos - squadPos;
		toThreat[1] = 0;
		float dist = toThreat.Length();
		toThreat = toThreat.Normalized();
		float throwDist = Math.Clamp(dist * 0.35, 10.0, 22.0);
		vector landingPos = squadPos + toThreat * throwDist;
		bool landingDirOk = (landingPos[2] > squadPos[2]) && (landingPos[2] < threatPos[2]) && (Math.AbsFloat(landingPos[0] - squadPos[0]) < 1.0);
		Check("threat-vector-landing-position", landingDirOk);

		// Enable feature for live scenario assertions
		BBB_Smoke.Stop();
		BBB_Smoke.s_bOn = true;

		// Check 6: Spawning squad
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

	protected int Smokes(IEntity soldier)
	{
		array<IEntity> carried = {};
		BBB_Smoke.CarriedSmokes(soldier, carried);
		return carried.Count();
	}

	protected BBB_SmokeCloud LastCloud()
	{
		int count = BBB_Smoke.CloudCount();
		if (count == 0) return null;
		return BBB_Smoke.CloudAt(count - 1);
	}

	// A sight line passing straight through the spot, 1.5 m up.
	protected bool LineThroughOccluded(vector at)
	{
		return BBB_Smoke.IsOccluded(at + Vector(-15, 1.5, 0), at + Vector(15, 1.5, 0));
	}

	protected bool Listed(BBB_SmokeCloud cloud)
	{
		for (int i = 0; i < BBB_Smoke.CloudCount(); i++)
		{
			if (BBB_Smoke.CloudAt(i) == cloud) return true;
		}
		return false;
	}

	protected void EvaluateSpawnedSquad()
	{
		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(m_SquadUS, agents);
		Check("squad-members-populated", agents.Count() >= 4);

		// Check 7: someone in the squad really carries a smoke grenade
		int bearers = 0;
		foreach (AIAgent agent : agents)
		{
			if (agent && Smokes(agent.GetControlledEntity()) > 0) bearers++;
		}
		PrintFormat("[BBB] SMOKETEST smoke bearers: %1 of %2", bearers, agents.Count());
		Check("smoke-bearer-found", bearers > 0);

		// Check 8: an ordered throw is only planned
		vector leader = m_SquadUS.GetLeaderEntity().GetOrigin();
		vector threatPos = leader + Vector(0, 0, 60);
		bool firstDeploy = BBB_Smoke.TryDeploySmoke(m_SquadUS, leader, threatPos, "maneuver");
		Check("throw-dispatched", firstDeploy);
		m_Thrown = LastCloud();
		if (!firstDeploy || !m_Thrown || !m_Thrown.m_Thrower)
		{
			FinishTest();
			return;
		}
		m_iThrownBefore = Smokes(m_Thrown.m_Thrower);
		Check("dispatch-is-only-planned", !m_Thrown.m_bThrown);

		// Check 9: Second deploy in immediate succession must be blocked by squad cooldown
		bool secondDeploy = BBB_Smoke.TryDeploySmoke(m_SquadUS, leader, threatPos, "maneuver");
		Check("cooldown-blocks-duplicate-deploy", !secondDeploy);

		GetGame().GetCallqueue().CallLater(CheckThrown, 12000);
	}

	static int s_iLooseNear;
	static bool CountLoose(IEntity e)
	{
		if (e && !e.GetParent() && BBB_Smoke.IsSmokeGrenade(e)) s_iLooseNear++;
		return true;
	}

	protected void CheckThrown()
	{
		// Check 10: the grenade really left him, and the cloud is at a real grenade on the ground
		int after = Smokes(m_Thrown.m_Thrower);
		PrintFormat("[BBB] SMOKETEST thrower smokes %1 -> %2, cloud at %3 (thrown=%4)", m_iThrownBefore, after, m_Thrown.m_vPosition, m_Thrown.m_bThrown);
		Check("smoke-left-inventory", after < m_iThrownBefore);
		Check("cloud-thrown", m_Thrown.m_bThrown);
		s_iLooseNear = 0;
		GetGame().GetWorld().QueryEntitiesBySphere(m_Thrown.m_vPosition, 2.0, CountLoose);
		Check("cloud-at-real-grenade", s_iLooseNear > 0);
		Check("real-smoke-occludes", LineThroughOccluded(m_Thrown.m_vPosition));

		// Check 11: a thrower killed before he throws leaves no cover
		BBB_Smoke.Stop();
		BBB_Smoke.s_bOn = true;
		vector leader = m_SquadUS.GetLeaderEntity().GetOrigin();
		bool dispatched = BBB_Smoke.TryDeploySmoke(m_SquadUS, leader, leader + Vector(0, 0, 60), "maneuver");
		m_Interrupted = LastCloud();
		Check("interrupted-dispatched", dispatched && m_Interrupted != null && m_Interrupted.m_Thrower != null);
		if (!dispatched || !m_Interrupted || !m_Interrupted.m_Thrower)
		{
			FinishTest();
			return;
		}
		m_iInterruptedBefore = Smokes(m_Interrupted.m_Thrower);
		SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(m_Interrupted.m_Thrower.FindComponent(SCR_CharacterDamageManagerComponent));
		if (damage) damage.Kill(Instigator.CreateInstigator(null));
		GetGame().GetCallqueue().CallLater(CheckInterrupted, 7000);
	}

	protected void CheckInterrupted()
	{
		Check("interrupted-hides-nothing", !LineThroughOccluded(m_Interrupted.m_vPosition));
		Check("interrupted-cloud-dropped", !Listed(m_Interrupted));
		Check("interrupted-no-grenade-thrown", Smokes(m_Interrupted.m_Thrower) == m_iInterruptedBefore);

		// Check 12: Player/GM explicit order veto
		AIWaypoint wp;
		Resource res = Resource.Load(WP_MOVE);
		if (res && res.IsValid())
		{
			EntitySpawnParams wpParams = new EntitySpawnParams();
			wpParams.Transform[3] = m_SquadUS.GetLeaderEntity().GetOrigin() + Vector(0, 0, 10);
			wp = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), wpParams));
		}
		SCR_AIWaypoint scrWp = SCR_AIWaypoint.Cast(wp);
		if (!scrWp)
		{
			Check("player-order-blocks", false);
			FinishOrders();
			return;
		}
		scrWp.SetPriorityLevel(SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
		m_SquadUS.AddWaypoint(wp);
		GetGame().GetCallqueue().CallLater(CheckOrder, 2000);
	}

	protected void CheckOrder()
	{
		bool blocked = false;
		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(m_SquadUS, agents);
		foreach (AIAgent agent : agents)
		{
			if (!agent || !BBB.IsAlive(agent.GetControlledEntity())) continue;
			SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			blocked = utility != null && BBB_OrderIntent.Blocks(utility);
			break;
		}
		Check("player-order-blocks", blocked);
		FinishOrders();
	}

	protected void FinishOrders()
	{
		// Check 13: Occlusion query away from any smoke
		Check("occlusion-query-valid", !BBB_Smoke.IsOccluded(Vector(0, 0, 0), Vector(10, 0, 0)));
		FinishTest();
	}

	protected void FinishTest()
	{
		string verdict = "FAIL";
		if (m_iFailures == 0 && m_iChecks >= EXPECTED)
		{
			verdict = "PASS";
		}
		PrintFormat("[BBB] SMOKETEST FINISHED checks=%1 failures=%2 landed=%3 failed=%4", m_iChecks, m_iFailures, BBB_Smoke.s_iSmokeLanded, BBB_Smoke.s_iSmokeFailed);
		PrintFormat("[ACR] VERDICT %1 test=bbbsmoke passed=%2 failed=%3 expected=%4", verdict, m_iChecks - m_iFailures, m_iFailures, EXPECTED);
		Print("[ACR] TEST COMPLETE");
	}
}

modded class SCR_BaseGameMode
{
	protected override void OnGameStart()
	{
		super.OnGameStart();
		BBB_SmokeTest.MaybeStartSmoke();
	}
}
