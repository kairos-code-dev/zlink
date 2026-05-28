# Systems.Zlink

Systems.Zlink is the .NET binding package for zlink.

The package exposes the public API under the `Systems.Zlink` namespace and
ships the native zlink runtime files needed by supported platforms.

Routed receive results expose `Received.Send(...)` for sending a normal routed
message back over the original receive context. Request messages also keep
`Received.Reply(...)`; `Send` does not use request-reply state.

Create caller-owned receive buffers with `Received.Create()` and reuse the
same instance across `Recv(...)` calls when draining hot paths.

Project repository: https://github.com/kairos-code-dev/zlink
