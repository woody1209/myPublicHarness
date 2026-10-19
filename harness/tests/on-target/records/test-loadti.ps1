# test-loadti.ps1 - one-off: loadti load+run with SCI capture
$sci = Join-Path $PSScriptRoot 'loadti-sci-test.log'
$ccxml = "E:\AIcherry\C2000TI\F280039C-harness\projects\launchxl-f280039c-led-chaser\targetConfigs\TMS320F280039C_LaunchPad.ccxml"
$out = "E:\AIcherry\C2000TI\F280039C-harness\projects\launchxl-f280039c-led-chaser\CPU1_FLASH\launchxl-f280039c-led-chaser.out"

$job = Start-Job -ScriptBlock {
    param($p, $sec, $f)
    $port = New-Object System.IO.Ports.SerialPort $p, 115200, 'None', 8, 'One'
    $port.ReadTimeout = 200
    $port.Open()
    $sb = New-Object System.Text.StringBuilder
    $dl = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $dl) {
        try { [void]$sb.Append($port.ReadExisting()) } catch { }
        Start-Sleep -Milliseconds 50
    }
    $port.Close()
    [System.IO.File]::WriteAllText($f, $sb.ToString())
} -ArgumentList 'COM5', 12, $sci

Start-Sleep -Milliseconds 500
& "D:\ti\ccs2101\ccs\ccs_base\scripting\examples\loadti\loadti.bat" -c $ccxml -l $out -n -t 4000 -v
Wait-Job $job | Out-Null
Remove-Job $job -Force
"=== SCI CAPTURE ==="
Get-Content -Raw -Encoding UTF8 $sci
