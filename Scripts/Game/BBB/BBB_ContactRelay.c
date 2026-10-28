// Contact relay (WP-6, Jacob 24 Oct): squads pass fresh sightings to friendly squads by radio.
// A group's own members identify an enemy (vanilla group perception, IDENTIFIED); friendly
// groups within the sender's radio range get it 2-6 s later as an unidentified report at the
// spot where he was seen, through the same vanilla path as a heard gunshot
// (SCR_AIGroupPerception.AddOrUpdateGunshot, not endangering). Vanilla then decides: it
// investigates the cluster with its own fireteams unless a player leads the group, it holds
// fire or its waypoint keeps it in place. A report is never an identified target, so it gives
// no aim point and no suppression; relayed reports are not relayed again.
// Range: the sender's best working radio (personal 1300 m, manpack 2000 m in vanilla); both
// groups need a conscious soldier with a radio, otherwise only voice range (s_fVoiceM).
//  -bbbContactRelay 0   off
class BBB_RelayReport : Managed
{
 SCR_AIGroup m_Receiver;
 IEntity m_Enemy;
 Faction m_Faction;
 vector m_vPosition;
 float m_fSeen;        // perception time of the sighting
 float m_fDeliverAt;   // world time ms
 string m_sFrom;
}

class BBB_ContactRelay
{
 static bool s_bOn = true;
 protected static int s_iRead;
 static float s_fFreshS = 8;          // only sightings this recent are passed on
 static float s_fVoiceM = 60;         // without radios: shouting distance
 static const int STAGGER = 4;            // each group sends every 4th tick
 static const float DELAY_MIN_MS = 2000;
 static const float DELAY_MAX_MS = 6000;
 static const float RADIO_CACHE_MS = 30000;
 static const int MAX_PENDING = 200;
 protected static ref array<ref BBB_RelayReport> s_Pending = {};
 // receiver -> enemy -> sighting time last relayed (no duplicate reports of one sighting)
 protected static ref map<SCR_AIGroup, ref map<IEntity, float>> s_Sent = new map<SCR_AIGroup, ref map<IEntity, float>>();
 protected static ref map<IEntity, float> s_RadioRange = new map<IEntity, float>();
 protected static ref map<IEntity, float> s_RadioAt = new map<IEntity, float>();
 protected static int s_iTick;
 static int s_iQueued;    // tests read these
 static int s_iDelivered;

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbContactRelay", value) && value == "0");
  if (BBB_Settings.Get("bbbRelayFresh", value)) s_fFreshS = Math.Clamp(value.ToFloat(), 2, 30);
  if (BBB_Settings.Get("bbbRelayVoice", value)) s_fVoiceM = Math.Clamp(value.ToFloat(), 0, 150);
  PrintFormat("[BBB] CONTACT RELAY enabled=%1 fresh=%2s voice=%3m delay=%4-%5s", s_bOn, s_fFreshS, s_fVoiceM, DELAY_MIN_MS / 1000, DELAY_MAX_MS / 1000);
 }

 static void Stop()
 {
  s_Pending.Clear();
  s_Sent.Clear();
  s_RadioRange.Clear();
  s_RadioAt.Clear();
  s_iTick = 0;
 }

 static void Tick()
 {
  if (!s_bOn) return;
  float now = GetGame().GetWorld().GetWorldTime();
  Deliver(now);
  s_iTick++;
  array<SCR_AIGroup> groups = {};
  BBB.s_Hooks.GetGroups(groups);
  PerceptionManager perception = GetGame().GetPerceptionManager();
  if (!perception) return;
  float perceptionNow = perception.GetTime();
  foreach (int i, SCR_AIGroup sender : groups)
  {
   if ((i + s_iTick) % STAGGER != 0) continue;
   Send(sender, groups, perceptionNow, now);
  }
  // Forget receivers that are gone, and radio checks of the dead.
  if (s_RadioAt.Count() > 400) { s_RadioAt.Clear(); s_RadioRange.Clear(); }
  for (int k = s_Sent.Count() - 1; k >= 0; k--)
   if (!s_Sent.GetKey(k)) s_Sent.RemoveElement(k);
 }

 // The best transmit range among a group's conscious soldiers' working radios (0: none).
 static float GroupRadioRange(SCR_AIGroup group, float now)
 {
  float best = 0;
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(group, agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   IEntity body = agent.GetControlledEntity();
   if (!BBB.IsConscious(body)) continue;
   best = Math.Max(best, RadioRange(body, now));
  }
  return best;
 }

 static float RadioRange(IEntity body, float now)
 {
  float at;
  if (s_RadioAt.Find(body, at) && now - at < RADIO_CACHE_MS) return s_RadioRange.Get(body);
  float range = 0;
  SCR_GadgetManagerComponent gadgets = SCR_GadgetManagerComponent.GetGadgetManager(body);
  if (gadgets)
  {
   array<SCR_GadgetComponent> radios = gadgets.GetGadgetsByType(EGadgetType.RADIO);
   if (radios)
   {
    foreach (SCR_GadgetComponent gadget : radios)
    {
     if (!gadget || !gadget.GetOwner()) continue;
     BaseRadioComponent radio = BaseRadioComponent.Cast(gadget.GetOwner().FindComponent(BaseRadioComponent));
     if (!radio || !radio.IsPowered()) continue;
     for (int t = 0; t < radio.TransceiversCount(); t++)
     {
      BaseTransceiver transceiver = radio.GetTransceiver(t);
      if (transceiver) range = Math.Max(range, transceiver.GetRange());
     }
    }
   }
  }
  s_RadioRange.Set(body, range);
  s_RadioAt.Set(body, now);
  return range;
 }

 protected static void Send(SCR_AIGroup sender, array<SCR_AIGroup> groups, float perceptionNow, float now)
 {
  if (!sender) return;
  SCR_AIGroupUtilityComponent utility = sender.GetGroupUtilityComponent();
  if (!utility || !utility.m_Perception) return;
  array<SCR_AITargetInfo> fresh = {};
  foreach (SCR_AITargetInfo target : utility.m_Perception.m_aTargets)
  {
   if (!target || !target.m_Entity || target.m_eCategory != EAITargetInfoCategory.IDENTIFIED) continue;
   if (perceptionNow - target.m_fTimestamp > s_fFreshS) continue;
   if (target.m_Perceivable && target.m_Perceivable.GetUnitType() == EAIUnitType.UnitType_Aircraft) continue;
   fresh.Insert(target);
  }
  if (fresh.IsEmpty()) return;
  IEntity anchor = BBB.s_Hooks.AnchorOf(sender);
  if (!anchor) return;
  float senderRange = GroupRadioRange(sender, now);
  Faction side = sender.GetFaction();
  foreach (SCR_AIGroup receiver : groups)
  {
   if (!receiver || receiver == sender || receiver.GetFaction() != side) continue;
   IEntity there = BBB.s_Hooks.AnchorOf(receiver);
   if (!there) continue;
   float distance = vector.DistanceXZ(anchor.GetOrigin(), there.GetOrigin());
   if (distance > Math.Max(s_fVoiceM, senderRange)) continue;
   if (distance > s_fVoiceM && GroupRadioRange(receiver, now) <= 0) continue;
   if (!Accepts(receiver)) continue;
   foreach (SCR_AITargetInfo seen : fresh) Queue(receiver, seen, BBB.s_Hooks.NameOf(sender), now);
  }
 }

 // The receiving group may take a report: the host allows it and no player/GM order holds it.
 static bool Accepts(SCR_AIGroup receiver)
 {
  if (!BBB.s_Hooks.AllowContactRelay(receiver)) return false;
  AIAgent leader = receiver.GetLeaderAgent();
  if (!leader) return false;
  SCR_AIUtilityComponent leaderUtility = SCR_AIUtilityComponent.Cast(leader.FindComponent(SCR_AIUtilityComponent));
  return !BBB_OrderIntent.Held(leaderUtility);
 }

 protected static void Queue(SCR_AIGroup receiver, SCR_AITargetInfo seen, string from, float now)
 {
  // Already knows this much (its own sighting or an earlier report of the same one).
  SCR_AIGroupUtilityComponent utility = receiver.GetGroupUtilityComponent();
  if (!utility || !utility.m_Perception) return;
  int known = utility.m_Perception.m_aTargetEntities.Find(seen.m_Entity);
  if (known > -1 && utility.m_Perception.m_aTargets[known].m_fTimestamp >= seen.m_fTimestamp) return;
  map<IEntity, float> sent = s_Sent.Get(receiver);
  if (!sent)
  {
   sent = new map<IEntity, float>();
   s_Sent.Set(receiver, sent);
  }
  float last;
  if (sent.Find(seen.m_Entity, last) && last >= seen.m_fTimestamp) return;
  if (s_Pending.Count() >= MAX_PENDING) return;
  sent.Set(seen.m_Entity, seen.m_fTimestamp);
  BBB_RelayReport report = new BBB_RelayReport();
  report.m_Receiver = receiver;
  report.m_Enemy = seen.m_Entity;
  report.m_Faction = seen.m_Faction;
  report.m_vPosition = seen.m_vWorldPos;
  report.m_fSeen = seen.m_fTimestamp;
  report.m_fDeliverAt = now + Math.RandomFloat(DELAY_MIN_MS, DELAY_MAX_MS);
  report.m_sFrom = from;
  s_Pending.Insert(report);
  s_iQueued++;
 }

 protected static void Deliver(float now)
 {
  for (int i = s_Pending.Count() - 1; i >= 0; i--)
  {
   BBB_RelayReport report = s_Pending[i];
   if (report.m_fDeliverAt > now) continue;
   s_Pending.Remove(i);
   if (!report.m_Receiver || !report.m_Enemy || !Accepts(report.m_Receiver)) continue;
   SCR_AIGroupUtilityComponent utility = report.m_Receiver.GetGroupUtilityComponent();
   if (!utility || !utility.m_Perception) continue;
   utility.m_Perception.AddOrUpdateGunshot(report.m_Enemy, report.m_vPosition, report.m_Faction, report.m_fSeen, false);
   s_iDelivered++;
   BBB_Perf.Count("contactRelay.delivered");
   IEntity anchor = BBB.AnchorOf(report.m_Receiver);
   if (anchor) BBB_Think.NoteGroup(report.m_Receiver, "Radio", string.Format("contact reported by %1, %2 m away", report.m_sFrom, (int)vector.DistanceXZ(report.m_vPosition, anchor.GetOrigin())));
  }
 }
}
