# Java/Kotlin runtime poller 회귀 검증 기록

2026-08-02 22:25 KST 기준 Java/Kotlin Framework runtime gap 후속 검증 기록이다. Core
source·public header·ABI와 공통 contract 문서는 수정하지 않았다. 현재 working tree의
다른 workstream 변경은 보존했다.

## 구현 확인

- .NET의 `ZLinkBackendSocketPoller`와 `ZLinkChannelReceiveLoop`를 기준으로 Java
  receive 순서를 `poller wait → application capacity → DONT_WAIT recv/subscribe`로
  정렬했다.
- `StreamSocket`, `RouterSocket`, `DealerSocket`, `SubscriberSocket`과 raw service
  `RouterSocket`은 public `Poller` readiness를 사용한다. readiness는
  `POLLIN|POLLERR|POLLPRI`를 받으며, completion이 필요한 경로는
  `POLLIN|POLLCOMPLETION`을 같은 poller에 등록한다.
- STREAM Framework ingress는 packet callback을 등록하지 않고 public `recv`로 raw part를
  받아 frame을 조립한다. HWM admission 실패 뒤에는 receive를 발행하지 않고 capacity
  회복 후 재개한다.
- Codex read-only review는 `gpt-5.6-luna`, `reasoning_effort=low`로 수행했다. 이
  범위에는 Critical·High·Medium finding이 없었다. `RequestProgressPump.track()`와
  `NativePoller.add()` 사이의 단일 초기화 owner race 가능성만 Low 후속 항목으로 남았다.

## 실행 결과

| 명령 | 결과 |
|---|---|
| `:zlink-framework-core:test :zlink-framework-testkit:test :zlink-framework-kotlin:test :zlink-stream-connector:test :zlink-http-client:test :zlink-http-client-kotlin:test --rerun-tasks` | PASS, 41 actionable tasks |
| `:zlink-framework-core:integrationTest --rerun-tasks` | PASS, 8 actionable tasks |
| `:zlink-framework-kotlin:integrationTest --rerun-tasks` | PASS, 22 actionable tasks |
| `bindings/java :test` with approved Core 11.1.0 prefix | PASS |
| `scripts/verify_packaged_contract.sh java` | PASS |
| `scripts/verify_packaged_contract.sh kotlin` | PASS |
| `:zlink-framework-core:contractTest --rerun-tasks` | FAIL, Java/Kotlin exact E2E inventory 2건 |
| `git diff --check` | PASS |

contract inventory의 현재 결과는 Java `expected=374/sourceIds=220/missingIds=154`,
Kotlin `expected=374/sourceIds=183/missingIds=191`이다. Java의 Config 12/14, Kotlin의
Config 6/12/13/14 source/runner가 아직 없다. Java PubSub PS-B2 process E2E는 publisher
topology row timeout으로 남아 있으며, 전체 aggregate E2E·API snapshot·CI gate도 아직
완료로 올리지 않는다.
