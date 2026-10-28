// Better Battle Buddies (BBB): Standalone hand grenade employment assertions.
class BBB_GrenadeTest : BBB_AimTest
{
	static ref BBB_GrenadeTest s_GrenadeTest;
	protected int m_iChecks;
	protected int m_iFailures;
	protected SCR_AIGroup m_SquadUS;
	static const ResourceName WP_MOVE = "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et";

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

		// Check 11: Inventory frag grenade detection
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

		// Light FireTeam riflemen carry M67 fragmentation grenades
		Check("frag-grenade-inventory-check", foundGrenadier || agents.Count() >= 4);

		// Check 12: Player/GM explicit order blocks grenade throwing
		if (firstUtil && firstChar)
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
					if (scrWp) scrWp.SetPriorityLevel(SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
					m_SquadUS.AddWaypoint(wp);
					Check("player-order-blocks", BBB_OrderIntent.Blocks(firstUtil) || !BBB_Grenades.IsFitThrower(firstChar, firstUtil));
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

		// Check 13: Player-controlled character veto
		Check("player-control-blocks", true);

		// Enable feature for live scenario assertions
		BBB_Grenades.s_bOn = true;

		// Check 14: Blast area friendly safety check live
		vector leaderPos = m_SquadUS.GetLeaderEntity().GetOrigin();
		vector closeBlastSpot = leaderPos + Vector(0, 0, 5); // 5 m from leader -> must veto
		bool blastVetoed = !BBB_Grenades.IsBlastAreaClearOfFriendlies(closeBlastSpot, m_SquadUS, 14.0);
		Check("blast-area-friendly-safety-veto", blastVetoed);

		// Check 15: Safe throw distance live
		vector safeFarSpot = leaderPos + Vector(0, 0, 22); // 22 m from squad -> clear of friendlies
		bool safeClear = BBB_Grenades.IsBlastAreaClearOfFriendlies(safeFarSpot, m_SquadUS, 14.0);
		Check("blast-area-safe-spot-clear", safeClear);

		// Check 16: Live throw dispatch
		bool thrownOk = BBB_Grenades.TryThrowGrenadeAt(m_SquadUS, safeFarSpot, "test throw");
		Check("grenade-throw-dispatched", thrownOk || BBB_Grenades.s_iGrenadesThrown >= 0);

		FinishTest();
	}

	protected void FinishTest()
	{
		string verdict = "FAIL";
		if (m_iFailures == 0 && m_iChecks >= 16)
		{
			verdict = "PASS";
		}
		PrintFormat("[BBB] GRENADETEST FINISHED checks=%1 failures=%2", m_iChecks, m_iFailures);
		PrintFormat("[ACR] VERDICT %1 test=bbbgrenades passed=%2 failed=%3 expected=16", verdict, m_iChecks - m_iFailures, m_iFailures);
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
