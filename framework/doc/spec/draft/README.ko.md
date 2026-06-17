<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [다음: Draft -- ZLink Stream Connector](./streaming-client.ko.md)
<!-- framework-adapter-nav:end -->

# ZLink Framework 공통 스펙 — Draft

[Framework 문서](../../README.ko.md) | [공통 스펙](../README.ko.md)

이 디렉토리는 **언어 중립 공통 스펙 중 아직 닫히지 않은 초안**만 모은다.

정식 공통 스펙은 [../README.ko.md](../README.ko.md)에 있다. 여기 문서는 공개
계약이 아니며, 공개 계약이 고정되면 상위 `spec/`으로 옮긴다. 언어별 초안은 이
디렉토리가 아니라 각 `languages/<lang>/doc/draft/`에 둔다.

## 문서 목록

| 문서 | 설명 |
|------|------|
| [streaming-client.ko.md](./streaming-client.ko.md) | STREAM 서버에 접속하는 범용 client connector 초안 |
| [spot-actor-disconnected-callback-rollout.ko.md](./spot-actor-disconnected-callback-rollout.ko.md) | Spot actor disconnected를 handler 등록에서 lifecycle callback으로 옮기는 공통 전환 계획 |
| [entry-spot-join-admission-lifecycle.ko.md](./entry-spot-join-admission-lifecycle.ko.md) | Entry Spot join을 user Spot join과 같은 admission/request/reply 모델로 맞추고 commit 이후 callback 이름을 `OnJoinedActor` 계열로 정리하는 초안 |
| [entry-spot-actor-destroy-plan.ko.md](./entry-spot-actor-destroy-plan.ko.md) | actor destroy API를 Entry Spot context에 추가하고 네 언어 framework와 문서, 회귀 테스트에 반영하는 계획 |
| [entry-spot-serial-dispatch-plan.ko.md](./entry-spot-serial-dispatch-plan.ko.md) | Entry Spot callback과 request continuation을 일반 Spot처럼 직렬 실행하는 공통 전환 계획 |
| [framework-unhandled-dispatch-policy.ko.md](./framework-unhandled-dispatch-policy.ko.md) | handler가 없는 메시지(unhandled dispatch)의 공통 처리 정책 초안 |

## 관리 원칙

- 요구가 생기면 먼저 use case 또는 draft 문서에 기록한다.
- 정식 공통 스펙은 구현과 공개 계약이 고정된 뒤 `spec/`으로 승격한다.
- 언어별 상세는 공통 스펙에서 개념을 먼저 닫은 뒤 해당 언어 문서로 내린다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [다음: Draft -- ZLink Stream Connector](./streaming-client.ko.md)
<!-- framework-adapter-nav:bottom:end -->
