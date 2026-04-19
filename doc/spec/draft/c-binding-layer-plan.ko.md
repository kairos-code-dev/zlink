[스펙 목차](../README.ko.md)

# Draft -- C Binding Layer Plan

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 정책을
> 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 앞으로 C 계층을 어떻게 둘지 계획을 정리한다.

핵심 방향은 아래와 같다.

- 기존 공개 C API를 갑자기 없애지 않는다.
- core에는 bindings 친화적인 helper C API를 먼저 추가한다.
- C는 그 helper 위에 올라가는 **얇은 C binding 계층**으로 다시 정리한다.

관련 helper C API 초안:
- [bindings-helper-capi-partwise-send-recv.ko.md](bindings-helper-capi-partwise-send-recv.ko.md)

## 2. 왜 C binding 계층이 필요한가

지금 구조는 아래처럼 한 계층에 두 역할이 섞여 있다.

- C 최종 사용자가 직접 쓰는 공개 API
- 다른 bindings 구현이 의존하는 substrate 성격의 API

이 구조는 send/recv 모델이 단순할 때는 괜찮지만, bindings 성능 최적화를 위해 더
낮은 수준 helper를 넣기 시작하면 문제가 생긴다.

- bindings 구현은 더 primitive한 part-by-part helper를 원한다.
- C 최종 사용자는 여전히 `parts + part_count` aggregate API가 더 편하다.
- 둘을 한 표면에 계속 같이 두면 헤더가 커지고 의미가 섞인다.

즉 앞으로는 아래처럼 계층을 나누는 편이 더 낫다.

- helper C API: bindings 구현 친화적
- C binding API: 사람이 직접 쓰기 쉬운 aggregate convenience

## 3. C binding의 성격

이 방향으로 가면 C binding은 다른 언어 binding보다 훨씬 얇다.

이유는 아래와 같다.

- C는 helper와 가장 가까운 언어다.
- GC 객체 materialization이나 언어별 wrapper 비용이 없다.
- 따라서 C binding은 주로 aggregate convenience를 제공하는 역할만 하면 된다.

즉 C binding은 대체로 아래 역할을 가진다.

- `zlink_send(parts, part_count, ...)`
- `zlink_recv(..., parts_out, part_count_out, ...)`
- `zlink_publish(...)`
- `zlink_*_request(...)`

이 API들은 helper 계층 위에 다시 쌓인 **aggregate convenience wrapper**에 가깝다.

## 4. helper와 C binding의 역할 분리

### 4.1 helper 계층

helper 계층은 bindings 구현이 직접 쓸 수 있는 낮은 수준 API다.

예:

- `zlink_send_part(...)`
- `zlink_send_part_rid(...)`
- `zlink_recv_part(...)`
- `zlink_router_recv_part(...)`
- `zlink_publish_part(...)`
- `zlink_subscribe_part(...)`

helper는 아래 특징을 가진다.

- part-by-part 모델
- `has_more` 기반 recv
- caller-provided `zlink_msg_t`
- bindings hot path에서 aggregate materialization을 뒤로 미룰 수 있음

### 4.2 C binding 계층

C binding 계층은 helper를 직접 노출하지 않고, 현재와 비슷한 aggregate 사용성을
제공한다.

예:

- `zlink_send(parts, part_count, ...)`
- `zlink_recv(..., parts_out, part_count_out, ...)`
- `zlink_router_recv(...)`
- `zlink_publish(...)`

즉 C binding은 helper 위에 다시 쌓인 사람 친화적 API다.

## 5. 이름과 헤더 방향

현재 공개 헤더는 helper 이름과 C binding 이름을 아래처럼 정리해 두고 있다.

- helper:
  - `zlink_send_part`
  - `zlink_recv_part`
  - `zlink_router_recv_part`
  - `zlink_publish_part`
- C binding convenience:
  - `zlink_send`
  - `zlink_recv`
  - `zlink_router_recv`
  - `zlink_publish`

즉 `_part` suffix가 helper 계층을 뜻하고, 짧은 기존 이름은 C binding convenience를
뜻한다.

두 계층은 현재 `core/include/zlink.h` 하나에 함께 두고, section comment로 경계를
표시한다. 별도 헤더 분리는 `bindings/c/` 디렉터리가 실제로 도입될 때까지 미룬다.

## 6. 구현 순서 초안

이 방향은 한 번에 뒤집는 것보다 아래 순서로 진행하는 방식으로 잡았고, 현재
1단계와 2단계는 반영됐다.

1. helper C API를 먼저 추가했다.
2. 기존 aggregate 공개 API 구현을 helper 위로 다시 정리했다.
3. `.NET`, `Java` bindings가 helper를 실제로 쓰도록 hot path를 재구성한다.
4. 마지막에 C 계층 문서와 구조를 "C binding" 관점으로 다시 정리한다.

즉 우선순위는 C binding 재포장 자체가 아니라, helper를 먼저 만들고 bindings 성능
이득을 실제로 얻는 데 있다.

이 순서는 helper 초안 문서인
[bindings-helper-capi-partwise-send-recv.ko.md](bindings-helper-capi-partwise-send-recv.ko.md)
의 `MORE/FINAL`, request 실패 규칙, routed metadata lifetime이 먼저 고정돼 있어야
안전하게 진행할 수 있다.

### 6.1 구현 현황

helper substrate인 `*_part` 계열은 현재 `core/include/zlink.h`에 반영돼 있다.

aggregate convenience 계열도 같은 헤더에 남아 있으며, 구현은 helper 위에 올라가는
얇은 wrapper로 정리돼 있다.

