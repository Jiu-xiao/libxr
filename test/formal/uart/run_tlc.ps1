[CmdletBinding()]
param(
  [string]$JarPath = "",
  [string]$Workers = "auto"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
  $PSNativeCommandUseErrorActionPreference = $false
}

$TlaVersion = "1.7.4"
$TlcVersion = "2.19"
$JarUrl =
  "https://github.com/tlaplus/tlaplus/releases/download/v1.7.4/tla2tools.jar"
$JarSha256 = "936A262061C914694DFD669A543BE24573C45D5AA0FF20A8B96B23D01E050E88"
$ServiceBrokenSignature = "Invariant NoLostEvents is violated"
$EphemeralRetrySignature = "Invariant TxWorkHasCarrier is violated"
$MissingSelfDispatchSignature = "Invariant DeferredWorkHasDispatcher is violated"
$BoundSubmitContextSignature = "Invariant TxWorkHasCarrier is violated"
$MovingBoundarySignature = "Action property ConfigBoundaryDoesNotMove is violated"
$SubmitAfterTakeSignature = "Invariant NoSubmitAfterTakeWitness is violated"
$OwnerRetakeSignature = "Invariant NoOwnerRetakeWitness is violated"
$NoRemaskSignature = "Invariant ScanRequiresMask is violated"
$LateStatusSignature = "Invariant StatusMatchesActiveGeneration is violated"
$EarlyQuiesceSignature =
  "Invariant QuiescenceRequiresSettledAbort is violated"
$AbortAbaSignature = "Invariant OldAbortCannotEnterNewAdmission is violated"
$StartWindowEarlyMutationSignature =
  "Invariant ExternalPathsArePublishOnly is violated"
$StartWindowDoubleTerminalSignature =
  "Invariant ErrorAbsorbsComplete is violated"
$StartWindowMissingFailureConfigSignature =
  "Invariant FailedHasConfigCarrier is violated"
$StartWindowStartedPreCommitWitnessSignature =
  "Invariant NoStartedPreCommitWindowWitness is violated"
$StartWindowFailedConfigPreCommitWitnessSignature =
  "Invariant NoFailedConfigPreCommitWindowWitness is violated"
$StartWindowFailedCallbackPreConfigWitnessSignature =
  "Invariant NoFailedCallbackPreConfigWindowWitness is violated"
$PipelineEarlyPromoteSignature = "Invariant PromotedPendingWasPublished is violated"
$PipelineDoubleFinishSignature = "Invariant FinishAtMostOnce is violated"
$PipelineRetryConsumeSignature = "Invariant RetryKeepsRecordOwned is violated"
$PipelineWrongCompletionSignature =
  "Action property CompletionIdentityMatchesActiveRecord is violated"
$PipelineOwnerPublicationSignature =
  "Action property NoWriterPublicationWhileOwnerWitness is violated"
$RxMovingBoundarySignature = "Invariant NoPostSnapshotLoss is violated"
$RxStaleReleaseEventSignature =
  "Invariant PostSnapshotDataReturnsEvent is violated"
$ConfigDrainMovingBoundarySignature = "Invariant PostBoundarySurvives is violated"
$ConfigDrainPendingPayloadSignature = "Invariant MetaDataAligned is violated"

