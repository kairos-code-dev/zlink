[English](connection-memory.md)

# Connection별 memory

각 transport connection은 session, engine state, pipe endpoint, handshake buffer와 kernel
socket buffer를 할당한다. Queued message storage는 고정된 connection 비용이 아니라 effective message
size와 HWM에 따라 증가한다.

## 고정 구성 요소

- session과 engine object
- pipe metadata와 queue chunk
- routing id와 endpoint metadata
- protocol handshake state
- 운영체제 socket 구조

## 가변 구성 요소

Send와 receive queue는 message object와 참조된 payload storage를 유지한다. Kernel buffer는 platform
autotuning에 따라 증가할 수 있다. TLS는 record와 handshake storage를 추가한다. Monitor snapshot은
적용된 HWM 계획을 보고하지만 allocator와 kernel overhead 전체를 측정하지는 않는다.

Capacity planning에서는 production transport와 message-size 분포를 사용해 idle, traffic 이후 잔류와
burst peak memory를 각각 측정한다.
