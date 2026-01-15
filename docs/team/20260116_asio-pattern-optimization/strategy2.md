# Option 2: Custom Handler Allocation 전략

## 1. Handler 재사용 실패 원인 (기술적 분석)
- `poll_entry_t`에 read/write 핸들러를 멤버로 고정하면서 객체 크기가 32 bytes 증가(각 16 bytes).
- `poll_entry_t` 증가분이 캐시 라인 압박을 유발:
  - 동일 캐시 라인에 더 적은 엔트리가 들어가며, 빈번한 접근 경로에서 L1/L2 miss가 증가.
  - 특히 poll loop에서 `poll_entry_t` 배열/벡터를 순회할 때 캐시 효율이 악화.
- 컴파일러가 이미 small lambda를 stack allocation으로 최적화하고 있었음:
  - lambda 재생성 비용이 예상보다 낮아, 재사용의 이득이 크지 않음.
  - 반대로 핸들러 멤버화로 인해 object footprint가 증가해 순손실 발생.
- 결과적으로 “lambda 생성 비용 절감”보다 “캐시/메모리 접근 비용 증가”가 더 크게 작용.

## 2. BOOST_ASIO_CUSTOM_HANDLER_ALLOCATION 동작 원리
- ASIO는 handler를 내부 큐에 보관하기 위해 동적 할당을 수행할 수 있음.
- `BOOST_ASIO_CUSTOM_HANDLER_ALLOCATION`을 사용하면 handler별 커스텀 allocator를 제공 가능:
  - `asio_handler_allocate(size, handler)` / `asio_handler_deallocate(ptr, size, handler)`를
    오버로드하여, ASIO의 메모리 요청을 직접 처리.
  - small handler의 경우에도 “ASIO 내부 큐 저장”을 위한 할당이 발생하는데,
    이를 전용 pool로 대응해 malloc/free 비용 제거.
- 결과적으로 handler 재사용 없이도 “할당/해제 오버헤드”만 선택적으로 제거 가능.

## 3. Custom allocator 구현 방안
### A. Per-entry pool (권장)
- 각 `poll_entry_t`가 고정 크기 슬롯(예: 1~2개)만 보유.
- 장점:
  - 완전한 locality 확보(핫 데이터와 동일 캐시 라인/근접 영역).
  - lock-free로 구현 가능(단일 entry 전용).
- 단점:
  - pool 크기 제한이 명확해야 함(초과 시 fallback 필요).
  - entry 수만큼 메모리 예약 → 전체 footprint는 증가 가능.

### B. Global pool (대안)
- 전역 슬랩/arena를 두고 handler allocation을 전담.
- 장점:
  - 메모리 효율이 높고, burst 상황에서 유연.
  - entry당 추가 footprint를 최소화.
- 단점:
  - 동기화 비용 발생 가능(락/atomics).
  - cache locality가 약해질 수 있음.

### 선택 기준
- “핫 루프에서의 캐시 효율”이 핵심이면 per-entry pool 우선.
- “메모리 사용량과 유연성”이 우선이면 global pool 고려.
- 현실적인 절충안: per-entry 1-slot + global fallback.

## 4. 구조체 레이아웃 최적화 (Cache line alignment)
- `poll_entry_t`의 핫 필드와 콜드 필드를 분리:
  - 핫 필드: fd, 관심 플래그, 최근 이벤트 상태 등.
  - 콜드 필드: 디버그/통계/에러 상태 등 드물게 접근하는 값.
- hot/cold 분리로 캐시 라인 낭비 최소화.
- alignment 고려:
  - 핫 필드 묶음을 64-byte cache line에 맞춤.
  - handler allocator 슬롯은 별도 구조로 분리하여 hot path에서 접근 빈도를 낮춤.
- 목표: “핫 경로에 필요한 최소 데이터만 한두 캐시 라인에 배치”.

## 5. 예상 성능 개선 효과
- 핸들러 재사용 제거로 인한 footprint 증가를 피하면서,
  handler allocation 오버헤드를 직접 제거.
- 기대 효과(추정):
  - TCP 64B: -34% → -20%~-25% 수준까지 회복 가능.
  - inproc 64B: -32% → -18%~-22% 수준까지 회복 가능.
- 실제 개선 폭은 allocator 설계(슬롯 수, fallback 비율)에 따라 변동.

## 6. 구현 단계
1. **현행 할당 경로 파악**
   - ASIO handler가 실제로 malloc/free를 타는 지점 확인.
2. **커스텀 allocator 인터페이스 추가**
   - handler 타입에 `asio_handler_allocate/deallocate` 오버로드 도입.
3. **Per-entry pool 설계**
   - 고정 슬롯 1~2개를 `poll_entry_t` 또는 별도 구조에 추가.
   - 초과 시 global fallback을 설계(옵션).
4. **구조체 레이아웃 정리**
   - hot/cold 분리 및 64-byte alignment 검토.
5. **벤치마크 측정**
   - 기존 결과 대비 TCP/inproc 64B 비교.
   - allocator fallback 비율과 latency 상관관계 확인.
6. **튜닝/고도화**
   - 슬롯 크기, 개수, fallback 정책 조정.
   - 필요 시 global pool 락 최적화 또는 lock-free 구조 검토.
