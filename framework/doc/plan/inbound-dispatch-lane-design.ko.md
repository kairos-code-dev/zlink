# 수신 backpressure 설계 검토

> 상태: 최종 승인 전 목표 설계안. 현재 구현에는 적용하지 않음
>
> 두 connection 구조, byte 기준 HWM과 수신 permit을 후속 구현의 목표 구조로 사용한다.
> 적용 버전과 일정은 정하지 않았으며, 작성자가 최종 승인하기 전에는 구현하거나 정식
> 계약에 반영하지 않는다.
>
> 이 문서는 최종 승인 전 설계 초안이며 정식 공개 계약이나 구현 계획이 아니다.

## 1. 현재 적용 상태

이 문서는 후속 구현이 따라야 할 수신 dispatch와 backpressure의 목표 구조를 정한다. 적용
버전과 일정은 작성자가 별도로 결정한다. 최종 승인 전에는 이 문서의 계산식, 목표 값과 구조를
현재 계약이나 구현 근거로 사용하지 않는다.

- 최종 승인 전에는 Framework, Core와 bindings에 새 public API를 추가하지 않는다.
- 최종 승인 전에는 wire protocol과 Core HWM 동작을 변경하지 않는다.
- 두 connection, byte HWM, 수신 permit과 내부 queue 제거를 목표 구조로 유지한다.
- 정확한 기본값과 protocol 세부 사항은 최종 승인 뒤 정식 draft와 spec에서 확정한다.

기존 구현의 명백한 버그는 별도로 수정할 수 있다. 그러나 이 문서에 기록한 문제를 근거로
최종 승인 없이 runtime 구조를 변경하지 않는다.

## 2. 검토하는 문제

