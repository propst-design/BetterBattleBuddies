// Better Battle Buddies (BBB): how a prisoner shows he has given up (BBB_Morale surrender).
// With ACE Captives loaded (-bbbMoraleSurrenderAce, default on) he becomes ACE's surrendered man: ACE's
// own hands-up pose and "surrendered" flag, so players carrying zip cuffs can take him captive and
// escort him, and ACE's Game Master tools apply. ACE is reached by name only (ScriptModule.Call and
// class names as strings): BBB neither depends on ACE nor contains any of its code, and compiles and
// runs the same without it. Without ACE (or with the switch off) he kneels holding the halt sign, then
// sits down on the ground unarmed (the vanilla sit emote, replicated by vanilla's loiter RPC).
// ACE switches a surrendered man to CIV; BBB_SurrenderState keeps his real faction for BBB's own checks.
class BBB_SurrenderPose
{
	static bool s_bAceOn = true;
	protected static int s_iRead;
	protected static int s_iAce;                 // 0 not looked up, 1 ACE Captives loaded, 2 absent
	static float s_fSitAfterS = 12.0;            // without ACE: kneeling with the halt sign this long, then he sits
	static const int GESTURE_HALT = 2;           // ECharacterGestures.COMMAND_STOP (Gesture_Halt.conf)
	static const float RISE_TIMEOUT_S = 6.0;     // a ruse stops waiting to be on his feet after this
	static const float SEAT_WAIT_S = 3.0;        // fates wait this long for ACE's seat to land

	static const int POSE_NONE = 0;
	static const int POSE_ACE = 1;               // in ACE's surrender helper compartment
	static const int POSE_KNEEL = 2;             // crouched, halt sign held
	static const int POSE_STANDING = 3;          // getting up to sit
	static const int POSE_SIT = 4;               // sitting on the ground (vanilla emote)
	static const int POSE_ACE_PENDING = 5;       // kneeling while his weapons hit the ground, then ACE
	static const int ACE_AFTER_MS = 2000;        // the animated weapon drop first: seating him cut it short

	static void Read()
	{
		if (!BBB_Settings.Due(s_iRead)) return;
		string value;
		s_bAceOn = !(BBB_Settings.Get("bbbMoraleSurrenderAce", value) && value == "0");
		PrintFormat("[BBB] SURRENDER POSE ace=%1 aceLoaded=%2", s_bAceOn, AceLoaded());
	}

	// ACE Captives is part of this mission (its surrender helper class exists). Looked up once.
	static bool AceLoaded()
	{
		if (s_iAce == 0)
		{
			string helperClass = "ACE_Captives_SurrenderHelperCompartment";
			typename helper = helperClass.ToType();
			s_iAce = 2;
			if (helper) s_iAce = 1;
		}
		return s_iAce == 1;
	}

	static bool UseAce()
	{
		Read();
		return s_bAceOn && AceLoaded();
	}

	// ACE's controller methods, called by name (they exist only with ACE Captives loaded).
	protected static bool AceSet(CharacterControllerComponent controller, string method, bool value)
	{
		bool unused;
		return GetGame().GetScriptModule().Call(controller, method, false, unused, value);
	}

	protected static bool AceGet(CharacterControllerComponent controller, string method)
	{
		bool result;
		if (!GetGame().GetScriptModule().Call(controller, method, false, result)) return false;
		return result;
	}

	// Shows he gives up, right after his guns are dropped. He kneels first either way; with ACE he
	// enters ACE's surrender once the (animated) drop is done: seating him at once cut the drop short
	// and left the rifle in his hands. ACE seats him through his compartment access, so it works
	// with BBB's AI switch already off.
	static void Start(BBB_SurrenderState state)
	{
		ChimeraCharacter soldier = state.m_Soldier;
		CharacterControllerComponent controller = soldier.GetCharacterController();
		if (!controller) return;
		state.m_TrueFaction = SCR_AIFactionHandling.GetEntityPerceivedFaction(soldier);
		controller.SetWeaponRaised(false);
		controller.SelectWeapon(null); // empty hands: a kept pistol stays holstered
		SCR_AIStanceHandling.SetStance(controller, ECharacterStance.CROUCH);
		if (UseAce())
		{
			state.m_iPose = POSE_ACE_PENDING;
			GetGame().GetCallqueue().CallLater(EnterAce, ACE_AFTER_MS, false, state);
			return;
		}
		state.m_iPose = POSE_KNEEL;
		// Once his hands are empty (vanilla emotes wait for the unequip too); held until he sits.
		GetGame().GetCallqueue().CallLater(RaiseHand, 1500, false, state);
	}

