# ODES AI chat

## Current input path

The board currently accepts text over USB serial. It does not yet use the
microphone; the exact audio codec and pin mapping for this board still need to
be confirmed. The small display uses an ASCII font, so the proxy asks the model
for concise English replies. Conversation context is kept in memory by the
proxy for up to eight turns.

## Start the local proxy

1. Open PowerShell in this project folder.
2. Create a BigModel API key in the [智谱开放平台](https://open.bigmodel.cn/),
   then run `.\tools\run_ai_proxy.ps1` and enter it at the secure prompt.
   The key is held in process memory and is not written to the project or flash.
3. Note the PC IPv4 address on the same Wi-Fi network as the ESP32. If Windows
   Firewall asks, allow Python on Private networks only.
4. Keep the proxy window open while using the assistant.

The proxy defaults to the Zhipu BigModel endpoint
`https://open.bigmodel.cn/api/paas/v4` and model `glm-4.5-flash`. The current
Zhipu model page says GLM-4.5-Flash was retired on 2026-01-30 and requests using
that id are automatically routed to GLM-4.7-Flash. The page describes 4.5 Flash
as free, but does not guarantee the price of the replacement route; check the
model and current billing/quota in the BigModel console before testing. The
startup prompt lets you change the model id if your account has a different
Flash model available.

## Configure and chat over USB serial

Open the PlatformIO serial monitor at 115200 baud on the board's COM port.
Enter the two commands printed by the proxy window, replacing the address:

```text
url http://<PC-IPv4>:8765/chat
token <device-token>
```

Then send a message on its own line. For example:

```text
ask What can you help me with?
```

Lines without the `ask ` prefix are also treated as messages. Pressing Enter
submits the line. Long-press the board's MENU key on the AI page
to clear the displayed message; the proxy's short conversation context remains
until its process is restarted. Endpoint and device token are stored in ESP32
NVS; the model API key is not. A new random device token is printed each time
the proxy starts, so send the new `token ...` command after restarting it.

## Network and security

The proxy binds to all PC interfaces so the board can reach it. Use it only on
a trusted LAN, do not forward port 8765 from the router, and stop it with
Ctrl+C when done. The proxy requires a random per-run device token. The ESP32
to proxy hop uses plain HTTP on the local network; do not use this design on an
untrusted network.

Check `http://<PC-IPv4>:8765/health` from a browser to confirm the proxy is
running. `model_configured` should be `true`.

References: [GLM-4.5-Flash model notes](https://docs.bigmodel.cn/cn/guide/models/free/glm-4.5-flash)
and [HTTP API guide](https://docs.bigmodel.cn/cn/guide/develop/http/introduction).
