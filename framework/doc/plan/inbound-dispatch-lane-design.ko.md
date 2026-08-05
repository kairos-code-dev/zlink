# 수신 backpressure 목표 설계와 적용 계획

> 상태: Framework 적용 진행 중. C-01부터 F-02까지 완료, F-03 진행 중
>
> 두 connection 구조와 byte 기준 HWM을 구현 기준으로 사용한다.
> C-01부터 BP-03까지의 Core·bindings 작업과 검증은 이 문서만 단일 기준으로 사용한다.
>
> 2026-07-30 사용자 승인에 따라 Framework F-01부터 F-09까지 이어서 진행한다.

## 1. 이 문서가 정하는 결과

이 문서는 target이 처리할 수 있는 양보다 많은 message가 도착할 때 dispatch를 기다리는
backlog를 byte 상한 안에 유지하고, source의 송신을 기다리게 만드는 목표 구조를 정한다.
Queue와 transport가 보관하는 in-flight message의 허용 상한을 high water mark(HWM)라고 한다.
Target이 HWM 때문에 수신을 멈추면 Core의 network pipe가 차고, 그 결과 source의 송신이
대기한다.

Application message와 request는 memory 여유가 없을 때 수신을 중단한다. 이미 보낸 request의 reply와
liveness·admission·relocation·reply recovery service control은 별도 Completion connection으로 수신한다.
Core의 send-ready 상태는 network message가 아니라 기존 callback으로 전달한다. Actor lifecycle처럼
application callback을 실행하는 작업은 Application 영역에서 처리한다. 이 구조로 application 수신
중단이 completion과 필수 Framework coordination 진행을 막는 순환 의존성을 제거한다.

Core와 bindings 범위 C-01부터 BP-03까지는 완료했다. Framework는 F-01부터 F-09까지 공통
정식 spec을 먼저 갱신한 뒤 구현·contract test·E2E·성능 검증 순서로 진행한다.

### 1.1 목표 결과

- Core와 bindings는 C-01부터 BP-03까지 이 문서의 순서와 gate를 따라 변경한다.
- 두 connection, byte HWM과 내부 queue 제거를 Core 목표 구조로 적용한다.
- Framework Application HWM은 공통 spec과 다섯 언어 exact interface를 먼저 확정한 뒤 적용한다.
- 정확한 public contract는 이 문서, C header, 정식 spec과 contract test에서 같은 의미로
  유지한다.

범위 밖 public API나 호환 계층은 추가하지 않는다.

### 1.2 각 계층의 책임

| 계층 | 이 설계에서 맡는 책임 |
|---|---|
| Application | 유한한 최대 message 크기를 설정한다. `ApplicationHwmBytes`를 생략하면 선택한 Auto HWM profile, 양수를 지정하면 고정 HWM, `0`을 지정하면 무제한을 사용한다. Auto profile 기본값은 `BALANCED`다. |
| Framework | Host 전체에서 수신 후 handler가 완료되지 않은 payload byte를 계산하고 Application `Recv`를 중단·재개한다. Reply가 사용하는 memory는 별도 동시성과 reserve로 제한한다. |
| Core | Directional pipe의 실제 보관 byte를 HWM으로 제한하고 Application·Completion connection pair를 관리한다. |
| Bindings | Core의 64-bit byte option, monitoring 값과 오류를 각 언어에서 같은 의미로 제공한다. |
| Remote runtime | 같은 pair identity와 generation을 검증하고 stale reply를 적용하지 않는다. |

### 1.3 정상 처리 순서

1. Framework는 처리 중인 application payload 합계가 HWM보다 작으면 complete message를
   `Recv`한다.
2. Core에서 완성된 message를 받은 뒤 실제 payload byte를 처리 중인 합계에 더한다.
3. 처리 중인 payload 합계가 HWM에 도달하거나 초과하면 Application connection의 `Recv`만
   중단한다.
4. Core pipe가 byte HWM까지 차면 source의 send가 대기한다.
5. Completion connection의 poller는 이미 보낸 request의 reply와 bounded service control을 계속 처리한다.
   Send-ready callback도 Application `Recv`와 독립적으로 처리한다.
6. Request handler는 실행 전에 reply 한 건의 최대 memory를 예약한다. Reply를 Core가
   받아들이면 Framework의 reply byte를 Core transport reserve로 넘긴다.
7. Handler가 terminal 상태에 도달하여 처리 중인 payload 합계가 HWM보다 작아지면 Application
   `Recv`를 다시 시작한다.

## 2. 왜 현재 구조를 바꿔야 하는가