Target의 처리 속도보다 source의 송신 속도가 빠르면 수신 대기 message가 계속 증가할 수 있다.
원하는 동작은 message를 버리는 것이 아니라, target의 처리 지연이 source의 송신 대기로 이어지는
[backpressure](../framework/common/spec/01-glossary.ko.md#backpressured)다.

후속 설계를 다시 검토한다면 다음 조건을 함께 평가해야 한다.

- 수신한 application message를 부하 때문에 버리지 않는다.
- Target의 수신 여유가 부족하면 source의 송신 압력을 낮춘다.
- Message 개수뿐 아니라 payload 크기도 고려한다.
- Application `Recv`가 중단되어도 Completion connection에서 reply와 필수 runtime
  control을 계속 처리한다.
- 수천 개 peer를 사용하는 환경에서 두 connection의 socket과 memory 비용을 계산하고
  Completion connection의 HWM과 buffer를 application traffic보다 작게 제한한다.
- Backpressure 상태를 계산하는 동기화가 정상 dispatch 성능을 크게 낮추지 않는다.
- Process 전체 ingress가 중단되면 byte를 가장 많이 보유한 실행 대상을 진단할 수 있어야 한다.
- 양방향 nested request와 두 connection의 순서 역전이 영구 교착이나 stale control 적용으로
  이어지지 않아야 한다.

## 3. 정식 계약 전에 확정할 항목

이 문서는 목표 구조와 계산 방법을 정하지만 아직 적용 승인을 받지 않았다. 작성자가 최종
승인한 뒤에도 다음 값과 protocol 세부 사항을 정식 spec에서 확정해야 한다.

- 언어별 `minimumMessageChargeBytes`와 memory amplification 측정값
- Framework Application HWM과 Completion connection HWM의 production 기본값
- Application LWM profile과 목표 동시 `Recv` 수
- Host 전체 `maxPendingCompletionSends`, peer별 공정성 상한과 completion send permit 값
- 복수 ingress poller의 성능 허용 기준과 budget shard를 사용할 조건
- Completion connection handshake, reconnect와 pending request 종료 error
- Framework Auto HWM profile의 비율과 절대 상한
- Owner attribution top-N 수, 장시간 pause 진단 threshold와 조회 rate limit
- Core Auto HWM planning unit option의 정확한 migration 계약
- C monitoring ABI version과 다섯 Framework 언어의 exact public interface

## 4. 목표 Framework Application HWM 계산안

> 이 절은 최종 승인 전 목표 설계안이다. 수치, 계산식과 `receivePermitBytes` 방식은
> 승인 전 검토 대상이며 구현과 정식 계약에 사용하지 않는다.

Framework HWM은 connection, MeshNode, Channel, Spot이나 Actor마다 따로 계산하지 않는다.
한 Framework host가 보관하는 application message 전체에 하나의 byte HWM을 적용한다.

```text
Framework host application budget
    MeshNode queues
    RouteMesh and ClientServer queues
    Spot and Actor queues
    Executing handler messages
    Ingress and relocation holds
```

각 queue와 handler는 같은 전역 budget에서 message byte를 확보하고 처리가 끝날 때
반환한다. 객체마다 HWM을 따로 만들지 않으므로 connection이나 node 수가 증가해도
Framework가 허용하는 application memory 총량은 자동으로 증가하지 않는다.

### 4.1 HWM에 포함하는 상태

`applicationUsedBytes`에는 Framework가 소유한 다음 message를 포함한다.

- Queue에서 dispatch를 기다리는 message
- Handler가 실행되는 동안 참조하는 message
- Target을 확인한 뒤 queue 공간을 기다리는 message
- Relocation ingress hold와 Message Follow가 보관하는 message
- Framework 내부 queue 사이를 이동 중인 message

Queue에서 dequeue한 시점에는 byte를 반환하지 않는다. Handler 완료, 예외, cancellation,
relocation이나 shutdown 가운데 어느 경로로 끝나더라도 message 소유권을 실제로 반환할 때
정확히 한 번 차감한다.

One-way message는 handler terminal에서 application payload 소유권을 반환할 때 차감한다.
Request handler는 실행 전에 최대 reply 한 건을 위한 completion send permit을 확보한다.
Permit을 확보할 수 없으면 request를 Application queue에 남기고 application byte를 계속
계산하며 handler를 시작하지 않는다.

Handler가 terminal 결과를 만들고 reply serialization을 끝내면 budget owner는 원본 request의
application lease를 반환하고, completion send permit을 실제 reply accounted byte로 바꾼다.
사용하지 않은 permit byte는 같은 전환에서 반환한다. 이 전환은 원본 request와 reply byte를
누락하거나 이중으로 계산하지 않도록 한 번에 처리한다.

Terminal 뒤에는 원본 request byte를 reply가 wire에 기록되거나 peer가 읽을 때까지 유지하지
않는다. Completion send가 backpressured인 동안에는 serialization을 마친 reply payload만
completion reserve를 사용한다. 느린 completion consumer는 새 request handler admission을
중단시킬 수 있지만, 이미 끝난 handler의 원본 request lease를 계속 점유하게 만들지는 않는다.

HWM 계산에서 사용하는 `accountedMessageBytes`는 payload 크기만 뜻하지 않는다. Framework가
message와 함께 보관하는 envelope, routing 정보와 고정 metadata를 포함하고, 작은 message도
최소 charge를 적용한다.

```text
accountedMessageBytes =
    max(
        payloadPartBytes
        + retainedEnvelopeAndRoutingBytes
        + retainedFrameworkMetadataBytes,
        minimumMessageChargeBytes)
```

`minimumMessageChargeBytes`는 zero-length message나 작은 message가 HWM을 거의 사용하지
않으면서 queue metadata를 계속 늘리는 문제를 막는다. 각 언어의 message wrapper와 allocator
비용을 측정해 언어별 내부 상수로 정하고 monitoring에서 실제 적용값을 제공한다.

같은 payload를 여러 queue나 handler가 각각 보관하면 각 소유권마다 전체
`accountedMessageBytes`를 charge한다. Shared buffer의 실제 allocation보다 크게 계산될 수
있지만 memory 상한을 낮추는 방향이므로 허용한다. Serialization 중 잠시만 존재하는 buffer와
allocator 여유분은 `memoryAmplification`과 `emergencyHeadroom`으로 반영한다.

Core pipe와 OS socket buffer가 보관하는 memory는 `applicationUsedBytes`에 포함되지 않는다.
따라서 Framework Application HWM은 process 전체 memory의 정확한 상한이 아니다. Core
transport와 completion 처리에 필요한 memory는 HWM 계산 전에 별도 reserve로 남겨야 한다.

### 4.2 수신 중단과 재개

Framework는 complete message를 받기 전에 그 message가 사용할 수 있는 최대 byte를 전역
budget에서 먼저 확보한다. 이 예약을 `receivePermitBytes`라 한다.

```text
receivePermitBytes =
    effectiveMaxAccountedMessageBytes

applicationCommittedBytes =
    applicationUsedBytes
    + outstandingReceivePermitBytes

canStartRecv =
    applicationCommittedBytes + receivePermitBytes
    <= applicationHwmBytes
```

`effectiveMaxAccountedMessageBytes`는 Core에 설정한 유한한 `MaxMessageSize`에 Framework
envelope와 최소 charge를 더한 값이다. 따라서 memory 기준 backpressure를 사용하려면 다음
startup 조건을 만족해야 한다.

```text
0 < effectiveMaxAccountedMessageBytes
effectiveMaxAccountedMessageBytes <= applicationHwmBytes
```

`MaxMessageSize`가 무제한이거나 최대 message 한 건이 Application HWM보다 크면 startup
configuration error로 처리한다. 값을 조용히 낮추거나 큰 message를 HWM 밖에서 예외로
보관하지 않는다. 이 조건으로 HWM 크기와 관계없이 허용된 최대 message 한 건은 항상 받을
수 있다.

최대 message를 선예약하므로 동시에 시작할 수 있는 `Recv` 수에는 다음 상한이 생긴다.

```text
maxConcurrentReceivePermits =
    floor(applicationHwmBytes / receivePermitBytes)

requiredApplicationHwmBytes =
    desiredConcurrentReceives * receivePermitBytes
```

Production 설정은 목표 동시 `Recv` 수를 정하고
`applicationHwmBytes >= requiredApplicationHwmBytes`인지 확인해야 한다. 이 조건을 만족하지
않아도 startup 자체는 가능하지만 monitoring과 startup log에 실제
`maxConcurrentReceivePermits`를 제공한다.

여러 Application connection이 수신 가능 상태가 되어도 host의 application budget owner
하나가 `Recv` 시작 순서와 permit을 관리한다. 따라서 connection별 receive worker가 전역
counter를 경쟁적으로 갱신하지 않는다. Complete message를 받은 뒤 실제
`accountedMessageBytes`를 계산한다. Permit에서 실제 message byte를 `applicationUsedBytes`로
옮기고 사용하지 않은 차이를 즉시 반환한다. 이 handoff 동안 두 값을 동시에 charge하지
않는다.

Core는 multipart의 마지막 frame이 commit되기 전에는 incomplete frame을 Application
connection의 reader에 공개하지 않는다. 따라서 Framework의 poller와 `Recv`는 완성된
multipart만 관측한다. Framework는 완성된 message를 `Recv`하기 직전에 complete message 한
건의 permit을 확보하며, network에서 첫 frame과 마지막 frame 사이를 수신하는 동안에는 이
permit을 점유하지 않는다. Core는 incomplete multipart를 Framework에 전달하거나 dispatch
가능 상태로 표시해서는 안 된다.

Permit을 확보할 수 없으면 Framework host의 모든 Application connection에서 `Recv`를 중단한다.
특정 MeshNode 하나만 중단하지 않고 같은 process의 application ingress 전체를 중단한다.
이 방식은 한 workload의 과부하가 다른 workload의 수신도 늦출 수 있지만 process 전체를
burst로부터 보호한다는 목적과 일치한다.

Application `Recv`를 중단한 직후 source가 멈추는 것은 아니다. Core receive queue가
HWM까지 찬 뒤 해당 directional pipe의 write가 대기하면서 source까지 backpressure가
전달된다.

Completion connection은 Application connection과 별도 pipe와 HWM을 사용한다. Framework는
Application `Recv`가 중단된 동안에도 Completion connection을 계속 poll하고 reply와 필수
runtime control을 처리한다. 따라서 application pipe가 HWM에 도달해도 이미 보낸 request의
completion은 application payload와 같은 FIFO 뒤에서 대기하지 않는다.

Low watermark 비율은 profile 값으로 둔다. 초기 profile 후보는 `0.5`지만 production
기본값은 pause duration과 resume 빈도를 측정한 뒤 확정한다.

```text
profileLwmBytes =
    ceil(applicationHwmBytes * applicationLwmRatio)

permitResumeCeilingBytes =
    applicationHwmBytes - receivePermitBytes

applicationResumeThresholdBytes =
    min(profileLwmBytes, permitResumeCeilingBytes)
```

`applicationCommittedBytes`가 `applicationResumeThresholdBytes` 이하로 내려가면 모든
Application connection의 `Recv`를 재개한다. Permit이 HWM의 절반보다 크면 실제 재개
threshold는 profile LWM보다 낮아진다. Monitoring은 profile LWM과 실제 resume threshold를
구분해서 제공한다. Reply와 completion 처리는 application budget과 별도로 확보한 reserve를
사용해야 한다.

### 4.3 명시적 설정

이 설계에서는 `ApplicationHwmBytes`를 모든 Framework 언어가 제공하는 64-bit public
설정으로 정의한다. Application이 지정한 값은 Auto HWM보다 우선한다.

```text
explicit ApplicationHwmBytes
    -> use explicit value

no explicit value
    -> calculate Auto Application HWM
```

`0`은 Auto HWM을 요청한다. 명시한 값이 `effectiveMaxAccountedMessageBytes`보다 작거나
계산한 안전한 memory 상한보다 크면 startup configuration error로 처리한다. 잘못된 값을
조용히 낮추지 않는다. 안정적인 process memory limit이나 검증된 Auto profile을 구할 수
없는데 `0`을 설정한 경우에도 startup error로 처리한다.

Framework `ApplicationHwmBytes`의 `0`은 Auto 계산을 요청하지만 Core socket의
`ZLINK_OPT_SNDHWM`과 `ZLINK_OPT_RCVHWM`에서 `0`은 무제한을 뜻한다. Framework 설정값을 Core
option에 그대로 전달하지 않으며, Auto 계산이 끝난 유한한 byte 값을 Core connection에
적용한다.

정식 구현 전에 Framework 공통 spec과 다섯 언어 exact interface에 같은 단위, 범위,
startup error와 Auto 우선순위를 먼저 기록한다.

### 4.4 Memory 기준 상한

Memory 관점에서 허용할 수 있는 HWM은 다음과 같이 계산한다.

```text
memoryHwmBytes =
    (
        processMemoryLimit
        - baselinePeakMemory
        - coreTransportReserve
        - completionReserve
        - ingressReceiveReserve
        - emergencyHeadroom
    )
    / memoryAmplification
```

| 값 | 의미 |
|---|---|
| `processMemoryLimit` | Application이 명시한 process limit, container·cgroup 또는 Windows Job Object limit처럼 안정적인 memory 상한 |
| `baselinePeakMemory` | Backlog가 없을 때 application state, 비어 있는 connection metadata, runtime과 GC가 사용하는 peak memory |
| `coreTransportReserve` | Application·Completion connection의 Core pipe, transport buffer와 OS socket buffer가 사용할 memory |
| `completionReserve` | 요청자의 pending request·reply 완료와 응답자의 handler reply permit·미전송 reply에 사용할 memory |
| `ingressReceiveReserve` | 동시 `Recv` permit을 관리하면서 필요한 일시적 frame, decoding과 bookkeeping 여유 |
| `emergencyHeadroom` | 일시적 allocation, GC, TLS, allocator와 OS buffer 증가를 감당할 여유 |
| `memoryAmplification` | Framework가 센 message byte에 비해 실제 process memory가 증가하는 비율 |

`memoryAmplification`은 다음과 같이 측정한다.

```text
memoryAmplification =
    processMemoryIncrease
    / accountedApplicationMessageBytes
```

Payload 1 MiB를 queue에 추가했을 때 process memory가 1.5 MiB 증가했다면 값은 `1.5`다.
언어 runtime, message wrapper와 serialization 방식이 다르므로 다섯 Framework 언어에서
각각 측정해야 한다.

예를 들어 다음 조건을 사용한다고 가정한다.

```text
Process memory limit:       8 GiB
Baseline peak:              3 GiB
Core transport reserve:   512 MiB
Completion reserve:       512 MiB
Ingress receive reserve:   64 MiB
Emergency headroom:         1 GiB
Memory amplification:       1.5
```

Memory 기준 상한은 약 `1.96 GiB`다.

### 4.5 Queue 지연 기준 상한

Memory가 충분해도 너무 큰 HWM은 오래된 message를 queue에 계속 남긴다. 운영에서 허용하는
최대 queue 지연도 HWM 상한으로 사용한다.

```text
latencyHwmBytes =
    sustainableDrainBytesPerSecond
    * maximumQueueDelaySeconds
```

지속적으로 초당 `200 MiB`를 처리하고 최대 queue 지연을 `2초`로 제한한다면
latency HWM은 `400 MiB`다.

최종 HWM은 memory와 queue 지연 상한 중 작은 값이다.

```text
applicationHwmBytes =
    min(memoryHwmBytes, latencyHwmBytes)
```

앞의 예에서는 memory 상한이 `1.96 GiB`이고 latency 상한이 `400 MiB`이므로 최종 HWM은
`400 MiB`다. LWM ratio가 `0.5`이고 receive permit이 `64 MiB`라면 profile LWM과 실제
resume threshold는 모두 `200 MiB`다. Permit이 `256 MiB`라면 profile LWM은 같지만 실제
resume threshold는 `144 MiB`다.

### 4.6 예상 burst 확인

설정한 HWM이 운영에서 예상하는 burst를 얼마나 흡수하는지 다음 식으로 확인한다.

```text
requiredBurstBytes =
    max(
        peakIngressBytesPerSecond
        - sustainableDrainBytesPerSecond,
        0)
    * burstDurationSeconds
```

Peak ingress가 초당 `350 MiB`, 지속 처리량이 초당 `200 MiB`, burst가 `2초`라면 필요한
buffer는 `300 MiB`다. HWM이 `400 MiB`이면 이 burst를 Framework queue에서 흡수할 수 있다.
Burst가 HWM을 넘으면 Application `Recv`를 중단하여 source에 backpressure를 전달한다.
이는 실패가 아니라 HWM의 정상 동작이다.

### 4.7 Auto Application HWM

명시적 HWM이 없을 때만 안정적인 process memory limit과 검증된 profile로 Auto HWM을
계산한다.

```text
autoApplicationHwmBytes =
    min(
        stableProcessMemoryLimit * profileRatio,
        profileAbsoluteMaximum,
        memoryHwmBytes,
        latencyHwmBytes)
```

현재 OS free memory를 주기적으로 읽어 HWM을 직접 바꾸지 않는다. 부하가 증가해서 free
memory가 줄어드는 순간 HWM까지 함께 줄이면 모든 ingress가 반복해서 중단될 수 있다.

`stableProcessMemoryLimit`의 선택 순서는 다음과 같다.

1. Application이 명시한 Framework 또는 process memory limit
2. Container·cgroup 또는 Windows Job Object memory limit
3. Managed runtime의 heap hard limit
4. 같은 host의 다른 process가 사용할 memory를 반영한 명시적 fallback

Host physical memory만 확인할 수 있고 여러 process가 함께 사용하는 환경에서는 각 process가
같은 memory를 자신의 가용량으로 계산할 수 있다. 이런 경우에는 physical memory만으로 Auto
HWM을 만들지 않고 명시적 `ApplicationHwmBytes`를 요구한다.

`profileRatio`, `profileAbsoluteMaximum`, memory amplification과 지속 처리량은 언어와
runtime별 benchmark로 검증한 profile에 포함한다. 검증된 profile이 없으면 임의의 비율을
runtime 기본값으로 사용하지 않고 명시적 `ApplicationHwmBytes`를 요구한다.

Process limit의 `10%`는 profile을 만들기 위한 benchmark 시작값으로만 사용할 수 있다.
Production Auto HWM의 fallback이나 공개 기본값으로 사용하지 않는다. Auto 계산은 startup과
명시적인 topology 변경 시에만 수행하며 message 처리 중 OS free memory나 process RSS를
조회하지 않는다.

### 4.8 설정 절차

Production HWM은 다음 순서로 결정한다.

1. 목표 connection 수와 평상시 application state를 구성한다.
2. Backlog가 없는 정상 workload에서 `baselinePeakMemory`를 측정한다.
3. Application과 Completion connection의 Core HWM을 합산하여 `coreTransportReserve`를
   계산한다.
4. 최대 pending request 수, completion 동시 처리량과 최대 미전송 reply 수로
   `completionReserve`를 계산한다.
5. 알려진 byte의 message를 Framework queue에 쌓아 `memoryAmplification`을 구한다.
6. 지속 가능한 handler 처리 byte를 측정한다.
7. 운영에서 허용할 `maximumQueueDelaySeconds`를 정한다.
8. `ApplicationHwmBytes`가 `effectiveMaxAccountedMessageBytes` 이상인지 확인한다.
9. 목표 동시 `Recv` 수를 정하고 HWM이 필요한 permit 총량을 허용하는지 확인한다.
10. Memory 상한과 queue 지연 상한 중 작은 값을 HWM으로 선택한다.
11. Peak ingress와 burst 지속 시간을 대입하여 예상 burst가 들어오는지 확인한다.
12. LWM ratio와 permit을 반영한 실제 resume threshold를 계산하고 pause duration과 resume
    횟수를 관측한다.
13. 목표 connection 수에서 process peak memory가 limit과 reserve를 넘지 않는지 검증한다.

최종 계산은 다음과 같다.

```text
applicationHwmBytes =
    min(
        (
            processMemoryLimit
            - baselinePeakMemory
            - coreTransportReserve
            - completionReserve
            - ingressReceiveReserve
            - emergencyHeadroom
        ) / memoryAmplification,
        sustainableDrainBytesPerSecond
            * maximumQueueDelaySeconds)
```

### 4.9 Completion connection과 내부 payload queue 제거 계획

현재 Core request/reply helper는 network socket에 message handler를 설치한다. 이 handler는
reply를 completion queue로 보내고 application message와 수신한 request는 내부 PAIR
`recv_queue`로 옮긴다. Framework의 Application `Recv`는 network pipe가 아니라 이 내부
queue에서 message를 읽는다.

현재 completion queue도 reply payload를 `request_completion::queue_state_t::pending` deque에
보관하고 내부 PAIR signal socket으로 owner thread를 깨운다. 따라서 application payload와
reply payload 모두 network pipe에서 빠져나온 뒤 별도 memory queue에 다시 쌓일 수 있다.

이 구조에서는 Framework가 Application `Recv`를 중단해도 Core message handler가 network
pipe에서 application message를 계속 꺼낼 수 있다. 내부 PAIR queue는 기본 message-count
HWM을 사용하고 `applicationUsedBytes`에도 포함되지 않으므로, Framework Application HWM이
실제 보관 byte를 제한하지 못한다. 따라서 memory 기준 backpressure를 적용하기 전에
application payload를 보관하는 내부 PAIR queue를 제거해야 한다.

목표 구조에서는 MeshNode pair마다 다음 두 connection을 설정한다. 두 connection은 각각
양방향으로 사용하므로, A와 B 사이에 application용 두 개와 completion용 두 개를 만드는
구조가 아니라 physical connection 두 개를 공유하는 구조다.

| Connection | 전달하는 message | 수신 중단 조건 |
|---|---|---|
| Application connection | 일반 application message와 request | Framework Application HWM에 도달하면 `Recv`를 중단한다. |
| Completion connection | Request reply와 진행에 필요한 runtime control | Application HWM으로 중단하지 않는다. 별도 Core byte HWM과 completion reserve로 제한한다. |

Application message는 Framework가 `Recv`할 때까지 Application connection의 network pipe에
남긴다. Framework는 `Recv` 전에 최대 receive permit을 확보하고, message를 받은 뒤 permit을
실제 accounted byte로 바꾼다.
Request/reply helper는 application payload를 별도 queue로 복사하거나 이동하지 않는다.
Reply는 request와 같은 Application connection으로 되돌려 보내지 않고 peer의 Completion
connection으로 보낸다. Request sequence와 peer identity를 사용하여 원래 pending request와
연결한다. Completion connection의 network pipe가 reply backlog를 담당하므로 Core는 받은
reply payload를 별도 completion deque에 다시 넣지 않고 Completion poller owner에서 pending
request를 직접 완료한다.

두 connection 사이에는 공통 FIFO 순서가 없다. Completion connection의 control이 먼저 보낸
Application message를 추월할 수 있고, 반대로 Application message가 관련 control보다 먼저
도착할 수도 있다. 따라서 relocation, session binding, peer lifecycle과 shutdown control은
connection 도착 순서로 application payload의 선후 관계를 추론하면 안 된다.

Control이 특정 application message 이후에만 적용되어야 한다면 protocol에 request sequence,
object generation, binding generation, session epoch 또는 명시적인 barrier high-water를
포함한다. Receiver는 필요한 fence를 확인할 때까지 control 적용이나 application dispatch를
보류한다. Wall-clock 수신 순서만으로 두 connection을 합치지 않는다.

제거 작업은 다음 순서로 진행한다.

1. Core가 peer pair의 Application connection과 Completion connection을 같은 peer identity로
   연결하도록 connection 설정과 handshake를 정의한다. Completion connection이 다른
   peer의 application connection과 잘못 결합되지 않도록 pair 식별자를 검증한다.
2. Request는 Application connection으로 보내고 reply와 필수 runtime control은 Completion
   connection으로 보내도록 request/reply protocol과 routing을 바꾼다.
3. Framework poller가 두 connection을 함께 감시하되, Application HWM에 도달하면
   Application connection의 `Recv`만 중단하도록 수신 경로를 나눈다.
4. Application message는 Application connection의 network pipe에서 Framework의 직접
   `Recv` 경로로 전달하고, Completion connection에서 받은 reply는 pending request를
   완료하는 경로로만 전달한다.
5. `socket_request_reply_state_t::recv_queue`와 이를 생성하고 채우고 읽는 helper를 제거한다.
   제거 대상에는 `internal_pair_queue::ensure`, `queue_router_message`,
   `queue_dealer_message`와 내부 queue 전용 receive 경로가 포함된다.
6. Reply payload를 보관하는 `request_completion::queue_state_t::pending`,
   `queue_reply_completion`, `queue_router_reply_completion`과 drain 경로를 제거한다.
   Timeout과 cancellation처럼 다른 thread에서 발생하는 payload 없는 완료 명령만 bounded
   control queue와 기존 poller signaler로 owner thread에 전달한다.
7. Framework의 RouteMesh와 ClientServer 수신 경로가 Core Application connection에서 직접
   application message를 받고, 받은 byte를 전역 budget에 정확히 한 번 반영하도록 바꾼다.
8. 내부 PAIR queue를 전제로 한 shutdown, timeout, cancellation과 request target 보관
   경로를 정리한다.
9. 기존 단일 FIFO의 순서를 전제로 한 relocation, session binding, peer lifecycle과 shutdown
   경로를 감사하고 필요한 generation·epoch·sequence fence를 protocol에 추가한다.

다음 조건을 모두 검증한 뒤에만 제거가 완료된 것으로 판단한다.

- Application `Recv`를 중단하면 application payload가 다른 내부 queue로 이동하지 않고
  network pipe의 byte HWM에 도달한다.
- Request/reply 기능을 사용해도 application payload나 reply payload를 보관하는 내부 PAIR
  socket과 deque가 생성되지 않는다.
- Framework가 받은 message는 queue 대기부터 handler 완료까지 전역 budget에 정확히 한 번
  포함된다.
- 양방향 request가 동시에 발생해도 별도 Completion connection에서 completion이 중단되지
  않는다.
- 큰 single-part message와 multipart message를 반복해서 보내도 process memory가 계산한
  Core reserve와 Framework Application HWM 범위 안에 유지된다.
- Completion connection이 끊기면 해당 peer의 pending request를 정해진 오류로 완료하고,
  재연결 뒤 새 request와 이전 connection의 늦은 reply가 섞이지 않는다.

Completion connection은 application payload를 전달하지 않으므로 Application connection보다
작은 HWM과 buffer를 사용할 수 있다. 정확한 기본값은 reply payload 분포, 최대 pending
request 수와 동시에 완료될 수 있는 reply byte를 측정한 뒤 정한다. Connection 수와 file
descriptor는 두 배가 되므로, 이 비용은 HWM 축소로 제거되지 않는다.

### 4.10 Completion reserve와 request·reply 상한

Completion connection의 Core pipe와 OS socket buffer는 `coreTransportReserve`에 포함한다.
Framework의 `completionReserve`는 요청자가 reply를 기다리고 완료하는 비용과 응답자가
request handler를 실행하고 아직 Core가 받지 않은 reply를 보관하는 비용을 모두 포함한다.

```text
completionSendPermitBytes =
    effectiveMaxReplyAccountedBytes
    * completionMemoryAmplification

requesterCompletionReserve =
    maxPendingRequests * pendingRequestMetadataBytes
    + completionDispatchConcurrency
        * effectiveMaxReplyAccountedBytes
        * completionMemoryAmplification

responderCompletionSendReserve =
    maxPendingCompletionSends
    * completionSendPermitBytes

completionReserve =
    requesterCompletionReserve
    + responderCompletionSendReserve
    + completionBookkeepingReserve
```

`maxPendingCompletionSends`는 host 전체에서 completion send permit을 확보한 request
handler와 serialization을 끝냈지만 Core Completion connection이 아직 받지 않은 reply의 합계
상한이다. Peer마다 이 값을 따로 허용하면 connection 수에 따라 memory가 증가하므로 host 전체
상한은 반드시 유한해야 한다. 한 peer가 permit을 모두 사용하지 못하도록 별도의 peer별 공정성
상한을 둘 수 있지만, peer별 상한의 합으로 host 상한을 대신하지 않는다.

Request handler를 실행하기 전에 `completionSendPermitBytes`를 한 번 확보한다. 상한에
도달하면 새 request handler admission을 기다리게 하고 request는 Application queue와
application budget에 남긴다. Reply serialization을 끝내면 원본 request의 application lease를
반환하고 permit을 실제 reply byte로 바꾸며, 남은 permit을 즉시 반환한다.

Core가 Completion connection send를 받아들이면 reply 소유권과 accounted byte를
`coreTransportReserve`로 한 번에 넘긴다. 이 전환에서도 두 reserve에 동시에 계산하거나 어느
reserve에서도 누락하는 구간을 만들지 않는다. Core send admission, wire write와 peer receive가
늦어지는 동안 Framework가 소유한 reply만 responder completion reserve를 사용한다. Send
failure, timeout, cancellation이나 shutdown으로 reply 소유권을 폐기할 때 completion byte를
정확히 한 번 반환한다.

`maxPendingRequests`는 반드시 유한해야 한다. 상한에 도달하면 새 request를 application
connection에 보내지 않고 request admission에서 기다리게 한다. Timeout, cancellation이나
reply로 기존 pending request가 끝나면 대기 중인 request를 다시 허용한다. Low-level
non-blocking API는 같은 조건을 명시적인 backpressure error로 반환할 수 있지만 request를
보낸 뒤 completion metadata만 버려서는 안 된다.

`completionDispatchConcurrency`는 Core에서 reply를 받은 뒤 pending request callback이나
Task/Future/Promise 완료로 소유권을 넘기는 작업의 최대 동시 실행 수다. 사용자 continuation을
Completion poller에서 직접 실행하지 않으며, reply payload 소유권을 넘긴 시점까지
completion reserve에 포함한다.

`completionBookkeepingReserve`에는 두 connection 사이의 generation·epoch·sequence fence를
기다리는 bounded control record도 포함한다. Fence 대기 count와 byte 상한을 넘으면 control을
무제한 보관하지 않고 해당 peer의 Application·Completion connection pair 전체를 protocol
error로 종료한다. Pair generation을 무효화하고 해당 pair의 pending request를 정해진 terminal
error로 완료한 뒤에만 새 pair로 재연결한다.

Application HWM과 Completion connection HWM은 서로 대신 사용할 수 없다. Application HWM을
줄여도 pending request metadata가 줄어들지 않으며, Completion HWM을 크게 해도 Framework가
동시에 완료할 수 있는 request 수가 늘어나는 것은 아니다.

### 4.11 Core transport 총 reserve

Core의 `SNDHWM`과 `RCVHWM`은 directional pipe마다 적용된다. 따라서 byte 단위로 바꿔도 HWM
한 개만으로 process 전체 memory 상한을 보장하지 않는다. Framework는 Application connection과
Completion connection의 모든 local pipe를 합산해야 한다.

빈 pipe에서는 HWM보다 큰 message 한 건을 허용하므로 directional pipe 하나의 최악 조건은
다음과 같이 계산한다.

```text
directionalPipeBudgetBytes =
    max(effectiveHwmBytes, effectiveMaxMessageBytes)

coreQueueBudgetBytes =
    sum(directionalPipeBudgetBytes for all local pipes)

coreTransportReserve =
    coreQueueBudgetBytes * coreMemoryAmplification
    + osSocketBufferReserve
    + transportBookkeepingReserve
```

Application과 Completion connection의 send·receive 방향을 모두 포함한다. `0=무제한` HWM이나
유한하지 않은 `MaxMessageSize`가 하나라도 있으면 이 식으로 process memory 상한을 만들 수
없으므로 memory 제한을 사용하는 Framework host는 startup configuration error로 처리한다.

수동 기본값 `4,096,000 bytes`는 connection별 값이다. 수천 connection에 그대로 곱한 결과가
process memory limit을 넘으면 startup을 실패시키거나 connection 수를 반영한 Auto HWM을
사용해야 한다. 값을 조용히 낮추면 monitoring 값과 운영자가 지정한 계약이 달라지므로
허용하지 않는다.

### 4.12 Hot path overhead 제한

Memory HWM을 적용하려면 complete message마다 byte를 더하고 소유권을 반환할 때 빼는 연산은
필요하다. 그 외의 memory 측정과 동기화는 normal dispatch hot path에 추가하지 않는다.

Framework는 host마다 application budget의 최종 회계 권한을 가진 owner 하나를 둔다. Owner가
하나라는 것은 모든 MeshNode의 `Recv`와 dispatch를 한 thread에서 실행한다는 뜻이 아니다.
각 MeshNode의 기존 ingress poller와 I/O thread 병렬성은 유지해야 한다.

Budget owner와 ingress poller는 다음 규칙을 따른다.

- `accountedMessageBytes`는 이미 수신한 part 길이와 미리 계산한 고정 charge로 구한다.
  Payload를 다시 순회하거나 복사하지 않는다.
- Budget lease는 dispatch work item 안에 값으로 보관한다. Message마다 별도 accounting
  객체를 allocation하지 않는다.
- 한 ingress poller가 소유한 permit의 reserve와 실제 크기 조정에는 일반 64-bit 정수 연산을
  사용한다.
- Handler가 다른 thread에서 끝나면 반환 byte만 lock-free 누적 counter에 한 번 더한다.
  Budget owner나 해당 shard의 ingress poller는 다음 loop에서 누적 반환값을 한 번에 회수한다.
- Node, Channel, Spot과 Actor의 실행 owner에는 진단용 일반 64-bit retained-byte counter를
  둔다. 기존 execution gate나 budget owner가 값을 갱신하므로 owner마다 atomic counter나
  새 lock을 추가하지 않는다.
- Queue 전체를 순회해 사용량을 다시 계산하지 않는다.
- OS free memory, process RSS와 GC heap 크기는 startup 또는 명시적인 진단 시점에만 읽는다.

Connection별 chunk lease는 기본 경로에 사용하지 않는다. 사용하지 않은 chunk도 전역 HWM에
포함하면 안전한 동시 `Recv` 수가 늘지 않고, connection 수에 비례해 예약 byte만 증가한다.
복수 MeshNode와 복수 I/O thread benchmark에서 중앙 permit 발급이 병목으로 확인되면 고정된
수의 ingress poller 또는 I/O thread별 budget shard를 사용한다. 전역 owner는 complete-message
permit 단위로만 shard에 임대하고, 모든 shard의 committed byte 합이 Application HWM을 넘지
않게 한다. Shard 수를 connection이나 무제한 MeshNode 수에 비례해 늘리지 않으며, 사용하지
않은 permit은 정해진 idle·rebalance 시점에 전역 owner로 반환한다.

나중에 Core가 dequeue 전에 complete message의 정확한 accounted byte를 제공할 수 있을 때는
실측 결과를 근거로 최대 message 선예약 방식도 다시 검토한다.

Core는 기존 pipe 동기화 구간에서 message count 증가를 accounted byte 증가로 바꾼다. HWM을
위해 새 mutex나 message별 atomic counter를 추가하지 않는다. Multipart total은 기존
multipart transaction이 part를 처리할 때 함께 누적하고, HWM 검사를 위해 payload를 다시
순회하지 않는다.

이 설계의 성능 목표는 HWM이 충분한 정상 상태에서 새 system call과 새 heap allocation이
없고, complete message마다 고정 횟수의 정수 연산만 추가되는 것이다. 성능 검증에서는 HWM
기능을 끈 build와 같은 workload를 비교하여 throughput, CPU/message와 p99 latency 변화를
측정한다. 복수 MeshNode와 복수 I/O thread에서 작은 message를 처리할 때도 현재 MeshNode별
병렬 drain 대비 aggregate throughput과 scaling이 정식 계약 전에 정한 허용 범위 안에 있어야
한다. 이 기준을 만족하지 못하면 중앙 permit 발급을 적용하지 않고 위의 bounded budget shard
방식을 검증한다.

### 4.13 Framework monitoring

운영자는 설정값뿐 아니라 실제 byte 사용량과 backpressure 원인을 확인할 수 있어야 한다.
Framework runtime status에는 적어도 다음 값을 같은 snapshot 시점으로 제공한다.

| 값 | 단위와 의미 |
|---|---|
| Application HWM·profile LWM | 적용한 `uint64_t` bytes와 LWM ratio |
| Effective resume threshold | Permit 크기까지 반영해 실제 재개에 사용하는 bytes |
| Application used | Queue, handler와 ingress hold가 소유한 accounted bytes |
| Receive permit | 시작했지만 아직 반환하지 않은 permit bytes |
| Receive permit concurrency | 현재·최대 permit 수와 설정으로 가능한 최대 동시 수 |
| Application receive state | 실행 중 또는 HWM으로 중단 |
| Pause count·duration | 수신 중단 횟수와 누적 시간 |
| Minimum message charge | 현재 언어 runtime에 적용한 bytes |
| Core transport reserve | Startup 계산에 사용한 bytes |
| Completion reserve·used | 요청자·응답자별 계획 reserve와 현재 accounted bytes |
| Pending request | 현재 값과 허용한 최대 count |
| Completion send permit | 현재 permit·미전송 reply count와 bytes, host 상한과 peer별 상한 |

Status를 만들 때 queue나 pending request map 전체를 순회하지 않는다. Budget owner와 request
admission이 이미 유지하는 counter의 snapshot만 읽는다. Message마다 monitor event를 만들지
않고 기존 주기적 status 또는 명시적 조회에서만 값을 내보낸다.

### 4.14 Application byte 귀속 진단

전역 HWM으로 모든 Application connection을 중단하면 어떤 실행 대상이 byte를 보유하는지
확인할 수 있어야 한다. Node, Channel, Spot과 Actor가 이미 소유한 mailbox 또는 execution
gate에 다음 진단 counter를 둔다.

- Queue에서 기다리는 accounted bytes
- 실행 중인 handler가 보유한 accounted bytes
- Target-local ingress hold와 relocation hold bytes
- 위 상태를 합한 owner total bytes와 message count

Target이 정해지기 전의 message는 `unattributedIngressBytes`에 포함한다. Message Follow,
relocation transfer처럼 둘 이상의 owner 사이에 있는 message는 별도
`infrastructureHoldBytes`에 포함한다.

```text
applicationUsedBytes =
    sum(applicationOwnerBytes)
    + unattributedIngressBytes
    + infrastructureHoldBytes
```

Budget lease는 target이 정해지면 기존 mailbox나 execution owner의 counter를 가리킨다.
Enqueue와 handler terminal은 기존 execution gate 또는 host budget owner 안에서 counter를
갱신한다. 다른 thread에서 반환된 byte는 owner key와 함께 budget owner에 전달하여 다음
poller loop에서 반영한다. Owner별 새 atomic이나 lock을 추가하지 않는다.

정상 주기 status는 owner 전체를 순회하지 않는다. 운영자가 명시적으로 요청하거나 HWM
pause가 정해진 시간 이상 지속될 때만 active owner registry를 rate-limit하여 조사하고,
보유 byte가 큰 상위 N개 owner를 반환한다. 결과에는 owner kind와 안정적인 논리 ID, queued,
executing, hold와 total byte를 포함한다. Owner resource는 관련 lease가 모두 반환되기 전에
제거하지 않는다.

### 4.15 상호 과부하와 nested request

A와 B의 request handler가 서로에게 nested request를 보내고 두 host가 동시에 Application
HWM에 도달하면, 두 nested request가 Application connection의 network pipe에서 기다릴 수
있다. 실행 중인 handler는 원본 request byte와 execution gate를 유지한 채 nested reply를
기다리므로 양쪽 application ingress가 함께 중단될 수 있다.

Completion connection과 local timeout scheduler는 application ingress와 독립적으로
진행한다. 유한한 nested request timeout이 끝나면 waiting handler의 completion을 확정하고,
handler가 terminal로 끝날 때 원본 request lease를 반환한다. 이 동작으로 영구 교착을
피하지만 timeout 시간 동안 양쪽 application ingress가 중단될 수 있다.

Timeout은 backpressure 제어 수단이 아니라 마지막 liveness 경계다. Caller timeout은 이미
시작한 remote handler를 rollback하지 않으며, timeout을 받은 handler가 무한히 대기하거나
application code가 terminal로 끝나지 않으면 Framework가 byte를 강제로 반환하지 않는다.
Nested request를 사용하는 workload는 유한한 request timeout을 사용해야 하며 pause duration,
nested timeout 수와 timeout 뒤 budget 회복 시간을 관측한다.

## 5. Core C HWM contract 변경 범위

> 이 절도 작성자가 최종 승인하기 전까지 현재 Core C 계약이나 구현을 변경하지 않는다.

Core HWM을 message count에서 byte로 바꾸는 것은 `pipe_t` 내부 구현만의 변경이 아니다.
현재 C API가 HWM option의 값과 단위를 공개하므로 public contract, ABI에 전달하는 값의
크기, Auto HWM, monitoring과 모든 bindings를 함께 변경해야 한다.

### 5.1 현재 C 계약

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

### 5.2 목표 option 변경 방식

이 목표안은 기존 `ZLINK_OPT_SNDHWM`과 `ZLINK_OPT_RCVHWM`의 단위를 message count에서 byte로
직접 바꾼다. 기존 동작과 source·binary 호환성을 제공하지 않는 breaking change이며,
호환 계층은 만들지 않는다.

- Count HWM과 byte HWM을 함께 유지하지 않는다.
- `SNDBYTESHWM`이나 `RCVBYTESHWM` 같은 compatibility option을 추가하지 않는다.
- Option value 길이나 값으로 이전 count 설정인지 추측하지 않는다.
- 이전 binding의 `message_count_t`를 compatibility alias로 남기지 않는다.
- 이전 binary가 전달하는 4-byte HWM 값은 명시적인 configuration error로 실패한다.

따라서 HWM은 모든 public API, 내부 admission과 monitoring에서 byte 한 단위만 사용한다.
작성자가 최종 승인하면 이 breaking contract를 정식 Core draft에 먼저 기록하고 같은
release에서 Core header, 모든 bindings와 Framework를 함께 변경한다.

### 5.3 기존 option을 byte로 바꾸는 경우

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

### 5.4 Auto HWM option

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

### 5.5 Monitoring ABI

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

### 5.6 Core 내부 구현

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

Standalone Core raw socket에서 `MaxMessageSize`가 무제한이면 빈 pipe의 한 message 예외도
유한한 process memory 상한을 보장하지 않는다. 이는 per-pipe liveness 규칙으로만 취급한다.
Framework의 memory 제한 모드는 유한한 `MaxMessageSize`를 startup 조건으로 강제하므로
`coreTransportReserve`를 계산할 수 있다.

Receiver가 읽은 누적 byte와 마지막으로 알린 byte의 차이가 byte LWM 이상이면 sender에
credit을 반환한다.

```text
lwmBytes =
    ceil(hwmBytes / 2)

returnCredit =
    bytesReadSinceLastCredit >= lwmBytes
```

이 Core pipe LWM은 transport credit을 반환하는 주기를 정하며 HWM의 절반으로 고정한다.
Framework의 profile LWM과 `applicationResumeThresholdBytes`는 Application `Recv`를 재개하는
별도 기준이다. Framework profile을 바꿔도 Core pipe LWM을 바꾸지 않으며, 두 값을 하나의
설정으로 통합하지 않는다.

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

### 5.7 영향을 받는 저장소 표면

| 범위 | 필요한 변경 |
|---|---|
| C public header | Option value type·단위, 기본값과 monitoring struct를 변경한다. |
| Core option storage | `sndhwm`, `rcvhwm`, deferred HWM과 Auto HWM 계획값을 64-bit byte로 바꾼다. |
| `pipe_t` | Byte written·read·peer credit, admission, LWM, multipart와 rollback을 구현한다. |
| Socket lifecycle | Endpoint pair, inproc 합산, reconnect와 runtime HWM 갱신에 byte 값을 전달한다. |
| Peer connection 관리 | 두 connection을 같은 peer pair로 검증하고 generation·epoch·sequence fence로 connection 사이 순서를 보장한다. |
| Request/reply runtime | 내부 payload queue를 제거하고 reply를 Completion connection에서 직접 완료하며 request byte를 completion reserve로 transfer한다. |
| Auto HWM | Profile·bucket 결과를 message slot이 아니라 최종 byte HWM으로 적용한다. |
| Monitoring | Applied·deferred HWM, in-flight byte, pause, permit, owner attribution과 completion reserve를 제공한다. |
| Bindings | C 값 타입과 단위를 각 언어의 64-bit byte 타입으로 노출한다. |
| Framework | Host budget owner, owner-local attribution, receive permit과 nested overload 관측을 구현한다. |
| Benchmark와 test | 기존 HWM 환경 변수와 fixture를 count에서 byte로 바꾸고 migration 값을 기록한다. |
| Spec과 guide | Core draft, 정식 socket option, monitoring, bindings와 migration 문서를 함께 갱신한다. |

C++ binding처럼 현재 HWM을 `message_count_t`로 표현하는 binding은 값 타입 이름도
`byte_count_t` 또는 byte 전용 타입으로 바꿔야 한다. 단순히 내부 정수만 크게 만들어서는
호출자가 단위를 알 수 없다.

### 5.8 검증 기준

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
14. 유한한 `MaxMessageSize`, `ApplicationHwmBytes >= effectiveMaxAccountedMessageBytes`와
    unlimited HWM 거부 조건을 startup contract test로 검증한다.
15. Core가 마지막 frame을 commit하기 전에는 incomplete multipart를 Framework에 공개하지
    않고 receive permit도 점유하지 않으며, 완성된 큰 multipart를 한 건의 message로
    `Recv`하는지 검증한다.
16. 최대 pending request 수에 도달하면 새 request admission만 대기하고 기존 completion은
    계속 처리되는지 검증한다.
17. 목표 최대 connection 수에서 요청자·응답자 `completionReserve`,
    `coreTransportReserve`와 Framework backlog를 함께 채워도 process memory limit과
    emergency headroom을 침범하지 않는지 검증한다.
18. HWM 기능을 끈 기준 build와 비교하여 새 allocation·mutex·system call이 hot path에
    추가되지 않았는지 확인하고 throughput, CPU/message와 p99 latency를 기록한다.
19. 복수 MeshNode와 복수 I/O thread에서 작은 message를 처리하여 현재 MeshNode별 병렬 drain
    대비 aggregate throughput과 scaling이 허용 범위 안에 있는지 검증한다. 중앙 permit
    발급이 기준에 미달하면 bounded budget shard가 전역 HWM을 넘지 않으면서 기준을
    만족하는지 비교한다.
20. 한 Spot이나 Actor가 대부분의 byte를 보유하면 host 합계와 owner·unattributed·infrastructure
    합계가 일치하고 명시적 top-N 진단에서 해당 owner가 확인되는지 검증한다.
21. A와 B가 동시에 HWM에 도달한 상태에서 양방향 nested request를 보내고, Completion
    connection과 local timeout이 계속 진행되어 handler terminal 뒤 양쪽 budget과 ingress가
    회복되는지 검증한다.
22. Completion send permit이 host 상한에 도달하면 추가 request handler admission이
    대기하고 request byte가 Application budget에 남는지 검증한다. 이미 terminal에 도달한
    handler의 original request lease는 반환되고 미전송 reply count와 byte는 responder
    completion reserve 상한을 넘지 않으며, Core가 send를 받아들일 때 두 reserve 사이에서
    byte가 누락되거나 이중으로 계산되지 않아야 한다.
23. HWM과 `MaxMessageSize` 조합별 `maxConcurrentReceivePermits`, startup log와 실제 permit
    상한이 일치하고 connection별 미사용 chunk reserve가 생기지 않는지 검증한다.
24. Application message와 relocation·session binding·peer lifecycle control을 두
    connection에서 양방향으로 추월시켜도 generation, epoch, sequence와 barrier가 stale
    control 적용과 순서 역전을 막는지 검증한다. Fence 대기 상한을 넘으면 pair 전체와 pair
    generation이 종료되고 해당 pending request가 terminal error로 끝나는지도 확인한다.
25. LWM profile별 `applicationResumeThresholdBytes`, 실제 pause duration과 resume 횟수가
    계산값과 일치하며 Framework profile 변경이 Core pipe LWM을 바꾸지 않는지 검증한다.
26. Framework `ApplicationHwmBytes=0`은 Auto 계산을 수행하고, 계산한 유한값을 Core에
    전달하며 Core HWM의 `0=무제한`과 혼동하지 않는지 contract test로 검증한다.

## 6. 적용 조건

이 문서의 내용은 작성자가 최종 승인을 명시한 뒤에만 정식 spec과 구현 계획에 반영한다.
특정 버전, 일정, 측정 결과나 리뷰 완료 여부만으로 자동 적용하지 않는다. 최종 승인 전에는
Core, Framework, bindings, wire protocol, public header와 정식 spec을 변경하지 않는다.

최종 승인한 뒤에는 다음 순서를 지킨다.

1. Core draft에 count HWM을 제거하고 기존 option을 `uint64_t` byte HWM으로 바꾸는 breaking
   contract를 기록한다. 이전 동작을 유지하는 option, alias와 adapter는 만들지 않는다.
2. Completion connection의 handshake, peer pair 검증, reconnect, pending request 종료
   error와 wire fixture를 확정한다.
3. Framework 공통 spec과 다섯 언어 exact interface에 `ApplicationHwmBytes`, 유한한
   `MaxMessageSize`, startup error와 Auto 우선순위를 기록한다.
4. 목표 connection 수에서 minimum charge, Core·Framework memory amplification,
   `coreTransportReserve`와 `completionReserve`를 측정한다.
5. Core byte accounting, 두 connection과 내부 PAIR receive queue 제거를 구현한다.
6. Framework host 전역 budget owner와 Application `Recv` 중단·재개를 구현한다.
7. 이 문서의 기능·memory·성능 검증 기준을 모두 통과한 뒤 정식 spec과 guide를 현재
   구현에 맞춰 갱신한다.
