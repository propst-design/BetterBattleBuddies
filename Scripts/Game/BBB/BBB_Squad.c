// Squad picture (squad brain stage 2, Jacob 24 Oct): one shared read of each squad, built the
// first time a feature asks in a given second and shared by every reader that second.
// Main threat: every enemy the members perceive themselves (radio relay reports live in group
// perception and never become an aim point here), counted once at its freshest sighting and
// weighted by freshness and range. Aim reads sightings up to 20 s old; the way out remembers 60 s.
// Sightings no member still holds are kept (aging) for 60 s while the enemy lives: the squad
// doesn't forget a group because the men who saw it fell or looked away. The heaviest 60-degree sector around the squad wins and its
// weighted centre is the threat; enough weight beyond 90 degrees of it means the squad is flanked.
// The old per-feature centroids averaged every report, so fire from two sides put the "threat"
// between them (often on the squad itself) and each man's current target counted twice.
// Escape(): the middle of the widest gap between contact bearings, so a withdrawal leaves through
// the open side instead of backing off one group into another (a direction, never an aim point).
// Readers: Morale/Smoke GetThreatCentroid (withdrawal aim and break-contact range, smoke aim,
// StaticWeapons arc), Morale's withdrawal direction (Escape), FireManeuver's first threat report,
// Think.
//  -bbbSquadPicture 0   off: the old per-feature functions
class BBB_SquadContact : Managed
{
 IEntity m_Enemy;
 vector m_vPos;
 float m_fAge;     // s since detected
 float m_fWeight;
 float m_fBearing; // degrees from the squad, 0 = +Z
}

class BBB_SquadPicture : Managed
{
 SCR_AIGroup m_Group;
 float m_fBuilt;     // world time s
 int m_iMembers;
 int m_iConscious;
 bool m_bLeaderUp;
 bool m_bHasCentre;
 vector m_vCentre;   // conscious members (any living member when none is conscious)
 float m_fAvgThreat; // native threat measure of the conscious members
 ref array<ref BBB_SquadContact> m_Contacts = {};
}