$TlcCases = @(
  @{ Name = "SerializedServiceCorrect"; Module = "SerializedService";
    Config = "SerializedServiceCorrect.cfg"; ExpectedExitCode = 0 },
  @{ Name = "UartTxControl"; Module = "UartTxControl";
    Config = "UartTxControl.cfg"; ExpectedExitCode = 0 },
  @{ Name = "UartTxControlLiveness"; Module = "UartTxControl";
    Config = "UartTxControlLiveness.cfg"; ExpectedExitCode = 0 },
  @{ Name = "UartHardwareControl"; Module = "UartHardwareControl";
    Config = "UartHardwareControl.cfg"; ExpectedExitCode = 0 },
  @{ Name = "UartHardwareControlLiveness"; Module = "UartHardwareControl";
    Config = "UartHardwareControlLiveness.cfg"; ExpectedExitCode = 0 },
  @{ Name = "UartTxStartWindow"; Module = "UartTxStartWindow";
    Config = "UartTxStartWindow.cfg"; ExpectedExitCode = 0 },
  @{ Name = "UartTxPipeline"; Module = "UartTxPipeline";
    Config = "UartTxPipeline.cfg"; ExpectedExitCode = 0 },
  @{ Name = "UartTxPipelineLiveness"; Module = "UartTxPipeline";
    Config = "UartTxPipelineLiveness.cfg"; ExpectedExitCode = 0 },
  @{ Name = "UartRxSpsc"; Module = "UartRxSpsc";
    Config = "UartRxSpsc.cfg"; ExpectedExitCode = 0 },
  @{ Name = "UartRxSpscLiveness"; Module = "UartRxSpsc";
    Config = "UartRxSpscLiveness.cfg"; ExpectedExitCode = 0 },
  @{ Name = "UartTxConfigDrain"; Module = "UartTxConfigDrain";
    Config = "UartTxConfigDrain.cfg"; ExpectedExitCode = 0 },
  @{ Name = "UartTxConfigDrainLiveness"; Module = "UartTxConfigDrain";
    Config = "UartTxConfigDrainLiveness.cfg"; ExpectedExitCode = 0 },
  @{ Name = "SerializedServiceBroken"; Module = "SerializedService";
    Config = "SerializedServiceBroken.cfg"; ExpectedExitCode = 12;
    ExpectedText = $ServiceBrokenSignature },
  @{ Name = "UartTxControlEphemeralRetry"; Module = "UartTxControl";
    Config = "UartTxControlEphemeralRetry.cfg"; ExpectedExitCode = 12;
    ExpectedText = $EphemeralRetrySignature },
  @{ Name = "UartTxControlMissingSelfDispatch"; Module = "UartTxControl";
    Config = "UartTxControlMissingSelfDispatch.cfg"; ExpectedExitCode = 12;
    ExpectedText = $MissingSelfDispatchSignature },
  @{ Name = "UartTxControlBoundSubmitContext"; Module = "UartTxControl";
    Config = "UartTxControlBoundSubmitContext.cfg"; ExpectedExitCode = 12;
    ExpectedText = $BoundSubmitContextSignature },
  @{ Name = "UartTxControlMovingBoundary"; Module = "UartTxControl";
    Config = "UartTxControlMovingBoundary.cfg"; ExpectedExitCode = 13;
    ExpectedText = $MovingBoundarySignature },
  @{ Name = "UartTxControlSubmitAfterTake"; Module = "UartTxControl";
    Config = "UartTxControlSubmitAfterTake.cfg"; ExpectedExitCode = 12;
    ExpectedText = $SubmitAfterTakeSignature },
  @{ Name = "UartTxControlOwnerRetake"; Module = "UartTxControl";
    Config = "UartTxControlOwnerRetake.cfg"; ExpectedExitCode = 12;
    ExpectedText = $OwnerRetakeSignature },
  @{ Name = "UartHardwareControlNoRemask"; Module = "UartHardwareControl";
    Config = "UartHardwareControlNoRemask.cfg"; ExpectedExitCode = 12;
    ExpectedText = $NoRemaskSignature },
  @{ Name = "UartHardwareControlLateStatus"; Module = "UartHardwareControl";
    Config = "UartHardwareControlLateStatus.cfg"; ExpectedExitCode = 12;
    ExpectedText = $LateStatusSignature },
  @{ Name = "UartHardwareControlEarlyQuiesce"; Module = "UartHardwareControl";
    Config = "UartHardwareControlEarlyQuiesce.cfg"; ExpectedExitCode = 12;
    ExpectedText = $EarlyQuiesceSignature },
  @{ Name = "UartHardwareControlAbortABA"; Module = "UartHardwareControl";
    Config = "UartHardwareControlAbortABA.cfg"; ExpectedExitCode = 12;
    ExpectedText = $AbortAbaSignature },
  @{ Name = "UartTxStartWindowEarlyMutation"; Module = "UartTxStartWindow";
    Config = "UartTxStartWindowEarlyMutation.cfg"; ExpectedExitCode = 12;
    ExpectedText = $StartWindowEarlyMutationSignature },
  @{ Name = "UartTxStartWindowDoubleTerminal"; Module = "UartTxStartWindow";
    Config = "UartTxStartWindowDoubleTerminal.cfg"; ExpectedExitCode = 12;
    ExpectedText = $StartWindowDoubleTerminalSignature },
  @{ Name = "UartTxStartWindowMissingFailureConfig";
    Module = "UartTxStartWindow";
    Config = "UartTxStartWindowMissingFailureConfig.cfg"; ExpectedExitCode = 12;
    ExpectedText = $StartWindowMissingFailureConfigSignature },
  @{ Name = "UartTxStartWindowStartedPreCommitWitness";
    Module = "UartTxStartWindow";
    Config = "UartTxStartWindowStartedPreCommitWitness.cfg"; ExpectedExitCode = 12;
    ExpectedText = $StartWindowStartedPreCommitWitnessSignature },
  @{ Name = "UartTxStartWindowFailedConfigPreCommitWitness";
    Module = "UartTxStartWindow";
    Config = "UartTxStartWindowFailedConfigPreCommitWitness.cfg";
    ExpectedExitCode = 12;
    ExpectedText = $StartWindowFailedConfigPreCommitWitnessSignature },
  @{ Name = "UartTxStartWindowFailedCallbackPreConfigWitness";
    Module = "UartTxStartWindow";
    Config = "UartTxStartWindowFailedCallbackPreConfigWitness.cfg";
    ExpectedExitCode = 12;
    ExpectedText = $StartWindowFailedCallbackPreConfigWitnessSignature },
  @{ Name = "UartTxPipelineEarlyPromote"; Module = "UartTxPipeline";
    Config = "UartTxPipelineEarlyPromote.cfg"; ExpectedExitCode = 12;
    ExpectedText = $PipelineEarlyPromoteSignature },
  @{ Name = "UartTxPipelineTerminalDoubleFinish"; Module = "UartTxPipeline";
    Config = "UartTxPipelineTerminalDoubleFinish.cfg"; ExpectedExitCode = 12;
    ExpectedText = $PipelineDoubleFinishSignature },
  @{ Name = "UartTxPipelineRetryConsume"; Module = "UartTxPipeline";
    Config = "UartTxPipelineRetryConsume.cfg"; ExpectedExitCode = 12;
    ExpectedText = $PipelineRetryConsumeSignature },
  @{ Name = "UartTxPipelineWrongCompletion"; Module = "UartTxPipeline";
    Config = "UartTxPipelineWrongCompletion.cfg"; ExpectedExitCode = 13;
    ExpectedText = $PipelineWrongCompletionSignature },
  @{ Name = "UartTxPipelineOwnerPublication"; Module = "UartTxPipeline";
    Config = "UartTxPipelineOwnerPublication.cfg"; ExpectedExitCode = 13;
    ExpectedText = $PipelineOwnerPublicationSignature },
  @{ Name = "UartRxSpscMovingBoundary"; Module = "UartRxSpsc";
    Config = "UartRxSpscMovingBoundary.cfg"; ExpectedExitCode = 12;
    ExpectedText = $RxMovingBoundarySignature },
  @{ Name = "UartRxSpscStaleReleaseEvent"; Module = "UartRxSpsc";
    Config = "UartRxSpscStaleReleaseEvent.cfg"; ExpectedExitCode = 12;
    ExpectedText = $RxStaleReleaseEventSignature },
  @{ Name = "UartTxConfigDrainMovingBoundary"; Module = "UartTxConfigDrain";
    Config = "UartTxConfigDrainMovingBoundary.cfg"; ExpectedExitCode = 12;
    ExpectedText = $ConfigDrainMovingBoundarySignature },
  @{ Name = "UartTxConfigDrainPendingPayload"; Module = "UartTxConfigDrain";
    Config = "UartTxConfigDrainPendingPayload.cfg"; ExpectedExitCode = 12;
    ExpectedText = $ConfigDrainPendingPayloadSignature }
)

