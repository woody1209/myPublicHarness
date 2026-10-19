# diag18.ps1 - scratch: run diag18.js while sniffing COM5 + COM6 to find which
# XDS110 virtual port carries the target SCI. Evidence for the run-hil fix.
$env:PATH = 'C:\Windows\System32;C:\Windows'
$rec = 'E:\AIcherry\C2000TI\F280039C-harness\harness\tests\on-target\records'
$ccxml = 'E:\AIcherry\C2000TI\F280039C-harness\projects\launchxl-f280039c-led-chaser\targetConfigs\TMS320F280039C_LaunchPad.ccxml'

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

$j5 = Start-Job -ScriptBlock $sniff -ArgumentList 'COM5', 25, "$rec\diag18-com5.log"
$j6 = Start-Job -ScriptBlock $sniff -ArgumentList 'COM6', 25, "$rec\diag18-com6.log"

& 'D:\ti\ccs2101\ccs\ccs_base\scripting\bin\dss.bat' 'E:\AIcherry\C2000TI\F280039C-harness\harness\tools\dss\diag18.js' $ccxml *>&1 |
    Out-File -Encoding utf8 "$rec\diag18-dss.log"

Wait-Job $j5, $j6 | Out-Null
Receive-Job $j5, $j6
Remove-Job $j5, $j6 -Force
Write-Host ("com5 bytes: " + (Get-Item "$rec\diag18-com5.log").Length)
Write-Host ("com6 bytes: " + (Get-Item "$rec\diag18-com6.log").Length)
