[CmdletBinding()]
param(
    [string]$PdbPath = "",
    [string]$SymbolStore = "\\172.21.10.34\Symbols",
    [string]$BuildId = "",
    [string]$Product = "KraftonEngine",
    [switch]$SkipPublish
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$EngineRoot = (Resolve-Path (Join-Path $RepoRoot "KraftonEngine")).Path.TrimEnd("\")
$LocalSymbolStore = Join-Path $RepoRoot "Symbols"

if ([string]::IsNullOrWhiteSpace($PdbPath)) {
    $PdbPath = Join-Path $EngineRoot "Bin\Release\KraftonEngine.pdb"
}
$PdbPath = (Resolve-Path $PdbPath).Path

if ([string]::IsNullOrWhiteSpace($BuildId)) {
    $BuildId = "Release-x64-" + (Get-Date -Format "yyyyMMdd-HHmmss")
}

$SrcSrvTools = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Debuggers\x64\srcsrv"
$PdbStr = Join-Path $SrcSrvTools "pdbstr.exe"
$SrcTool = Join-Path $SrcSrvTools "srctool.exe"
$SymStore = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Debuggers\x64\symstore.exe"

foreach ($Tool in @($PdbStr, $SrcTool, $SymStore)) {
    if (-not (Test-Path -LiteralPath $Tool)) {
        throw "Required Debugging Tools executable was not found: $Tool"
    }
}

$SnapshotRoot = Join-Path $LocalSymbolStore ("SourceStore\" + $BuildId)
$IndexRoot = Join-Path $LocalSymbolStore ("SourceIndex\" + $BuildId)
$StreamPath = Join-Path $IndexRoot ($Product + ".srcsrv")
$ReadbackPath = Join-Path $IndexRoot ($Product + ".readback.srcsrv")
$PdbBackupPath = Join-Path $IndexRoot ($Product + ".pre-srcsrv.pdb")

New-Item -ItemType Directory -Path $SnapshotRoot -Force | Out-Null
New-Item -ItemType Directory -Path $IndexRoot -Force | Out-Null

$RawSourcePaths = & $SrcTool -r $PdbPath 2>$null
$SourceFiles = $RawSourcePaths |
    Where-Object {
        $_ -and
        $_.StartsWith($EngineRoot + "\", [System.StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $_ -PathType Leaf)
    } |
    Sort-Object -Unique

if (-not $SourceFiles -or $SourceFiles.Count -eq 0) {
    throw "No source files under $EngineRoot were found in the PDB."
}

$Mappings = New-Object System.Collections.Generic.List[string]
foreach ($SourceFile in $SourceFiles) {
    $RelativePath = $SourceFile.Substring($EngineRoot.Length + 1)
    $Destination = Join-Path $SnapshotRoot $RelativePath
    New-Item -ItemType Directory -Path (Split-Path $Destination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $SourceFile -Destination $Destination -Force
    $Mappings.Add("$SourceFile*$RelativePath")
}

$RemoteSourceRoot = $SymbolStore.TrimEnd("\") + "\SourceStore\" + $BuildId
$StreamLines = New-Object System.Collections.Generic.List[string]
$StreamLines.Add("SRCSRV: ini ------------------------------------------------")
$StreamLines.Add("VERSION=1")
$StreamLines.Add("INDEXVERSION=2")
$StreamLines.Add("VERCTRL=SharedSourceStore")
$StreamLines.Add("DATETIME=" + (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))
$StreamLines.Add("SRCSRV: variables ------------------------------------------")
$StreamLines.Add("SOURCE_ROOT=$RemoteSourceRoot")
$StreamLines.Add("SRCSRVTRG=%targ%\$Product\%var2%")
$StreamLines.Add('SRCSRVCMD=cmd.exe /c copy /y "%SOURCE_ROOT%\%var2%" "%srcsrvtrg%" >NUL')
$StreamLines.Add("SRCSRV: source files ---------------------------------------")
$StreamLines.AddRange($Mappings)
$StreamLines.Add("SRCSRV: end ------------------------------------------------")

[System.IO.File]::WriteAllLines($StreamPath, $StreamLines, [System.Text.Encoding]::ASCII)
Copy-Item -LiteralPath $PdbPath -Destination $PdbBackupPath -Force

& $PdbStr -w -p:$PdbPath -i:$StreamPath -s:srcsrv
if ($LASTEXITCODE -ne 0) {
    throw "pdbstr failed while writing srcsrv data."
}

& $PdbStr -r -p:$PdbPath -i:$ReadbackPath -s:srcsrv
if ($LASTEXITCODE -ne 0 -or -not (Select-String -Path $ReadbackPath -SimpleMatch "SOURCE_ROOT=$RemoteSourceRoot" -Quiet)) {
    throw "Source indexing readback verification failed."
}

Write-Host "Source-indexed PDB: $PdbPath"
Write-Host "Source snapshot:    $SnapshotRoot"
Write-Host "Indexed files:      $($SourceFiles.Count)"
Write-Host "Source URL root:    $RemoteSourceRoot"

if (-not $SkipPublish) {
    & $SymStore add /f $PdbPath /s $SymbolStore /t $Product /v $BuildId
    if ($LASTEXITCODE -ne 0) {
        throw "symstore failed while publishing the source-indexed PDB."
    }
    Write-Host "Published symbols:  $SymbolStore"
}
