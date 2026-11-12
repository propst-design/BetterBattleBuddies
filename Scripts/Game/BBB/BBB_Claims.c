// Role claims (squad brain stage 1, Jacob 24 Oct): one BBB job per soldier at a time.
// A feature that moves, seats or holds a soldier takes him with Take() before it acts and
// renews the claim while its job runs (claims lapse after their TTL, so a missed Release
// cannot hold a man for good). A higher role takes a man from a lower one; the owner sees
// Can() turn false on its next tick and drops him the way it already does. Existing
// cross-feature guards stay, so in practice the first job keeps him unless a higher one
// needs him:
//   RESCUE (drag, medic on his way) > STATIC (walking to a static gun) > REARM (ammo / AT
//   run) > FIRE (FireManeuver bound or cover) > BUILDINGS (moving indoors / holding a room)
// Throws (grenade, smoke, flare) and foot gunner replacement are seconds long and claim
// nobody, but they never pick a man who is Committed() (RESCUE, STATIC or REARM).
// Withdrawal stays a group flag (BBB_Morale.IsWithdrawing), which every feature reads.
//  -bbbClaims 0   off: Can/Take/Holds always true, nothing is recorded
//
// Jobs (the one "who has this soldier or vehicle" registry, broad review 12 Nov): beside the roles,
// every BBB job that has a soldier or vehicle marks it here when it starts and unmarks it when it
// ends, and other features ask Busy(entity, job) instead of scanning that feature's own job list.
// Jobs don't arbitrate and don't lapse: they mirror the owner's list exactly (a count per job, so two
// entries for one entity need two Unmarks), and -bbbClaims doesn't switch them off. Keys left by a
// deleted entity are dropped in Tick.
class BBB_Claim : Managed
{
 int m_iRole;
 float m_fUntil; // world time ms
 float m_fSince;
}

class BBB_Claims
{
 static const int NONE = 0;
 static const int BUILDINGS = 20;
 static const int FIRE = 30;
 static const int REARM = 40;
 static const int STATIC = 50;
 static const int RESCUE = 60;
 static const float TTL_MS = 3000;
 static bool s_bOn = true;
 protected static int s_iRead;
 protected static ref map<IEntity, ref BBB_Claim> s_Claims;
 static ref array<IEntity> s_aVanilla; // test only: soldiers who keep the old arbitration
 // Called by BBB_StaticInit at game start (static initializer budget: tools/check_static_init.py).
 static void BBB_InitStatics()
 {
  s_Claims = new map<IEntity, ref BBB_Claim>();
  s_aVanilla = {};
  s_Jobs = new map<IEntity, ref array<int>>();
 }
 // Jobs: soldiers
 static const int JOB_DRAG = 0;        // a healer dragging a casualty (BBB_Drag)
 static const int JOB_TEAM = 1;        // a specialist-team assistant on a leash, supply or pick-up errand (BBB_Teams)
 static const int JOB_PRISONER = 2;    // surrendered (BBB_Morale)
 // Jobs: vehicles and static weapons
 static const int JOB_STATIC_GUN = 3;  // a static weapon a gunner is walking to or manning (BBB_StaticWeapons)
 static const int JOB_GIVE_WAY = 4;    // a vehicle giving way to another (BBB_GiveWay)
 static const int JOB_PULL_OVER = 5;   // a vehicle pulling over or holding (BBB_PullOver)
 static const int JOB_UNSTICK = 6;     // a vehicle reversing out of a stall (BBB_Unstick)
 static const int JOB_PILOT_HOLD = 7;  // a vehicle whose driver seat BBB has unhooked (BBB_PilotHold)
 static const int JOB_DRIVE = 8;       // a vehicle BBB's scripted driver is driving (BBB_Drive)
 static const int JOB_COUNT = 9;
 protected static ref map<IEntity, ref array<int>> s_Jobs; // entity -> how many entries each job has for it

 static int s_iTaken;     // tests read these
 static int s_iPreempted;
 static int s_iRefused;

 static void Read()
 {
  if (!BBB_Settings.Due(s_iRead)) return;
  string value;
  s_bOn = !(BBB_Settings.Get("bbbClaims", value) && value == "0");
  PrintFormat("[BBB] ROLE CLAIMS enabled=%1 ttl=%2s", s_bOn, TTL_MS / 1000);
 }

 static void Stop()
 {
  s_Claims.Clear();
  s_aVanilla.Clear();
  // s_Jobs stays: each owner unmarks its own entries when it clears its list.
 }

 // The owning job started on him (or it): call once per entry the owner adds to its list.
 static void Mark(IEntity entity, int job)
 {
  if (!entity) return;
  array<int> counts = s_Jobs.Get(entity);
  if (!counts)
  {
   counts = {};
   for (int j = 0; j < JOB_COUNT; j++) counts.Insert(0);
   s_Jobs.Set(entity, counts);
  }
  counts[job] = counts[job] + 1;
 }