$RejectedTlcFailurePattern =
  "(?im)Parse Error|Semantic errors?:|TLC Bug:|Exception in thread|" +
  "OutOfMemoryError|StackOverflowError|^java\..*(Exception|Error):|" +
  "^Error: TLC (encountered|threw|was unable|cannot|could not)|" +
  "^Error: The configuration file|^Error: Cannot find source file"

function Assert-ExactConfigAllowlist {
  param([Parameter(Mandatory = $true)][array]$Cases)

  $expected = @($Cases | ForEach-Object { [string]$_.Config })
  $duplicates = @($expected | Group-Object | Where-Object { $_.Count -ne 1 })
  if ($duplicates.Count -ne 0) {
    $names = ($duplicates | ForEach-Object { $_.Name }) -join ", "
    throw "Duplicate TLC configs in runner allowlist: $names"
  }

  $actual = @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter "*.cfg" -File |
      ForEach-Object { $_.Name })
  $missing = @($expected | Where-Object { $_ -notin $actual } | Sort-Object)
  $unexpected = @($actual | Where-Object { $_ -notin $expected } | Sort-Object)
  if (($missing.Count -ne 0) -or ($unexpected.Count -ne 0)) {
    $missingText = if ($missing.Count -eq 0) { "<none>" } else { $missing -join ", " }
    $unexpectedText = if ($unexpected.Count -eq 0) {
      "<none>"
    } else {
      $unexpected -join ", "
    }
    throw "TLC config allowlist mismatch. Missing: $missingText. Unexpected: $unexpectedText."
  }
}