Target의 처리 속도보다 source의 송신 속도가 빠르면 수신 대기 message가 계속 증가할 수 있다.
원하는 동작은 message를 버리는 것이 아니라, target의 처리 지연이 source의 송신 대기로 이어지는
[backpressure](../framework/common/spec/01-glossary.ko.md#backpressured)다.

목표 구조는 다음 조건을 모두 만족해야 한다.

- 수신한 application message를 부하 때문에 버리지 않는다.
- Target의 수신 여유가 부족하면 source의 송신 압력을 낮춘다.
- Message 개수뿐 아니라 payload 크기도 고려한다.
- Application `Recv`가 중단되어도 Completion connection에서 reply와 bounded service control을 처리하고
  Core의 send-ready callback을 계속 실행한다.
- 수천 개 peer를 사용하는 환경에서 두 connection의 socket과 memory 비용을 계산하고
  Completion connection의 HWM과 buffer를 application traffic보다 작게 제한한다.
- Backpressure 상태를 계산하는 동기화가 정상 dispatch 성능을 크게 낮추지 않는다.
- Process 전체 ingress가 중단되면 byte를 가장 많이 보유한 실행 대상을 진단할 수 있어야 한다.
- 양방향 nested request와 두 connection의 순서 역전이 영구 교착이나 stale completion 적용으로
  이어지지 않아야 한다.

## 3. 구현하면서 정식 계약에 고정할 값

목표 구조와 계산 방법은 승인되었다. 다음 값과 protocol 세부 사항은 C-01부터 C-08까지
구현·측정하고 C header, 정식 spec과 contract test에 같은 의미로 고정한다.

- Framework가 수신 후 처리 중인 application payload에 적용할 byte 합산 규칙
- Framework Application HWM과 Completion connection HWM의 production 기본값
- Host 전체 `maxPendingCompletionSends`, peer별 공정성 상한과 completion send permit의 내부 기본값
- Completion connection handshake, reconnect와 pending request 종료 error
- 장시간 pause 진단 threshold와 조회 rate limit
- Core Auto HWM planning unit option의 정확한 migration 계약
- C monitoring ABI version과 다섯 Framework 언어의 exact public interface

Core가 내부에서 만드는 Completion connection은 방향별 HWM을 `262144` byte로 제한한다.
Application socket에 더 작은 HWM이 설정되어 있으면 그 값을 유지한다. Network Completion
connection의 `SNDBUF`와 `RCVBUF`는 각각 `65536` byte를 상한으로 사용한다. 이 값은 hidden
transport lane의 내부 memory 정책이며 새 public option으로 노출하지 않는다.

## 4. Framework는 처리 중인 application payload를 어떻게 제한하는가

이 절은 Framework가 수신했지만 handler 처리가 아직 완료되지 않은 application job의 payload
합계를 제한하는 공개 동작을 정의한다. 이 제한을 Application HWM이라고 한다. Application HWM은
한 Framework host 전체에 적용하며 connection, MeshNode, Channel, Spot 또는 Actor마다 나누지
않는다.

Application은 HWM 값을 직접 지정하거나 Auto profile을 선택한다. Framework는 적용한 HWM과
현재 pending payload 합계를 비교하여 application message 수신을 중단하거나 재개한다.

### 4.1 어떤 payload byte를 계산하는가

`applicationPendingPayloadBytes`는 Framework가 `Recv`한 complete message 가운데 handler
처리가 아직 terminal 상태에 도달하지 않은 application job의 payload byte 합계다. Job은
`Recv`가 완료된 시점부터 handler가 정상 완료, 실패 또는 cancellation로 끝날 때까지 합계에
포함된다.

Queue에서 dispatch를 기다리는 job과 handler가 실행 중인 job을 모두 포함한다. Core pipe와 OS
socket buffer에 남아 있어 Framework가 아직 `Recv`하지 않은 message, handler가 끝난 job과
completion 처리는 포함하지 않는다.

Multipart message 한 건은 모든 application payload part의 길이를 합산한다. Envelope, routing
정보, Framework metadata, allocator overhead와 minimum charge는 더하지 않는다. 같은 job을
Framework 내부에서 이동할 때는 한 번만 계산한다. 같은 payload를 서로 다른 job으로 복제하면
각 job의 payload byte를 계산한다.

Application HWM은 process memory 전체의 상한이 아니다. Framework가 `Recv`하여 처리 중인
application payload의 합계를 기준으로 새 수신을 중단하는 값이다.

#### Payload 합계를 어떻게 유지하는가

Framework는 queue나 실행 중인 handler를 순회하여 `applicationPendingPayloadBytes`를 다시
계산하지 않는다. Complete message를 `Recv`할 때 각 payload part의 길이를 합산하고 그 결과를
내부 HandlerContext에 immutable 값으로 저장한다. Payload를 복사하거나 HWM 계산을 위해 part를
다시 순회하지 않는다. 이 값은 내부 accounting에만 사용하며 public HandlerContext API로
노출하지 않는다.

Framework는 Core pipe의 byte credit과 같이 수신한 누적 payload와 완료한 누적 payload의 차이로
현재 처리 중인 payload를 계산한다.

```text
applicationPendingPayloadBytes =
    cumulativeReceivedPayloadBytes
    - cumulativeCompletedPayloadBytes
```

두 누적값은 감소시키거나 message마다 다시 계산하지 않는다. Framework는 다음 상태 변경에서만
해당 누적값을 증가시킨다.

1. Complete message의 `Recv`가 성공하면 HandlerContext에 저장한 payload byte를 합계에 한 번
   더해 `cumulativeReceivedPayloadBytes`를 갱신한다.
2. Job이 queue 사이를 이동하거나 handler 실행을 시작할 때는 두 누적값을 변경하지 않는다.
3. Handler가 정상 완료, 실패 또는 cancellation로 terminal 상태에 도달하면 HandlerContext에
   저장한 payload byte를 `cumulativeCompletedPayloadBytes`에 한 번 더한다.
4. Handler를 시작하기 전에 shutdown, routing failure 또는 cancellation로 job이 terminal
   상태에 도달해도 같은 완료 경로에서 저장한 payload byte를 완료 누적값에 한 번 더한다.

HandlerContext에는 payload byte 값만 추가하며 별도의 accounting object를 만들지 않는다.
Handler thread는 host 전체 counter를 직접 변경하지 않는다. 기존 handler terminal 통지에
payload byte를 포함하고 Application HWM owner가 그 통지를 처리할 때 완료 누적값을 갱신한다.
모든 terminal 처리는 기존 공통 완료 경로를 사용하여 같은 payload byte를 두 번 반영하지 않는다.

Application HWM owner는 두 누적값을 일반 64-bit 값으로 관리한다. Ingress poller와 handler
terminal 경로가 이미 사용하는 dispatch·완료 통지에 byte를 함께 전달한다. Message마다
accounting object, queue, event 또는 wakeup을 만들지 않는다. 다만 수신을 멈춘 뒤 handler
terminal로 pending payload가 HWM 아래로 내려가면 receive loop를 다시 실행할 신호가 필요하다.
Framework는 host 수명 동안 하나만 유지하는 resume gate를 사용하고
`paused → resumable` 전환에서 한 번만 signal한다. Handler terminal마다 signal하지 않으며
polling으로 재개 여부를 확인하지 않는다. 이 gate는 HWM 계산용 message allocation을 만들지
않고, 여러 ingress loop가 같은 host budget을 사용하게 한다.

### 4.2 설정값은 어떻게 해석하는가

`ApplicationHwmBytes`는 host 전체 `applicationPendingPayloadBytes`에 적용할 64-bit byte 값이다.

| 설정 | 적용 결과 |
|---|---|
| 설정하지 않음 | `ApplicationHwmProfile`로 Auto HWM을 계산한다. |
| `0` | Application HWM을 적용하지 않는다. |
| 양수 | 지정한 byte 값을 그대로 적용한다. |

`ApplicationHwmProfile`을 설정하지 않으면 `BALANCED`를 사용한다. Profile은 Auto mode에서만
사용한다. Framework는 Auto mode에서 선택한 profile을 Framework가 사용하는 Core context의
`ZLINK_CTX_OPT_AUTO_HWM_PROFILE`에도 동일하게 설정한다. Profile을 생략한 경우에도 Framework와
Core context는 모두 `BALANCED`를 사용한다. `ApplicationHwmBytes`에 `0` 또는 양수를 지정하면
해당 값이 Application HWM에 우선한다.

양수 HWM이 [`MaxMessageSize`](../framework/common/spec/01-glossary.ko.md#maxmessagesize)보다
작아도 설정 오류가 아니다. Application HWM은 message 한 건의 허용 크기를 제한하지 않는다.

Framework Application HWM은 Core socket의 send·receive HWM과 별도다. Framework는 이 값을
각 Core connection에 복사하거나 connection 수로 나누지 않는다.

### 4.3 Auto HWM은 어떻게 계산하는가

Auto mode에서는 Application에 할당된 memory 중 선택한 profile의 비율을 pending application
payload에 배정한다.

```text
autoApplicationHwmBytes =
    floor(applicationAllocatedMemoryBytes * selectedProfileRatio)
```

`applicationAllocatedMemoryBytes`는 다음 순서로 결정한다.

1. Application이 Framework host에 명시한 process memory limit을 사용한다.
2. 명시한 값이 없으면 container, cgroup 또는 Windows Job Object가 해당 process에 적용하는
   유한한 memory limit을 사용한다.
3. 두 값을 모두 확인할 수 없으면 startup configuration error로 처리한다.

Framework는 host physical memory, 현재 OS free memory, process RSS, CPU 사용률과 ingress
처리량을 Auto HWM 계산에 사용하지 않는다.

| Profile | `selectedProfileRatio` | 적용 결과 |
|---|---:|---|
| `COMPACT` | `2%` (`0.02`) | 할당된 memory의 2%를 pending payload에 배정한다. |
| `LOW_LATENCY` | `5%` (`0.05`) | 할당된 memory의 5%를 pending payload에 배정한다. |
| `BALANCED` | `10%` (`0.10`) | 할당된 memory의 10%를 배정하며 별도 설정이 없을 때 사용한다. |
| `THROUGHPUT` | `20%` (`0.20`) | 할당된 memory의 20%를 pending payload에 배정한다. |

모든 Framework 언어는 이 비율을 그대로 사용한다. 같은 profile 이름을 Core context에도
설정하지만 비율을 Core HWM 계산에 사용하지 않는다. Core는 동일한 profile 이름에 해당하는
Core의 connection별 Auto HWM 정책으로 socket HWM을 계산한다.

Framework는 application ingress를 시작하기 전에 Auto HWM을 계산한다. Application에 할당된
memory limit이나 profile을 명시적으로 변경한 경우에만 다시 계산한다. 실행 중 관측한 memory나
처리량에 따라 HWM을 자동으로 변경하지 않는다.

### 4.4 언제 HWM에 도달한 것으로 판단하는가

HWM이 `0`이면 Framework는 pending payload 때문에 수신을 중단하지 않는다. HWM이 양수이면
다음 조건에서 새 application `Recv`를 시작한다.

```text
canStartRecv =
    applicationPendingPayloadBytes < applicationHwmBytes
```

Framework는 다음 message 크기를 미리 알 수 없으므로 HWM byte를 선예약하지 않는다. 처리 중인
payload 합계가 HWM보다 작을 때 시작한 message는 HWM보다 크더라도 끝까지 수신한다. 그 결과
HWM을 초과하면 이후의 새 `Recv`만 중단한다. 따라서 HWM보다 큰 message 한 건도 처리 중인
application job이 없으면 수신할 수 있다.

여러 ingress poller가 동시에 시작한 `Recv`도 모두 완료할 수 있다. Application HWM은 이미
시작한 message를 실패시키는 memory 상한이 아니라 새 수신을 중단하는 기준이다.

### 4.5 HWM에 도달하면 Framework는 무엇을 하는가

`applicationPendingPayloadBytes`가 HWM에 도달하거나 초과하면 Framework는 다음 순서로
처리한다.

1. 새로운 application message의 `Recv`를 시작하지 않는다.
2. 이미 시작한 `Recv`는 완료하고 받은 payload byte를 처리 중인 합계에 더한다. HWM을 초과했다는
   이유로 message를 제거하거나 application error로 완료하지 않는다.
3. Framework queue에서 기다리는 application job은 계속 dispatch한다.
4. 별도 Completion connection에서 이미 보낸 request의 reply와 bounded service control을 계속 수신하고
   Core의 send-ready callback도 계속 처리한다.
5. Handler가 terminal 상태에 도달하면 HandlerContext에 저장한 payload byte를 완료 누적값에
   더한다. Pending payload는 수신 누적값과 완료 누적값의 차이만큼 감소한다.
6. 처리 중인 payload 합계가 HWM보다 작아지면 application message의 `Recv`를 재개한다.

Framework가 application `Recv`를 중단하면 이후 message는 Core receive queue에 남는다. Core
receive queue가 해당 pipe의 HWM에 도달하면 source의 새 송신이 대기한다. 이 과정으로
Framework의 Application HWM이 source의 backpressure로 전달된다.

### 4.6 Application은 어떤 기준으로 값을 선택하는가

유한한 application memory limit이 있고 별도의 운영 측정값이 없으면 `BALANCED` Auto profile을
사용한다.

| 운영 조건 | 선택할 profile |
|---|---|
| Pending payload를 가장 작게 제한해야 한다. | `COMPACT` |
| 짧은 queue 지연이 burst 흡수보다 중요하다. | `LOW_LATENCY` |
| 별도의 우선 조건이 없다. | `BALANCED` |
| 추가 memory와 queue 지연을 허용하고 burst 흡수가 중요하다. | `THROUGHPUT` |

Profile만으로 운영 요구를 만족할 수 없으면 production과 같은 workload에서 지속 처리량을
측정하여 양수 `ApplicationHwmBytes`를 지정한다. 지속 처리량은 대기 중인 job이 존재하는 동안
handler가 처리를 완료한 application payload byte를 실행 시간으로 나눈 값이다. Ingress byte와
순간 peak는 사용하지 않는다. 같은 실행에서 handler가 실행 중인 payload byte의 최대 합계도
측정한다.

Application은 허용할 최대 queue 대기 시간을 정하고 다음 값을 HWM 후보로 사용한다.

```text
candidateApplicationHwmBytes =
    measuredPeakActiveHandlerPayloadBytes
    + measuredSustainableDrainBytesPerSecond
        * maximumQueueDelaySeconds
```

실행 중인 handler payload도 Application HWM에 포함하므로 HWM 후보에서 먼저 확보한다. 나머지
byte가 허용한 queue 대기 시간 동안 dispatch를 기다릴 수 있는 payload다.

후보값은 다음 조건을 같은 workload에서 확인한 뒤 production 값으로 사용한다.

- Pending payload 합계가 후보값에 도달해도 process memory limit을 넘지 않는다.
- Queue 대기 시간이 Application이 정한 최대값을 넘지 않는다.
- HWM보다 큰 최대 message 한 건도 처리 중인 application job이 없을 때 수신할 수 있다.
- HWM에 도달하면 message를 버리지 않고 source에 backpressure가 전달된다.

Application HWM은 처리 중인 payload 합계와 예상 queue 대기 시간을 제한한다. CPU 사용률이나
handler 동시 실행 수를 제한하지 않는다. CPU 목표는 dispatch concurrency나 별도 rate limit으로
맞춘 뒤 지속 처리량을 측정해야 한다.

성능 테스트 guide는 이 절의 처리량 정의, 후보 계산식과 검증 조건을 사용한다. Warm-up 시간,
반복 횟수, workload 구성과 결과표처럼 실행 절차에 필요한 내용만 guide에서 추가한다.

### 4.7 어떤 설정이 실패하는가

다음 조건에서는 Framework host startup이 실패한다.

- `ApplicationHwmBytes`가 64-bit byte 값의 범위를 벗어난다.
- 알 수 없는 `ApplicationHwmProfile` 값을 지정한다.
- Auto mode에서 유한한 `applicationAllocatedMemoryBytes`를 확인할 수 없다.
- Auto mode에서 선택한 profile로 유한한 양수 HWM을 계산할 수 없다.

양수 `ApplicationHwmBytes`와 명시적인 `0`은 application memory limit을 확인하지 못해도 그대로
적용한다. HWM보다 큰 message의 허용 여부는 Application HWM이 아니라 `MaxMessageSize` 계약으로
판단한다.
## 5. Core C HWM 계약은 어떻게 바뀌는가

> 이 절은 C-01부터 C-08까지의 Core 구현과 B-01부터 B-05까지의 bindings 계약 기준이다.

Core HWM을 message count에서 byte로 바꾸는 것은 `pipe_t` 내부 구현만의 변경이 아니다.
현재 C API가 HWM option의 값과 단위를 공개하므로 public contract, ABI에 전달하는 값의
크기, Auto HWM, monitoring과 모든 bindings를 함께 변경해야 한다.

### 5.1 현재 계약은 무엇인가

현재 Core는 다음 계약을 사용한다.

| 항목 | 현재 의미 |
|---|---|
| `ZLINK_OPT_SNDHWM` | `int` message count. `0`은 무제한이다. |
| `ZLINK_OPT_RCVHWM` | `int` message count. `0`은 무제한이다. |
| 수동 기본값 | 송신과 수신 모두 `1,000 messages`다. |
| Auto HWM | Byte budget을 effective message size로 나누어 message slot 수를 계산한다. |
| Pipe admission | Peer가 읽지 않은 message count가 HWM에 도달하면 write를 중단한다. |
| Low watermark | Message count HWM의 절반이다. |
| Monitoring | 적용·보류 HWM을 `int32_t` message count로 제공한다. |

따라서 기존 application이 다음 값을 설정했다면 의미는 `1,000 bytes`가 아니라
`1,000 messages`다.

```c
int hwm = 1000;
zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));
```

같은 option ID에 byte 의미를 적용하면 기존 binary와 source가 같은 값을 전달해도 전혀
다른 용량을 사용한다. 함수와 enum 이름이 같아도 동작 호환성이 깨지는 public contract
변경이다.

### 5.2 호환성 없이 어떤 계약으로 바꾸는가

이 목표안은 기존 `ZLINK_OPT_SNDHWM`과 `ZLINK_OPT_RCVHWM`의 단위를 message count에서 byte로
직접 바꾼다. 기존 동작과 source·binary 호환성을 제공하지 않는 breaking change이며,
호환 계층은 만들지 않는다.

- Count HWM과 byte HWM을 함께 유지하지 않는다.
- `SNDBYTESHWM`이나 `RCVBYTESHWM` 같은 compatibility option을 추가하지 않는다.
- Option value 길이나 값으로 이전 count 설정인지 추측하지 않는다.
- 이전 binding의 `message_count_t`를 compatibility alias로 남기지 않는다.
- 이전 binary가 전달하는 4-byte HWM 값은 명시적인 configuration error로 실패한다.

따라서 HWM은 모든 public API, 내부 admission과 monitoring에서 byte 한 단위만 사용한다.
이 breaking contract는 이 문서에 먼저 고정하고 같은 작업에서 Core header와 모든 bindings를
변경한다. Framework public contract 적용은 F-01 이후 별도 범위다.

### 5.3 기존 option의 새 타입과 기본값은 무엇인가

기존 option ID와 이름은 유지할 수 있지만 값의 타입과 단위를 명확히 바꿔야 한다.

| 항목 | 목표 의미 |
|---|---|
| `ZLINK_OPT_SNDHWM` | Directional send pipe의 byte HWM |
| `ZLINK_OPT_RCVHWM` | Directional receive pipe의 byte HWM |
| Option value type | `uint64_t` |
| `0` | 기존과 같이 무제한 |
| 수동 기본값 | `1,000 * 4,096 = 4,096,000 bytes` |
| Auto HWM 기본값 | Profile과 connection bucket이 계산한 byte 값 |

기존 `int` value와 새 64-bit value를 모두 받아 값에 따라 count와 byte를 추측해서는 안 된다.
호출자가 4-byte `int`를 전달하면 명시적 configuration error를 반환하고, 새 header와
bindings가 64-bit byte 값을 전달하게 해야 한다. 그래야 이전 binary가 조용히 `1,000
bytes`를 사용하는 문제를 막을 수 있다.

Auto HWM이 기본으로 활성화된 socket에서는 수동 기본값보다 Auto HWM 계획값이 우선한다.
수동 기본값 `4,096,000 bytes`는 Auto HWM을 끄거나 사용자가 수동 HWM을 선택했을 때의
기본값이다. 이 값은 connection별 HWM이므로 수천 connection의 process memory 상한으로
그대로 사용할 수 없다. Framework는 전체 Core reserve를 계산해 startup 시 검증해야 한다.

### 5.4 Auto HWM은 byte 값을 어떻게 계산하는가

현재 `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES`와
`ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`는 byte budget을 message slot으로 환산할 때 쓰는
message 크기다. Byte HWM에서는 이 역산 단계가 필요하지 않다.

이 설계에서는 두 planning unit option을 유지하되 byte HWM 계획값을 계산하는 입력으로
의미를 바꾼다.

```text
autoHwmBytes =
    selectedProfileSlots
    * autoHwmPlanningUnitBytes
```

- Non-STREAM 기본 planning unit은 `4,096 bytes`다.
- STREAM 기본 planning unit은 기존과 같이 `1,024 bytes`다.
- 이 값은 현재 workload의 실제 평균 message 크기가 아니다.
- Pipe admission은 planning unit이 아니라 실제 `bytesInFlight`를 사용한다.
- Profile의 slot cap은 planning byte 상한을 계산하는 입력으로만 사용한다.

이렇게 의미를 바꾸는 것도 public contract 변경이다. Option을 제거할지 planning unit
override로 유지할지는 다시 선택하지 않는다. 값 타입은 `uint64_t`로 바꾸고 이전 4-byte
호출과의 호환성은 제공하지 않는다.

### 5.5 Monitoring ABI는 무엇이 바뀌는가

현재 `zlink_monitor_status_t`에는 count 단위를 전제로 한 다음 field가 있다.

- `auto_hwm_socket_message_slots`
- `auto_hwm_size_cap`
- `auto_hwm_effective_message_bytes`
- `auto_hwm_applied_sndhwm`
- `auto_hwm_applied_rcvhwm`
- `auto_hwm_deferred_sndhwm`
- `auto_hwm_deferred_rcvhwm`

Applied HWM과 deferred HWM을 byte로 바꾸면서 기존 `int32_t` field 이름과 타입을 그대로
사용하면 consumer가 count로 잘못 해석할 수 있다. 목표 monitoring은 적어도 다음 값을
구분한다.

| Field 역할 | 목표 단위 |
|---|---|
| Auto HWM planning unit | bytes |
| Profile과 bucket이 선택한 slot | count diagnostic |
| 계획한 send·receive HWM | `uint64_t` bytes |
| 실제 적용한 send·receive HWM | `uint64_t` bytes |
| 축소가 보류된 send·receive HWM | `uint64_t` bytes와 별도 유효 여부 |
| 현재 send·receive bytes in flight | `uint64_t` bytes |
| Pending send·receive message | 기존과 같이 count |
| Oversize single-message admission | count와 최대 bytes |

새 field에는 `_bytes`가 드러나는 이름을 사용한다. ABI struct layout과 bindings의 snapshot
type이 바뀌므로 C header, monitoring spec과 모든 bindings를 같은 release에서 갱신해야 한다.

### 5.6 Core pipe는 byte를 어떻게 계산하고 반환하는가

Public option을 바꾸는 것만으로는 동작하지 않는다. Core의 directional pipe는 message
counter와 별도로 byte counter와 credit을 관리해야 한다.

Core에서 HWM에 반영하는 `accountedCoreMessageBytes`도 payload 크기만 사용하지 않는다.

```text
accountedCoreMessageBytes =
    max(
        allPayloadAndRoutingFrameBytes
        + retainedCoreMessageMetadataBytes,
        minimumCoreMessageChargeBytes)
```

Zero-length frame과 작은 message도 `minimumCoreMessageChargeBytes` 이상을 사용한다.
고정 charge는 `msg_t`, queue slot과 allocator chunk 비용을 benchmark로 측정해 정하고
monitoring에서 제공한다. Core memory와 정확히 일치시키려고 allocator를 message마다
조회하지 않는다. 실제 allocation 차이는 `coreMemoryAmplification`으로 반영한다.

```text
bytesInFlight =
    bytesWritten
    - peerBytesRead
```

일반 write admission은 다음 조건을 사용한다.

```text
normalWrite =
    bytesInFlight + accountedCoreMessageBytes
    <= hwmBytes
```

Pipe가 비어 있으면 HWM보다 큰 message도 해당 방향의 유한한 `MaxMessageSize` 이하인 한
건은 허용한다.

```text
emptyPipeException =
    messagesInFlight == 0
    AND noMultipartInProgress
    AND accountedCoreMessageBytes <= effectiveMaxMessageBytes
```

Multipart transaction은 기존 part 처리 과정에서 total accounted byte를 함께 누적한다.
Commit 전에 complete message 전체를 한 번 admission하고, 마지막 frame까지 같은 결과를
사용한다. HWM 검사를 위해 part나 payload를 다시 순회하지 않으며, 마지막 frame이 기록되기
전에 HWM 때문에 중단하여 불완전한 message를 receiver에 남기지 않는다.

Standalone Core raw socket에서 `MaxMessageSize`가 무제한이면 빈 pipe의 complete message 한
건 예외도 유한한 process memory 상한을 보장하지 않는다. 이는 per-pipe liveness 규칙으로만
취급한다. 끝나지 않은 multipart에는 이 예외를 적용하지 않고 일반 byte HWM을 적용한다.
Framework의 memory 제한 모드는 유한한 `MaxMessageSize`를 startup 조건으로 강제하므로
`coreTransportReserve`를 계산할 수 있다.

Receiver가 읽은 누적 byte와 마지막으로 알린 byte의 차이가 byte LWM 이상이면 sender에
credit을 반환한다.

```text
lwmBytes =
    ceil(hwmBytes / 2)

returnCredit =
    bytesReadSinceLastCredit >= lwmBytes
    OR (
        senderBlockedAtHwm
        AND visibleInputDrained
    )
```

이 Core pipe LWM은 transport credit을 반환하는 주기를 정하며 HWM의 절반으로 고정한다.
두 번째 조건은 sender가 HWM 판정에 실패한 경우에만 사용한다. Sender는 실패 시 peer가 이미
공개한 누적 read snapshot을 한 번 확인하고, 이후 receiver가 입력을 모두 읽으면 조기 credit을
한 번 보낸다. 따라서 LWM-only 방식에서 작은 backlog가 남기는 stall을 없애면서 정상
ping-pong message마다 mailbox를 깨우지 않는다.
Framework Auto HWM profile을 변경하면 Framework가 사용하는 Core context에도 같은 profile을
설정하므로 자동 HWM을 사용하는 Core pipe의 HWM과 그 절반인 LWM이 다시 계산될 수 있다.
Framework Application HWM은 같은 profile의 application memory 비율로 별도 계산하며, 그 byte
값을 Core pipe HWM으로 복사하지 않는다.

다음 경로에서 byte를 누락하거나 두 번 반환하지 않아야 한다.

- Single-part와 multipart write·read
- Routing identity와 credential frame
- Rollback과 partially written multipart
- Pipe termination, reconnect와 hiccup
- Runtime HWM 증가와 deferred shrink
- Inproc 양쪽 HWM 결합과 HWM boost
- Conflate와 무제한 HWM

Inproc처럼 양쪽 endpoint의 HWM을 합산하는 경로는 byte 합산으로 바꾸고 64-bit overflow를
검사해야 한다. 현재 사용량보다 HWM을 작게 변경해도 기존 message를 제거하지 않고,
`bytesInFlight`가 새 LWM 아래로 내려갈 때까지 새 write만 중단한다.

HWM option은 local pipe admission 설정이므로 byte accounting 자체는 network frame format을
바꿀 필요가 없다. 별도 Completion connection이나 새 control protocol을 추가한다면 그것은
이 변경과 다른 wire protocol 작업이다.

## 6. 저장소의 어느 부분이 바뀌는가

| 범위 | 필요한 변경 |
|---|---|
| C public header | Option value type·단위, 기본값과 monitoring struct를 변경한다. |
| Core option storage | `sndhwm`, `rcvhwm`, deferred HWM과 Auto HWM 계획값을 64-bit byte로 바꾼다. |
| `pipe_t` | Byte written·read·peer credit, admission, LWM, multipart와 rollback을 구현한다. |
| Socket lifecycle | Endpoint pair, inproc 합산, reconnect와 runtime HWM 갱신에 byte 값을 전달한다. |
| Peer connection 관리 | 두 connection을 같은 peer pair로 검증하고 generation·epoch·sequence fence로 connection 사이 순서를 보장한다. |
| Request/reply runtime | 내부 payload queue를 제거하고 reply를 Completion connection에서 직접 완료한다. Handler 실행 전 completion send permit을 확보하고 request·reply byte의 소유권 전환을 한 번만 계산한다. |
| Auto HWM | Profile·bucket 결과를 message slot이 아니라 최종 byte HWM으로 적용한다. |
| Monitoring | Applied·deferred HWM, in-flight byte, pause, permit, owner attribution과 completion reserve를 제공한다. |
| Core clean review | 동일한 immutable Core candidate를 Codex 5.6 High(`gpt-5.6-sol high`)가 전체 검토하고, `Medium` 이상 finding이 0인지 확인한다. |
| Core perf smoke | Core clean review를 통과한 candidate로 `bindings/c/perf`의 single·multi 전체 경로가 실행되는지 확인한다. |
| Bindings | .NET, Java, Node.js와 C++에서 C 값 타입과 단위를 각 언어의 64-bit byte 타입으로 노출한다. |
| Bindings clean review | 동일한 immutable bindings candidate를 Codex 5.6 High가 검토하고 `Medium` 이상 finding이 0인지 확인한다. |
| Bindings perf smoke | Bindings clean review를 통과한 candidate에서도 같은 C single·multi smoke가 통과하는지 다시 확인한다. |
| Framework | Host budget owner, owner-local attribution, HWM 기반 수신 중단·재개, completion send permit과 nested overload 관측을 구현한다. |
| Benchmark와 test | 기존 HWM 환경 변수와 fixture를 count에서 byte로 바꾸고 migration 값을 기록한다. |
| Spec과 guide | 이 문서, 정식 socket option, monitoring, bindings와 migration 문서를 함께 갱신한다. |

C++ binding처럼 현재 HWM을 `message_count_t`로 표현하는 binding은 값 타입 이름도
`byte_count_t` 또는 byte 전용 타입으로 바꿔야 한다. 단순히 내부 정수만 크게 만들어서는
호출자가 단위를 알 수 없다.

## 7. 최종 결과는 무엇을 검증해야 하는가

1. 4-byte 기존 HWM option value가 조용히 byte로 해석되지 않고 명시적으로 실패한다.
2. 64-bit 수동 HWM의 set/get과 `0=무제한` 계약을 검증한다.
3. 수동 기본값 `4,096,000 bytes`와 모든 Auto HWM profile·bucket 값을 검증한다.
4. HWM 이하 message, HWM보다 큰 빈-pipe message 한 건과 후속 write 대기를 검증한다.
5. Single-part, multipart, routing identity, rollback과 termination의 byte credit을 검증한다.
6. Runtime HWM 증가와 축소가 기존 queued message를 제거하지 않는지 검증한다.
7. TCP, IPC와 inproc에서 effective directional byte HWM을 검증한다.
8. Monitoring과 모든 bindings가 같은 byte 값과 유효 범위를 제공하는지 contract test로
   확인한다.
9. 기존 count HWM과 byte HWM의 peak memory, throughput과 tail latency를 같은 workload로
   비교한다.
10. Network wire fixture가 byte HWM 변경만으로 달라지지 않는지 확인한다.
11. Request/reply helper를 활성화해도 application·reply payload용 내부 PAIR socket,
    receive queue나 completion deque가 생성되지 않는지 확인한다.
12. Application connection과 Completion connection의 pair identity, 연결 순서, 한쪽
    connection의 단절·protocol error 때 pair 전체가 종료되는지, 재연결 뒤 이전
    connection에서 늦게 도착한 reply가 적용되지 않는지 검증한다.
13. Zero-length, 작은 message와 routing frame에 minimum byte charge가 적용되는지 검증한다.
14. 유한한 `MaxMessageSize`, HWM보다 큰 message의 빈 budget 한 건 허용, 명시적
    `ApplicationHwmBytes=0`의 무제한 동작과 Auto mode의 유한한 계산 결과를 startup contract
    test로 검증한다.
15. Core가 마지막 frame을 commit하기 전에는 incomplete multipart를 Framework에 공개하지
    않고, 완성된 큰 multipart를 한 건의 message로 `Recv`하는지 검증한다.
16. 최대 pending request 수에 도달하면 새 request admission만 대기하고 기존 completion은
    계속 처리되는지 검증한다.
17. 목표 최대 connection 수에서 요청자·응답자 `completionReserve`,
    `coreTransportReserve`와 Framework pending payload를 함께 채워도 process memory limit을
    넘지 않는지 검증한다.
18. HWM 기능을 끈 기준 build와 비교하여 새 allocation·mutex·system call이 hot path에
    추가되지 않았는지 확인하고 throughput, CPU/message와 p99 latency를 기록한다.
19. 복수 MeshNode와 복수 I/O thread에서 작은 message를 처리하여 현재 MeshNode별 병렬 drain
    대비 aggregate throughput과 scaling이 허용 범위 안에 있는지 검증한다.
20. 수신 누적 payload에서 완료 누적 payload를 뺀 값이 host pending payload와 일치하는지
    검증한다. 한 Spot이나 Actor가 대부분의 pending payload byte를 보유하면 queued, active
    handler, unattributed, infrastructure 합계와 명시적 top-N 진단에서도 같은 owner가
    확인되어야 한다.
21. A와 B가 동시에 HWM에 도달한 상태에서 양방향 nested request를 보내고, Completion
    connection과 local timeout이 계속 진행되어 handler terminal 뒤 pending payload 합계가
    줄고 completion reserve와 ingress가 회복되는지 검증한다.
22. Completion send permit이 host 상한에 도달하면 추가 request handler admission이
    대기하고 request byte가 Application budget에 남는지 검증한다. Handler 실행을 시작하면
    original request의 queue lease가 반환되고 미전송 reply count와 byte는 responder
    completion reserve 상한을 넘지 않으며, Core가 send를 받아들일 때 두 reserve 사이에서
    byte가 누락되거나 이중으로 계산되지 않아야 한다.
23. HWM과 `MaxMessageSize` 조합별로 처리 중인 payload 합계가 HWM보다 작을 때 시작한 message는
    완료하고, 합계가 HWM에 도달하거나 초과한 뒤에는 새 `Recv`를 시작하지 않으며,
    connection별 미사용 chunk reserve가 생기지 않는지 검증한다. 복수 ingress poller가 이미
    시작한 message는 완료될 수 있음을 함께 검증한다.
24. Application message와 request completion을 두 connection에서 양방향으로 추월시켜도
    generation, epoch와 sequence가 stale completion 적용과 순서 역전을 막는지 검증한다.
    Fence 대기 상한을 넘으면 pair 전체와 pair generation이 종료되고 해당 pending request가
    terminal error로 끝나는지도 확인한다.
25. `applicationPendingPayloadBytes`가 HWM에 도달하거나 초과하면 새 수신을 중단하고, handler
    terminal로 HWM보다 작아지면 수신을 재개하는지 검증한다. Framework Auto HWM profile을 변경하면
    Framework가 사용하는 Core context의 `ZLINK_CTX_OPT_AUTO_HWM_PROFILE`도 같은 값으로
    변경되는지 검증한다.
26. Framework에서 `ApplicationHwmBytes`를 생략하면 Application 할당 memory에 선택한 profile
    비율을 곱하는지 확인한다. `COMPACT=2%`, `LOW_LATENCY=5%`, `BALANCED=10%`,
    `THROUGHPUT=20%`를 사용하고 profile을 생략하면 `BALANCED`를 적용하는지 검증한다. 양수를
    설정하면 host 전체 pending payload에 해당 byte 값을 사용하고, `0`을 명시하면 무제한으로 동작하는지
    검증한다. Auto mode에서는 profile 이름만 Core context에 동일하게 설정하고 Framework가
    계산한 byte HWM을 connection별 Core HWM으로 복제하지 않는지도 contract test로 검증한다.
27. Core clean review를 통과한 candidate에서 `bindings/c/perf`의 single·multi 전체 pattern과
    transport를 64B로 실행하고 두 report가 `status=complete`인지 검증한다.
28. Bindings clean review를 통과한 candidate에서도 같은 C perf smoke를 다시 실행하고,
    `core/build` runtime provenance와 두 report를 binding package manifest와 함께 기록한다.

## 8. 어떤 순서로 적용하는가

최종 승인 뒤 작업은 Core 구현, Core clean review, Core perf smoke, bindings 구현,
bindings clean review, bindings perf smoke, Framework 적용의 일곱 단계로 나눈다. 각 단계는
바로 앞 단계의 완료 조건과 증거를 확인한 뒤 시작한다. 뒤 단계의 코드를 먼저 추가하여 앞
단계의 미완성 계약을 우회하지 않는다.

### 8.1 1단계: Core 라이브러리 수정

이 단계의 결과는 C header, Core runtime과 test가 message count가 아닌 byte 하나의 단위로
HWM을 처리하고, peer마다 Application·Completion connection pair를 제공하는 것이다.

1. 이 문서와 C header에 breaking contract를 함께 반영한다. 기존 `ZLINK_OPT_SNDHWM`과
   `ZLINK_OPT_RCVHWM`은 유지하되 값 타입을 `uint64_t`, 단위를 byte로 바꾼다. `0`은
   무제한으로 유지하고 4-byte 기존 값은 명시적인 configuration error로 거부한다.
2. 수동 기본값, Auto HWM planning unit, profile·connection bucket 계산과
   `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES` 계열 option의 64-bit 계약을 확정한다.
3. Monitoring struct를 version하고 계획값, 적용값, 축소 보류값과 현재 in-flight 값을
   `_bytes`가 드러나는 64-bit field로 바꾼다. Message count 진단값과 byte 제한값을 서로
   다른 field로 제공한다.
4. `pipe_t`는 payload, routing frame, metadata와 minimum charge를 write 과정에서 한 번
   계산한다. Byte admission, credit 반환, HWM 절반의 Core LWM, runtime HWM 변경, inproc
   합산, reconnect, termination, multipart commit과 rollback에 같은 계산을 적용한다.
5. 빈 pipe에는 HWM보다 큰 message 한 건을 허용하되 해당 방향의 유한한
   `MaxMessageSize`를 넘지 못하게 한다. HWM 검사를 위해 payload를 다시 순회하거나
   message별 allocation·mutex·system call을 추가하지 않는다.
6. Peer마다 Application connection과 Completion connection을 하나씩 설정한다. Handshake는
   peer identity, pair identity와 generation을 검증한다. 한 connection이 끊기거나 protocol
   error가 발생하면 pair 전체를 종료하고 이전 generation의 reply를 폐기한다.
7. Request와 application message는 Application connection으로, reply와 bounded
   liveness·admission·relocation·reply recovery service control은 Completion connection으로 전달한다.
   Send-ready는 기존 Core callback으로 전달한다. Application payload를 옮기던 내부 PAIR
   `recv_queue`, reply payload를 보관하던 completion deque와 signal socket을 제거한다.
8. Core는 incomplete multipart를 reader에 공개하지 않는다. Completion poller가 reply를
   받으면 별도 deque에 다시 보관하지 않고 pending request를 직접 완료할 수 있는 record를
   제공한다.
9. C unit·integration test, transport별 fixture, wire fixture와 benchmark로 §7의 Core 관련
   항목을 검증한다.
10. 구현과 test가 완료되면 Core의 정식 socket option, monitoring, 오류와 내부 구조 문서를
    실제 C header와 동작에 맞춘다.

1단계는 다음 조건을 모두 만족해야 완료된다.

- 이 문서와 C header가 동일한 64-bit byte 계약을 정의한다.
- TCP, IPC와 inproc의 byte HWM, Auto HWM, monitoring, multipart와 runtime HWM test가
  통과한다.
- Application·Completion connection의 pair 생성, 종료, reconnect와 stale generation
  차단을 wire fixture로 확인한다.
- Request/reply 기능을 사용해도 제거 대상 PAIR queue와 completion deque가 생성되지 않는다.
- HWM을 끈 기준 build와 비교한 Core 성능 결과와 memory amplification 측정값을 기록한다.
- Core 정식 spec, C header와 test가 같은 타입, 단위, 기본값과 오류를 설명한다.

### 8.2 2단계: Core clean review

이 단계는 1단계 결과가 문서의 계약을 빠짐없이 구현했으며, 구조와 성능 측면에서도 bindings에
전파할 수 있는 상태인지 확인한다. Codex 5.6 High(`gpt-5.6-sol high`)가 immutable Core
candidate의 전체 범위를 검토한다. 같은 candidate에서 `Medium` 이상 finding을 한 건도
남기지 않은 경우에만 `CLEAN`으로 판정한다. 2026-07-30 사용자 결정에 따라 이후 review에는
다른 reviewer를 추가하지 않는다.

#### 검토 입력과 기록

Review coordinator는 각 round를 시작하기 전에 candidate commit SHA와 비교 기준 commit을
고정한다. Codex reviewer에 범위, severity 기준과 질문을 제공한다. 다음 자료를 전체 범위로
검토한다.

- 이 설계 문서와 1단계에서 갱신한 Core 정식 spec
- `AGENTS.md`, [스펙 문서 작성 가이드](../../../doc/principal/documentation/spec-writing-guide.ko.md),
  [source comment 원칙](../../../doc/principal/source-comment-principles.ko.md),
  [POSDDD 설계 원칙](../../../doc/principal/dev/posddd.ko.md)과
  [ZLink 시스템 개발 원칙](../../../doc/principal/dev/zlink-system-design-principles.ko.md)
- C public header, Core source, test, benchmark와 monitoring 변경
- 비교 기준 commit부터 candidate commit까지의 전체 diff
- 이전 round finding과 수정 commit, test·benchmark 결과

각 실행에는 reviewer 이름, 실제 model ID와 version, reasoning level, candidate SHA, 검토
prompt·rubric version과 실행 시각을 기록한다. 지정한 reviewer를 사용할 수 없으면 임의의
model로 대체하지 않고 이 단계를 `차단`으로 둔다. 역사적 결과는
[Core review 기록](https://github.com/kairos-code-dev/zlink/blob/3291e338f4f700484780560cd81345a647ef0948/framework/doc/plan/log/inbound-dispatch-lane-core-review.ko.md)에서 확인한다. 이 기록은 현재 plan 묶음에서 제거된 legacy ledger다.

#### 공통 검토 기준

Reviewer는 다음 질문에 답한다.

1. 이 문서의 지시와 수식, breaking contract, 제거 항목, 오류, monitoring과 검증 항목을
   잘못 해석하거나 구현하지 않은 부분이 있는가. 문서에 없는 호환 경로나 추가 동작을
   도입했는가.
2. POSD 관점에서 shallow module, information leakage, pass-through method·variable,
   temporal decomposition, 중복, 특수 경로와 범용 경로의 혼합, 호출자에게 전달된 복잡성이
   있는가.
3. DDD 관점에서 byte accounting, pipe admission, connection pair, request completion,
   ownership, lifecycle, state transition과 failure invariant의 owner가 분명한가. Core,
   bindings와 Framework의 책임이 섞이거나 transport·codec·storage detail이 상위 계약에
   노출되었는가.
4. Hot path에 불필요한 allocation, payload 재순회, copy, atomic, lock, system call, branch,
   queue scan이나 cache contention이 추가되었는가. Memory amplification, throughput,
   CPU/message, p99 latency와 다중 connection scaling을 개선할 여지가 있는가.
5. 제거하기로 한 queue, deque, signal socket과 count 호환 경로가 남아 있거나 사용되지 않는
   code·option·test가 새 계약을 흐리는가.

POSD 또는 DDD finding은 위반한 원칙과 책임 경계를 적는다. 비자명한 구조 변경은 서로 다른
대안 두 가지 이상을 제시하고 단순성, 일반성, 성능과 호출자 부담을 비교한 뒤 권장안을
선택한다. Public interface를 바꾸는 권장안은 호출자 복잡성이 실제로 줄어드는지도 확인한다.
성능 권장안은 먼저 critical path를 측정하고, 변경 전·후 결과가 의미 있게 달라지지 않으면
추가한 복잡성을 되돌리는 조건을 포함한다.

#### Severity와 `CLEAN` 판정

| Severity | 판정 기준 | Gate 결과 |
|---|---|---|
| `Critical` | Data corruption, security 문제, ABI·wire 파손, deadlock 또는 유계가 아닌 memory 증가가 발생한다. | `NOT CLEAN` |
| `High` | 핵심 contract·invariant를 위반하거나 실제 workload에서 큰 correctness·성능 regression이 발생할 가능성이 높다. | `NOT CLEAN` |
| `Medium` | 다음 단계 전에 고쳐야 할 누락, 책임 경계 누출, 구체적인 리팩토링 대상 또는 측정 가능한 성능 위험이 있다. | `NOT CLEAN` |
| `Low` | 현재 계약과 다음 단계의 안전성을 해치지 않는 선택적 개선이다. | 근거와 disposition을 기록하면 통과 가능 |

각 finding에는 round, reviewer, ID, severity, category, file·line, 근거, 위반한 지시나 원칙,
영향, 대안 둘 이상, 권장안, 처리 결과, fix commit과 재검토 결과를 기록한다. False positive나
severity 하향은 반증 자료와 해당 reviewer의 재검토가 있어야 하며, coordinator의 판단만으로
finding을 닫지 않는다. Reviewer 간 severity가 다르면 근거로 합의하기 전까지 더 높은
severity를 적용한다.

Review는 다음 순서로 반복한다.

1. 고정한 candidate를 Codex reviewer가 전체 범위로 검토한다.
2. 결과를 합쳐 `Critical`, `High` 또는 `Medium` finding이 하나라도 있으면
   `NOT CLEAN`으로 판정한다.
3. 구현 담당자가 finding을 수정하고 관련 test와 benchmark를 실행한 뒤 새 candidate SHA를
   만든다.
4. Codex reviewer가 새 candidate의 전체 범위를 다시 검토한다. 수정 diff만 검토해서는 안 된다.
5. 최신 round의 candidate SHA에서 `Medium` 이상 finding이 0건일
   때만 `CLEAN`으로 판정한다. 남은 `Low` finding은 owner와 disposition을 기록한다.

2단계에서 Core code, public header, contract 또는 test가 바뀔 때마다 이전 `CLEAN` 판정은
무효다. 새 candidate로 Codex 전체 review를 다시 통과해야 한다.

### 8.3 3단계: Core `bindings/c/perf` smoke

이 단계는 2단계에서 `CLEAN`으로 판정한 Core candidate가 C 기준 benchmark의 모든 single·multi
패턴과 transport를 실행할 수 있는지 확인한다. 성능 수치를 승인하는 단계가 아니라 runtime,
runner와 주요 data path가 정상적으로 연결되는지를 확인하는 gate다.

먼저 같은 candidate source로 공식 Core runtime을 다시 만든다. `build_cpp_release`나 다른 임시
build 결과를 사용하지 않는다.

```bash
# Review를 통과한 Core source로 공식 runtime을 만든다.
cmake --build core/build

# 64B 하나로 single의 전체 pattern과 transport를 확인한다.
bindings/c/perf/run_benchmarks.sh --pattern ALL --msg-sizes 64

# 64B 하나로 multi의 전체 pattern과 transport를 확인한다.
bindings/c/perf/run_benchmarks_multi.sh --pattern ALL --msg-sizes 64
```

두 runner가 출력한 실제 `libzlink.so` 경로가 `core/build` 아래에 있고 candidate source보다
오래되지 않았는지 확인한다. Single과 multi report가 모두 `status=complete`이고 전체
pattern·transport 조합에 `fail`이 없어야 통과한다. Candidate SHA, runtime 절대 경로와
SHA-256, build 명령, 두 report 위치를 진행표에 기록한다.

Smoke가 실패하면 3단계를 통과하지 못한다. 원인을 수정하여 source candidate가 바뀌면 2단계의
Codex review부터 다시 수행한다. 실행 환경 문제만 수정했고 candidate source가 바뀌지
않았다면 같은 SHA로 smoke를 다시 실행하고 원인과 재실행 결과를 기록한다.

### 8.4 4단계: bindings 라이브러리 수정

이 단계의 결과는 .NET, Java, Node.js와 C++ binding이 3단계까지 통과한 Core 계약을 같은
단위와 범위로 제공하는 것이다. Java binding은 Java와 Kotlin Framework가 함께 사용한다.

각 binding은 구현 전에 해당 언어의 exact interface에 다음 계약을 기록한다.

- HWM과 Auto HWM planning unit은 64-bit byte 값이다.
- `0=무제한`, 수동 기본값과 4-byte 기존 값 거부 결과는 Core와 같다.
- Applied·deferred HWM과 in-flight monitoring 값은 64-bit byte다.
- Message slot이나 pending message count는 count 진단값으로 남고 byte field와 이름을
  공유하지 않는다.
- 기존 `message_count_t`처럼 count 의미를 포함한 public type 이름은 byte 전용 이름으로
  바꾼다. 이전 이름을 alias나 adapter로 유지하지 않는다.

언어별 작업 범위와 완료 조건은 다음과 같다.

| Binding | 수정 범위 | 완료 조건 |
|---|---|---|
| .NET | Socket option 값, context Auto HWM 설정, monitoring snapshot과 request/reply wrapper를 새 Core ABI에 맞춘다. | 64-bit 경계값, `0`, 기본값, 기존 32-bit 호출 실패와 monitoring contract test가 통과한다. |
| Java | Java/Kotlin이 공유하는 option 타입, native bridge, monitoring record와 request/reply wrapper를 바꾼다. | Java와 Kotlin consumer가 같은 byte 값을 읽고 경계값 contract test가 통과한다. |
| Node.js | JavaScript의 안전한 정수 범위를 넘는 byte 값도 손실 없이 전달할 수 있는 공개 표현과 native bridge를 확정한다. | 최대 지원값의 왕복, 잘못된 타입 거부와 monitoring contract test가 통과한다. |
| C++ | Count 의미의 wrapper 타입을 byte 전용 타입으로 바꾸고 option, monitoring과 request/reply wrapper를 갱신한다. | Compile-time interface test와 64-bit 값 왕복 test가 통과한다. |

모든 binding package는 같은 Core contract version을 사용해야 한다. 네 binding 가운데 하나라도
이전 count 의미를 유지하면 4단계를 완료한 것으로 보지 않는다. 각 package는
[local package 배포 절차](../../../scripts/local-package/README.ko.md)를 따라 만들고, consumer
test를 마친 뒤 version과 artifact 위치를 진행표에 기록한다.

### 8.5 5단계: bindings clean review

이 단계는 4단계의 bindings 변경을 Codex 5.6 High(`gpt-5.6-sol high`)가 검토한다. Claude
Fable은 bindings review에 사용하지 않는다. Candidate commit SHA, 비교 기준, prompt·rubric
version과 reviewer 실행 정보를 고정한다. 역사적 결과는
[bindings review 기록](https://github.com/kairos-code-dev/zlink/blob/3291e338f4f700484780560cd81345a647ef0948/framework/doc/plan/log/inbound-dispatch-lane-bindings-review.ko.md)에서 확인한다.

검토 범위는 2단계의 공통 검토 기준과 severity를 그대로 사용하되, 다음 항목을 네 binding과
package 전체에서 확인한다.

- Core C ABI의 64-bit byte 값, 오류, lifetime과 ownership을 손실 없이 표현하는가.
- .NET, Java·Kotlin, Node.js와 C++의 public contract가 같은 의미를 제공하며 언어별 변환
  과정에서 overflow, truncation이나 count 단위가 다시 생기지 않는가.
- Binding이 Core 또는 Framework의 책임을 가져오지 않으며 pass-through layer, 중복 변환,
  private API 접근이나 호환 adapter를 추가하지 않았는가.
- Native call과 monitoring hot path에 불필요한 allocation, copy, boxing, JNI·N-API 전환,
  lock 또는 반복 변환이 없는가.
- Exact interface, package metadata, consumer test와 실제 구현이 일치하는가.

`Critical`, `High` 또는 `Medium` finding이 하나라도 있으면 `NOT CLEAN`이다. 구현 담당자가
수정하고 contract·consumer test와 필요한 benchmark를 실행한 뒤 새 SHA를 만든다. Codex는
새 SHA의 전체 범위를 다시 검토한다. 최신 round에서 `Medium` 이상 finding이 0건일 때만
`CLEAN`으로 판정하며, `Low` finding은 owner와 disposition을 기록한다.

Finding 기록, 두 가지 이상 대안 비교, caller complexity 확인, 성능 측정과 false positive
처리는 2단계 규칙을 그대로 적용한다. 수정이 Core code, Core public header나 Core contract를
건드리면 5단계에서 계속 진행하지 않고 2단계 Core clean review부터 다시 시작한다.

### 8.6 6단계: bindings 이후 `bindings/c/perf` smoke

5단계에서 `CLEAN`으로 판정한 bindings candidate에서도 §8.3의 build와 C single·multi smoke
명령을 동일하게 실행한다. 이 gate는 bindings 변경 뒤 Core ABI와 C 기준 runtime이 의도하지
않게 달라지지 않았는지 확인한다. C perf는 binding package를 직접 사용하지 않으므로 package와
Core의 조합은 4단계 consumer test가 검증한다.

통과 조건과 증거는 §8.3과 같다. Candidate SHA, Core runtime 절대 경로와 SHA-256, package
version·artifact manifest, single·multi report 위치를 함께 기록한다. Smoke 원인을 수정하면서
bindings source가 바뀌면 5단계 review를 다시 수행한다. Core source나 contract가 바뀌면 2단계로
돌아가 Core review, Core smoke와 bindings 단계를 순서대로 다시 통과해야 한다.

### 8.7 7단계: Framework 적용

이 단계의 결과는 Framework host가 Core와 bindings의 byte HWM을 사용하여 process 전체의
처리 중인 application payload 합계를 제한하고, application 수신이 중단되어도 request completion을 계속
처리하는 것이다.

1. 구현 전에 [Framework 공개 계약 관리 절차](../framework/common/spec/00-public-contract-governance.ko.md)에
   따라 공통 spec과 .NET, Java, Kotlin, Node.js, C++ exact interface에
   설정 여부를 구분하는 `ApplicationHwmBytes`, `ApplicationHwmProfile`, 유한한
   `MaxMessageSize`, startup error, 미설정·양수·`0`의 선택 규칙과 monitoring 계약을 기록한다.
2. 각 Framework는 6단계까지 통과한 binding package version을 중앙 dependency 위치에서
   참조한다. Binding source를 직접 참조하거나 private API로 우회하지 않는다.
3. Application·Completion connection을 함께 poll하되 Application HWM에 도달하면
   Application `Recv`만 중단한다. Core에서 받은 application message를 다른 payload queue로
   복사하거나 이동하지 않는다.
4. Host 전체 `applicationPendingPayloadBytes`를 관리한다. Complete message를 `Recv`하면 내부
   HandlerContext에 payload byte를 저장하고 수신 누적값에 더한다. 기존 handler terminal 통지에
   저장한 값을 포함하고 Application HWM owner가 완료 누적값에 더한다. 두 누적값의 차이로
   pending payload를 계산하며 기존 ingress poller 병렬성을 유지한다.
5. Request handler를 실행하기 전에 completion send permit을 확보한다. Permit 상한에
   도달하면 request를 Application queue와 budget에 남긴다. Handler 실행을 시작하면 request의
   queue lease를 반환한다. Handler terminal에서는 completion permit을 실제 reply byte로
   바꾸고, Core가 reply를 받으면 Core transport reserve로 소유권을 넘긴다.
6. `ApplicationHwmBytes`가 생략되면 §4의 계산 순서로 Auto HWM을 결정하고, 양수이면 해당
   byte 값을 사용하며, 명시적인 `0`이면 무제한으로 동작한다. Auto mode에서 검증된 profile을
   확인할 수 없으면 startup을 실패시킨다. Auto mode에서 선택한 profile은 Framework가 사용하는
   Core context의 `ZLINK_CTX_OPT_AUTO_HWM_PROFILE`에도 동일하게 설정한다.
7. Framework monitoring은 pending application payload 합계와 queued·active handler 구분,
   적용한 HWM, pause,
   requester·responder completion reserve, completion send permit과 pending request를
   queue 순회 없이 제공한다. Actor ID·Spot ID와 owner별 top-N은 public status·metric에
   노출하지 않는다. 장시간 pause의 내부 진단 log도 message hot path에 owner별 event를 만들지 않는다.
8. 양방향 nested request, pair reconnect, timeout, cancellation, shutdown, relocation과
   두 connection 사이의 순서 역전을 검증한다.
9. 다섯 Framework 언어에서 같은 public 동작과 E2E를 제공하고, memory·throughput·CPU/message·
   p99 latency와 다중 MeshNode scaling 결과를 기록한다.
10. 다섯 언어의 server guide에 §4.6의 처리량 정의, HWM 후보 계산식과 검증 조건을 같은
    의미로 제공한다. Warm-up, 반복 실행, workload 구성, 언어별 command와 monitoring API처럼
    실제 측정에 필요한 절차와 예제를 추가한다.

7단계는 §7의 모든 통합 검증을 통과하고, 정식 spec·guide·monitoring 문서가 구현과 일치해야
완료된다.

## 9. 진행표

상태는 `대기`, `진행 중`, `완료`, `차단`, `범위 밖` 가운데 하나를 사용한다. 현재 승인
범위는 F-09까지다. 완료 증거에는 test 명령, 결과 log, contract 문서, commit 또는 artifact
위치를 기록한다.

2026-07-30 기준으로 C-01부터 C-06까지의 Core 구현과 C-08 정식 문서 동기화를 완료했다.
64-bit byte HWM, Auto HWM, monitoring ABI v2, Application·Completion pair와 숨은 payload
queue 제거가 반영되었다. `cmake --build core/build -j 6`과
`ctest --test-dir core/build --output-on-failure -j 1`을 실행해 Core test 80/80이
통과했다. 여기에는 public surface contract, TCP·IPC·inproc transport, request/reply,
pair reconnect, byte backpressure와 monitoring 회귀가 포함된다. `test_connect_rid` 20회
반복과 동시 connect·disconnect case도 통과했고, 동시 case는 Valgrind error 0건을
확인했다.

그 뒤 req/rep 경로에서 byte HWM이 드러낸 세 결함을 수정했다. 처리 내역과 근거는
[reqrep multipart rollback review](https://github.com/kairos-code-dev/zlink/blob/3291e338f4f700484780560cd81345a647ef0948/framework/doc/plan/log/inbound-dispatch-lane-reqrep-multipart-rollback-review.ko.md)
§9에 있다.

| 결함 | 근본 원인 | commit |
|---|---|---|
| Completion reply가 약 4 MiB 뒤 backpressure에서 회복하지 못함 | Reply는 `send()`를 거치지 않으므로 submit 경로가 socket command를 배수하지 않았고, peer의 activate-write command가 mailbox에 남아 `_out_active`가 false로 고정됨 | `563e11d614` |
| 실패한 multipart의 앞선 frame이 다음 message와 병합됨 | `lb` one-pipe fast path·DEALER `xrollback`·`dist` per-pipe 실패 경로가 rollback하지 않음 | `563e11d614` |
| PUB blocking publish가 block 대신 drop | Multipart byte admission을 모든 frame에 적용해 `*_no_recursive_hwm_check` writer의 whole-message 보장을 깨뜨림 | `58aa55df8b` |

이어서 C perf runner를 transport 전체로 돌리자 paired transport(DEALER·ROUTER)가 inproc에서
`ZLINK_EVENT_CONNECTION_READY`를 전혀 발행하지 않는 결함이 드러났다. Data 경로는 정상인데
readiness 대기가 timeout된다. Application·Completion pair(C-05)의 inproc 누락이며, Core test
80/80이 통과한 이유는 paired transport의 inproc readiness coverage가 없었기 때문이다. inproc
경로가 lane마다 pair-aware 발행을 호출하도록 고치고, pair readiness key에서 peer routing id를
제거해 `(endpoint, pair id, generation)`으로 식별하게 했다(`0830b29317`). 회귀 test 3건을
`test_monitor_socket_contract`에 추가했다.

수정 뒤 `ctest --test-dir core/build -j 4`가 80/80으로 통과했고, 각 수정은 되돌리면 해당
회귀 test가 실패하는 것까지 확인했다.

C-07 비교는 baseline worktree(`8bc2aa6786`)에 같은 perf fixture reply 경로를 이식해 측정
경로 code를 동일하게 맞춘 뒤, 양쪽에서 같은 runner 호출로 실행했다. Byte HWM이 여섯
transport 모두에서 throughput이 높고 tail latency가 낮다. tcp는 261,030 → 330,500 msg/s
(+26.6 %), inproc은 347,740 → 578,370 msg/s(+66.3 %)이고 두 report 모두
`status=complete`다. Memory amplification은 `core/study/hwm-bytes/` 하네스로 측정했다.
Accounted byte 기준으로 64 B payload에서 1.38, 1 KiB에서 1.04,
64 KiB에서 1.00이다. 측정 방법과 전체 수치는
[reqrep multipart rollback review](https://github.com/kairos-code-dev/zlink/blob/3291e338f4f700484780560cd81345a647ef0948/framework/doc/plan/log/inbound-dispatch-lane-reqrep-multipart-rollback-review.ko.md)
§9.6과 §9.7에 있다.

이로써 Core 1단계 완료 조건을 모두 만족한다. Clean review, perf smoke와 bindings 작업은
아직 시작하지 않았다.

| ID | 단계 | 작업 | 완료 증거 | 상태 |
|---|---|---|---|---|
| C-01 | Core | 이 문서와 C header의 64-bit byte HWM 계약 확정 | 64-bit option·4-byte 거부·기본값 test와 Core 80/80 통과 | 완료 |
| C-02 | Core | `pipe_t` byte accounting, admission과 credit 반환 | Byte admission·credit·multipart·transport 회귀를 포함한 Core 83/83 통과 | 완료 |
| C-03 | Core | Auto HWM, 기본값과 runtime HWM 변경 | Context·typed option·runtime HWM test를 포함한 Core 80/80 통과 | 완료 |
| C-04 | Core | Monitoring ABI version과 byte field 적용 | Monitoring ABI v2 header·snapshot·contract test 통과 | 완료 |
| C-05 | Core | Application·Completion connection pair와 handshake | Request/reply·handover·reconnect·transport matrix를 포함한 Core 83/83 통과 | 완료 |
| C-06 | Core | 내부 PAIR receive queue와 completion deque 제거 | Payload queue source 제거와 request/reply 전체 회귀 통과 | 완료 |
| C-07 | Core | Core memory·성능·wire regression 검증 | Core 83/83, targeted 20회와 Valgrind error 0건. req/rep 결함 3건(`563e11d614`, `58aa55df8b`)과 inproc pair readiness(`0830b29317`) 수정 뒤 재통과. 같은 fixture로 측정한 count HWM 대비 여섯 transport throughput·tail latency 비교와 memory amplification 1.38(64 B)·1.00(64 KiB) 기록 | 완료 |
| C-08 | Core | Core 정식 spec·monitoring·오류 문서 갱신 | Socket·context·polling·monitoring 정식 spec과 internals 동기화, public surface contract 통과 | 완료 |
| CR-01 | Core review | Candidate SHA, 비교 기준과 공통 review 입력 고정 | Candidate `d7d682bb1f`, 비교 기준 `8bc2aa6786`, [core review 기록](https://github.com/kairos-code-dev/zlink/blob/3291e338f4f700484780560cd81345a647ef0948/framework/doc/plan/log/inbound-dispatch-lane-core-review.ko.md) round 1 입력 manifest | 완료 |
| CR-02 | Core review | Codex 5.6 High 독립 전체 review | `gpt-5.6-sol` high, [core review 기록](https://github.com/kairos-code-dev/zlink/blob/3291e338f4f700484780560cd81345a647ef0948/framework/doc/plan/log/inbound-dispatch-lane-core-review.ko.md) round 1. `NOT CLEAN`(`Critical` 2, `High` 1, `Medium` 2) | 완료 |
| CR-03 | Core review | 사용자 정책 변경 전 Claude Fable 독립 전체 review | Round 1~4 report와 finding을 core review log에 기록 | 완료 |
| CR-04 | Core review | Finding 통합, severity와 대안 검토 | Round 1~4 finding과 disposition 기록 | 완료 |
| CR-05 | Core review | `Medium` 이상 finding 수정과 회귀 검증 | Round 6의 `High` 4건·`Medium` 3건과 `Low` 1건을 모두 반영. Core non-serial 20/20, serial 63/63 통과 | 완료 |
| CR-06 | Core review | 변경 candidate의 Codex 전체 재검토 | Candidate `84d01c95c7`, `gpt-5.6-sol` high Round 6 전체 report는 `NOT CLEAN` | 완료 |
| CR-07 | Core review | 마지막 review finding 반영 확인 | 사용자 결정으로 Round 6을 마지막 review로 고정. Report를 `CLEAN`으로 바꾸지 않고 finding 전체 반영과 83/83 검증으로 종료 | 완료 |
| CP-01 | Core perf smoke | `core/build` 공식 runtime 재build와 provenance 확인 | Candidate `6985cf1a61`, `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.11.0.0`, SHA-256 `cc3e0968cd076e0c4807a8ebb25d9b42882622972242c7178defb5a955a1d51e` | 완료 |
| CP-02 | Core perf smoke | C single 전체 64B smoke 실행 | [single report](../../../bindings/c/perf/results/single/report/perf_c_single_linux_20260730_192736_core-r6.txt): 7 patterns·6 transports, 210/210, `status=complete` | 완료 |
| CP-03 | Core perf smoke | C multi 전체 64B smoke 실행 | [multi report](../../../bindings/c/perf/results/multi/report/perf_c_multi_linux_20260730_193614_core-r6-64b.txt): 8 patterns·4 transports, 160/160, fail 0, `status=complete` | 완료 |
| B-01 | Bindings | .NET binding과 package 갱신 | Contract 22/22, 전체 132/132 두 번, NuGet clean consumer 통과. `Systems.Zlink.11.0.0.nupkg` | 완료 |
| B-02 | Bindings | Java binding과 package 갱신 | Java 62/62, targeted 7/7, Kotlin consumer 1/1 통과. Maven `zlink-11.0.0.jar` | 완료 |
| B-03 | Bindings | Node.js binding과 package 갱신 | HWM contract 2/2, raw 31/31, npm clean consumer 통과. `zlink-systems-zlink-11.0.0.tgz` | 완료 |
| B-04 | Bindings | C++ binding과 package 갱신 | Contract 10/10과 isolated CMake consumer 통과. `install/zlink-cpp/11.0.0` | 완료 |
| B-05 | Bindings | 네 binding의 타입·단위·monitoring parity 확인 | 64-bit byte HWM·monitor ABI v2 parity와 package version 11.0.0 확인 | 완료 |
| BR-01 | Bindings review | Candidate SHA, 비교 기준과 review 입력 고정 | Candidate `9f08fdefec`, base `6985cf1a61`, [bindings review 기록](https://github.com/kairos-code-dev/zlink/blob/3291e338f4f700484780560cd81345a647ef0948/framework/doc/plan/log/inbound-dispatch-lane-bindings-review.ko.md) | 완료 |
| BR-02 | Bindings review | Codex 5.6 High 전체 review | `gpt-5.6-sol` high, `NOT CLEAN`(`Medium` 2, `Low` 1) | 완료 |
| BR-03 | Bindings review | `Medium` 이상 finding 수정과 회귀 검증 | Fix candidate `37f4f394b1`, finding 3건 모두 반영. C++ 10/10, Node 2/2·31/31 | 완료 |
| BR-04 | Bindings review | 변경 candidate의 검증 | 사용자 지시에 따라 추가 review 없이 Round 1 finding 반영 candidate의 contract test로 종료 | 완료 |
| BR-05 | Bindings review | 마지막 review finding 반영 확인 | Round 1 report는 `NOT CLEAN`으로 유지하고 세 finding 전체 반영을 확인 | 완료 |
| BP-01 | Bindings perf smoke | Candidate package와 `core/build` runtime provenance 확인 | [package manifest](../../../.artifacts/v11/evidence/BP-01/bindings-package-manifest-37f4f394b1.json), Core runtime SHA-256 `cc3e0968cd076e0c4807a8ebb25d9b42882622972242c7178defb5a955a1d51e` | 완료 |
| BP-02 | Bindings perf smoke | C single 전체 64B smoke 재실행 | [single report](../../../bindings/c/perf/results/single/report/perf_c_single_linux_20260730_200222_bindings-37f4f394b1.txt): 210/210, `status=complete` | 완료 |
| BP-03 | Bindings perf smoke | C multi 전체 64B smoke 재실행 | [multi report](../../../bindings/c/perf/results/multi/report/perf_c_multi_linux_20260730_200647_bindings-37f4f394b1.txt): 160/160, fail 0, `status=complete` | 완료 |
| F-01 | Framework | 공통 spec과 다섯 언어 exact interface 확정 | `01-glossary`, `06-framework-api`, `24-runtime-monitoring`, `25-runtime-metrics`와 [Framework 구현 갭 목록](for-interals/framework-internals-implementation-gaps.ko.md), .NET·Java·Kotlin·Node.js·C++ configuration·monitoring exact interface를 동기화. 기존 socket HWM도 64-bit byte type으로 정렬. `scripts/verify-framework-doc-contracts.sh` CLEAN | 완료 |
| F-02 | Framework | 6단계 통과 binding package version 적용 | 네 중앙 dependency를 `11.0.0`으로 고정했다. 승인된 Core package로 다시 만든 .NET·Java·C++ isolated consumer와 Node CJS·ESM consumer가 통과했다. 증거: `.artifacts/v11/evidence/F-03/dotnet-package-consumer-r2.json`, `.artifacts/v11/evidence/F-02/java-consumer-r2.json`, `.artifacts/v11/evidence/F-02/cpp-consumer-r2.json` | 완료 |
| F-03 | Framework | 두 connection poll과 직접 Application `Recv` 적용 | .NET Node·Channel·Spot·Actor request를 Core request callback과 Completion connection reply로 연결했다. Target reply는 `RequestSeq`를 사용하며 두 번째 reply는 `InvalidState`다. Generic opaque completion-control C ABI와 .NET binding도 새 connection 없이 구현해 Core 28/28, .NET bindings 15/15를 통과했다. Application `Recv`가 멈춘 상태의 callback, payload ownership과 callback-close race를 검증했다. Framework allowlist·Instance Spot 수렴과 C++·Node.js·Java bindings parity가 남았다. | 진행 중 |
| F-04 | Framework | Application HWM, 수신 중단·재개와 Auto 계산 적용 | .NET 설정·Auto 계산·pre-bind validation·16 MiB 기본값·64-bit socket HWM과 Classic Channel host budget 연결을 완료했다. HWM 해제 뒤 32개 receive waiter를 하나씩 재개하고 shutdown queue를 single reader로 정리했다. Inbound budget 7/7, ClientServer 24/24, automatic fanout 6/6과 source·unit build warning·error 0이다. 증거: `.artifacts/v11/evidence/F-04/dotnet-classic-budget-audit.md`. ClientServer control과 automatic fanout liveness를 Application gate 밖으로 옮기는 작업과 Mesh runtime 연결은 F-03 capability 뒤에 남았다. | 진행 중 |
| F-05 | Framework | Completion reserve, send permit과 pending 상한 적용 | 정적 audit에서 네 검증이 모두 미구현임을 확인했다. Host-level completion admission owner와 focused test를 구현 중이다. 증거: `.artifacts/v11/evidence/F-05/dotnet-static-audit.md` | 진행 중 |
| F-06 | Framework | Host byte accounting과 bounded monitoring 적용 | .NET public status와 pull-based 5개 metric seam, Classic Channel의 queued→active→completed·shutdown reject accounting과 host snapshot wiring을 완료했다. RuntimeMetrics 22/22, MaintenanceRuntime 19/19, inbound budget 7/7과 diff-check가 통과했다. Mesh·completion reserve snapshot 연결은 진행 중이다. | 진행 중 |
| F-07 | Framework | 다섯 언어 public contract와 E2E parity 확인 | 언어별 contract test와 공통 E2E 결과 | 대기 |
| F-08 | Framework | Memory·성능·다중 MeshNode 통합 검증 | 검증 17~19과 benchmark log | 대기 |
| F-09 | Framework | 정식 spec, guide와 운영 문서 갱신 | 문서 검증 결과와 최종 review | 대기 |

### 9.1 지금까지 한 일과 다음 작업

2026-07-30 기준 상태다. 다른 담당자가 이어받을 때 필요한 것만 적는다.

**끝난 것.** 1단계 C-01~C-08 전부. Stage 1 도중 찾아 고친 결함은 네 건이다.

| commit | 내용 |
| --- | --- |
| `563e11d614` | reply submit이 socket command를 배수하지 않아 completion credit이 회복되지 않던 문제, lb·DEALER·dist의 multipart rollback 누락 |
| `58aa55df8b` | multipart byte admission을 per-call HWM writer로 한정(PUB blocking publish가 drop되던 회귀) |
| `0830b29317` | inproc paired transport의 `ZLINK_EVENT_CONNECTION_READY` 미발행, pair readiness key의 routing id 의존 |
| `af2ef1e558` | perf fixture reply retry를 측정 경로 밖으로 이동 |

C-07 증거는 `d7d682bb1f`다. Byte HWM이 여섯 transport 모두에서 count HWM보다 빠르고 tail
latency가 낮으며, memory amplification은 accounted byte 기준 64 B에서 1.38, 64 KiB에서 1.00이다.
측정 방법·수치·결함별 근거는
[reqrep multipart rollback review](https://github.com/kairos-code-dev/zlink/blob/3291e338f4f700484780560cd81345a647ef0948/framework/doc/plan/log/inbound-dispatch-lane-reqrep-multipart-rollback-review.ko.md)
§9에 있다.

**끝난 것.** 이 문서에서 승인한 C-01~BP-03을 모두 마쳤다. Core candidate는 `6985cf1a61`,
bindings finding 반영 candidate는 `37f4f394b1`이다. 네 binding package는 모두 11.0.0으로
생성했고 contract·consumer test를 통과했다. Bindings 이후 C single 210/210과 multi 160/160
64B smoke도 모두 `status=complete`로 끝났다.

**다음에 할 일.**

1. F-03에서 .NET request/reply 완료와 bounded service control을 bindings의 Completion connection으로
   연결한다. Application 수신을 멈춰도 reply·service control과 send-ready callback 처리가 계속되어야 한다.
2. F-04에서 Host 전체 Application HWM을 receive·queue·handler terminal 경계에 연결한다.
   이미 받은 message는 버리지 않고 처리하며, 상한에 도달하면 새 Application receive만 멈춘다.
3. F-05와 F-06에서 completion permit, byte accounting과 낮은 비용의 monitoring을 적용한다.
4. .NET 기준 구현과 회귀 test가 끝나면 Java·Kotlin, Node.js와 C++가 같은
   accounting·ordering·error 계약을 이식한다.

**작업 규칙.** 작업 tree(`/home/hep7/project/kairos/zlink`)에는 다른 담당자의 미완료 변경이
있다. `git reset`, `git checkout`, `git clean`을 쓰지 않고, 이 작업과 관련된 file만 stage해서
commit한다. Perf 공식 runtime은 `core/build/lib/libzlink.so`이고 `build_cpp_release` 같은 임시
build 결과를 쓰지 않는다. count HWM 비교 기준 worktree는
`/tmp/zlink-hwm-baseline-8bc2aa6786`이며, perf fixture reply 경로를 candidate와 같게 맞춰 둔
상태다.

## 10. 언제 적용할 수 있는가

작성자가 C-01부터 BP-03까지의 적용을 승인했고 해당 범위는 완료됐다. 2026-07-30 추가 승인으로
Framework F-01부터 F-09까지 진행한다.

원래 gate는 새 candidate가 `CLEAN` 판정을 받을 때까지 review를 반복하도록 정했다. 그러나
2026-07-30 사용자 지시에 따라 Core Round 6과 bindings Round 1을 각각 마지막 review로 삼고,
각 report의 finding을 모두 반영한 뒤 추가 review 없이 다음 단계로 진행했다. 두 report는
`NOT CLEAN` 기록을 그대로 유지하며 `CLEAN`으로 소급 변경하지 않는다. 이 예외와 수정 뒤 test,
package, perf smoke 결과는 각 review log와 진행표에 기록했다.

이전 count 동작을 보존하는 option, alias, adapter나 runtime 자동 변환은 어느 단계에도
추가하지 않는다. Review나 smoke failure를 수정하면서 앞 단계의 source·contract가 바뀌면
해당 clean review부터 다시 시작한다.

7단계와 §7의 통합 검증이 끝나면 진행표의 모든 완료 증거를 확인한다. 누락된 항목이 있으면
적용 완료로 판단하지 않는다.
