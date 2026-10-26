# Independent BBB acceptance runner. Loads BBB + base game, never ACA.
[CmdletBinding()]
param(
 [ValidateSet('vanilla','rearm','seatdiag','seatfire','seatidle','look','firemaneuverperf','morale','smoke','staticweapons','grenades','featureaudit','reactions','crewturnout','driveralign','driveraligncomplete','commanderseat','think','flares','doors','orderleash','crewstays','gatelane','contactrelay','claims','squad','tactic','coverfire','flank','search','heldguns','vanillanodes','compile')][string]$Preset = 'vanilla',
 [string]$AddonRoot,
 [string]$ServerPath,
 [ValidateRange(0,1200)][int]$WaitForServerSeconds = 120,
 [string]$ExtraArgs = ''
)
$ErrorActionPreference = 'Stop'
if (!$AddonRoot) { $AddonRoot = Split-Path $PSScriptRoot -Parent }
$AddonRoot = [IO.Path]::GetFullPath($AddonRoot)
$project = Join-Path $AddonRoot 'addon.gproj'
if (!(Test-Path -LiteralPath $project) -or (Get-Content -LiteralPath $project -Raw) -notmatch 'GUID "BBB0261015000001"') { throw 'Not a BBB addon root.' }
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
$timeouts = @{ vanilla = 100; rearm = 65; seatdiag = 330; seatfire = 150; seatidle = 150; look = 90; firemaneuverperf = 160; morale = 120; smoke = 120; staticweapons = 120; grenades = 120; featureaudit = 200; reactions = 160; compile = 45 }
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
$timeouts.crewturnout = 120
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
$timeouts.doors = 180
$flags.crewstays = '-bbbCrewStaysTest 1 -bbbPerf 1'
$timeouts.crewstays = 120
$flags.gatelane = '-bbbGateLaneTest 1'
$timeouts.gatelane = 90
$flags.contactrelay = '-bbbContactRelayTest 1 -bbbPerf 1'
$timeouts.contactrelay = 230
$flags.claims = '-bbbClaimsTest 1 -bbbPerf 1'
$timeouts.claims = 150
$flags.squad = '-bbbSquadTest 1 -bbbPerf 1'
$timeouts.squad = 200
$flags.tactic = '-bbbTacticTest 1 -bbbPerf 1'
$timeouts.tactic = 230
$flags.coverfire = '-bbbStage4Test 1 -bbbStage4Case fire -bbbPerf 1'
$timeouts.coverfire = 230
$flags.flank = '-bbbStage4Test 1 -bbbStage4Case flank -bbbPerf 1'
$timeouts.flank = 260
$flags.search = '-bbbStage4Test 1 -bbbStage4Case search -bbbPerf 1'
$timeouts.search = 230
$flags.heldguns = '-bbbHeldGunsTest 1 -bbbStaticQuiet 5 -bbbPerf 1'
$timeouts.heldguns = 160
$flags.vanillanodes = '-bbbVanillaNodesTest 1'
$timeouts.vanillanodes = 150
$world = '{AC230926A00100E0}Worlds/BBB_TestArland.ent'
if ($Preset -eq 'compile') { $world = '{77C41EA429B6AC35}worlds/GameMaster/GM_Arland.ent' }
$argsLine = '-server "' + $world + '" -addonsDir "' + (Split-Path $AddonRoot -Parent) + '" -addons BBB0261015000001 -profile "' + $profile + '" -maxFPS 60 -noBackend ' + $flags[$Preset] + ' ' + $ExtraArgs
$process = Start-Process -FilePath $ServerPath -ArgumentList $argsLine -WorkingDirectory (Split-Path $ServerPath -Parent) -WindowStyle Hidden -PassThru
$started = Get-Date
$status = 'timeout'
$log = $null
$body = ''
try {
 while (((Get-Date) - $started).TotalSeconds -lt $timeouts[$Preset]) {
  Start-Sleep -Seconds 2
  if (!$log) { $log = Get-ChildItem -LiteralPath $profile -Filter console.log -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 }
  if ($log) {
   $body = Get-Content -LiteralPath $log.FullName -Raw
   if ($body -match "SCRIPT\s+\(E\)|Can't compile|Unable to start replication") { $status = 'error'; break }
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
 if ($serverMutexHeld) { $serverMutex.ReleaseMutex() }
 $serverMutex.Dispose()
}
