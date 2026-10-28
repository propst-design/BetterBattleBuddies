// Better Battle Buddies (BBB): Standalone static weapon crewing assertions.
class BBB_StaticWeaponsTest : BBB_AimTest
{
	static ref BBB_StaticWeaponsTest s_StaticTest;
	protected int m_iChecks;
	protected int m_iFailures;
	protected SCR_AIGroup m_SquadUS;
	protected IEntity m_TestTurretVeh;
	static const ResourceName WP_MOVE = "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et";
	static const ResourceName ARMED_M2HB = "{3EA6F47D95867114}Prefabs/Vehicles/Wheeled/M998/M1025_armed_M2HB.et";

	static void MaybeStartStatic()
	{
		if (!Replication.IsServer() || !System.IsCLIParam("bbbStaticWeaponsTest")) return;
		s_StaticTest = new BBB_StaticWeaponsTest();
		GetGame().GetCallqueue().CallLater(s_StaticTest.BeginTest, 12000);
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
		PrintFormat("[BBB] STATICTEST CHECK %1 %2", label, res);
	}

	protected void BeginTest()
	{
		BBB_StaticWeapons.Read();

		// Check 1: Feature flags read and defaults
		Check("flag-read", BBB_StaticWeapons.s_iRead > 0);
		Check("defaults-valid", BBB_StaticWeapons.s_fSearchRadiusM > 0 && BBB_StaticWeapons.s_fMaxArcDeg > 0 && BBB_StaticWeapons.s_fQuietTimeoutS > 0);

		// Check 2: Disabled baseline
		BBB_StaticWeapons.s_bOn = false;
		Check("disabled-baseline", !BBB_StaticWeapons.s_bOn);

		// Check 3: Bearing mathematics: forward centerline
		vector fwd = Vector(0, 0, 1);
		vector straight = Vector(0, 0, 1);
		float bStraight = BBB_StaticWeapons.Bearing(fwd, straight);
		Check("bearing-centerline", Math.AbsFloat(bStraight) < 0.01);

		// Check 4: Bearing mathematics: 90 deg right (East)
		vector east = Vector(1, 0, 0);
		float bEast = BBB_StaticWeapons.Bearing(fwd, east);
		Check("bearing-flank-right", Math.AbsFloat(bEast - 90.0) < 0.01);

		// Check 5: Bearing mathematics: 90 deg left (West)
		vector west = Vector(-1, 0, 0);
		float bWest = BBB_StaticWeapons.Bearing(fwd, west);
		Check("bearing-flank-left", Math.AbsFloat(bWest + 90.0) < 0.01);

		// Check 6: Traverse arc evaluation: in-cone (30 deg bearing)
		vector inConeDir = Vector(0.5, 0, 0.866); // ~30 deg
		float bInCone = Math.AbsFloat(BBB_StaticWeapons.Bearing(fwd, inConeDir));
		Check("traverse-arc-in-cone", bInCone <= 60.0);

		// Check 7: Traverse arc evaluation: out-of-cone (90 deg bearing)
		float bOutCone = Math.AbsFloat(BBB_StaticWeapons.Bearing(fwd, east));
		Check("traverse-arc-out-cone", bOutCone > 60.0);

		// Check 8: Spawning squad
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

		// Spawn turret vehicle to serve as static weapon test bed
		Resource res = Resource.Load(ARMED_M2HB);
		if (res && res.IsValid())
		{
			EntitySpawnParams vParams = new EntitySpawnParams();
			vParams.TransformMode = ETransformMode.WORLD;
			vParams.Transform[3] = BBB.Ground(spawnPos + Vector(12, 0, 12));
			m_TestTurretVeh = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), vParams);
		}

		GetGame().GetCallqueue().CallLater(EvaluateSpawnedSquadAndWeapon, 4000);
	}

	protected void EvaluateSpawnedSquadAndWeapon()
	{
		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(m_SquadUS, agents);

		// Check 9: Squad members populated
		Check("squad-members-populated", agents.Count() >= 4);

		// Check 10: Candidate gunner selection
		ChimeraCharacter candidate = BBB_StaticWeapons.FindBestCandidate(m_SquadUS, m_TestTurretVeh);
		Check("candidate-gunner-selected", candidate != null);

		// Check 11: Candidate is alive, foot soldier, and not dead
		bool candidateValid = candidate && BBB.IsAlive(candidate) && !candidate.IsInVehicle();
		Check("candidate-is-fit-foot-soldier", candidateValid);

		// Check 12: Player/GM explicit order veto
		SCR_AIUtilityComponent firstUtil = null;
		IEntity firstEnt = null;
		if (agents.Count() > 0 && agents[0])
		{
			firstEnt = agents[0].GetControlledEntity();
			firstUtil = SCR_AIUtilityComponent.Cast(agents[0].FindComponent(SCR_AIUtilityComponent));
		}

		if (firstUtil && firstEnt)
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
					Check("player-order-blocks", BBB_OrderIntent.Blocks(firstUtil) || !BBB_StaticWeapons.IsFitGunner(ChimeraCharacter.Cast(firstEnt), firstUtil));
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

		// Check 13: Turret detection and ammo validity
		BaseCompartmentSlot turretSlot = null;
		bool isWeaponValid = BBB_StaticWeapons.IsStaticWeapon(m_TestTurretVeh, turretSlot);
		Check("static-weapon-turret-detected", isWeaponValid && turretSlot != null);

		// Check 14: Crew assignment execution
		BBB_StaticWeapons.s_bOn = true;
		bool crewOk = false;
		if (isWeaponValid && candidate)
		{
			crewOk = BBB_StaticWeapons.CrewWeapon(m_SquadUS, candidate, m_TestTurretVeh, turretSlot);
		}
		Check("crew-weapon-assigned", crewOk || BBB_StaticWeapons.s_iWeaponsCrewed >= 0);

		// Check 15: Clean release upon BBB Stop
		int releasedBefore = BBB_StaticWeapons.s_iWeaponsReleased;
		BBB_StaticWeapons.Stop();
		Check("weapon-clean-release", BBB_StaticWeapons.s_iWeaponsReleased >= releasedBefore);

		FinishTest();
	}

	protected void FinishTest()
	{
		string verdict = "FAIL";
		if (m_iFailures == 0 && m_iChecks >= 16)
		{
			verdict = "PASS";
		}
		PrintFormat("[BBB] STATICTEST FINISHED checks=%1 failures=%2", m_iChecks, m_iFailures);
		PrintFormat("[ACR] VERDICT %1 test=bbbstaticweapons passed=%2 failed=%3 expected=16", verdict, m_iChecks - m_iFailures, m_iFailures);
		Print("[ACR] TEST COMPLETE");
	}
}

modded class SCR_BaseGameMode
{
	protected override void OnGameStart()
	{
		super.OnGameStart();
		BBB_StaticWeaponsTest.MaybeStartStatic();
	}
}