class BBB_Squad
{
 static const float REFRESH_S = 1.0;
 static const float RECENT_S = 20.0;  // aim: sightings this fresh
 static const float MEMORY_S = 60.0;  // way out: a group unseen for a while is still there
 static const float SECTOR_HALF_DEG = 30.0;
 static const float FLANK_DEG = 90.0;
 static const float FLANK_SHARE = 0.25;
 static bool s_bOn = true;
 protected static int s_iRead;
 protected static ref map<SCR_AIGroup, ref BBB_SquadPicture> s_Pictures = new map<SCR_AIGroup, ref BBB_SquadPicture>();
 static ref array<SCR_AIGroup> s_aVanilla = {}; // test only: squads that keep the old functions
 static int s_iBuilt; // tests and perf read this

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbSquadPicture", value) && value == "0");
  PrintFormat("[BBB] SQUAD PICTURE enabled=%1 refresh=%2s", s_bOn, REFRESH_S);
 }

 static void Stop()
 {
  s_Pictures.Clear();
  s_aVanilla.Clear();
 }

 // Does this squad use the shared picture?
 static bool On(SCR_AIGroup group)
 {
  Read();
  return s_bOn && group && !s_aVanilla.Contains(group);
 }

 protected static float Now() { return GetGame().GetWorld().GetWorldTime() / 1000; }

 static BBB_SquadPicture Get(SCR_AIGroup group)
 {
  if (!On(group)) return null;
  float now = Now();
  BBB_SquadPicture picture = s_Pictures.Get(group);
  if (picture && now - picture.m_fBuilt < REFRESH_S) return picture;
  if (!picture)
  {
   picture = new BBB_SquadPicture();
   picture.m_Group = group;
   s_Pictures.Set(group, picture);
  }
  Build(picture, now);
  return picture;
 }

 protected static void Build(BBB_SquadPicture picture, float now)
 {
  s_iBuilt++;
  float since = now - picture.m_fBuilt;
  // The squad remembers what its men saw (a man who falls or looks away doesn't take it with him).
  array<ref BBB_SquadContact> before = {};
  foreach (BBB_SquadContact old : picture.m_Contacts) before.Insert(old);
  picture.m_fBuilt = now;
  picture.m_iConscious = 0;
  picture.m_bLeaderUp = false;
  picture.m_bHasCentre = false;
  picture.m_fAvgThreat = 0;
  picture.m_Contacts.Clear();
  array<AIAgent> agents = {};
  BBB.s_Hooks.GetMembers(picture.m_Group, agents);
  picture.m_iMembers = agents.Count();
  IEntity leader = picture.m_Group.GetLeaderEntity();
  vector conscious;
  vector living;
  int livingCount;
  float threat;
  map<IEntity, BBB_SquadContact> byEnemy = new map<IEntity, BBB_SquadContact>();
  array<BaseTarget> targets = {};
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   ChimeraCharacter character = ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!character || !BBB.IsAlive(character)) continue;
   living = living + character.GetOrigin();
   livingCount++;
   SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
   CharacterControllerComponent controller = character.GetCharacterController();
   if (controller && !controller.IsUnconscious())
   {
    picture.m_iConscious++;
    conscious = conscious + character.GetOrigin();
    if (character == leader) picture.m_bLeaderUp = true;
    if (utility && utility.m_ThreatSystem) threat += utility.m_ThreatSystem.GetThreatMeasure();
   }
   if (!utility || !utility.m_PerceptionComponent) continue;
   targets.Clear();
   utility.m_PerceptionComponent.GetTargetsList(targets, ETargetCategory.ENEMY);
   foreach (BaseTarget target : targets)
   {
    if (!target || target.GetTargetCategory() != ETargetCategory.ENEMY || target.IsDisarmed()) continue;
    float age = target.GetTimeSinceDetected();
    if (age < 0 || age > MEMORY_S) continue;
    IEntity enemy = target.GetTargetEntity();
    if (enemy && !BBB.IsAlive(enemy)) continue;
    BBB_SquadContact contact;
    if (enemy) contact = byEnemy.Get(enemy);
    if (contact && contact.m_fAge <= age) continue;
    if (!contact)
    {
     contact = new BBB_SquadContact();
     contact.m_Enemy = enemy;
     picture.m_Contacts.Insert(contact);
     if (enemy) byEnemy.Set(enemy, contact);
    }
    contact.m_fAge = age;
    contact.m_vPos = target.GetLastDetectedPosition();
   }
  }
  foreach (BBB_SquadContact kept : before)
  {
   if (!kept.m_Enemy || byEnemy.Contains(kept.m_Enemy) || !BBB.IsAlive(kept.m_Enemy)) continue;
   kept.m_fAge = kept.m_fAge + since;
   if (kept.m_fAge > MEMORY_S) continue;
   picture.m_Contacts.Insert(kept);
   byEnemy.Set(kept.m_Enemy, kept);
  }
  if (picture.m_iConscious > 0)
  {
   picture.m_bHasCentre = true;
   picture.m_vCentre = conscious * (1.0 / picture.m_iConscious);
   picture.m_fAvgThreat = threat / picture.m_iConscious;
  }
  else if (livingCount > 0)
  {
   picture.m_bHasCentre = true;
   picture.m_vCentre = living * (1.0 / livingCount);
  }
  foreach (BBB_SquadContact item : picture.m_Contacts)
  {
   vector offset = item.m_vPos - picture.m_vCentre;
   float range = vector.DistanceXZ(item.m_vPos, picture.m_vCentre);
   item.m_fBearing = Math.Atan2(offset[0], offset[2]) * Math.RAD2DEG;
   // A fresh, close enemy matters most; a 20 s old one far off still counts a little.
   item.m_fWeight = (1.0 / (1.0 + item.m_fAge / 4.0)) * Math.Clamp(100.0 / Math.Max(range, 1), 0.5, 2.0);
  }
 }

 static float AngleBetween(float a, float b)
 {
  float d = Math.AbsFloat(a - b);
  while (d > 360) d -= 360;
  if (d > 180) d = 360 - d;
  return d;
 }

 // The squad's main threat from sightings at most maxAge s old. False: none.
 static bool Threat(SCR_AIGroup group, float maxAge, out vector position)
 {
  bool flanked;
  return MainThreat(group, maxAge, position, flanked);
 }

 static bool MainThreat(SCR_AIGroup group, float maxAge, out vector position, out bool flanked)
 {
  int enemies;
  return MainSector(group, maxAge, position, flanked, enemies);
 }

 // MainThreat plus how many enemies the squad has seen in that sector.
 static bool MainSector(SCR_AIGroup group, float maxAge, out vector position, out bool flanked, out int enemies)
 {
  position = vector.Zero;
  flanked = false;
  enemies = 0;
  BBB_SquadPicture picture = Get(group);
  if (!picture || !picture.m_bHasCentre) return false;
  BBB_SquadContact seed;
  float best = -1;
  float total;
  foreach (BBB_SquadContact contact : picture.m_Contacts)
  {
   if (contact.m_fAge > maxAge) continue;
   total += contact.m_fWeight;
   float sector;
   foreach (BBB_SquadContact other : picture.m_Contacts)
    if (other.m_fAge <= maxAge && AngleBetween(contact.m_fBearing, other.m_fBearing) <= SECTOR_HALF_DEG) sector += other.m_fWeight;
   if (sector > best) { best = sector; seed = contact; }
  }
  if (!seed) return false;
  vector sum;
  float weight;
  float behind;
  foreach (BBB_SquadContact member : picture.m_Contacts)
  {
   if (member.m_fAge > maxAge) continue;
   float off = AngleBetween(seed.m_fBearing, member.m_fBearing);
   if (off <= SECTOR_HALF_DEG) { sum = sum + member.m_vPos * member.m_fWeight; weight += member.m_fWeight; enemies++; }
   else if (off > FLANK_DEG) behind += member.m_fWeight;
  }
  position = sum * (1.0 / weight);
  flanked = total > 0 && behind / total >= FLANK_SHARE;
  return true;
 }

 // Which way out: the middle of the widest gap between the bearings of sightings at most maxAge
 // s old (straight away from one group; between two groups' far sides; sideways when they are
 // on opposite sides). Not an enemy position: only for choosing where to go.
 static bool Escape(SCR_AIGroup group, float maxAge, out vector direction)
 {
  direction = vector.Zero;
  BBB_SquadPicture picture = Get(group);
  if (!picture || !picture.m_bHasCentre) return false;
  array<float> bearings = {};
  foreach (BBB_SquadContact contact : picture.m_Contacts)
   if (contact.m_fAge <= maxAge) bearings.Insert(contact.m_fBearing);
  return EscapeFrom(bearings, direction);
 }

 static bool EscapeFrom(notnull array<float> bearings, out vector direction)
 {
  direction = vector.Zero;
  if (bearings.IsEmpty()) return false;
  bearings.Sort();
  float widest = bearings[0] + 360 - bearings[bearings.Count() - 1];
  float middle = bearings[bearings.Count() - 1] + widest * 0.5;
  for (int i = 1; i < bearings.Count(); i++)
  {
   float gap = bearings[i] - bearings[i - 1];
   if (gap > widest) { widest = gap; middle = bearings[i - 1] + gap * 0.5; }
  }
  direction = Vector(Math.Sin(middle * Math.DEG2RAD), 0, Math.Cos(middle * Math.DEG2RAD));
  return true;
 }

 // The freshest real sighting (at most maxAge s old) on the main front: where covering fire goes.
 static bool FreshReport(SCR_AIGroup group, float maxAge, out vector report)
 {
  report = vector.Zero;
  vector main;
  bool flanked;
  BBB_SquadPicture picture = Get(group);
  if (!picture || !MainThreat(group, RECENT_S, main, flanked)) return false;
  vector offset = main - picture.m_vCentre;
  float bearing = Math.Atan2(offset[0], offset[2]) * Math.RAD2DEG;
  float best = maxAge + 1;
  foreach (BBB_SquadContact contact : picture.m_Contacts)
  {
   if (contact.m_fAge > maxAge || contact.m_fAge >= best || AngleBetween(contact.m_fBearing, bearing) > SECTOR_HALF_DEG) continue;
   best = contact.m_fAge;
   report = contact.m_vPos;
  }
  return best <= maxAge;
 }

 // No sighting at most maxAge s old within halfAngle degrees of this direction?
 static bool Clear(SCR_AIGroup group, vector direction, float halfAngle, float maxAge)
 {
  BBB_SquadPicture picture = Get(group);
  if (!picture) return true;
  float bearing = Math.Atan2(direction[0], direction[2]) * Math.RAD2DEG;
  foreach (BBB_SquadContact contact : picture.m_Contacts)
   if (contact.m_fAge <= maxAge && AngleBetween(contact.m_fBearing, bearing) <= halfAngle) return false;
  return true;
 }

 // Think panel line.
 static string Describe(SCR_AIGroup group)
 {
  BBB_SquadPicture picture = Get(group);
  if (!picture) return "";
  string leader = "down";
  if (picture.m_bLeaderUp) leader = "up";
  string text = string.Format("squad: %1/%2 up, leader %3, threat %4", picture.m_iConscious, picture.m_iMembers, leader, picture.m_fAvgThreat.ToString(4, 2));
  vector main;
  bool flanked;
  if (!MainThreat(group, RECENT_S, main, flanked)) return text + ", no contact";
  vector offset = main - picture.m_vCentre;
  float bearing = Math.Atan2(offset[0], offset[2]) * Math.RAD2DEG;
  if (bearing < 0) bearing += 360;
  text += string.Format(", enemy %1 deg %2 m (%3 seen)", Math.Round(bearing), Math.Round(vector.DistanceXZ(main, picture.m_vCentre)), picture.m_Contacts.Count());
  if (flanked) text += ", FLANKED";
  return text;
 }

 // Once a tick: drop pictures of squads gone or unread for a while.
 static void Tick()
 {
  if (s_Pictures.IsEmpty()) return;
  float now = Now();
  for (int i = s_Pictures.Count() - 1; i >= 0; i--)
  {
   BBB_SquadPicture picture = s_Pictures.GetElement(i);
   if (!s_Pictures.GetKey(i) || !picture || now - picture.m_fBuilt > 30) s_Pictures.RemoveElement(i);
  }
  for (int v = s_aVanilla.Count() - 1; v >= 0; v--)
   if (!s_aVanilla[v]) s_aVanilla.Remove(v);
 }

 static int Count() { return s_Pictures.Count(); }
}
