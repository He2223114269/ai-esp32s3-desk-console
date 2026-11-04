$ErrorActionPreference = "Stop"

$model = Read-Host "Model ID (press Enter for glm-4.5-flash)"
if ([string]::IsNullOrWhiteSpace($model)) { $model = "glm-4.5-flash" }
$secureKey = Read-Host "BigModel API key (input is hidden)" -AsSecureString
$keyPtr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureKey)
$plainKey = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($keyPtr)
$deviceToken = [Guid]::NewGuid().ToString("N")

$env:OPENAI_API_KEY = $plainKey
$env:OPENAI_BASE_URL = "https://open.bigmodel.cn/api/paas/v4"
$env:OPENAI_MODEL = $model
$env:ODES_DEVICE_TOKEN = $deviceToken

Write-Host ""
Write-Host "Provider: Zhipu BigModel"
Write-Host "Model: $model"
Write-Host "ODES proxy device token: $deviceToken"
Write-Host "Board URL command: url http://<this-PC-IPv4>:8765/chat"
Write-Host "Board token command: token $deviceToken"
Write-Host "Use the PC IPv4 address on the same network as the ESP32."
Write-Host "Allow Python on Private networks if Windows Firewall asks."
Write-Host "Press Ctrl+C to stop the proxy."
Write-Host ""
Get-NetIPConfiguration -ErrorAction SilentlyContinue |
  Where-Object { $_.IPv4DefaultGateway -ne $null } |
  Select-Object InterfaceAlias, @{Name="IPv4"; Expression={ $_.IPv4Address.IPAddress }} |
  Format-Table -AutoSize

try {
  python (Join-Path $PSScriptRoot "ai_proxy.py")
} finally {
  [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($keyPtr)
  $env:OPENAI_API_KEY = $null
  $env:ODES_DEVICE_TOKEN = $null
  $plainKey = $null
  $secureKey = $null
}
