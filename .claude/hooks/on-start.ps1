try {
    $body = @{name = "upright"} | ConvertTo-Json
    Invoke-RestMethod -Uri "http://127.0.0.1:5679/api/command" -Method Post -ContentType "application/json" -Body $body -TimeoutSec 3 -ErrorAction Stop | Out-Null
} catch {}
exit 0
