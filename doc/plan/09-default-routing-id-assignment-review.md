# Default Routing ID 전소켓 적용 검토

> 상태: Review
> 대상 스펙: [00-routing-id-unification.md](00-routing-id-unification.md), [01-enhanced-monitoring.md](01-enhanced-monitoring.md)

> 업데이트(2026-02-07):
> 본 문서의 기존 결론(기본 5B `[0x00][uint32]`)은 최신 합의로 대체됨.
> 최신 정책은 다음과 같음:
> - 소켓 own 자동 routing_id: 16B UUID(binary)
> - STREAM peer/client routing_id: 4B uint32

## 1) 목적

요구사항:
- 소켓별 기능적 필요 여부와 무관하게, 디버깅/모니터링 식별 목적으로 모든 소켓에 기본 routing_id를 부여
- 현재 `core/src/services/gateway/routing_id_utils.hpp`에 있는 자동 생성 로직의 위치 적절성 검토
- 해당 유틸을 공통 자동 생성 경로로 사용 가능한지 검토
- 스펙대로 "소켓 생성 시 기본 routing_id 부여"를 적용해도 이슈가 없는지 검토

## 2) 현재 코드 기준 사실 확인

### 2.1 전소켓 기본 routing_id 부여는 이미 코어에서 동작 중

`core/src/sockets/socket_base.cpp` 생성자에서 다음이 이미 수행됨(현 코드):
- `options.routing_id_size == 0`이면 자동 생성
- 포맷: 5B `[0x00][uint32]`
- 값: `sid` 기반(0 회피)

즉, "소켓 생성 기본 routing_id 부여"는 서비스 레이어가 아니라 코어 소켓 생성 경로에서 일괄 적용되고 있음.
최신 방향에서는 이 생성 포맷을 16B UUID로 바꾸는 것으로 문서 정책이 갱신됨.

### 2.2 자동 생성 ID의 전역 유일성

`core/src/core/ctx.cpp`의 `ctx_t::max_socket_id`는 전역(static) 카운터로 관리됨.
- 서로 다른 context 간에도 socket_id가 중복되지 않도록 설계됨
- 따라서 기본 routing_id(현 5B)의 충돌 가능성은 프로세스 내에서 매우 낮음(사실상 없음)
- 최신 정책(16B UUID) 기준으로는 프로세스/노드 간 충돌 가능성도 더 낮아짐

### 2.3 `routing_id_utils.hpp`의 현재 용도

`core/src/services/gateway/routing_id_utils.hpp`:
- gateway/receiver 서비스 전용 보조 유틸로 사용됨
- override가 없을 때 `generate_random()` 기반 5B routing_id를 `setsockopt(ZLINK_ROUTING_ID)`로 강제 설정(서비스 전용)
- 사용처: `services/gateway/gateway.cpp`, `services/gateway/receiver.cpp`

## 3) 핵심 결론

### 결론 A: 전소켓 기본 routing_id를 위해 `routing_id_utils.hpp`를 코어 기본 경로로 쓰는 것은 비권장

이유:
1. 중복 책임
- 코어(`socket_base`)에서 이미 기본값을 부여하고 있음
- 서비스 유틸로 다시 설정하면 책임이 중복되고 동작 순서 의존성이 증가

2. 충돌/안정성 측면
- 코어 기본값은 `socket_id` 기반으로 유일성/재현성이 좋음
- 유틸은 랜덤 기반이라 충돌 확률은 낮지만 0이 아님
- 전소켓 기본 정책은 랜덤보다 코어 카운터 기반이 적합

3. 계층 위반
- `services/gateway/*` 유틸은 서비스 계층 관심사
- 전소켓 정책(코어 정책)에 사용하면 의존 방향이 역전됨

### 결론 B: 기본 정책은 "코어 자동 생성 유지", 서비스 유틸은 "override 목적"으로만 제한하는 것이 맞음

