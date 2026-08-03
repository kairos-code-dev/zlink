# Java/Kotlin Framework runtime gap unit 진행 기록

실행 시각: 2026-08-03 08:50 KST
브랜치: `agent/framework-contract-runtime-update`
기준 HEAD: `b718b4d5cd7ed28f3e399339220b83431bdae902`

## 이번 범위

사용자 요청에 따라 Java/Kotlin Framework의 JK-IMP-001~012 중 production runtime
구현과 unit/contract regression을 먼저 확인했다. process E2E, sample, CI는 이 기록의
runtime unit gate와 분리했다.

## 이번에 반영한 runtime 회귀

- `ZLinkInboundDispatchBudget`의 Application HWM 기준을 queued payload로 정렬했다.
  handler가 실행을 시작하면 해당 payload는 queue HWM에서 빠지고, 실행 중인 handler의
  payload는 terminal result 전까지 active 상태로 관찰한다.
- runtime shutdown에서 새 payload/completion admission을 막고, 기존 payload lease가
  terminal close될 때 accounting을 안전하게 정리하도록 했다. shutdown 뒤 receive가
  다시 열리지 않는 회귀를 추가했다.
- STREAM ingress 회귀를 queue-only HWM 의미로 변경해 active handler가 HWM을 다시 열고
  다음 frame을 받을 수 있는지 확인했다.
- Java HTTP client가 request deadline의 남은 시간을 body read에도 적용하는 회귀를
  추가했다.

## 통과한 검증

다음 명령은 exit 0으로 끝났다.

```text
./gradlew --no-daemon --no-parallel \
  :zlink-framework-core:test \
  :zlink-framework-kotlin:test \
  :zlink-framework-kotlin:contractTest \
  :zlink-stream-connector:test \
  :zlink-http-client:test \
  :zlink-http-client-kotlin:test
```

Gradle은 37 actionable tasks를 처리했고, Java core test는 726 tests를 실행했다.
추가 focused contract 명령도 exit 0이다.

```text
./gradlew --no-daemon --no-parallel \
  :zlink-framework-core:test \
  :zlink-framework-core:contractTest \
  --tests systems.zlink.framework.runtime.binding.ZLinkJavaRawSpotNodeM6BTest \
  --tests systems.zlink.framework.runtime.internal.dispatch.ZLinkInboundDispatchBudgetTest \
  --tests systems.zlink.framework.JavaTargetContractGapTest \
  --tests systems.zlink.framework.JvmPublicContractSourceOwnerTest \
  --tests systems.zlink.framework.runtime.streams.StreamNetworkDefaultsTest \
  --tests systems.zlink.framework.runtime.streams.ZLinkStreamRuntimeIngressTest
```

Java와 Kotlin packaged clean consumer 및 API snapshot도 각각 exit 0이다.

- Java: API snapshot `dd5f7c3e071c7bf290d95148921919684ea84d46cd9a73c21db88e3374d6d994`,
  2,851 lines, 9 artifacts
- Kotlin: API snapshot `0bf535f36206e713566375a40555a36d6fe1511dc905add8e89610cb75b4f287`,
  3,359 lines, 11 artifacts

## 별도 common contract blocker

`./gradlew --no-daemon --no-parallel :zlink-framework-core:contractTest`는 exit 1이다.
26개 contract test 중 common E2E inventory 두 항목만 실패했다.

- Java: `expected=374`, `sourceIds=220`, `missingIds=154`, missing suite는
  `Config 12=ChannelEgressRouting`, `Config 14=InstanceSpot`
- Kotlin: `expected=374`, `sourceIds=183`, `missingIds=191`, missing suite는
  `Config 6=StoreFailure`, `Config 12=ChannelEgressRouting`,
  `Config 13=SubmitAdmission`, `Config 14=InstanceSpot`

이는 이번 runtime source/unit 변경으로 발생한 failure가 아니라 Java/Kotlin E2E
source·runner inventory 누락이다. 따라서 runtime unit gate와 process/E2E gate를
분리해 기록한다.

## publish 상태

runtime ledger와 현재 worktree 변경은 `ab79dc36e45` (`framework: complete runtime gap
unit gates`)로 먼저 commit하고 origin branch에 push했다. push 직후 다른 workstream이
수정한 C++ contract assertion 3개는 사용자 요청의 전체 변경 범위에 맞춰 후속 commit에
`323fdcc3548` (`framework: publish post-push contract updates`)로 포함해 push했다.
최종 local/remote branch SHA는 `323fdcc3548bc90345b5b9f08044000e9eb667a7`로 일치한다.

## 현재 판정

JK-IMP-001~012의 source/runtime unit 또는 contract regression 범위는 이번 실행으로
확인했다. 특히 stream queue/drop, handler-less wait ownership, content type 전달,
actor authority/local join, global ActorId builder, fanout prefix, RouteMesh option,
Application HWM, Kotlin `yield`/coroutine bridge, HTTP deadline, Socket Poller/STREAM
recv 경계를 포함한다.

이는 runtime unit·source·package gate의 판정이다. common 374 scenario inventory의
누락, SubmitAdmission의 stale native package, 전체 role process E2E, six sample process,
CI와 최종 POSD/DDD review는 아직 별도 blocker다. 따라서 ledger의 전체 완료 상태는
`진행 중`으로 유지한다.
