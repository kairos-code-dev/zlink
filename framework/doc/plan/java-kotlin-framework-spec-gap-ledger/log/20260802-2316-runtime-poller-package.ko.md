# Java/Kotlin runtime poller·package 후속 검증 기록

2026-08-02 23:16 KST 기준 기록이다. Java/Kotlin Framework runtime과 Java binding만
대상으로 했으며, Core 11.1.0 source·public header·ABI와 공통 contract 문서는 수정하지
않았다. working tree의 다른 workstream 변경은 staging 대상에서 제외했다.

## 변경 및 review

- .NET의 `poller wait → capacity 확인 → DONT_WAIT recv/subscribe` 순서를 Java runtime의
  모든 native Socket receive 경로에 적용했다. STREAM Framework ingress는 packet callback을
  등록하지 않고 public `recv`로 raw part를 받아 frame을 조립한다.
- Java binding의 completion 진행권을 public `Poller`와 fallback `RequestProgressPump` 중
  하나만 보유하도록 직렬화했다. completion 경로는 `POLLIN|POLLCOMPLETION`을 하나의
  poller에 등록하고, 일반 receive readiness는 `POLLIN|POLLERR|POLLPRI`를 사용한다.
- Application HWM은 explicit `ProcessMemoryLimitBytes`를 우선하고, otherwise OS/cgroup와
  `Runtime.maxMemory()` 중 유효한 작은 값을 사용하며 physical memory를 최종 fallback으로
  사용한다. binding patch release는 `11.1.1`로 올렸지만 embedded native Core는 `11.1.0`이다.
- 첫 Codex review(`gpt-5.6-luna`, low)는 STREAM admission과 shutdown이 겹칠 때 frame을
  닫지 않을 수 있는 Low를 지적했다. `ZLinkStreamRuntime.tryAdmitFrame()`를
  `retainForRetry` 기반으로 수정해 HWM admission retry 두 경로에서만 frame을 보류하고,
  나머지 경로에서는 `finally`로 닫도록 했다.
- 수정 후 Codex follow-up은 Critical, High, Medium, Low finding이 없다고 판정했다.
  evidence는 다음 두 파일이다.
  - `.artifacts/v11/evidence/codex-java-poller-review-20260802-r4.md`
  - `.artifacts/v11/evidence/codex-java-poller-review-20260802-r5.md`

## 실행 결과

| 명령 | 결과 |
|---|---|
| `:zlink-framework-core:test --tests systems.zlink.framework.runtime.streams.ZLinkStreamRuntimeIngressTest --rerun-tasks` | PASS, 7 actionable tasks |
| `:zlink-framework-core:test :zlink-framework-testkit:test :zlink-framework-kotlin:test --rerun-tasks` | PASS, 29 actionable tasks |
| `:zlink-framework-core:integrationTest :zlink-framework-kotlin:integrationTest --rerun-tasks` | PASS, Java 8·Kotlin 22 actionable tasks; package version 변경 전 실행도 포함해 별도 기록 유지 |
| `bindings/java :test` with approved Core 11.1.0 prefix | PASS |
| fresh `systems.zlink:zlink:11.1.1` package와 isolated consumer | PASS. native Core 11.1.0 확인 |
| Java/Kotlin packaged consumer | PASS |
| `:zlink-framework-core:contractTest --rerun-tasks` | FAIL. Java 220/374, Kotlin 183/374 exact E2E inventory 누락 |
| Java PubSub PS-B2 process E2E | FAIL. publisher topology row timeout; 기존 evidence와 동일 |
| `git diff --check` | PASS |

## 판정

Socket Poller·STREAM recv·Application HWM runtime gap의 구현과 좁은 regression,
binding package/consumer gate는 완료 조건을 충족했다. 전체 Java/Kotlin ledger는 아직
완료하지 않는다. common 374 scenario aggregate, missing Config suite, Java PubSub PS-B2,
API snapshot, sample configuration policy와 CI gate는 별도 blocker로 남아 있다.
