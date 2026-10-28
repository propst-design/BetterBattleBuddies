// Better Battle Buddies (BBB): Standalone morale, retrograde break-contact
// withdrawal, and surrender assertions.
class BBB_MoraleTest : BBB_AimTest
{
	static ref BBB_MoraleTest s_MoraleTest;
	protected int m_iChecks;
	protected int m_iFailures;
	protected SCR_AIGroup m_SquadUSSR;
	protected SCR_AIGroup m_SquadUS;
	static const ResourceName WP_MOVE = "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et";
	static const int EXPECTED = 23;

	static void MaybeStartMorale()
	{
		if (!Replication.IsServer() || !System.IsCLIParam("bbbMoraleTest")) return;
		s_MoraleTest = new BBB_MoraleTest();
		GetGame().GetCallqueue().CallLater(s_MoraleTest.BeginTest, 12000);
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
		PrintFormat("[BBB] MORALETEST CHECK %1 %2", label, res);
	}

	protected void BeginTest()
	{
		BBB_Morale.Read();

		// Check 1: Feature flags and defaults
		Check("flag-read", BBB_Morale.s_iRead > 0);
		Check("defaults-valid", BBB_Morale.s_fCasualtyThreshold > 0 && BBB_Morale.s_fSuppressionThreshold > 0);

		// Check 2: Retrograde destination calculation away from threat
		vector squadPos = Vector(1000, 50, 1000);
		vector threatPos = Vector(1000, 50, 1050); // Threat is 50 m to the north (+Z)
		vector retreatPos = BBB_Morale.CalculateRetrogradeDestination(squadPos, threatPos, 30.0);
		// Retreat should be south (-Z)
		bool retreatDirOk = (retreatPos[2] < squadPos[2]) && (Math.AbsFloat(retreatPos[0] - squadPos[0]) < 1.0);
		Check("retrograde-direction-away", retreatDirOk);

		// Check 3: Disabled baseline
		BBB_Morale.s_bOn = false;
		Check("disabled-baseline", !BBB_Morale.s_bOn && !BBB_Morale.IsWithdrawing(null));

		// Enable morale for live scenario assertions
		BBB_Morale.s_bOn = true;

		// Check 4: Spawning squad and evaluating baseline morale
		vector spawnPos = BBB.Ground(Vector(1340, 0, 2770));
		vector facing = Vector(0, 0, 1);
		m_SquadUSSR = SCR_AIGroup.Cast(Spawn(TEAM_USSR, spawnPos, facing));
		if (!m_SquadUSSR)
		{
			Check("squad-spawn", false);
			FinishTest();
			return;
		}
		Check("squad-spawn", true);

		GetGame().GetCallqueue().CallLater(EvaluateInitialMorale, 4000);
	}

