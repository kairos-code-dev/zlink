# C 바인딩 보안 검토 보고서

- 작성일: 2026-06-14
- 대상 범위: `bindings/c/include/zlink.h`, `bindings/c/include/zlink/common.h`
- 검토 방식: 공개 헤더와 버전 매크로를 코드 기준으로 확인했다.
- 상태: 2026-06-14 수정 필요 항목 1건 수정 완료. Codex 에이전트 리뷰 통과.

## 요약

C 바인딩은 별도 런타임 구현을 거의 두지 않고 core 공개 헤더를 다시 노출한다. 그래서 메모리 소유권, 콜백, 동시성의 대부분은 `core/include/zlink.h`와 하위 공개 헤더의 계약을 따른다.

이번 검토에서 C 바인딩 자체의 직접적인 메모리 손상 코드는 확인되지 않았다. 다만 `zlink.h`와 `zlink/common.h`의 버전 매크로 기본값이 서로 달라서, 어떤 헤더를 먼저 포함하느냐에 따라 컴파일 타임 버전 판정이 달라질 수 있다.

## 확인된 이슈

### C-BINDING-001: 버전 매크로 기본값 불일치

- 심각도: 낮음
- 상태: 2026-06-14 수정 완료
- 근거:
  - `bindings/c/include/zlink.h:8-10`은 `ZLINK_VERSION_PATCH`를 `4`로 정의한다.
  - 수정 전 `bindings/c/include/zlink/common.h:7-15`는 기존 정의가 없을 때 `ZLINK_VERSION_PATCH`를 `3`으로 정의했다.
- 영향:
  - 보안 취약점으로 바로 이어지는 항목은 아니다.
  - 사용자가 `zlink/common.h`를 직접 포함하고 버전 조건부 코드를 작성하면 실제 배포 버전보다 낮은 값으로 판단할 수 있다.
  - 기능 영향은 낮지만, 빌드 조건이나 호환성 분기에서 잘못된 경로를 선택할 수 있다.
  - 성능 영향은 없다.
- 권장 수정:
  - `bindings/c/include/zlink/common.h`의 기본 patch 값을 `4`로 맞춘다.
  - 버전 값의 단일 기준을 한 곳으로 줄일 수 있으면 더 좋다. 최소한 릴리스 갱신 시 두 헤더를 함께 확인하는 회귀 검사를 추가한다.
- 처리 결과:
  - `core/include/zlink/common.h`와 `bindings/c/include/zlink/common.h`의 `ZLINK_VERSION_PATCH` 기본값을 `4`로 맞췄다.
  - `bindings/c/tests/test_c_contract_surface.c`의 버전 기대값을 `6.0.4`로 갱신해 `zlink.h` 매크로와 `zlink_version()` 결과가 맞는지 확인한다.
  - `bindings/c/tests/test_c_common_header_version.c`를 추가해 `zlink/common.h`를 직접 포함했을 때의 버전 매크로도 `6.0.4`인지 확인한다.

## 추가 확인 사항

- `bindings/c/include/zlink.h:16-25`는 core 공개 헤더를 포함하는 얇은 진입점이다.
- 이 바인딩 자체에서 별도 동적 로더, 스레드, 버퍼 복사 로직은 확인되지 않았다.
- C API의 메시지 크기, 포인터 수명, 콜백 수명 문제는 core 공개 계약과 구현 검토에서 다뤄야 한다.

검증:

- `cmake --build core/build` 통과.
- `bindings/c/tests/run_tests.sh` 통과. C contract 7개와 sample 13개가 통과했다.
- core 공개 헤더 변경 후 `bindings/dev_sync_local_core_libs.sh`를 실행했고, 동기화된 native library를 사용해 `bindings/python/tests/run_tests.sh`가 통과했다. Python 단위 테스트 81개와 sample 14개가 통과했다.
- Codex 에이전트 리뷰에서 "추가 이슈 없음" 판정을 받았다.

## 결론

C 바인딩 자체의 기능·성능 위험은 낮다. 2026-06-14에 버전 매크로 불일치를 수정했으며, 이는 보안보다 빌드 조건 정확성 문제에 가깝다.
