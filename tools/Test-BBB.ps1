# Independent BBB acceptance runner. Loads BBB + base game, never ACA.
[CmdletBinding()]
param(
 [ValidateSet('vanilla','rearm','seatdiag','seatfire','seatidle','look','firemaneuverperf','morale','smoke','staticweapons','grenades','featureaudit','reactions','crewturnout','driveralign','driveraligncomplete','commanderseat','think','flares','doors','orderleash','crewstays','gatelane','contactrelay','claims','teams','squad','fallback','tactic','assault','coverfire','flank','search','heldguns','vanillanodes','indoorhold','roadexit','roadexitiso','roadexiton','roadexitnear','armourrounds','launcherinf','launcherinfrhs','settings','seatrevoke','firediscipline','crewcallout','dragcover','crewleader','squadleader','squadjobs','squadteams','moraleace','zombies','armour','gunner','rearmweapon','compile')][string]$Preset = 'vanilla',
 [string]$AddonRoot,
 [string]$ServerPath,
 [ValidateRange(0,1200)][int]$WaitForServerSeconds = 120,
 [string]$ExtraArgs = '',
 # Folder holding RHS: Status Quo and its content packs (preset launcherinfrhs); searched if empty.
 [string]$RhsAddonsDir,
 # Folder holding ACE Core and ACE Captives (preset moraleace); searched if empty.
 [string]$AceAddonsDir,
 # Folder holding Creature Melee AI and Project Outbreak (preset zombies); searched if empty.
 [string]$ZombieAddonsDir,
 # Skip the shared test-server lock: for a caller that already holds it (a sequence run under
 # _wt/lockrun.sh sets BBB_LOCK_HELD=1, which does the same).
 [switch]$NoLock
)
$ErrorActionPreference = 'Stop'
if (!$AddonRoot) { $AddonRoot = Split-Path $PSScriptRoot -Parent }
$AddonRoot = [IO.Path]::GetFullPath($AddonRoot)
$project = Join-Path $AddonRoot 'addon.gproj'
if (!(Test-Path -LiteralPath $project) -or (Get-Content -LiteralPath $project -Raw) -notmatch 'GUID "BBB0261015000001"') { throw 'Not a BBB addon root.' }
# Cross-repo test-server lock (same protocol as _wt/lockrun.sh, which ACA's runs use): ACA and AiO
# watchers also start servers on port 2001. Wait while the lock is fresh (stale after 40 min) or an
# AiO request is pending, write "<owner> <date>", touch every 5 min, remove it only if still ours.
# Caller already holds it (lockrun.sh around a sequence): pass -NoLock or set BBB_LOCK_HELD=1.
$lockOwner = 'bbb-' + $PID
$lockHeld = $false
$lockFile = $env:BBB_LOCK_FILE   # test override only
if (!$lockFile) { $lockFile = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'my games/ArmaReforgerWorkbench/_wt/test-server.lock' }
# The Workbench addons folder holding AIOCommandMenu: two levels up from the main checkout, further
# from a worktree (.claude/worktrees/<name>).
$aioRequests = Join-Path (Split-Path (Split-Path $AddonRoot -Parent) -Parent) 'AIOCommandMenu/.aio-requests'
for ($up = Split-Path $AddonRoot -Parent; $up; $up = Split-Path $up -Parent) {
 if (Test-Path -LiteralPath (Join-Path $up 'AIOCommandMenu')) { $aioRequests = Join-Path $up 'AIOCommandMenu/.aio-requests'; break }
}
if ($env:BBB_AIO_REQUESTS) { $aioRequests = $env:BBB_AIO_REQUESTS }   # test override only
$lockTouched = [DateTime]::UtcNow
function Test-BbbLockOurs {
 if (!(Test-Path -LiteralPath $lockFile)) { return $false }
 $line = Get-Content -LiteralPath $lockFile -TotalCount 1 -ErrorAction SilentlyContinue
 return ($line -is [string]) -and $line.StartsWith($lockOwner + ' ')
}
function Update-BbbLock {
 if (!$lockHeld -or ([DateTime]::UtcNow - $lockTouched).TotalSeconds -lt 300) { return }
 if (Test-BbbLockOurs) { (Get-Item -LiteralPath $lockFile).LastWriteTimeUtc = [DateTime]::UtcNow }
 $script:lockTouched = [DateTime]::UtcNow
}
if (!$NoLock -and $env:BBB_LOCK_HELD -ne '1') {
 New-Item -ItemType Directory -Path (Split-Path $lockFile -Parent) -Force | Out-Null
 for ($i = 0; $i -lt 120; $i++) {
  if ((Test-Path -LiteralPath $lockFile) -and ([DateTime]::UtcNow - (Get-Item -LiteralPath $lockFile).LastWriteTimeUtc).TotalSeconds -lt 2400) { Start-Sleep -Seconds 30; continue }
  if (@(Get-ChildItem -Path (Join-Path $aioRequests '*.request') -ErrorAction SilentlyContinue).Count -gt 0) { Start-Sleep -Seconds 30; continue }
  Set-Content -LiteralPath $lockFile -Value ($lockOwner + ' ' + (Get-Date).ToString('R')) -Encoding ASCII
  Start-Sleep -Milliseconds 200   # a racing writer overwrites; the check below tells who won
  $lockHeld = Test-BbbLockOurs
  if ($lockHeld) { break }
 }
 if (!$lockHeld) { throw 'BBB: could not get the shared test-server lock (waited 60 min).' }
 $lockTouched = [DateTime]::UtcNow
}
# Per-run -addonsDir folders (see $stageDir below). Only ever delete the junction itself
# (non-recursive), never through it: that would delete the addon it points to.
$stageBase = Join-Path ([IO.Path]::GetTempPath()) 'bbb-test-addons'
function Clear-BbbStage {
 foreach ($dir in @(Get-ChildItem -LiteralPath $stageBase -Directory -ErrorAction SilentlyContinue)) {
  try {
   $link = Join-Path $dir.FullName 'BetterBattleBuddies'
   if (Test-Path -LiteralPath $link) {
    if (!((Get-Item -LiteralPath $link -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { continue }
    [IO.Directory]::Delete($link, $false)
   }
   [IO.Directory]::Delete($dir.FullName, $false)
  } catch { Write-Warning ('BBB: left ' + $dir.FullName + ': ' + $_) }
 }
}
try {
# Serialize BBB runners before the free-port check; two quiet waits can race.
$serverMutex = New-Object System.Threading.Mutex($false, 'Local\BBB_Acceptance_Server_2001')
$serverMutexHeld = $false
try { $serverMutexHeld = $serverMutex.WaitOne($WaitForServerSeconds * 1000) }
catch [System.Threading.AbandonedMutexException] { $serverMutexHeld = $true }
if (!$serverMutexHeld) { $serverMutex.Dispose(); throw 'Another BBB acceptance runner owns the server slot.' }
try {
function Test-BbbServerBusy {
 if (Get-Process -Name 'ArmaReforgerServ*' -ErrorAction SilentlyContinue) { return $true }
 # A listen host can own 2001 without a dedicated-server process.
 return @([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveUdpListeners() | Where-Object Port -eq 2001).Count -gt 0
}
$serverDeadline = [DateTime]::UtcNow.AddSeconds($WaitForServerSeconds)
$quietSince = $null
while ($true) {
 if (Test-BbbServerBusy) {
  $quietSince = $null
  if ([DateTime]::UtcNow -ge $serverDeadline) { throw 'Another host/server holds port 2001; let it finish first.' }
 } else {
  if (!$quietSince) { $quietSince = [DateTime]::UtcNow }
  # Let the shared launcher start its next queued test before claiming the port.
  if (([DateTime]::UtcNow - $quietSince).TotalSeconds -ge 5) { break }
 }
 Start-Sleep -Milliseconds 400
}
if (!$ServerPath) {
 $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
 $libraries = @($steam, 'E:\SteamLibrary', 'C:\Program Files (x86)\Steam')
 if ($steam) {
  $vdf = Join-Path $steam 'steamapps/libraryfolders.vdf'
  if (Test-Path -LiteralPath $vdf) {
   foreach ($match in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]+)"')) { $libraries += $match.Groups[1].Value.Replace('\\', '\') }
  }
 }
 foreach ($library in $libraries) {
  if (!$library) { continue }
  $candidate = Join-Path $library 'steamapps/common/Arma Reforger Server/ArmaReforgerServer.exe'
  if (Test-Path -LiteralPath $candidate) { $ServerPath = $candidate; break }
 }
}
if (!$ServerPath -or !(Test-Path -LiteralPath $ServerPath)) { throw 'Install Arma Reforger Server or supply -ServerPath.' }
$root = Split-Path $PSScriptRoot -Parent
$id = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $Preset
$profile = Join-Path $root ('.local-testserver/' + $id)
$resultDir = Join-Path $root '.bbb-results'
New-Item -ItemType Directory -Path $profile,$resultDir -Force | Out-Null
$flags = @{ vanilla = '-bbbVanillaTest 1'; rearm = '-bbbRearmTest 1'; seatdiag = '-bbbSeatDiag 1 -bbbTurretTrace 1'; seatfire = '-bbbSeatDiag 1 -bbbTurretTrace 1 -bbbSeatDiagFireOnly 1'; seatidle = '-bbbSeatDiag 1 -bbbTurretTrace 1 -bbbSeatDiagIdleOnly 1'; look = '-acrBbbLookTest 1'; firemaneuverperf = '-acrBbbFireManeuverPerfTest 1'; morale = '-bbbMoraleTest 1 -bbbPerf 1'; smoke = '-bbbSmokeTest 1 -bbbPerf 1'; staticweapons = '-bbbStaticWeaponsTest 1 -bbbPerf 1'; grenades = '-bbbGrenadeTest 1 -bbbPerf 1'; featureaudit = '-bbbFeatureAuditTest 1 -bbbPerf 1'; reactions = '-bbbReactionTest 1 -bbbPerf 1'; compile = '' }
$timeouts = @{ vanilla = 100; rearm = 65; seatdiag = 330; seatfire = 150; seatidle = 150; look = 90; firemaneuverperf = 160; morale = 180; smoke = 120; staticweapons = 120; grenades = 120; featureaudit = 200; reactions = 160; compile = 45 }
$sourceSnapshot = Join-Path $resultDir ($id + '.source.json')
$sourceFiles = @('Scripts','Configs/BBB','Prefabs/BBB','Prefabs/BBB_Tests') | ForEach-Object {
 $sourcePath = Join-Path $AddonRoot $_
 if (Test-Path -LiteralPath $sourcePath) { Get-ChildItem -LiteralPath $sourcePath -File -Recurse }
}
@($sourceFiles | Sort-Object FullName | ForEach-Object {
 [PSCustomObject]@{ path = $_.FullName.Substring($AddonRoot.Length + 1); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
}) | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $sourceSnapshot -Encoding UTF8
$flags.crewturnout = '-bbbVehiclePostureTest 1 -bbbVehiclePostureCase turnout -bbbPerf 1'
$flags.driveralign = '-bbbVehiclePostureTest 1 -bbbVehiclePostureCase alignment -bbbPerf 1'
$timeouts.crewturnout = 190
$timeouts.driveralign = 110
$flags.driveraligncomplete = '-bbbVehiclePostureTest 1 -bbbVehiclePostureCase alignmentcomplete -bbbPerf 1'
$timeouts.driveraligncomplete = 110
$flags.commanderseat = '-bbbCommanderSeatTest 1'
$timeouts.commanderseat = 70
$flags.flares = '-bbbFlareTest 1 -bbbPerf 1'
$timeouts.flares = 150
$flags.think = '-bbbThinkTest 1 -bbbThink 2'
$timeouts.think = 130
$flags.orderleash = '-bbbOrderLeashTest 1 -bbbPerf 1'
$timeouts.orderleash = 190
$flags.doors = '-bbbDoorTest 1 -bbbPerf 1'
$timeouts.doors = 240
$flags.crewstays = '-bbbCrewStaysTest 1 -bbbPerf 1'
$timeouts.crewstays = 120
$flags.gatelane = '-bbbGateLaneTest 1'
$timeouts.gatelane = 90
$flags.contactrelay = '-bbbContactRelayTest 1 -bbbPerf 1'
$timeouts.contactrelay = 230
$flags.claims = '-bbbClaimsTest 1 -bbbPerf 1'
$flags.teams = '-bbbTeamsTest 1 -bbbPerf 1'
$timeouts.teams = 200
$timeouts.claims = 150
$flags.squad = '-bbbSquadTest 1 -bbbPerf 1'
$timeouts.squad = 200
$flags.fallback = '-bbbFallbackTest 1 -bbbPerf 1'
$timeouts.fallback = 160
$flags.tactic = '-bbbTacticTest 1 -bbbPerf 1'
$timeouts.tactic = 230
$flags.assault = '-bbbAssaultTest 1 -bbbPerf 1'
$timeouts.assault = 290
$flags.coverfire = '-bbbStage4Test 1 -bbbStage4Case fire -bbbPerf 1'
$timeouts.coverfire = 230
$flags.flank = '-bbbStage4Test 1 -bbbStage4Case flank -bbbPerf 1'
$timeouts.flank = 260
$flags.search = '-bbbStage4Test 1 -bbbStage4Case search -bbbPerf 1'
$timeouts.search = 230
$flags.heldguns = '-bbbHeldGunsTest 1 -bbbStaticQuiet 5 -bbbPerf 1'
$timeouts.heldguns = 260
$flags.vanillanodes = '-bbbVanillaNodesTest 1'
$timeouts.vanillanodes = 150
$flags.indoorhold = '-bbbIndoorHoldTest 1 -bbbPerf 1'
$timeouts.indoorhold = 200
$flags.roadexit = '-bbbRoadExitTest 1 -bbbPerf 1'
$timeouts.roadexit = 220
$flags.roadexitiso = '-bbbRoadExitTest 1 -bbbRoadExitIsolate 1 -bbbPerf 1'
$timeouts.roadexitiso = 220
$flags.roadexiton = '-bbbRoadExitTest 1 -bbbRoadExitOnFirst 1 -bbbPerf 1'
$timeouts.roadexiton = 220
$flags.roadexitnear = '-bbbRoadExitTest 1 -bbbRoadExitNear 1 -bbbPerf 1'
$timeouts.roadexitnear = 220
$flags.armourrounds = '-bbbArmourRoundsTest 1'
$timeouts.armourrounds = 150
$flags.settings = '-bbbSettingsTest 1 -bbbStaticArc 120'
$flags.launcherinf = '-bbbLauncherInfantryTest 1'
$timeouts.launcherinf = 240
$flags.launcherinfrhs = '-bbbLauncherInfantryTest 1 -bbbLauncherInfantryRhs 1'
$timeouts.launcherinfrhs = 660
$timeouts.settings = 70
$flags.seatrevoke = '-bbbSeatRevokeTest 1 -bbbTurretTrace 1'
$timeouts.seatrevoke = 260
$flags.firediscipline = '-bbbFireDisciplineTest 1 -bbbPerf 1'
$timeouts.firediscipline = 250
$flags.crewcallout = '-bbbCrewCalloutTest 1 -bbbPerf 1'
$timeouts.crewcallout = 260
$flags.dragcover = '-bbbDragCoverTest 1'
$flags.rearmweapon = '-bbbRearmWeaponTest 1 -bbbPerf 1'
$timeouts.rearmweapon = 170
$timeouts.dragcover = 120
$flags.crewleader = '-bbbCrewLeaderTest 1 -bbbPerf 1'
$timeouts.crewleader = 230
$flags.squadleader = '-bbbSquadLeaderTest 1 -bbbPerf 1'
$timeouts.squadleader = 200
$flags.squadjobs = '-bbbSquadJobsTest 1 -bbbPerf 1'
$timeouts.squadjobs = 200
$flags.squadteams = '-bbbSquadTeamsTest 1 -bbbPerf 1'
$flags.moraleace = '-bbbMoraleTest 1 -bbbMoraleAce 1 -bbbPerf 1'
$timeouts.moraleace = 180
$flags.zombies = '-bbbZombiesTest 1 -bbbPerf 1'
$timeouts.zombies = 260
$timeouts.squadteams = 230
# A/B measurements (about 15 and 27 min): the VERDICT grades only their unit checks.
$flags.armour = '-bbbArmourTest 1'
$timeouts.armour = 1080
$flags.gunner = '-bbbGunnerTest 1'
$timeouts.gunner = 1800
$world ='{AC230926A00100E0}Worlds/BBB_TestArland.ent'
if ($Preset -eq 'compile') { $world = '{77C41EA429B6AC35}worlds/GameMaster/GM_Arland.ent' }
# The game loads the first addon with BBB's GUID it finds in -addonsDir, and the parent folder can
# hold several: every sibling worktree under .claude/worktrees has the same GUID (9 Nov: a run from
# one worktree tested another). So -addonsDir is a per-run folder outside the addon holding only a
# junction to $AddonRoot, and the run fails if the log shows BBB loaded from anywhere else.
$parentDir = Split-Path $AddonRoot -Parent
Clear-BbbStage
$stageDir = Join-Path $stageBase $id
$stagedRoot = Join-Path $stageDir 'BetterBattleBuddies'
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
New-Item -ItemType Junction -Path $stagedRoot -Target $AddonRoot | Out-Null
$addonsDir = $stageDir
$addons = 'BBB0261015000001'
if ($Preset -eq 'launcherinfrhs') {
 if (!$RhsAddonsDir) {
  foreach ($candidate in @($parentDir, (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games/ArmaReforger/addons'), 'E:\ArmaReforgerMods\addons')) {
   if ($candidate -and (Test-Path -LiteralPath $candidate) -and (Get-ChildItem -LiteralPath $candidate -Directory -Filter 'RHS-StatusQuo_*' -ErrorAction SilentlyContinue)) { $RhsAddonsDir = $candidate; break }
  }
 }
 if (!$RhsAddonsDir) { throw 'RHS: Status Quo not found; supply -RhsAddonsDir.' }
 if ($RhsAddonsDir -ne $addonsDir) { $addonsDir += ',' + $RhsAddonsDir }
 $addons += ',595F2BF2F44836FB,1337C0DE5DABBEEF,BADC0DEDABBEDA5E'
}
if ($Preset -eq 'moraleace') {
 # The only preset that loads another mod's surrender: ACE Core + ACE Captives (GPL, never shipped with BBB).
 if (!$AceAddonsDir) {
  foreach ($candidate in @($parentDir, (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games/ArmaReforger/addons'), 'E:\ArmaReforgerMods\addons')) {
   if ($candidate -and (Test-Path -LiteralPath $candidate) -and (Get-ChildItem -LiteralPath $candidate -Directory -Filter 'ACECaptives_*' -ErrorAction SilentlyContinue)) { $AceAddonsDir = $candidate; break }
  }
 }
 if (!$AceAddonsDir) { throw 'ACE Captives not found; supply -AceAddonsDir.' }
 if ($AceAddonsDir -ne $addonsDir) { $addonsDir += ',' + $AceAddonsDir }
 $addons += ',60C4CE4888FF4621,646D52AF8BB3FF15'
}
if ($Preset -eq 'zombies') {
 # Creature Melee AI + Project Outbreak, found at runtime by BBB (no dependency, never shipped with BBB).
 if (!$ZombieAddonsDir) {
  foreach ($candidate in @($parentDir, (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games/ArmaReforger/addons'), 'E:\ArmaReforgerMods\addons')) {
   if ($candidate -and (Test-Path -LiteralPath $candidate) -and (Get-ChildItem -LiteralPath $candidate -Directory -Filter 'ProjectOutbreak*' -ErrorAction SilentlyContinue)) { $ZombieAddonsDir = $candidate; break }
  }
 }
 if (!$ZombieAddonsDir) { throw 'Project Outbreak not found; supply -ZombieAddonsDir.' }
 if ($ZombieAddonsDir -ne $addonsDir) { $addonsDir += ',' + $ZombieAddonsDir }
 $addons += ',4FD4A1BD550793F4,F1D4B9B5245B36ED'
}
$argsLine = '-server "' + $world + '" -addonsDir "' + $addonsDir + '" -addons ' + $addons + ' -profile "' + $profile + '" -maxFPS 60 -noBackend ' + $flags[$Preset] + ' ' + $ExtraArgs
$process = Start-Process -FilePath $ServerPath -ArgumentList $argsLine -WorkingDirectory (Split-Path $ServerPath -Parent) -WindowStyle Hidden -PassThru
$started = Get-Date
$status = 'timeout'
$log = $null
$body = ''
try {
 while (((Get-Date) - $started).TotalSeconds -lt $timeouts[$Preset]) {
  Start-Sleep -Seconds 2
  Update-BbbLock
  if (!$log) { $log = Get-ChildItem -LiteralPath $profile -Filter console.log -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 }
  if ($log) {
   $body = Get-Content -LiteralPath $log.FullName -Raw
   if ($body -match "SCRIPT\s+\(E\)|Can't compile|Unable to start replication") { $status = 'error'; break }
   $loaded = [regex]::Match($body, "gproj: '([^']+)' guid: 'BBB0261015000001'")
   if ($loaded.Success) {
    $loadedDir = Split-Path ([IO.Path]::GetFullPath($loaded.Groups[1].Value)) -Parent
    if (!$loadedDir.Equals($stagedRoot, [StringComparison]::OrdinalIgnoreCase) -and !$loadedDir.Equals($AddonRoot, [StringComparison]::OrdinalIgnoreCase)) { $status = 'wrong addon: ' + $loaded.Groups[1].Value; break }
   }
   if ($Preset -eq 'compile' -and $body -match '\[BBB\] started on its own') { $status = 'completed'; break }
   if ($body -match '\[ACR\] TEST COMPLETE') {
    $status = 'failed'
    if ($body -match '\[ACR\] VERDICT PASS\b' -and $body -notmatch '\[ACR\] VERDICT FAIL\b') { $status = 'completed' }
    break
   }
  }
  if ($process.HasExited) { $status = 'server exited'; break }
 }
} finally {
 if (!$process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
 $process.WaitForExit(15000) | Out-Null
}
$result = Join-Path $resultDir ($id + '.result.txt')
$sha = 'uncommitted extraction'
$sourceHead = & git -C $root rev-parse --verify HEAD
if ($LASTEXITCODE -eq 0) { $sha = $sourceHead.Trim() }
if (& git -C $root status --porcelain -- Scripts Configs Prefabs) { $sha += '+dirty' }
@("status=$status", "preset=$Preset", "source=$sha", "addonRoot=$AddonRoot", "sourceManifest=$sourceSnapshot", "args=$argsLine", "log=$log", $body) | Set-Content -LiteralPath $result -Encoding UTF8
Write-Output "BBB $Preset : $status; result=$result"
if ($status -ne 'completed') { exit 1 }

} finally {
 if ($serverMutexHeld) { Clear-BbbStage }
 if ($serverMutexHeld) { $serverMutex.ReleaseMutex() }
 $serverMutex.Dispose()
}

} finally {
 if ($lockHeld -and (Test-BbbLockOurs)) { Remove-Item -LiteralPath $lockFile -Force -ErrorAction SilentlyContinue }
}