[bindings-helper-capi-partwise-send-recv.ko.md](bindings-helper-capi-partwise-send-recv.ko.md)
§10.9에 따라, §8.2의 문서 승격 작업은 아직 미뤄 둔다.

현재 헤더 구조는 Option A인 단일 헤더 구분 방식을 따른다. helper와 C binding
convenience 경계는 section comment로 표시하고, 별도 헤더 분리는 `bindings/c/`
디렉터리가 실제로 도입될 때까지 보류한다.

## 7. 후속 작업

이 문서는 단독으로 완료되는 문서가 아니다. 아래 작업과 연결된다.

1. helper C API 초안인
   [bindings-helper-capi-partwise-send-recv.ko.md](bindings-helper-capi-partwise-send-recv.ko.md)
   의 `*_part` 계열을 구현한다.
2. 기존 aggregate 공개 C API를 helper 위로 다시 정리한다.
3. C binding 문서에서 "공개 aggregate convenience"와 "helper substrate"를
   구분해 설명한다.
4. `.NET`, `Java` bindings가 helper 위에서 실제로 더 얇은 recv/send 경로를
   갖는지 다시 측정한다.

즉 C binding 작업은 helper 구현이 끝난 뒤 바로 이어지는 작업이며, helper 계약이
흔들리는 상태에서 먼저 진행하는 작업은 아니다.

## 8. `doc/spec/bindings` 작성 계획

이 방향으로 가면 `doc/spec/bindings` 문서 구조도 함께 정리해야 한다.

핵심 원칙은 아래와 같다.

- `doc/spec/bindings/`는 계속 **각 binding의 공개 API 계약**만 다룬다.
- helper C API 같은 substrate 계약은 `doc/spec/bindings/`에 섞지 않는다.
- substrate helper는 먼저 `doc/spec/draft/`에서 설계하고, 구현이 끝난 뒤에는
  적절한 정식 spec 위치로 옮긴다.

즉 앞으로 문서 계층은 아래처럼 나뉜다.

1. helper substrate 계약
   - 대상: bindings 구현자, core 유지보수자
   - 내용: `*_part`, `has_more`, ownership, routed metadata lifetime
   - 위치:
     - 구현 전: `doc/spec/draft/`
     - 구현 후: 별도 정식 spec 위치로 승격

2. C binding 공개 계약
   - 대상: C 최종 사용자
   - 내용: `zlink_send(parts, count, ...)`, `zlink_recv(..., parts_out, count_out, ...)`
     같은 aggregate convenience API
   - 위치: 구현 후 새로 만드는 `doc/spec/bindings/c/` 또는 이에 준하는
     C binding spec 위치

3. 각 언어 binding 공개 계약
   - 대상: `.NET`, `Java`, `Go`, `Rust`, `Python`, `Node` 사용자
   - 내용: 각 언어의 `Message`, `Received`, `trySend`, `tryRecv`,
     request/reply, callback 규칙
   - 위치: 기존 `doc/spec/bindings/<language>/`

이때 request/reply 공개 형태는 아래처럼 정리하는 쪽을 기본으로 둔다.

- coroutine / await request: `request(...)`
- callback completion request:
  - 오버로드 가능한 언어: `request(..., callback, ...)` 와
    `tryRequest(..., callback, ...)`
  - 오버로드가 어려운 언어: 같은 의미의 callback pair
  - C binding: 예외적으로 기존 `zlink_*_request(... flags ..., timeout ...)`
    형태 유지

### 8.1 무엇을 `doc/spec/bindings`에 쓰지 않을 것인가

다음 내용은 `doc/spec/bindings`에 직접 쓰지 않는다.

- `zlink_send_part`, `zlink_recv_part` 같은 helper substrate 시그니처
- helper의 내부 state machine 설명
- bindings 최적화를 위한 core 내부 호출 순서

이런 내용은 binding 사용자 계약이 아니라 helper substrate 계약이기 때문이다.

### 8.2 구현 후 문서 반영 순서

helper 구현이 완료되면 문서는 아래 순서로 정리한다.

1. helper draft를 현재 구현과 `core/include/zlink.h` 기준으로 다시 검토한다.
2. helper substrate 계약을 정식 spec 위치로 승격한다.
3. C binding spec을 helper 위에 올라가는 aggregate convenience 관점으로 새로
   작성하거나 기존 문서를 재구성한다.
4. `.NET`, `Java` 등 각 binding spec은 helper를 직접 노출하지 않고, 현재 공개
   API 계약만 유지한 채 내부 구현 기반만 바뀐 것으로 정리한다.

### 8.3 공통 bindings 정책 문서와의 관계

`doc/spec/bindings/README.md` 같은 공통 정책 문서에는 아래 수준만 둔다.

- `send/trySend`, `recv/tryRecv` 같은 공통 naming 원칙
- blocking / nonblocking 의미
- ownership, error/result, multipart 일반 원칙

반면 helper substrate 설계는 공통 bindings 정책 문서에 넣지 않는다.

즉 공통 bindings 정책 문서는 "사용자에게 보이는 binding contract"만 다루고,
helper substrate는 별도 계층으로 분리한다.

## 9. 결론

이 초안의 핵심은 아래와 같다.

- helper C API는 bindings 친화적인 substrate다.
- C는 그 위에 올라가는 하나의 binding으로 본다.
- 다만 C binding은 가장 얇은 binding이므로, 형태는 현재 공개 C API와 크게
  다르지 않을 수 있다.
