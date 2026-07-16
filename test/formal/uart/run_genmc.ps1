[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
  $PSNativeCommandUseErrorActionPreference = $false
}

$ImageDigest = "sha256:f9a74c13505b07a0cfcc96d5c46813d975543a081d56449cd529ba2a2f9d4791"
$ImageRef = "genmc/genmc@$ImageDigest"
$GenMcVersion = "v0.17.0"
$GenMcBinary = "/usr/local/bin/genmc/genmc"
$SuccessSignature = "No errors were detected."

$script:Docker = (Get-Command docker -ErrorAction Stop).Source
$script:RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path

function Initialize-GenMcImage {
  & $script:Docker image inspect $ImageRef *> $null
  if ($LASTEXITCODE -ne 0) {
    Write-Host "Pulling pinned GenMC image $ImageRef..."
    & $script:Docker pull $ImageRef
    if ($LASTEXITCODE -ne 0) {
      throw "Unable to pull pinned GenMC image $ImageRef"
    }
  }

  $repoDigests = (& $script:Docker image inspect `
    --format "{{json .RepoDigests}}" $ImageRef 2>&1) -join "`n"
  if ($LASTEXITCODE -ne 0 -or -not $repoDigests.Contains($ImageRef)) {
    throw "Local GenMC image does not contain the pinned digest $ImageRef"
  }

  $versionOutput = (& $script:Docker run --rm --pull=never $ImageRef `
    $GenMcBinary --version 2>&1) -join "`n"
  if ($LASTEXITCODE -ne 0 -or -not $versionOutput.Contains("GenMC $GenMcVersion")) {
    throw "Expected GenMC $GenMcVersion at $GenMcBinary"
  }
  Write-Host "Using GenMC $GenMcVersion from $ImageRef"
}

function Invoke-GenMcCase {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string[]]$GenMcOptions,
    [Parameter(Mandatory = $true)][string[]]$CompilerOptions,
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][bool]$ExpectSuccess,
    [string]$ExpectedText = ""
  )

  $arguments = @(
    "run", "--rm", "--pull=never",
    "-v", "${script:RepoRoot}:/work:ro",
    "-w", "/tmp",
    $ImageRef,
    $GenMcBinary
  ) + $GenMcOptions + @("--") + $CompilerOptions + @($Source)

  Write-Host "`n== GenMC: $Name =="
  $previousErrorAction = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    $output = & $script:Docker @arguments 2>&1
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorAction
  }
  $outputLines = @($output | ForEach-Object { $_.ToString() })
  $outputLines | ForEach-Object { Write-Host $_ }
  $text = $outputLines -join "`n"

  if ($ExpectSuccess) {
    if ($exitCode -ne 0) {
      throw "$Name failed with exit code $exitCode"
    }
    if (-not $text.Contains($SuccessSignature)) {
      throw "$Name exited zero without '$SuccessSignature'"
    }
    Write-Host "PASS: $Name"
    return
  }

  if ($exitCode -eq 0) {
    throw "$Name unexpectedly passed"
  }
  if (-not $text.Contains($ExpectedText)) {
    throw "$Name failed without the expected signature '$ExpectedText'"
  }
  Write-Host "PASS: $Name produced the expected violation"
}

Initialize-GenMcImage

$CommonFlags = @("-std=c++20", "-pthread")
$GateFlags = @(
  "-std=c++20",
  "-pthread",
  "-fno-threadsafe-statics",
  "-D_BITS_PTHREADTYPES_COMMON_H=1",
  "-I/work/src/driver/model",
  "-I/work/src/core",
  "-I/work/src/core/assert"
)

Invoke-GenMcCase -Name "AtomicFrontendRC11" `
  -GenMcOptions @("--rc11") `
  -CompilerOptions $CommonFlags `
  -Source "/work/test/formal/uart/common/atomic_frontend_probe.cpp" `
  -ExpectSuccess $true

Invoke-GenMcCase -Name "AtomicFrontendBroken" `
  -GenMcOptions @("--rc11") `
  -CompilerOptions ($CommonFlags + @("-DBROKEN_MEMORY_ORDER=1")) `
  -Source "/work/test/formal/uart/common/atomic_frontend_probe.cpp" `
  -ExpectSuccess $false -ExpectedText "Safety violation"

Invoke-GenMcCase -Name "SerializedServiceLiveness" `
  -GenMcOptions @("--rc11", "--check-liveness") `
  -CompilerOptions ($CommonFlags + @("-I/work/src/driver/model")) `
  -Source "/work/test/formal/uart/genmc/serialized_service.cpp" `
  -ExpectSuccess $true

Invoke-GenMcCase -Name "UartHardwareGateSafety" `
  -GenMcOptions @("--rc11") `
  -CompilerOptions $GateFlags `
  -Source "/work/test/formal/uart/genmc/uart_hardware_gate.cpp" `
  -ExpectSuccess $true

Invoke-GenMcCase -Name "UartHardwareGateLiveness" `
  -GenMcOptions @("--rc11", "--check-liveness") `
  -CompilerOptions $GateFlags `
  -Source "/work/test/formal/uart/genmc/uart_hardware_gate.cpp" `
  -ExpectSuccess $true

Invoke-GenMcCase -Name "UartHardwareGateNestedSafety" `
  -GenMcOptions @("--rc11") `
  -CompilerOptions $GateFlags `
  -Source "/work/test/formal/uart/genmc/uart_hardware_gate_nested.cpp" `
  -ExpectSuccess $true

Invoke-GenMcCase -Name "UartHardwareGateNestedLiveness" `
  -GenMcOptions @("--rc11", "--check-liveness") `
  -CompilerOptions $GateFlags `
  -Source "/work/test/formal/uart/genmc/uart_hardware_gate_nested.cpp" `
  -ExpectSuccess $true

Write-Host "`nAll GenMC expectations passed."