if ([string]::IsNullOrWhiteSpace($JarPath)) {
  if (-not [string]::IsNullOrWhiteSpace($env:TLA2TOOLS_JAR)) {
    $JarPath = $env:TLA2TOOLS_JAR
  } else {
    $cacheRoot = if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
      $env:LOCALAPPDATA
    } elseif (-not [string]::IsNullOrWhiteSpace($env:XDG_CACHE_HOME)) {
      $env:XDG_CACHE_HOME
    } elseif (-not [string]::IsNullOrWhiteSpace($HOME)) {
      Join-Path $HOME ".cache"
    } else {
      [IO.Path]::GetTempPath()
    }
    $JarPath = Join-Path $cacheRoot "libxr\tla-plus\$TlaVersion\tla2tools.jar"
  }
}

function Get-VerifiedJar {
  param([Parameter(Mandatory = $true)][string]$Path)

  if (-not (Test-Path -LiteralPath $Path)) {
    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    $download = "$Path.download.$PID"
    try {
      Write-Host "Downloading pinned TLA+ v$TlaVersion tools..."
      Invoke-WebRequest -UseBasicParsing -Uri $JarUrl -OutFile $download
      $downloadHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $download).Hash
      if ($downloadHash -ne $JarSha256) {
        throw "Downloaded jar SHA-256 mismatch: $downloadHash"
      }
      Move-Item -LiteralPath $download -Destination $Path
    } finally {
      if (Test-Path -LiteralPath $download) {
        Remove-Item -LiteralPath $download -Force
      }
    }
  }

  $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
  if ($actualHash -ne $JarSha256) {
    throw "Pinned jar SHA-256 mismatch at '$Path': $actualHash"
  }
  return (Resolve-Path -LiteralPath $Path).Path
}

function Invoke-TlcCase {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Module,
    [Parameter(Mandatory = $true)][string]$Config,
    [Parameter(Mandatory = $true)][int]$ExpectedExitCode,
    [string]$ExpectedText = ""
  )

  $metadata = Join-Path $script:RunRoot $Name
  $caseWorkers = if ($ExpectedExitCode -eq 0) { $Workers } else { "1" }
  $arguments = @(
    "-XX:+UseParallelGC",
    "-cp", $script:ResolvedJar,
    "tlc2.TLC",
    "-cleanup",
    "-metadir", $metadata,
    "-workers", $caseWorkers,
    "-config", $Config,
    $Module
  )

  Write-Host "`n== TLC: $Name =="
  $output = @(& $script:Java @arguments 2>&1 |
      ForEach-Object { $_.ToString() })
  $exitCode = $LASTEXITCODE
  $output | ForEach-Object { Write-Host $_ }
  $text = $output -join "`n"

  if ($exitCode -ne $ExpectedExitCode) {
    throw "$Name exited $exitCode; expected $ExpectedExitCode"
  }
  if ($text -match $RejectedTlcFailurePattern) {
    throw "$Name reported a parser, semantic, or TLC internal error"
  }

  if ($ExpectedExitCode -eq 0) {
    if ($ExpectedText -ne "") {
      throw "$Name has an invalid success-case failure signature"
    }
    if ($output -notcontains "Model checking completed. No error has been found.") {
      throw "$Name exited zero without TLC's successful completion marker"
    }
    Write-Host "PASS: $Name"
    return
  }

  if ([string]::IsNullOrWhiteSpace($ExpectedText)) {
    throw "$Name has no expected counterexample signature"
  }
  $expectedError = "Error: $ExpectedText."
  $errorLines = @($output | Where-Object { $_.StartsWith("Error:") })
  if (($errorLines.Count -ne 2) -or
      ($errorLines -notcontains $expectedError) -or
      ($errorLines -notcontains "Error: The behavior up to this point is:")) {
    throw "$Name did not produce only the expected counterexample '$expectedError'"
  }
  Write-Host "PASS: $Name produced the expected counterexample"
}

$script:Java = (Get-Command java -ErrorAction Stop).Source
$script:ResolvedJar = Get-VerifiedJar -Path $JarPath
$versionOutput = (& $script:Java -cp $script:ResolvedJar tlc2.TLC -help 2>&1) -join "`n"
if ($versionOutput -notmatch "Version\s+$([regex]::Escape($TlcVersion))\b") {
  throw "Expected TLC $TlcVersion in the pinned TLA+ v$TlaVersion jar"
}

$script:RunRoot = Join-Path ([IO.Path]::GetTempPath()) (
  "libxr-tlc-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $script:RunRoot | Out-Null

Push-Location $PSScriptRoot
try {
  Assert-ExactConfigAllowlist -Cases $TlcCases
  foreach ($case in $TlcCases) {
    Invoke-TlcCase @case
  }
  Write-Host "`nAll TLC expectations passed."
} finally {
  Pop-Location
  if (Test-Path -LiteralPath $script:RunRoot) {
    Remove-Item -LiteralPath $script:RunRoot -Recurse -Force
  }
}

$global:LASTEXITCODE = 0
