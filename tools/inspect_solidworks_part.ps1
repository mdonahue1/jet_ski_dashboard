$ErrorActionPreference = 'Stop'

Add-Type -Path 'D:\SOLIDWORKS\SolidWorks.Interop.sldworks.dll'

$partPath = 'D:\SOLIDWORKS\solidworks parts and assemblies\jet_ski_dash_housing.SLDPRT'
$exportPath = 'C:\Users\miner\STM32CubeIDE\workspace_1.19.0\Jet_Ski_Dash_v1\dash_housing_work\source_geometry.step'

New-Item -ItemType Directory -Path (Split-Path -Parent $exportPath) -Force | Out-Null

$sw = New-Object SolidWorks.Interop.sldworks.SldWorksClass
$sw.Visible = $false
$errors = 0
$warnings = 0
$model = $sw.OpenDoc6($partPath, 1, 1, '', [ref]$errors, [ref]$warnings)
if ($null -eq $model) {
    throw "SolidWorks could not open the part. Errors=$errors warnings=$warnings"
}

Write-Output "TITLE $($model.GetTitle())"
Write-Output "PATH $($model.GetPathName())"

$part = [SolidWorks.Interop.sldworks.IPartDoc]$model
$box = $part.GetPartBox($true)
$boxMm = @($box | ForEach-Object { [math]::Round([double]$_ * 1000.0, 4) })
Write-Output "BOUNDING_BOX_MM $($boxMm -join ',')"
Write-Output "SIZE_MM $([math]::Round($boxMm[3]-$boxMm[0],3)),$([math]::Round($boxMm[4]-$boxMm[1],3)),$([math]::Round($boxMm[5]-$boxMm[2],3))"

$bodies = @($part.GetBodies2(0, $true))
Write-Output "SOLID_BODY_COUNT $($bodies.Count)"
foreach ($body in $bodies) {
    $bodyBox = $body.GetBodyBox()
    $bodyBoxMm = @($bodyBox | ForEach-Object { [math]::Round([double]$_ * 1000.0, 4) })
    Write-Output "BODY $($body.Name) BOX_MM $($bodyBoxMm -join ',')"
}

$feature = $model.FirstFeature()
while ($null -ne $feature) {
    Write-Output "FEATURE $($feature.Name) | $($feature.GetTypeName2()) | SUPPRESSED=$($feature.IsSuppressed())"
    $sub = $feature.GetFirstSubFeature()
    while ($null -ne $sub) {
        Write-Output "  SUBFEATURE $($sub.Name) | $($sub.GetTypeName2())"
        $sub = $sub.GetNextSubFeature()
    }
    $feature = $feature.GetNextFeature()
}

$saveErrors = 0
$saveWarnings = 0
$ok = $model.Extension.SaveAs(
    $exportPath,
    0,
    1,
    $null,
    [ref]$saveErrors,
    [ref]$saveWarnings
)
Write-Output "STEP_EXPORT ok=$ok errors=$saveErrors warnings=$saveWarnings path=$exportPath"

$sw.CloseDoc($model.GetTitle())
$sw.ExitApp()
