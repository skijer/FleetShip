# In-game asset review: Link holds up every item as from a chest; A marks it correct, B wrong, Z goes back.
# Verdicts land in logs/asset-catalogue/ingame-review.json; once every item is correct that catalogue becomes the baseline.
param([string]$Only = "", [switch]$All, [switch]$Failed)

$install = Resolve-Path (Join-Path $PSScriptRoot "..\x64\Release")
Push-Location $install
try {
    $session = "logs/asset-catalogue/sessions/review-$(Get-Date -Format yyyyMMdd-HHmmss)"
    $scope = if ($All) { "all" } elseif ($Failed) { "failed" } else { "pending" }
    $arguments = @("--oot", "Ship", "--mm", "2Ship", "--seed", "test-seeds/4-zonas-sin-compartir.json", "--session", $session,
                   "--slot", "0", "--smoke", "120", "--asset-review", $scope)
    if ($Only) {
        $arguments += @("--asset-review-only", $Only)
    }
    & .\fleet-test.exe @arguments
    python -I (Join-Path $PSScriptRoot "asset_report.py") --accept-reviewed
} finally {
    Pop-Location
}