권장 원칙:
- 기본값: `socket_base` 자동 생성(현행 유지)
- 명시적 오버라이드 필요 시만 `setsockopt(ZLINK_ROUTING_ID)` 사용
- gateway/receiver는 서비스 식별 요구가 있을 때만 override

### 결론 C: `routing_id_utils.hpp` 파일 위치/이름은 정리가 필요

현재 문제:
- 위치: `services/gateway/`
- 네임스페이스: `zlink::discovery`
- include guard 이름도 discovery로 되어 있어 의미가 혼재됨

권장:
- 서비스 전용이면 `services/gateway/`에 두고 네임스페이스/가드를 `gateway` 계열로 정리
- 여러 서비스가 공통 사용하면 `core/src/services/common/` 또는 `core/src/core/`로 이동
- 단, "전소켓 기본 생성" 로직 자체는 이동 대상이 아니라 코어(socket_base)에 남겨야 함

## 4) 모니터링 관점 영향

`01-enhanced-monitoring.md`의 의도(이벤트에 routing_id 기반 식별 정보 제공)에는 현재 구조가 부합함.
- 이미 모든 소켓이 기본 routing_id를 가짐
- peer routing_id 기반 모니터링 이벤트와 결합 가능

주의사항:
- monitoring에서 보는 값이 "socket own routing_id"인지 "peer routing_id"인지 API/문서에서 명확히 구분 필요
- 현재 이벤트 구조는 peer routing_id를 담는 방향이므로, 운영 문서에서도 동일하게 명시해야 혼선을 줄일 수 있음

## 5) 리스크 체크 (기본 routing_id 전소켓 적용 시)

현행 코어 방식 유지 기준으로 큰 이슈는 없음.

다만 확인 필요 항목:
1. STREAM 특성
- STREAM은 연결별 peer routing_id 맵핑을 사용하므로, socket 자체 routing_id와 의미가 다름
- 디버깅 시 "소켓 ID"와 "피어 ID"를 혼동하지 않도록 로그 라벨 분리 필요

2. 수동 override와의 우선순위
- 사용자가 `ZLINK_ROUTING_ID`를 설정한 경우 자동값을 덮어쓰지 않아야 함
- 현재 `socket_base`는 `routing_id_size == 0`일 때만 자동 생성하므로 안전

3. 운영 가독성
- 기본값이 바이너리 포맷(최신 정책: 16B UUID)이므로 사람이 읽기 어려움
- 운영 로그에서 hex 출력 표준화가 필요

## 6) 권장 실행안

1. 코어 기본 정책은 최신 스펙 기준으로 적용
- `socket_base`의 own routing_id 자동 생성을 16B UUID로 변경
- STREAM peer/client routing_id는 4B uint32로 분리

2. `routing_id_utils.hpp` 정리(리팩터링)
- 옵션 A(최소 변경): 파일은 유지, 네임스페이스/주석/가드만 gateway 의미로 정리
- 옵션 B(공통화): `services/common/routing_id_utils.hpp`로 이동 후 gateway/receiver가 include

3. 문서 보강
- `01-enhanced-monitoring.md`에 own/peer routing_id 구분 문구를 명시
- 운영 디버깅 예시에서 hex formatter 표준화

## 7) 최종 판단

- 질문 1: "자동 생성은 `core/src/services/gateway/routing_id_utils.hpp` 함수를 써도 되는가?"
  - 답: gateway/receiver 내부 보조 용도로는 가능
  - 전소켓 기본 정책 구현용으로는 비권장 (코어 기본 로직이 이미 존재하고 더 적합)

- 질문 2: "스펙대로 소켓 생성 기본 routing_id 할당해도 이슈 없는가?"
  - 답: 구조적으로 타당하며 최신 스펙(own 16B UUID, STREAM peer 4B)으로 정리 가능
  - 동일 목적의 재할당 로직을 서비스 레이어에 중복 도입하는 것은 피하는 것이 맞음
