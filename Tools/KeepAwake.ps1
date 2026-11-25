# Keeps the PC from idle-sleeping for a little over an hour, then exits (2026-11-25, owner away for Thanksgiving).
# Run hourly by the "Jeebsland KeepAwake" scheduled task until Monday 2026-11-30 23:00. The screen may still turn off; the PC stays on.
Add-Type -Namespace Jeeb -Name Power -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);'
$stop = [datetime]'2026-11-30 23:00'
$end = (Get-Date).AddMinutes(65)
if ($end -gt $stop) { $end = $stop }
while ((Get-Date) -lt $end) {
    [Jeeb.Power]::SetThreadExecutionState(0x80000001) | Out-Null   # ES_CONTINUOUS | ES_SYSTEM_REQUIRED
    Start-Sleep -Seconds 60
}
[Jeeb.Power]::SetThreadExecutionState(0x80000000) | Out-Null