 // ...and ended: once per entry the owner removes.
 static void Unmark(IEntity entity, int job)
 {
  if (!entity) return;
  array<int> counts = s_Jobs.Get(entity);
  if (!counts || counts[job] <= 0) return;
  counts[job] = counts[job] - 1;
  for (int j = 0; j < JOB_COUNT; j++)
   if (counts[j] > 0) return;
  s_Jobs.Remove(entity);
 }

 // The owner cleared its whole list.
 static void UnmarkAll(int job)
 {
  for (int i = s_Jobs.Count() - 1; i >= 0; i--)
  {
   array<int> counts = s_Jobs.GetElement(i);
   counts[job] = 0;
   bool any = false;
   for (int j = 0; j < JOB_COUNT; j++)
    if (counts[j] > 0) any = true;
   if (!any) s_Jobs.RemoveElement(i);
  }
 }

 // Does this job have him (or it) now?
 static bool Busy(IEntity entity, int job)
 {
  if (!entity) return false;
  array<int> counts = s_Jobs.Get(entity);
  return counts && counts[job] > 0;
 }

 static string Name(int role)
 {
  if (role == RESCUE) return "rescue";
  if (role == STATIC) return "static gun";
  if (role == REARM) return "rearm";
  if (role == FIRE) return "fire and manoeuvre";
  if (role == BUILDINGS) return "building";
  return "none";
 }

 protected static bool Off(IEntity soldier)
 {
  return !s_bOn || !soldier || s_aVanilla.Contains(soldier);
 }

 protected static float Now() { return GetGame().GetWorld().GetWorldTime(); }

 // His live claim's role (NONE: free).
 static int Role(IEntity soldier)
 {
  if (Off(soldier)) return NONE;
  BBB_Claim claim = s_Claims.Get(soldier);
  if (!claim || claim.m_fUntil <= Now()) return NONE;
  return claim.m_iRole;
 }

 // May this role have him? (free, already his, or held by a lower role)
 static bool Can(IEntity soldier, int role)
 {
  if (Off(soldier)) return true;
  int held = Role(soldier);
  return held == NONE || held <= role;
 }

 // Take or renew him for this role. False: a higher role has him.
 static bool Take(IEntity soldier, int role, float ttlMs = TTL_MS)
 {
  if (Off(soldier)) return true;
  int held = Role(soldier);
  if (held > role) { s_iRefused++; return false; }
  float now = Now();
  BBB_Claim claim = s_Claims.Get(soldier);
  if (!claim)
  {
   claim = new BBB_Claim();
   s_Claims.Set(soldier, claim);
  }
  if (held != role)
  {
   claim.m_fSince = now;
   s_iTaken++;
   if (held != NONE)
   {
    s_iPreempted++;
    PrintFormat("[BBB] CLAIM %1: %2 takes him from %3", soldier, Name(role), Name(held));
    BBB_Think.Note(soldier, "Claims", Name(role) + " takes him from " + Name(held));
   }
  }
  claim.m_iRole = role;
  claim.m_fUntil = now + ttlMs;
  return true;
 }

 // Does this role hold him now? (always true when off, so owners keep their old flow)
 static bool Holds(IEntity soldier, int role)
 {
  if (Off(soldier)) return true;
  return Role(soldier) == role;
 }

 static void Release(IEntity soldier, int role)
 {
  if (!soldier) return;
  BBB_Claim claim = s_Claims.Get(soldier);
  if (claim && claim.m_iRole == role) s_Claims.Remove(soldier);
 }

 // Too busy for a few seconds' extra task (throw, hop into a vehicle turret).
 static bool Committed(IEntity soldier)
 {
  return Role(soldier) >= REARM;
 }

 // Once a tick: drop lapsed claims and the dead (by index: a decayed key can't be looked up).
 static void Tick()
 {
  for (int k = s_Jobs.Count() - 1; k >= 0; k--)
   if (!s_Jobs.GetKey(k)) s_Jobs.RemoveElement(k);
  if (s_Claims.IsEmpty()) return;
  float now = Now();
  for (int i = s_Claims.Count() - 1; i >= 0; i--)
  {
   IEntity soldier = s_Claims.GetKey(i);
   BBB_Claim claim = s_Claims.GetElement(i);
   if (!soldier || !claim || claim.m_fUntil <= now || !BBB.IsAlive(soldier)) s_Claims.RemoveElement(i);
  }
  for (int v = s_aVanilla.Count() - 1; v >= 0; v--)
   if (!s_aVanilla[v]) s_aVanilla.Remove(v);
 }

 static int Count() { return s_Claims.Count(); }
}
