param(
    [string]$Port = "COM4",
    [int]$Seconds = 12,
    [int]$Baud = 115200,
    [switch]$NoReset,
    [string]$OutFile = ""
)
$p = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, One
$p.ReadTimeout = 500
$p.Open()
if (-not $NoReset) {
    # USB-Serial-JTAG 复位序列：RTS 拉高复位 -> 释放运行
    $p.DtrEnable = $false
    $p.RtsEnable = $true
    Start-Sleep -Milliseconds 100
    $p.RtsEnable = $false
} else {
    # 不复位也要拉 DTR，否则 USB-Serial-JTAG 不向主机推日志
    $p.DtrEnable = $true
    $p.RtsEnable = $false
}
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$log = ""
$writer = $null
if ($OutFile) {
    $writer = [System.IO.StreamWriter]::new($OutFile, $false)
}
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
    $chunk = ""
    try { $chunk = $p.ReadExisting() } catch {}
    if ($chunk) {
        $log += $chunk
        if ($writer) { $writer.Write($chunk); $writer.Flush() }
    }
    Start-Sleep -Milliseconds 200
}
$p.Close()
if ($writer) { $writer.Close() }
if ($log) { $log.Substring(0, [Math]::Min(60000, $log.Length)) } else { "NO_SERIAL_OUTPUT" }
