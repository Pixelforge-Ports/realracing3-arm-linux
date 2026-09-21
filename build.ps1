param([switch]$NoCache)
$ErrorActionPreference = 'Stop'
$dockerCommand = Get-Command docker -ErrorAction SilentlyContinue
if (-not $dockerCommand) { throw 'Open Docker Desktop, then open a new PowerShell window and retry.' }
$engine = & $dockerCommand.Source info --format '{{.OSType}}'
if ($LASTEXITCODE -ne 0 -or $engine -ne 'linux') { throw 'Start Docker Desktop with Linux containers enabled.' }
Push-Location $PSScriptRoot
try {
    $buildArguments = @('build', '--progress=plain', '-t', 'realracing3-build', '-f', 'Dockerfile.build')
    if ($NoCache) { $buildArguments += '--no-cache' }
    $buildArguments += '.'
    & $dockerCommand.Source @buildArguments
    if ($LASTEXITCODE -ne 0) { throw 'Docker build environment failed; see the error above.' }
    & $dockerCommand.Source run --rm --mount "type=bind,source=$PSScriptRoot,target=/src" -w /src realracing3-build bash -lc 'make -j2 && make libs && bash package_portmaster.sh'
    if ($LASTEXITCODE -ne 0) { throw 'Port compilation or packaging failed; see the error above.' }
    Write-Host "Built: $PSScriptRoot/build/realracing3-portmaster.zip"
} finally { Pop-Location }
