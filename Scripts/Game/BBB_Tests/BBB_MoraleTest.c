// Better Battle Buddies (BBB): Standalone morale, retrograde break-contact
// withdrawal, and surrender assertions. Two arms for the prisoner's pose (BBB_SurrenderPose):
// base game only (preset morale: kneel with the halt sign, then sit), and with ACE Captives loaded
// (preset moraleace, -bbbMoraleAce 1: ACE's surrender helper and flag). ACE is named in strings only.
class BBB_MoraleTest : BBB_AimTest
{
	static ref BBB_MoraleTest s_MoraleTest;
	protected int m_iChecks;
	protected int m_iFailures;
	protected SCR_AIGroup m_SquadUSSR;
	protected SCR_AIGroup m_SquadUS;
	static const ResourceName WP_MOVE = "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et";
	static const int EXPECTED = 30;
	protected bool m_bAce;                // ACE arm: ACE Captives must be loaded and used
	protected vector m_vSubjectAt;        // where the prisoner was last seen (helper check after despawn)
	protected static typename s_HelperType;
	protected static int s_iHelpers;

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
	protected bool m_bRuseSeenArmed;
	protected vector m_vOrigin;

	protected void BeginSurrender(array<AIAgent> agents)
	{
		// Withdrawals off (a wiped squad would pull him out of range); surrender runs on its own switch.
		BBB_Morale.s_bOn = false;
		// Rearm off: the fixture strips the subject and kills his squadmates beside him, so Rearm
		// (priority 91 for a man with no spare magazine) sends him to loot them before he is cornered.
		// That is the right order in play; here it would test Rearm, not surrender.
		BBB_Rearm.s_bOn = false;
		// Tactic off: after the execution the US squad assaulted, held, then searched and wandered
		// out of the next subject's sight (one ruse subject in two never saw a captor in 20 s).
		BBB_Tactic.s_bOn = false;
		BBB_Morale.s_fSurrenderThreat = 0;
		BBB_Morale.s_fSurrenderChance = 1;
		BBB_Morale.s_fPrisonerMinS = 600;
		BBB_Morale.s_fFateDelayMinS = 0;
		BBB_Morale.s_fFateDelayMaxS = 0;
		BBB_SurrenderPose.s_fSitAfterS = 6; // the fallback sits down inside the 12 s fire window
		m_bAce = System.IsCLIParam("bbbMoraleAce");
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
		// Fixture: he notices his captors promptly (one subject in ten saw a US soldier 15 m off only
		// after 20 s). Surrender still needs a real sighting.
		SCR_AIUtilityComponent subjectUtility = BBB_VehiclePosture.Utility(m_Subject);
		if (subjectUtility && subjectUtility.m_CombatComponent) subjectUtility.m_CombatComponent.SetPerceptionFactor(10);
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
			LogWhyNot();
			FinishTest();
			return -1;
		}
		PrintFormat("[BBB] MORALETEST %1 after %2 s", label, m_iWait * 0.5);
		return 1;
	}

	// A subject who never gave up: what he perceives (the cornered test is the usual miss).
	protected void LogWhyNot()
	{
		if (!BBB.IsAlive(m_Subject)) { Print("[BBB] MORALETEST why not: subject dead"); return; }
		SCR_AIUtilityComponent utility = BBB_VehiclePosture.Utility(m_Subject);
		if (!utility || !utility.m_PerceptionComponent) { Print("[BBB] MORALETEST why not: no utility"); return; }
		int spare, inGun;
		BBB_Morale.PrimaryAmmo(m_Subject, spare, inGun);
		float threat = -1;
		if (utility.m_ThreatSystem) threat = utility.m_ThreatSystem.GetThreatMeasure();
		PrintFormat("[BBB] MORALETEST why not: spare %1 in gun %2 threat %3 health %4", spare, inGun, threat, BBB.Health(m_Subject));
		array<BaseTarget> targets = {};
		utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
		foreach (BaseTarget target : targets)
		{
			if (target) PrintFormat("[BBB] MORALETEST   enemy %1 m, seen %2 s ago, trace %3, disarmed %4", Math.Round(target.GetDistance()), target.GetTimeSinceSeen(), target.GetTraceFraction(), target.IsDisarmed());
		}
		array<AIAgent> us = {};
		if (m_SquadUS) m_SquadUS.GetAgents(us);
		foreach (AIAgent agent : us)
		{
			if (agent && agent.GetControlledEntity()) PrintFormat("[BBB] MORALETEST   US soldier at %1 m", Math.Round(vector.Distance(agent.GetControlledEntity().GetOrigin(), m_Subject.GetOrigin())));
		}
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
		int poseWait = 2500;
		if (m_bAce) poseWait = 4500; // ACE's pose starts once the weapon drop is done (2 s), then seats him
		GetGame().GetCallqueue().CallLater(BeginFire, poseWait);
	}

	// Control: an armed USSR team 15 m east of him (on hold fire, so it can't kill the US team).
	// The US team goes fire-at-will: it should engage the control team and leave him be.
	protected void BeginFire()
	{
		// The pose this arm expects: ACE's surrender helper and flag, or kneeling (base game).
		Check("pose-arm-matches-mods", BBB_SurrenderPose.AceLoaded() == m_bAce);
		if (m_bAce) Check("surrender-ace-pose", BBB_SurrenderPose.AceHelper(m_Subject) != null && AceFlag("ACE_Captives_HasSurrendered"));
		else Check("surrender-kneels", m_Subject && m_Subject.GetCharacterController().GetStance() == ECharacterStance.CROUCH);
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
		// Still showing it after 14 s: in ACE's helper, or (base game) sitting on the ground by now.
		SCR_CharacterControllerComponent poseController;
		if (m_Subject) poseController = SCR_CharacterControllerComponent.Cast(m_Subject.GetCharacterController());
		if (m_bAce) Check("pose-held-through-fire", BBB_SurrenderPose.AceHelper(m_Subject) != null);
		else Check("pose-held-through-fire", poseController && poseController.IsLoitering());

		// No players on a test server: once the hold time passes he is removed.
		BBB_Morale.s_fPrisonerMinS = 2;
		m_iWait = 0;
		GetGame().GetCallqueue().CallLater(WaitRemoval, 500);
	}

	protected void WaitRemoval()
	{
		m_iWait++;
		if (m_Subject) m_vSubjectAt = m_Subject.GetOrigin();
		if (m_Subject && m_iWait < 16)
		{
			GetGame().GetCallqueue().CallLater(WaitRemoval, 500);
			return;
		}
		Check("prisoner-removed-unwatched", !m_Subject && BBB_Morale.s_iPrisonersRemoved > m_iRemovedBefore);
		int helpers = AceHelpersNear(m_vSubjectAt);
		PrintFormat("[BBB] MORALETEST ACE helpers left where the prisoner was: %1", helpers);
		Check("no-helper-left", helpers == 0);
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
		if (m_bUnarmedNext)
		{
			DisarmSubject();
			BBB_Morale.s_fSurrenderChance = 0;
			m_iUnarmedRollsBefore = BBB_Morale.s_iUnarmedRolls;
			m_iWait = 0;
			GetGame().GetCallqueue().CallLater(WaitUnarmedHolds, 500);
			return;
		}
		// Rearm's rounds remember the rifle he carries (as in play, where Rearm is on); surrender
		// must make it forget it, or a ruse who rejoins is sent back for it (review 0.5.13 #18).
		m_bRuseSeenArmed = m_iFateForNext == BBB_Morale.FATE_RUSE && m_Subject && !BBB_Rearm.LostPrimary(m_Subject) && BBB_Rearm.LastPrimary(m_Subject) != null;
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
		// Shot down: dead, or downed by the shots (vanilla AI don't finish the unconscious, and
		// vanilla flags a downed man disarmed again; one ACE run left him unconscious, not dead).
		bool shot = m_Subject && (!BBB.IsAlive(m_Subject) || (!BBB.IsConscious(m_Subject) && BBB.Health(m_Subject) < 1));
		if (!shot && m_iWait < 80)
		{
			if (m_iWait == 40) LogCaptors(); // 20 s and still up: is anyone even aiming at him?
			GetGame().GetCallqueue().CallLater(WaitExecution, 500);
			return;
		}
		if (!shot) LogCaptors();
		PrintFormat("[BBB] MORALETEST execution: shot down after %1 s (dead %2)", m_iWait * 0.5, m_Subject && !BBB.IsAlive(m_Subject));
		Check("executed-prisoner-killed", shot);
		// A downed (not dead) prisoner is a live friendly beside the US team: the ruse's frag is rightly
		// called off when he lies in its blast area (integration run 12 Nov). Clear him for the ruse.
		if (m_Subject && BBB.IsAlive(m_Subject)) Discard(m_Subject);
		BBB_Morale.s_fRuseRangeM = 40; // the US team stays 20 m off; in play someone walks up
		BBB_Morale.s_fRuseNerve = 1;
		// 20 m south of where the US team stands now (it moves during the execution; once it ended
		// up 5 m from a fixed spot, too close for a frag ruse to break, which waits as it should).
		vector usCentre = m_vOrigin + Vector(0, 0, 30);
		array<AIAgent> us = {};
		m_SquadUS.GetAgents(us);
		vector sum;
		int counted;
		foreach (AIAgent agent : us)
		{
			if (!agent || !agent.GetControlledEntity()) continue;
			sum = sum + agent.GetControlledEntity().GetOrigin();
			counted++;
		}
		if (counted > 0) usCentre = sum * (1.0 / counted);
		BeginNextSubject(usCentre + Vector(0, 0, -20) - m_vOrigin, BBB_Morale.FATE_RUSE);
	}

	// ---- C: ruse. He keeps a frag (else a pistol) and comes back to life at the nearest enemy.

	protected void WaitRuseSurrender()
	{
		int state = Surrendered("ruse-subject-surrendered");
		if (state == 0) GetGame().GetCallqueue().CallLater(WaitRuseSurrender, 500);
		if (state != 1) return;
		PrintFormat("[BBB] MORALETEST ruse subject carries %1 frags", m_iFrags);
		// Rearm back on for the ruse (review 0.5.13 #18): once he rejoins, weapon recovery must not
		// send him for the rifle he dropped, nor an ammo run take him off the frag or pistol.
		BBB_Rearm.s_bOn = true;
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
		SCR_CharacterControllerComponent ruseController;
		if (m_Subject) ruseController = SCR_CharacterControllerComponent.Cast(m_Subject.GetCharacterController());
		Check("ruse-stood-up", m_Subject && !m_Subject.IsInVehicle() && ruseController && !ruseController.IsLoitering());
		// The frag physically left his kit (a pistol ruse has no frag to count: then this checks
		// only that he is armed again).
		bool rejoined = ai && ai.GetAIAgent() && ai.GetAIAgent().GetParentGroup() != null;
		bool noRun = m_Subject && !BBB_Rearm.InRearmAction(m_Subject) && !BBB_Rearm.PrimaryOf(m_Subject) && !BBB_Rearm.LostPrimary(m_Subject);
		PrintFormat("[BBB] MORALETEST ruse rearm: seenArmed=%1 noRun=%2 rejoined=%3 rearmOn=%4", m_bRuseSeenArmed, noRun, rejoined, BBB_Rearm.s_bOn);
		Check("ruse-no-rearm-run", m_bRuseSeenArmed && noRun);
		if (m_iFrags > 0) Check("ruse-frag-thrown", thrown);
		else
		{
			SCR_AICombatComponent combat;
			if (m_Subject) combat = SCR_AICombatComponent.Cast(m_Subject.FindComponent(SCR_AICombatComponent));
			Check("ruse-pistol-kept", combat && combat.FindWeaponOfType(EWeaponType.WT_HANDGUN));
		}
		BeginUnarmedSubject();
	}

	// ---- D: unarmed (review 0.5.13 #19, Jacob: "roll like everyone"). A cut-off soldier with no
	// gun at all once gave up automatically; now he takes the nerve roll. Nerve 0: he must hold
	// out; nerve 1: he gives up (so the cornering itself was real).
	protected bool m_bUnarmedNext;

	protected void BeginUnarmedSubject()
	{
		m_bUnarmedNext = true;
		// The ruse is still fighting (pistol or frag) beside where D stands: one run lost D to it.
		if (m_Subject && BBB.IsAlive(m_Subject)) Discard(m_Subject);
		vector usCentre = m_vOrigin + Vector(0, 0, 30);
		array<AIAgent> us = {};
		if (m_SquadUS) m_SquadUS.GetAgents(us);
		vector sum;
		int counted;
		foreach (AIAgent agent : us)
		{
			if (!agent || !agent.GetControlledEntity()) continue;
			sum = sum + agent.GetControlledEntity().GetOrigin();
			counted++;
		}
		if (counted > 0) usCentre = sum * (1.0 / counted);
		// Where the ruse stood, 20 m in front of the US team (a spot 21 m off at an angle once
		// never gave him a sighting, so the nerve-1 half could not run).
		BeginNextSubject(usCentre + Vector(0, 0, -20) - m_vOrigin, BBB_Morale.FATE_GENUINE);
	}

	// Every gun dropped the engine's way, as a real loss (Rearm off, or he would pick one up).
	// Not deleted: deleting the gun his attack behaviour has selected trips a vanilla null in
	// SCR_AIUpdateTargetAttackData.
	protected void DisarmSubject()
	{
		BBB_Rearm.s_bOn = false;
		BaseWeaponManagerComponent wm = BaseWeaponManagerComponent.Cast(m_Subject.FindComponent(BaseWeaponManagerComponent));
		CharacterControllerComponent controller = m_Subject.GetCharacterController();
		array<WeaponSlotComponent> slots = {};
		if (wm) wm.GetWeaponsSlots(slots);
		foreach (WeaponSlotComponent slot : slots)
		{
			if (!slot || !slot.GetWeaponEntity() || !BBB_Morale.IsGun(slot.GetWeaponType())) continue;
			if (controller) controller.DropWeapon(slot);
		}
	}

	protected int m_iUnarmedRollsBefore;

	protected void WaitUnarmedHolds()
	{
		m_iWait++;
		int rolls = BBB_Morale.s_iUnarmedRolls - m_iUnarmedRollsBefore;
		if (!BBB_Morale.IsPrisoner(m_Subject) && BBB.IsAlive(m_Subject) && rolls < 3 && m_iWait < 40)
		{
			GetGame().GetCallqueue().CallLater(WaitUnarmedHolds, 500);
			return;
		}
		int spare, inGun;
		bool known = BBB_Morale.PrimaryAmmo(m_Subject, spare, inGun);
		PrintFormat("[BBB] MORALETEST unarmed subject: primary %1, ammo known %2 spare %3 in gun %4, prisoner %5", BBB_Rearm.PrimaryOf(m_Subject) != null, known, spare, inGun, BBB_Morale.IsPrisoner(m_Subject));
		PrintFormat("[BBB] MORALETEST unarmed subject reached the nerve roll %1 times in %2 s", rolls, m_iWait * 0.5);
		// Cornered and eligible (he reached the roll: before, an unarmed man gave up right there)...
		Check("unarmed-cornered-rolled", rolls >= 1);
		// ...and with nerve 0 he holds out: no automatic give-up.
		Check("unarmed-no-automatic-surrender", BBB.IsAlive(m_Subject) && !BBB_Rearm.PrimaryOf(m_Subject) && !BBB_Morale.IsPrisoner(m_Subject));
		BBB_Morale.s_fSurrenderChance = 1;
		FinishTest();
	}

	// The condemned man as his captors see him.
	protected void LogCaptors()
	{
		if (!m_Subject) return;
		PerceivableComponent perceivable = PerceivableComponent.Cast(m_Subject.FindComponent(PerceivableComponent));
		Faction side = SCR_AIFactionHandling.GetEntityPerceivedFaction(m_Subject);
		string key = "none";
		if (side) key = side.GetFactionKey();
		PrintFormat("[BBB] MORALETEST execution: subject faction %1, disarmed %2, in ACE helper %3, stance %4, conscious %5, health %6", key, perceivable && perceivable.IsDisarmed(), BBB_SurrenderPose.AceHelper(m_Subject) != null, m_Subject.GetCharacterController().GetStance(), BBB.IsConscious(m_Subject), BBB.Health(m_Subject));
		array<AIAgent> us = {};
		m_SquadUS.GetAgents(us);
		foreach (AIAgent agent : us)
		{
			SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
			if (!utility || !utility.m_PerceptionComponent || !utility.m_CombatComponent) continue;
			array<BaseTarget> targets = {};
			utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
			bool perceived;
			foreach (BaseTarget target : targets)
			{
				if (target && target.GetTargetEntity() == m_Subject) perceived = true;
			}
			BaseTarget current = utility.m_CombatComponent.GetCurrentTarget();
			PrintFormat("[BBB] MORALETEST   captor %1 m: enemy target %2, current target is him %3, combat mode %4", Math.Round(vector.Distance(utility.m_OwnerEntity.GetOrigin(), m_Subject.GetOrigin())), perceived, current && current.GetTargetEntity() == m_Subject, utility.m_CombatComponent.GetCombatMode());
		}
	}

	// ACE's controller getter by name (false without ACE).
	protected bool AceFlag(string method)
	{
		if (!m_Subject) return false;
		bool result;
		if (!GetGame().GetScriptModule().Call(m_Subject.GetCharacterController(), method, false, result)) return false;
		return result;
	}

	// ACE animation helper entities within 10 m (0 without ACE).
	protected int AceHelpersNear(vector at)
	{
		string helperBase = "ACE_AnimationHelperCompartment";
		s_HelperType = helperBase.ToType();
		s_iHelpers = 0;
		if (!s_HelperType) return 0;
		GetGame().GetWorld().QueryEntitiesBySphere(at, 10, CountHelper, null, EQueryEntitiesFlags.ALL);
		return s_iHelpers;
	}

	protected static bool CountHelper(IEntity entity)
	{
		if (entity && entity.IsInherited(s_HelperType)) s_iHelpers++;
		return true;
	}

	protected void FinishTest()
	{
		BBB_SurrenderPose.s_fSitAfterS = 12;
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
