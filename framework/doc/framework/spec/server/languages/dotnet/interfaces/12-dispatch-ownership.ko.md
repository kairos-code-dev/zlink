# .NET dispatch와 ownership 계약

[.NET exact interface 목차](README.ko.md)

## 1. Dispatch와 ownership

Bindings의 public raw socket readiness는 payload가 아니라 I/O 가능 상태만 알린다. Framework는 수신한
message를 Node, Spot과 Actor owner queue에 배치하고 typed handler에 전달한다. Spot Logical Multicast는 수신
MeshNode가 local subscription을 검사하고 일치하는 Spot queue에 immutable message reference를 넣는다.

Handler가 받은 metadata와 message context는 callback 동안 읽기 전용이다. Handler가 payload storage,
reply correlation, route envelope나 raw message storage를 dispose하지 않는다. Framework가 callback completion과 함께
해당 resource의 lifecycle을 관리한다.
