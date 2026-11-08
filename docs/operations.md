# Operations

## Remote scale connectivity

Decision accepted on 2026-11-03: Cloudflare Tunnel with Cloudflare Access
service-token authentication is the official path for the remote ESPHome scale
to reach Glaze Manager. Implementation is deferred; this records the deployment
decision only.

The scale runs at a separate location without another always-on computer.
Glaze Manager runs in a container on `dockerpi`. The planned request path is:

```text
ESPHome scale -> HTTPS + Access service credentials -> Cloudflare
             -> outbound tunnel -> cloudflared on dockerpi -> Glaze Manager
```

Run `cloudflared` in another container on the same Docker network as Glaze
Manager, forwarding to `http://app:8000`. Protect the public hostname with an
Access Service Auth policy allowing the scale's dedicated service token.
ESPHome uses its standard `http_request` component with TLS verification and
the `CF-Access-Client-Id` and `CF-Access-Client-Secret` headers stored in secrets.
The tunnel connector credential stays on `dockerpi` and is separate from the
scale's Access credentials.

This approach requires no inbound router port forwarding or VPN client on the
scale. Access supplies authentication because Glaze Manager has no built-in
login. It uses the existing HTTP device API and avoids extra hardware beside
the scale. WireGuard and Tailscale are alternatives considered, rather than
the selected deployment path.

The tradeoffs are reliance on Cloudflare and HTTPS termination there. Remote
ESPHome logs and OTA access are separate concerns; this decision covers the
scale's requests to Glaze Manager.
