# diag20.ps1 - scratch: reset-boot handoff check (diag20_reset_boot.js) with a
# concurrent COM5 capture, so a banner printed without any debugger-forced PC
# proves the boot ROM really handed off to flash.
$env:PATH = 'C:\Windows\System32;C:\Windows'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$h = 'E:\AIcherry\C2000TI\F280039C-harness'
$rec = "$h\harness\tests\on-target\records"
$ccxml = "$h\projects\launchxl-f280039c-led-chaser\targetConfigs\TMS320F280039C_LaunchPad.ccxml"

$sniff = {
    param($portName, $seconds, $outFile)
    try {
        $port = New-Object System.IO.Ports.SerialPort $portName, 115200, 'None', 8, 'One'
        $port.ReadTimeout = 200
        $port.Open()
        $sb = New-Object System.Text.StringBuilder
        $deadline = (Get-Date).AddSeconds($seconds)
        while ((Get-Date) -lt $deadline) {
            try { [void]$sb.Append($port.ReadExisting()) } catch { }
            Start-Sleep -Milliseconds 50
        }
        try { $port.Close() } catch { }
        [System.IO.File]::WriteAllText($outFile, $sb.ToString())
        "OK $portName bytes=$($sb.Length)"
    } catch { "FAIL ${portName}: $($_.Exception.Message)" }
}

$job = Start-Job -ScriptBlock $sniff -ArgumentList 'COM5', 22, "$rec\diag20-com5.log"
& 'D:\ti\ccs2101\ccs\ccs_base\scripting\bin\dss.bat' "$h\harness\tools\dss\diag20_reset_boot.js" $ccxml *>&1 |
    Out-File -Encoding utf8 "$rec\diag20-dss.log"
Wait-Job $job | Out-Null
Receive-Job $job
Remove-Job $job -Force
Get-Content "$rec\diag20-dss.log"
Write-Host ("--- COM5 captured bytes: " + (Get-Item "$rec\diag20-com5.log").Length)