	// ACE's own surrender (called by name). If ACE refuses, he keeps BBB's kneel and halt sign.
	static void EnterAce(BBB_SurrenderState state)
	{
		if (!state || state.m_iPose != POSE_ACE_PENDING) return; // released or gone meanwhile
		ChimeraCharacter soldier = state.m_Soldier;
		if (!BBB.IsConscious(soldier) || !BBB_Morale.IsPrisoner(soldier)) return;
		CharacterControllerComponent controller = soldier.GetCharacterController();
		if (controller && AceSet(controller, "ACE_Captives_SetSurrender", true))
		{
			state.m_iPose = POSE_ACE;
			state.m_fPoseSince = GetGame().GetWorld().GetWorldTime() / 1000.0;
			return;
		}
		PrintFormat("[BBB] SURRENDER POSE: ACE refused %1, kneeling instead", soldier);
		state.m_iPose = POSE_KNEEL;
		RaiseHand(state);
	}

	static void RaiseHand(BBB_SurrenderState state)
	{
		if (!state || state.m_iPose != POSE_KNEEL) return; // released, sitting or gone meanwhile
		ChimeraCharacter soldier = state.m_Soldier;
		if (!BBB.IsConscious(soldier) || !BBB_Morale.IsPrisoner(soldier)) return;
		CharacterControllerComponent controller = soldier.GetCharacterController();
		if (controller) controller.TryStartCharacterGesture(GESTURE_HALT, 0); // 0: until StopCharacterGesture
	}

	// Once a second for a conscious prisoner: the fallback kneels, then stands and sits down.
	static void Update(BBB_SurrenderState state, float now)
	{
		if (state.m_iPose != POSE_KNEEL && state.m_iPose != POSE_STANDING) return;
		SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(state.m_Soldier.GetCharacterController());
		if (!controller) return;
		if (state.m_iPose == POSE_KNEEL)
		{
			if (now - state.m_fSurrenderSince < s_fSitAfterS) return;
			controller.StopCharacterGesture();
			SCR_AIStanceHandling.SetStance(controller, ECharacterStance.STAND); // the sit emote starts standing
			state.m_iPose = POSE_STANDING;
			return;
		}
		vector none[4];
		controller.StartLoitering(null, ELoiteringType.SIT, false, true, false, none);
		state.m_iPose = POSE_SIT;
	}

	// Stands him up (a ruse, a player taking him over). ACE plays its own get-up.
	static void Release(BBB_SurrenderState state)
	{
		if (!state.m_Soldier) return;
		CharacterControllerComponent controller = state.m_Soldier.GetCharacterController();
		if (!controller) return;
		if (state.m_iPose == POSE_ACE) AceSet(controller, "ACE_Captives_SetSurrender", false);
		else
		{
			controller.StopCharacterGesture();
			SCR_CharacterControllerComponent scripted = SCR_CharacterControllerComponent.Cast(controller);
			if (scripted && scripted.IsLoitering()) scripted.StopLoitering(true);
		}
		state.m_iPose = POSE_NONE;
	}

	// ACE was asked to seat him and hasn't yet (it seats through an RPC to his owner). Releasing then
	// raced the seat: BBB took him for standing, switched his AI on, and the seat landed after.
	static bool Seating(BBB_SurrenderState state, float now)
	{
		return state.m_iPose == POSE_ACE && !AceHelper(state.m_Soldier) && now - state.m_fPoseSince < SEAT_WAIT_S;
	}

	// Up and free to move: out of ACE's helper and not sitting. Still parented to the helper counts as
	// seated (IsInVehicle alone was false there: his AI came back mid get-up and vanilla's vehicle
	// look-range task hit the helper's missing physics).
	static bool OnFeet(BBB_SurrenderState state)
	{
		if (!state.m_Soldier || state.m_Soldier.IsInVehicle() || AceHelper(state.m_Soldier)) return false;
		SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(state.m_Soldier.GetCharacterController());
		return !controller || !controller.IsLoitering();
	}

	// His captors stop sparing him: ACE made him CIV, which AI never shoots.
	static void RestoreFaction(BBB_SurrenderState state)
	{
		if (state.m_iPose != POSE_ACE || !state.m_TrueFaction) return;
		SCR_CharacterFactionAffiliationComponent affiliation = SCR_CharacterFactionAffiliationComponent.Cast(state.m_Soldier.FindComponent(SCR_CharacterFactionAffiliationComponent));
		if (affiliation) affiliation.SetAffiliatedFaction(state.m_TrueFaction);
	}

	// A player has taken him captive with ACE's zip cuffs: he is ACE's now.
	static bool CuffedByAce(BBB_SurrenderState state)
	{
		if (state.m_iPose != POSE_ACE || !state.m_Soldier) return false;
		CharacterControllerComponent controller = state.m_Soldier.GetCharacterController();
		return controller && AceGet(controller, "ACE_Captives_IsCaptive");
	}

	// ACE's helper compartment he sits in, if any.
	static IEntity AceHelper(IEntity soldier)
	{
		if (!soldier || !AceLoaded()) return null;
		IEntity parent = soldier.GetParent();
		string helperBase = "ACE_AnimationHelperCompartment";
		typename helper = helperBase.ToType();
		if (parent && helper && parent.IsInherited(helper)) return parent;
		return null;
	}

	// What to delete when he goes: ACE's helper with him in it, else just him.
	static IEntity Remains(IEntity soldier)
	{
		IEntity helper = AceHelper(soldier);
		if (helper) return helper;
		return soldier;
	}
}
