# Rebuild Feature Parity Matrix

## 1. 목적

- 이 문서는 `/home/hep7/project/kairos/zlink/core`의 기능 스펙을
  현재 워크스페이스 `core/`에 정말 모두 이식했는지 증명하는 추적표다.
- `단계를 다 진행했다`는 사실만으로는 완료를 주장할 수 없다.
- 아래 세 축이 모두 닫혀야 완료로 본다.
  - upstream `zlink.h`와 현재 `core/include/zlink.h` 완전 일치
  - upstream `core/tests` 의미가 현재 `core/tests`에 대응됨
  - 성능 기준선이 유지됨

## 2. 완료 기준

- 최종 `core/include/zlink.h`는
  `/home/hep7/project/kairos/zlink/core/include/zlink.h`
  와 동일해야 한다.
- 최종 `core/tests`는
  `/home/hep7/project/kairos/zlink/core/tests`
  의 테스트 의미를 모두 구현하고 통과해야 한다.
- 아래 표에서 `상태`가 하나라도 `미구현`, `부분`, `보류`, `미매핑`이면
  기능 완료가 아니다.

## 2.1 최종 검증 명령

```bash
diff -u /home/hep7/project/kairos/zlink/core/include/zlink.h core/include/zlink.h
./core/tests/run_test_lanes.sh --include-e2e --include-regression
./core/tests/run_thread_safe_contract_stress.sh --count 10 --build-dir core/build
./core/tests/run_thread_safe_contract_perf.sh --min-ratio 0.85 --build-dir core/build
```

- `diff` 출력이 비어 있어야 header parity가 닫힌다.
- test lane 중 하나라도 실패하면 기능 parity가 닫히지 않은 것이다.

## 2.2 상태 값

- `미구현`: 아직 시작 안 함
- `부분`: 일부 구현/일부 테스트만 있음
- `보류`: 설계나 성능 이유로 의도적으로 멈춤
- `미매핑`: upstream 기준과 현재 구현/테스트 연결이 없음
- `진행중`: 현재 단계에서 작업 중
- `완료`: 구현, 테스트, matrix 연결까지 닫힘

## 3. Header/API Parity

| 분류 | upstream 기준 | 현재 구현 | 대응 테스트 | 성능 민감 | 상태 | 메모 |
|---|---|---|---|---|---|---|
| Context / errno / version | `/home/hep7/project/kairos/zlink/core/include/zlink.h` | | | | 미구현 | |
| Socket create/bind/connect/close | `/home/hep7/project/kairos/zlink/core/include/zlink.h` | | | | 미구현 | |
| Message API / multipart API | `/home/hep7/project/kairos/zlink/core/include/zlink.h` | | | high | 미구현 | |
| Poll / monitor / events | `/home/hep7/project/kairos/zlink/core/include/zlink.h` | | | medium | 미구현 | |
| Discovery / registry / spot | `/home/hep7/project/kairos/zlink/core/include/zlink.h` | | | medium | 미구현 | |
| TLS / transport / socket options | `/home/hep7/project/kairos/zlink/core/include/zlink.h` | | | high | 미구현 | |
| Thread-safe / callback / lifecycle | `/home/hep7/project/kairos/zlink/core/include/zlink.h` | | | high | 미구현 | |

## 4. Test Parity

| 분류 | upstream 기준 | 현재 대응 테스트 | 구현 위치 | 성능 민감 | 상태 | 메모 |
|---|---|---|---|---|---|---|
| unittest lane | `/home/hep7/project/kairos/zlink/core/tests/unittest` | | | | 미구현 | |
| integration lane | `/home/hep7/project/kairos/zlink/core/tests/integration` | | | | 미구현 | |
| e2e lane | `/home/hep7/project/kairos/zlink/core/tests/e2e` | | | | 미구현 | |
| regression lane | `/home/hep7/project/kairos/zlink/core/tests` | | | | 미구현 | |
| thread-safe stress/perf | `/home/hep7/project/kairos/zlink/core/tests` | | | high | 미구현 | |

## 5. 단계별 사용 규칙

- 각 단계는 구현과 동시에 이 matrix를 갱신한다.
- stage 10에서 한꺼번에 정리하지 않는다.
- public symbol을 추가하거나 바꾸면 header parity 행을 먼저 갱신한다.
- upstream 테스트 의미를 가져오면 test parity 행을 같이 갱신한다.
- 성능 민감 항목은 hot-path 문서와 함께 본다.
