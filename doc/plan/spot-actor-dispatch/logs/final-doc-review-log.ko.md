# Final Doc Review Log

- 날짜: 2026-05-04
- 대상: doc/spec, doc/guide, doc/internals
- 수행한 명령: draft spec/plan 확인
- 발견한 문제: 최종 문서 반영 전
- 수정한 파일: 없음
- 남은 위험: 구현 완료 뒤 3회 문서 리뷰 필요
- 다음 확인: core 구현/테스트 완료 뒤 정식 문서 반영

## 2026-05-04 1차 문서 반영

- 대상: Discovery/Registry formal spec, Registry guide, monitoring spec
- 수정한 파일:
  - `doc/spec/core/service/discovery.ko.md`
  - `doc/spec/core/service/discovery.md`
  - `doc/spec/core/service/registry.ko.md`
  - `doc/spec/core/service/registry.md`
  - `doc/spec/core/monitoring.ko.md`
  - `doc/spec/core/monitoring.md`
  - `doc/guide/07-4-registry.ko.md`
  - `doc/guide/07-4-registry.md`
- 확인:
  - formal spec에서는 제거된 generic route API 계약을 삭제하고 `zlink_discovery_resolve_actor()` 계약을 추가했다.
  - guide에서는 generic route 사용 예제를 제거하고 Actor active route 조회 방향을 설명했다.
  - 금지 표현 검색 결과는 비어 있다.
- 남은 위험: Actor 전체 API의 정식 spec 분리는 아직 완료되지 않았다.

## 2026-05-04 3회 리뷰 결과

- 수행한 명령:
  - 금지 표현 검색 명령
  - `rg -n "zlink_discovery_bind_route|zlink_discovery_unbind_route|zlink_discovery_resolve_route|zlink_route_kind_t" core/include core/tests doc/spec/core doc/guide doc/internals`
- 반복 횟수: 3회
- 결과: 모두 빈 결과
- 남은 위험: bindings 문서와 언어별 spec에는 제거된 generic route 항목이 아직 남아 있다.

## 2026-05-05 POSD 후 1차 문서 리뷰

- 대상: `doc/spec/core`, `doc/guide`, `doc/internals`, `doc/site/docs`,
  sample 정책 문서
- 발견한 mismatch:
  - `doc/site/docs`의 Discovery/Registry/Monitoring 문서와 Registry guide에
    제거된 generic discovery route 공개 API가 남아 있었다.
  - `doc/site/docs/guide/07-3-spot.ko.md`가 source guide보다 오래되어 Actor 사용
    흐름이 빠져 있었다.
  - `doc/guide/07-3-spot.ko.md`에 내부 socket 세부를 설명하는 문장이 있었다.
- 수정:
  - site Discovery/Registry/Monitoring 문서에서 generic route 공개 API 설명을
    제거하고 `zlink_discovery_resolve_actor()` 기준으로 맞췄다.
  - site SPOT guide를 source guide와 동기화했다.
  - guide의 HWM 진단 설명을 사용자 관점의 snapshot/dispatch 기준으로 바꿨다.
- 검증 명령:
  - Actor public API list를 header, core spec, site spec과 대조
  - `rg`로 Actor route, join request, STREAM session Actor list, sample 반영 확인
  - errno/result mapping 문서 확인
- 결과: 1차 mismatch 수정 완료

## 2026-05-05 POSD 후 2차 문서 리뷰

- 검증 명령:
  - `rg -n "language-exchange|문서작성|zlink_spot_node_remote_actor_ref|zlink_stream_lookup_actor|zlink_stream_send_actor_part|session_actor_key|Actor HWM option|actor HWM option|ACTOR_HWM|HWM.*ACTOR|zlink_discovery_(bind_route|unbind_route|resolve_route)|zlink_route_kind_t|ZLINK_ROUTE_KIND_INVALID|ZLINK_ROUTE_KEY_MAX|ZLINK_ROUTE_VALUE_MAX|generation == 0.*invalid|invalid.*generation == 0|RemoteActor" doc/spec/core doc/guide doc/internals doc/site/docs --glob '!doc/spec/draft/**' --glob '!doc/plan/**'`
  - `rg -n "숨은 내부 socket|내부 socket|inproc endpoint|transport endpoint|lock|mutex|clear_actor_bound_session|clear_actor_joined_spot" doc/guide/07-3-spot.ko.md doc/site/docs/guide/07-3-spot.ko.md`
  - `diff -u doc/site/docs/guide/07-3-spot.ko.md doc/guide/07-3-spot.ko.md`
- 결과:
  - stale API 이름과 제거 대상 API 설명은 정식 문서, guide, internals, site docs에서
    검색되지 않았다.
  - guide에는 Actor 내부 endpoint, lock, helper 이름 설명이 남아 있지 않다.
  - site SPOT guide와 source guide가 일치한다.

## 2026-05-05 POSD 후 3차 문서 리뷰 완료

- 검증 명령:
  - `rg -n "사용법|사용 예|튜토리얼|tutorial|how to|when to use|언제 .*쓴|어떻게 .*쓴" doc/spec/core/service/spot.ko.md doc/spec/core/socket/stream.ko.md doc/spec/core/errno-map.ko.md doc/site/docs/api/spot.ko.md doc/site/docs/api/discovery.ko.md`
  - `rg -n "사용자는|사용법|샘플|sample|예제|tutorial|zlink_spot_node_actor_new\\(|zlink_stream_bind_actor\\(" doc/internals/spot-internals.ko.md doc/site/docs/internals/spot-internals.ko.md`
  - Actor 핵심 계약 검색: socket 없음, unchecked/checked ref, active route,
    join request, STREAM session Actor list
  - draft 문서의 정식 문서 링크 확인
- 결과:
  - spec, guide, internals의 목적이 섞인 새 항목은 없다.
  - 3차 리뷰 mismatch는 없다.
  - draft 문서는 정식 반영 문서 링크를 가진 historical draft 상태다.