	protected void EvaluateInitialMorale()
	{
		array<AIAgent> agents = {};
		BBB.s_Hooks.GetMembers(m_SquadUSSR, agents);
		Check("squad-members-populated", agents.Count() >= 4);

		int aliveCount;
		int outOfAmmoCount;
		BBB_GroupMoraleState state = new BBB_GroupMoraleState();
		state.m_Group = m_SquadUSSR;
		state.m_iInitialCount = agents.Count();
		float initialMorale = BBB_Morale.CalculateGroupMorale(state, agents, aliveCount, outOfAmmoCount);

		// A fresh healthy squad must have 0 or very low morale score
		Check("fresh-squad-calm-morale", initialMorale < 0.20);

		// Check 5: Synthetic casualties + leader loss calculation
		state.m_iInitialCount = 4;
		array<AIAgent> damagedAgents = {};
		if (agents.Count() >= 3)
		{
			damagedAgents.Insert(agents[1]);
			damagedAgents.Insert(agents[2]);
		}
		float brokenMorale = BBB_Morale.CalculateGroupMorale(state, damagedAgents, aliveCount, outOfAmmoCount);
		// 50% casualty (+0.40) + leader loss (+0.25) >= 0.65
		Check("casualty-leader-loss-triggers-break", brokenMorale >= 0.65);

		// Check 6: Explicit player/GM order veto
		AIWaypoint wp = AIWaypoint.Cast(Spawn(WP_MOVE, m_SquadUSSR.GetOrigin(), Vector(0, 0, 1)));
		if (wp)
		{
			SCR_AIWaypoint.Cast(wp).SetPriorityLevel(SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
			m_SquadUSSR.AddWaypoint(wp);
		}
		SCR_AIUtilityComponent firstUtility = SCR_AIUtilityComponent.Cast(agents[0].FindComponent(SCR_AIUtilityComponent));
		Check("player-order-blocks", BBB_OrderIntent.Blocks(firstUtility));

		// Surrender, through the real per-tick check. The player order would block it: clear it.
		if (wp)
		{
			m_SquadUSSR.RemoveWaypoint(wp);
			SCR_EntityHelper.DeleteEntityAndChildren(wp);
		}
		BeginSurrender(agents);
	}

	// ---------------------------------------------------------------- surrender
	// Fixture: a USSR team's other men are removed (the squad is wiped, he is cut off) and his
	// spare magazines taken; a US team 30 m north on hold fire is the enemy he sees. Threat is
	// not judged here (the threshold is lowered and the roll forced): this establishes what
	// surrender and each fate do once they fire, not how often a firefight produces them.
	// Three subjects in turn: A genuine (spared, then removed unwatched), B executed, C a ruse.
	protected ChimeraCharacter m_Subject;
	protected ref array<IEntity> m_aSubjectGuns = {};
	protected SCR_AIGroup m_ControlTeam;
	protected SCR_AIGroup m_SubjectTeam;
	protected ref array<BaseWeaponComponent> m_aUSWeapons = {};
	protected ref array<int> m_aUSLastAmmo = {};
	protected int m_iUSShots;
	protected bool m_bSubjectTargeted;
	protected int m_iSurrendersBefore;
	protected int m_iRemovedBefore;
	protected int m_iFrags;
	protected int m_iWait;
	protected vector m_vOrigin;

	protected void BeginSurrender(array<AIAgent> agents)
	{
		// Withdrawals off (a wiped squad would pull him out of range); surrender runs on its own switch.
		BBB_Morale.s_bOn = false;
		BBB_Morale.s_fSurrenderThreat = 0;
		BBB_Morale.s_fSurrenderChance = 1;
		BBB_Morale.s_fPrisonerMinS = 600;
		BBB_Morale.s_fFateDelayMinS = 0;
		BBB_Morale.s_fFateDelayMaxS = 0;
		m_iSurrendersBefore = BBB_Morale.s_iSurrenders;
		m_iRemovedBefore = BBB_Morale.s_iPrisonersRemoved;
		BBB_Morale.s_iForceFate = BBB_Morale.FATE_GENUINE;
		PrepareSubject(agents);
		m_vOrigin = m_Subject.GetOrigin();

		m_SquadUS = SCR_AIGroup.Cast(Spawn(TEAM_US, BBB.Ground(m_vOrigin + Vector(0, 0, 30)), Vector(0, 0, -1)));
		if (!m_SquadUS)
		{
			Check("surrender-fixture", false);
			FinishTest();
			return;
		}
		SetUSFire(false);
		m_iWait = 0;
		GetGame().GetCallqueue().CallLater(WaitSurrender, 500);
	}

	protected void SetUSFire(bool fire)
	{
		EAIGroupCombatMode mode = EAIGroupCombatMode.HOLD_FIRE;
		if (fire) mode = EAIGroupCombatMode.FIRE_AT_WILL;
		m_SquadUS.GetGroupUtilityComponent().SetCombatMode(mode);
	}

	// Keep member 1, remove the rest, take his spare magazines, note his guns and frags.
	protected void PrepareSubject(array<AIAgent> agents)
	{
		m_Subject = ChimeraCharacter.Cast(agents[1].GetControlledEntity());
		foreach (int i, AIAgent other : agents)
		{
			if (i != 1 && other) Discard(other.GetControlledEntity());
		}
		StripMagazines(m_Subject);
		m_aSubjectGuns.Clear();
		BaseWeaponManagerComponent wm = BaseWeaponManagerComponent.Cast(m_Subject.FindComponent(BaseWeaponManagerComponent));
		array<IEntity> weapons = {};
		if (wm) wm.GetWeaponsList(weapons);
		foreach (IEntity weapon : weapons)
		{
			BaseWeaponComponent component = BaseWeaponComponent.Cast(weapon.FindComponent(BaseWeaponComponent));
			if (component && BBB_Morale.IsGun(component.GetWeaponType())) m_aSubjectGuns.Insert(weapon);
		}
		m_iFrags = Frags(m_Subject);
	}

	// Killed, not deleted: a deleted soldier with a radio call pending makes vanilla comms log
	// an error, a body does not.
	protected void Discard(IEntity soldier)
	{
		SCR_DamageManagerComponent damage;
		if (soldier) damage = SCR_DamageManagerComponent.GetDamageManager(soldier);
		if (!damage) return;
		damage.EnableDamageHandling(true);
		damage.Kill(Instigator.CreateInstigator(null));
	}

	protected int Frags(ChimeraCharacter soldier)
	{
		int count;
		SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(soldier.FindComponent(SCR_InventoryStorageManagerComponent));
		array<IEntity> carried = {};
		if (inventory) inventory.GetItems(carried);
		foreach (IEntity item : carried)
		{
			BaseWeaponComponent weapon = null;
			if (item) weapon = BaseWeaponComponent.Cast(item.FindComponent(BaseWeaponComponent));
			if (weapon && weapon.GetWeaponType() == EWeaponType.WT_FRAGGRENADE) count++;
		}
		return count;
	}

	protected void StripMagazines(ChimeraCharacter soldier)
	{
		BaseMuzzleComponent muzzle;
		int count;
		BBB_Rearm.NeedsAmmo(soldier, muzzle, count);
		SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(soldier.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inventory || !muzzle || !muzzle.GetMagazineWell()) return;
		string well = muzzle.GetMagazineWell().Type().ToString();
		array<IEntity> carried = {};
		inventory.GetItems(carried);
		foreach (IEntity item : carried)
		{
			if (!item) continue;
			MagazineComponent magazine = MagazineComponent.Cast(item.FindComponent(MagazineComponent));
			if (!magazine || !magazine.GetMagazineWell() || magazine.GetMagazineWell().Type().ToString() != well) continue;
			inventory.TryRemoveItemFromInventory(item);
			SCR_EntityHelper.DeleteEntityAndChildren(item);
		}
	}

	// Polls until the subject is a prisoner (20 s): 0 still waiting (poll again), 1 he gave up,
	// -1 he never did (the test is over).
	protected int Surrendered(string label)
	{
		m_iWait++;
		if (!BBB_Morale.IsPrisoner(m_Subject) && m_iWait < 40 && BBB.IsAlive(m_Subject)) return 0;
		Check(label, BBB_Morale.IsPrisoner(m_Subject));
		if (!BBB_Morale.IsPrisoner(m_Subject))
		{
			FinishTest();
			return -1;
		}
		PrintFormat("[BBB] MORALETEST %1 after %2 s", label, m_iWait * 0.5);
		return 1;
	}

	// ---- A: genuine

	protected void WaitSurrender()
	{
		int state = Surrendered("surrender-triggered");
		if (state == 0) GetGame().GetCallqueue().CallLater(WaitSurrender, 500);
		if (state != 1) return;
		Check("surrender-counted", BBB_Morale.s_iSurrenders > m_iSurrendersBefore);

		AIControlComponent ai = AIControlComponent.Cast(m_Subject.FindComponent(AIControlComponent));
		bool grouped = ai && ai.GetAIAgent() && ai.GetAIAgent().GetParentGroup();
		Check("surrender-left-group-ai-off", ai && !grouped && !ai.IsAIActivated());

		PerceivableComponent perceivable = PerceivableComponent.Cast(m_Subject.FindComponent(PerceivableComponent));
		Check("surrender-disarmed-for-perception", perceivable && perceivable.IsDisarmed());
		GetGame().GetCallqueue().CallLater(BeginFire, 2500);
	}

	// Control: an armed USSR team 15 m east of him (on hold fire, so it can't kill the US team).
	// The US team goes fire-at-will: it should engage the control team and leave him be.
	protected void BeginFire()
	{
		Check("surrender-kneels", m_Subject && m_Subject.GetCharacterController().GetStance() == ECharacterStance.CROUCH);
		// Every gun he carried is loose in the world next to him (the drop from his hands animates).
		int onGround;
		foreach (IEntity weapon : m_aSubjectGuns)
		{
			if (weapon && !weapon.GetParent() && vector.Distance(weapon.GetOrigin(), m_Subject.GetOrigin()) < 3) onGround++;
		}
		PrintFormat("[BBB] MORALETEST guns carried %1, on the ground %2", m_aSubjectGuns.Count(), onGround);
		Check("surrender-guns-on-ground", m_aSubjectGuns.Count() > 0 && onGround == m_aSubjectGuns.Count());
		m_ControlTeam = SCR_AIGroup.Cast(Spawn(TEAM_USSR, BBB.Ground(m_vOrigin + Vector(15, 0, 0)), Vector(0, 0, 1)));
		if (m_ControlTeam) m_ControlTeam.GetGroupUtilityComponent().SetCombatMode(EAIGroupCombatMode.HOLD_FIRE);
		SetUSFire(true);
		array<AIAgent> us = {};
		m_SquadUS.GetAgents(us);
		foreach (AIAgent agent : us)
		{
			IEntity man = agent.GetControlledEntity();
			if (!man) continue;
			BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(man.FindComponent(BaseWeaponManagerComponent));
			if (!weapons || !weapons.GetCurrentWeapon()) continue;
			m_aUSWeapons.Insert(weapons.GetCurrentWeapon());
			m_aUSLastAmmo.Insert(int.MAX);
		}
		m_iWait = 0;
		GetGame().GetCallqueue().CallLater(SampleFire, 500);
	}

	protected void SampleFire()
	{
		m_iWait++;
		foreach (int i, BaseWeaponComponent weapon : m_aUSWeapons)
		{
			if (!weapon) continue;
			int ammo = 0;
			if (weapon.GetCurrentMagazine()) ammo = weapon.GetCurrentMagazine().GetAmmoCount();
			if (ammo < m_aUSLastAmmo[i] && m_aUSLastAmmo[i] != int.MAX) m_iUSShots += m_aUSLastAmmo[i] - ammo;
			m_aUSLastAmmo[i] = ammo;
		}
		array<AIAgent> us = {};
		m_SquadUS.GetAgents(us);
		foreach (AIAgent agent : us)
		{
			SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			if (!utility || !utility.m_CombatComponent) continue;
			BaseTarget target = utility.m_CombatComponent.GetCurrentTarget();
			if (target && target.GetTargetEntity() == m_Subject) m_bSubjectTargeted = true;
		}
		if (m_iWait < 24)
		{
			GetGame().GetCallqueue().CallLater(SampleFire, 500);
			return;
		}

		int controlDown;
		array<AIAgent> control = {};
		if (m_ControlTeam) m_ControlTeam.GetAgents(control);
		foreach (AIAgent agent : control)
		{
			if (agent && !BBB.IsConscious(agent.GetControlledEntity())) controlDown++;
		}
		PrintFormat("[BBB] MORALETEST fire window 12 s: US shots %1, control team down %2 of %3, prisoner targeted %4, prisoner alive %5",
			m_iUSShots, controlDown, control.Count(), m_bSubjectTargeted, BBB.IsAlive(m_Subject));
		// Without this the next check proves nothing: the US team must be able to fight here.
		Check("control-team-engaged", m_iUSShots > 0);
		Check("prisoner-not-engaged", BBB.IsAlive(m_Subject) && !m_bSubjectTargeted);

		// No players on a test server: once the hold time passes he is removed.
		BBB_Morale.s_fPrisonerMinS = 2;
		m_iWait = 0;
		GetGame().GetCallqueue().CallLater(WaitRemoval, 500);
	}

	protected void WaitRemoval()
	{
		m_iWait++;
		if (m_Subject && m_iWait < 16)
		{
			GetGame().GetCallqueue().CallLater(WaitRemoval, 500);
			return;
		}
		Check("prisoner-removed-unwatched", !m_Subject && BBB_Morale.s_iPrisonersRemoved > m_iRemovedBefore);
		BBB_Morale.s_fPrisonerMinS = 600;
		BeginNextSubject(Vector(-12, 0, 0), BBB_Morale.FATE_EXECUTED);
	}

	// A fresh USSR team at an offset from A's spot; the US team holds fire until he gives up.
	protected void BeginNextSubject(vector offset, int fate)
	{
		SetUSFire(false);
		if (m_ControlTeam)
		{
			array<AIAgent> control = {};
			m_ControlTeam.GetAgents(control);
			foreach (AIAgent agent : control)
			{
				if (agent) Discard(agent.GetControlledEntity());
			}
			m_ControlTeam = null;
		}
		m_SubjectTeam = SCR_AIGroup.Cast(Spawn(TEAM_USSR, BBB.Ground(m_vOrigin + offset), Vector(0, 0, 1)));
		if (!m_SubjectTeam)
		{
			Check("next-subject-spawn", false);
			FinishTest();
			return;
		}
		m_iFateForNext = fate;
		GetGame().GetCallqueue().CallLater(PrepareNextSubject, 3000);
	}

	protected int m_iFateForNext;

	protected void PrepareNextSubject()
	{
		array<AIAgent> agents = {};
		m_SubjectTeam.GetAgents(agents);
		if (agents.Count() < 2)
		{
			Check("next-subject-spawn", false);
			FinishTest();
			return;
		}
		BBB_Morale.s_iForceFate = m_iFateForNext;
		PrepareSubject(agents);
		m_iWait = 0;
		if (m_iFateForNext == BBB_Morale.FATE_EXECUTED) GetGame().GetCallqueue().CallLater(WaitExecutionSurrender, 500);
		else GetGame().GetCallqueue().CallLater(WaitRuseSurrender, 500);
	}

	// ---- B: executed. His captors are the US team (AI, within 40 m): once they may fire,
	// the condemned prisoner is an ordinary target again.

	protected void WaitExecutionSurrender()
	{
		int state = Surrendered("execution-subject-surrendered");
		if (state == 0) GetGame().GetCallqueue().CallLater(WaitExecutionSurrender, 500);
		if (state != 1) return;
		SetUSFire(true);
		m_iWait = 0;
		GetGame().GetCallqueue().CallLater(WaitExecution, 500);
	}

	protected void WaitExecution()
	{
		m_iWait++;
		if (BBB.IsAlive(m_Subject) && m_iWait < 40)
		{
			GetGame().GetCallqueue().CallLater(WaitExecution, 500);
			return;
		}
		PrintFormat("[BBB] MORALETEST execution: dead after %1 s", m_iWait * 0.5);
		Check("executed-prisoner-killed", m_Subject && !BBB.IsAlive(m_Subject));
		BBB_Morale.s_fRuseRangeM = 40; // the US team stays 30 m off; in play someone walks up
		BBB_Morale.s_fRuseNerve = 1;
		BeginNextSubject(Vector(8, 0, -6), BBB_Morale.FATE_RUSE);
	}

	// ---- C: ruse. He keeps a frag (else a pistol) and comes back to life at the nearest enemy.

	protected void WaitRuseSurrender()
	{
		int state = Surrendered("ruse-subject-surrendered");
		if (state == 0) GetGame().GetCallqueue().CallLater(WaitRuseSurrender, 500);
		if (state != 1) return;
		PrintFormat("[BBB] MORALETEST ruse subject carries %1 frags", m_iFrags);
		m_iWait = 0;
		GetGame().GetCallqueue().CallLater(WaitRuse, 500);
	}

	protected void WaitRuse()
	{
		m_iWait++;
		bool thrown = m_iFrags > 0 && BBB.IsAlive(m_Subject) && Frags(m_Subject) < m_iFrags;
		if (!thrown && BBB.IsAlive(m_Subject) && m_iWait < 24)
		{
			GetGame().GetCallqueue().CallLater(WaitRuse, 500);
			return;
		}
		AIControlComponent ai;
		if (m_Subject) ai = AIControlComponent.Cast(m_Subject.FindComponent(AIControlComponent));
		PerceivableComponent perceivable;
		if (m_Subject) perceivable = PerceivableComponent.Cast(m_Subject.FindComponent(PerceivableComponent));
		bool back = m_Subject && !BBB_Morale.IsPrisoner(m_Subject) && ai && ai.IsAIActivated() && perceivable && !perceivable.IsDisarmed();
		Check("ruse-back-in-the-fight", back && BBB_Morale.s_iRuses > 0);
		// The frag physically left his kit (a pistol ruse has no frag to count: then this checks
		// only that he is armed again).
		if (m_iFrags > 0) Check("ruse-frag-thrown", thrown);
		else
		{
			SCR_AICombatComponent combat;
			if (m_Subject) combat = SCR_AICombatComponent.Cast(m_Subject.FindComponent(SCR_AICombatComponent));
			Check("ruse-pistol-kept", combat && combat.FindWeaponOfType(EWeaponType.WT_HANDGUN));
		}
		FinishTest();
	}

	protected void FinishTest()
	{
		BBB_Morale.s_fSurrenderThreat = 0.60;
		BBB_Morale.s_fSurrenderChance = 0.35;
		BBB_Morale.s_fPrisonerMinS = 90;
		BBB_Morale.s_bOn = true;
		BBB_Morale.s_fFateDelayMinS = 8;
		BBB_Morale.s_fFateDelayMaxS = 30;
		BBB_Morale.s_fRuseRangeM = 12;
		BBB_Morale.s_fRuseNerve = 0.3;
		BBB_Morale.s_iForceFate = -1;
		string verdict = "FAIL";
		if (m_iFailures == 0 && m_iChecks >= EXPECTED)
		{
			verdict = "PASS";
		}
		PrintFormat("[BBB] MORALETEST FINISHED checks=%1 failures=%2", m_iChecks, m_iFailures);
		PrintFormat("[ACR] VERDICT %1 test=bbbmorale passed=%2 failed=%3 expected=%4", verdict, m_iChecks - m_iFailures, m_iFailures, EXPECTED);
		Print("[ACR] TEST COMPLETE");
	}
}

modded class SCR_BaseGameMode
{
	protected override void OnGameStart()
	{
		super.OnGameStart();
		BBB_MoraleTest.MaybeStartMorale();
	}
}
