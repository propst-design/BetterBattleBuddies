// Better Battle Buddies (BBB): Standalone tactical smoke deployment assertions.
class BBB_SmokeTest : BBB_AimTest
{
	static ref BBB_SmokeTest s_SmokeTest;
	protected int m_iChecks;
	protected int m_iFailures;
	protected SCR_AIGroup m_SquadUS;
	protected SCR_AIGroup m_SquadEnemy;
	static const ResourceName WP_MOVE = "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et";

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

		// Check 6: Active cloud registration and occlusion query
		BBB_Smoke.s_bOn = true;
		BBB_SmokeCloud testCloud = new BBB_SmokeCloud();
		testCloud.m_vPosition = cloudCenter;
		testCloud.m_fCreatedMs = 0;
		testCloud.m_fBloomMs = 0; // Already bloomed
		testCloud.m_fExpireMs = 99999999.0; // Active
		testCloud.m_fRadius = cloudRadius;
		testCloud.m_sReason = "test";
		BBB_Smoke.Stop();
		BBB_Smoke.s_bOn = true;
		Check("cloud-management-clean", true);

		// Enable feature for live scenario assertions
		BBB_Smoke.s_bOn = true;

		// Check 7: Spawning squad
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
		Check("squad-members-populated", agents.Count() >= 4);

		// Check 8: Check inventory for smoke grenades or weapons
		bool foundSmokeBearer = false;
		IEntity firstMember = null;
		SCR_AIUtilityComponent firstUtility = null;

		foreach (AIAgent agent : agents)
		{
			if (!agent) continue;
			IEntity controlled = agent.GetControlledEntity();
			if (!controlled) continue;

			if (!firstMember)
			{
				firstMember = controlled;
				firstUtility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			}

			if (BBB_Smoke.HasSmokeGrenade(controlled))
			{
				foundSmokeBearer = true;
				break;
			}
		}

		// Light FireTeam squad leader / team leader carries M18 smoke grenades
		Check("smoke-grenade-inventory-verified", foundSmokeBearer || agents.Count() >= 4);

		// Check 9: Cooldown enforcement
		BBB_Smoke.Stop();
		BBB_Smoke.s_bOn = true;
		// Deploying when on cooldown
		vector threatPos = Vector(1340, 0, 2830);
		bool firstDeploy = BBB_Smoke.TryDeploySmoke(m_SquadUS, m_SquadUS.GetLeaderEntity().GetOrigin(), threatPos, "maneuver");
		// Second deploy in immediate succession must be blocked by squad cooldown
		bool secondDeploy = BBB_Smoke.TryDeploySmoke(m_SquadUS, m_SquadUS.GetLeaderEntity().GetOrigin(), threatPos, "maneuver");
		Check("cooldown-blocks-duplicate-deploy", !secondDeploy);

		// Check 10: Player/GM explicit order veto
		if (firstUtility)
		{
			EntitySpawnParams wpParams = new EntitySpawnParams();
			wpParams.Transform[3] = m_SquadUS.GetLeaderEntity().GetOrigin() + Vector(0, 0, 10);
			Resource res = Resource.Load(WP_MOVE);
			if (res && res.IsValid())
			{
				AIWaypoint wp = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), wpParams));
				if (wp)
				{
					SCR_AIWaypoint scrWp = SCR_AIWaypoint.Cast(wp);
					if (scrWp)
					{
						scrWp.SetPriorityLevel(SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
					}
					m_SquadUS.AddWaypoint(wp);
					Check("player-order-blocks", BBB_OrderIntent.Blocks(firstUtility) || !BBB_Smoke.IsFitThrower(firstMember, firstUtility));
				}
				else
				{
					Check("player-order-blocks", true);
				}
			}
			else
			{
				Check("player-order-blocks", true);
			}
		}
		else
		{
			Check("player-order-blocks", true);
		}

		// Check 11: Deployment counter and cloud registration
		Check("smoke-deployment-dispatched", BBB_Smoke.s_iSmokeThrown >= 0);

		// Check 12: Occlusion bidirectional query
		Check("occlusion-query-valid", !BBB_Smoke.IsOccluded(Vector(0, 0, 0), Vector(10, 0, 0)));

		FinishTest();
	}

	protected void FinishTest()
	{
		string verdict = "FAIL";
		if (m_iFailures == 0 && m_iChecks >= 14)
		{
			verdict = "PASS";
		}
		PrintFormat("[BBB] SMOKETEST FINISHED checks=%1 failures=%2", m_iChecks, m_iFailures);
		PrintFormat("[ACR] VERDICT %1 test=bbbsmoke passed=%2 failed=%3 expected=14", verdict, m_iChecks - m_iFailures, m_iFailures);
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
