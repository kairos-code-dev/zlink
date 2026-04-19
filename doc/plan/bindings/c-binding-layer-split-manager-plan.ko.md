# C Binding 계층 분리 + 전체 바인딩 `*_part` 재배선 감독 계획

> 성격: 이 문서는 구현 로그가 아니라, 현재 main Claude가 **감독(manager)** 으로서
> 언어별 하위 Codex 에이전트들에게 작업을 분배하고, 응답을 직접 리뷰한 뒤,
> `doc/spec/draft/c-binding-layer-plan.ko.md` 와
> `doc/spec/draft/bindings-helper-capi-partwise-send-recv.ko.md` 의 구현 방향이
> 실제 코드에 전부 반영될 때까지 반복해서 재지시하는
> **감독용 실행 기준 문서**다.
>
> source of truth:
> - `doc/spec/draft/c-binding-layer-plan.ko.md`
> - `doc/spec/draft/bindings-helper-capi-partwise-send-recv.ko.md`
> - `core/include/zlink.h` (현재 반영 상태)
>
> 이번 라운드의 핵심은 `core/include/zlink.h` 에 섞여 있던
> **C binding convenience layer (aggregate)** 와
> **Helper substrate layer (`*_part`)** 두 계층을 실제로 분리하고,
> 각 언어 바인딩이 `*_part` substrate 위에서 현재 public 인터페이스를
> 유지하도록 다시 구현하는 것이다.
>
> 감독과 하위 에이전트는 draft spec 의 설계 의도를 절대 기준으로 삼고,
> hot path 성능(특히 Java, .NET 벤치)에서 regression 이 없는지까지 확인한다.
>
> 대상 범위:
> - `core/include/zlink.h` (aggregate 제거, `*_part` 유지)
> - `core/src/api/` (aggregate wrapper 구현 정리)
> - `core/samples/` → `bindings/c/samples/` 로 이동
> - `core/perf/` → `bindings/c/perf/` 로 이동 (aggregate API 벤치)
> - `bindings/c` (신규 생성, aggregate convenience 층 + samples + perf)
> - `bindings/cpp`
> - `bindings/dotnet`
> - `bindings/go`
> - `bindings/java`
> - `bindings/node`
> - `bindings/python`
> - `bindings/rust`

## 0. 최우선 운영 규칙

- main Claude 는 감독 역할만 수행한다.
- **코드 작업(구현 변경, 파일 이동, 빌드 배선)은 반드시 Codex 에이전트에게 위임한다.**
- **문서 작업(doc/guide 업데이트, 계획 문서 갱신, spec 문서 수정)은 Claude 에이전트에게 위임한다.**
- 감독은 두 종류의 에이전트 결과를 모두 리뷰한 뒤 상태표를 갱신한다.
- 하위 에이전트가 "완료" 라고 보고해도 감독은 직접 파일을 읽어 다음을 확인한다.
  - `core/include/zlink.h` 에 남은 aggregate 선언 여부
  - `bindings/c/` 신규 파일의 시그니처와 `*_part` 루프 정합성
  - 각 바인딩이 실제로 `*_part` 심볼만 참조하는지 (aggregate 심볼 누수 없음)
  - 각 바인딩 자체 복사본 `zlink.h` 와 `core/include/zlink.h` 정합성
  - 테스트/벤치 실행 결과
- 구현 변경은 반드시 draft spec 의 **§5.1.1 (open multipart 시퀀스 규칙)**,
  **§5.4.1 (request/reply 실패 규칙)**, **§6.3.1 (routed metadata lifetime)**,
  **§7.1/§7.2 (ownership)** 을 따라야 한다.
- 성능 회귀가 확인되면 정책 준수만으로 완료 처리하지 않는다.
- 모든 대상은 **2단계 순서** 로 처리한다.
  1. Core/C 계층 분리 (Phase A)
  2. 각 언어 바인딩 `*_part` 재배선 (Phase B)
- Phase A 가 완료되기 전에는 어떤 바인딩 Phase B 도 시작하지 않는다.

## 1. 이번 라운드의 목표

1. `core/include/zlink.h` 에서 `/* ========== C binding convenience layer
   (aggregate) ========== */` 로 표시된 모든 함수 선언을 제거한다.
2. 제거된 aggregate 함수들은 신규 `bindings/c/` 디렉토리 내 헤더와
   구현 파일로 이동한다. 각 함수는 `*_part` substrate 위에서
   `ZLINK_PART_MORE`/`ZLINK_PART_FINAL` 루프로 재구성한다.
3. `core/src/api/` 내 aggregate wrapper 구현은 삭제하거나
   `bindings/c/src/` 로 이동한다. `part_helper_api.cpp` 는 core 에 남긴다.
4. 각 언어 바인딩은 더 이상 aggregate 심볼을 import/link 하지 않고,
   `*_part` substrate 심볼만 호출하도록 재배선한다.
5. 각 언어 바인딩 디렉토리에 있는 자체 `zlink.h` 복사본도 core 와 동기화한다.
   - 복사본이 있는 언어: `cpp`, `go`, `rust`
   - ctypes/직접 로딩 언어: `python`, `node` 는 심볼 바인딩 목록만 동기화
6. 각 바인딩의 기존 public API 형태는 **유지** 한다. 내부 substrate 만 바뀐다.
7. 모든 대상에 대해 기본 검증(tests, sample, perf 기준선)을 통과시킨다.

## 2. 감독 진행표

상태 규칙:
- `pending`: 아직 시작 전
- `in_progress`: 현재 작업 요청/리뷰/재지시 중
- `rework`: 감독 리뷰 결과 추가 수정 지시가 내려간 상태
- `blocked`: 선행 단계 미완료로 진행 불가
- `failed`: 검증 근거 확인 실패
- `done`: 감독 리뷰상 남은 작업 없음

### Phase A — Core/C 계층 분리

| 단계 | 내용 | 상태 |
|------|------|------|
| A-0 | perf baseline 측정 (이동 전, Java/C++ 기준선 저장) | pending |
| A-1 | `bindings/c/` 디렉토리, 헤더, 구현 파일 생성 | pending |
| A-2 | `core/samples/` → `bindings/c/samples/` 이동 및 재배선 | pending |
| A-3 | `core/perf/` → `bindings/c/perf/` 이동 및 재배선 | pending |
| A-4 | `core/include/zlink.h` 에서 aggregate 블록 제거 | pending |
| A-5 | `core/src/api/` aggregate wrapper 구현 이동/삭제 | pending |
| A-6 | core/CMakeLists.txt + `bindings/c` 빌드 배선 | pending |
| A-7 | `bindings/update_zlink_libs.sh` 갱신 + 각 바인딩 native 라이브러리 디렉토리에 `libzlink_c` 배포 | pending |
| A-8 | Phase A 감독 리뷰 | pending |

### Phase B — 언어별 바인딩 재배선

| 대상 | Phase B 작업 | B 감독 리뷰 | Gate 1 spec | Gate 2 perf | Gate 3 sample | 검증 확인 | 최종 상태 |
|------|--------------|-------------|-------------|-------------|---------------|-----------|-----------|
| `bindings/cpp`    | pending | pending | pending | pending | pending | pending | pending |
| `bindings/dotnet` | pending | pending | pending | pending | pending | pending | pending |
| `bindings/go`     | pending | pending | pending | pending | pending | pending | pending |
| `bindings/java`   | pending | pending | pending | pending | pending | pending | pending |
| `bindings/node`   | pending | pending | pending | pending | pending | pending | pending |
| `bindings/python` | pending | pending | pending | pending | pending | pending | pending |
| `bindings/rust`   | pending | pending | pending | pending | pending | pending | pending |

## 3. 작업 순서

### Phase A 고정 순서

> **순서 근거**: samples/perf 이동(A-2/A-3)을 aggregate 제거(A-4) 이전에 먼저 수행해야
> 컴파일 파괴 없이 단계별 진행이 가능하다. perf baseline(A-0)은 이동 전에 측정해야
> 비교 기준이 유효하다.

1. A-0: perf baseline 측정 — Java perf, C++ perf 현재 수치 파일로 저장
2. A-1: `bindings/c/` 신규 생성 (헤더, 구현, CMakeLists)
3. A-2: `core/samples/` → `bindings/c/samples/` 이동 및 `zlink_c.h` 재배선
4. A-3: `core/perf/` → `bindings/c/perf/` 이동 및 `zlink_c.h` 재배선
5. A-4: `core/include/zlink.h` aggregate 블록 제거
6. A-5: `core/src/api/` aggregate 구현 이동/삭제
7. A-6: `core/CMakeLists.txt` + `bindings/c` 빌드 배선
8. A-7: `bindings/update_zlink_libs.sh` 갱신 + native 라이브러리 배포
9. A-8: 감독 리뷰 + 전체 빌드/테스트 기준선 확인

### Phase B 고정 순서 (Phase A 완료 후)

1. `bindings/cpp`
2. `bindings/dotnet`
3. `bindings/go`
4. `bindings/java`
5. `bindings/node`
6. `bindings/python`
7. `bindings/rust`

각 대상은 아래 운영 루프를 따른다.

1. 대상 언어 Codex 에이전트에게 Phase B 작업 지시.
2. 에이전트가 aggregate 심볼 사용 지점을 전부 찾고 `*_part` 로 치환.
3. 자체 `zlink.h` 복사본이 있으면 core 와 동기화.
4. 에이전트가 기본 검증 + hot path 스모크 결과와 함께 응답.
5. 감독이 직접 파일 단위 리뷰. aggregate 심볼 잔존 여부, ownership,
   open multipart 시퀀스 규칙 준수, metadata lifetime 준수 확인.
6. 잔존 이슈가 있으면 재지시 (`rework`).
7. **[Gate 1 — spec 준수 리뷰]** Claude 에이전트가 `doc/spec/bindings/<lang>/` 스펙
   전체를 현재 구현과 대조한다. 미적용/오적용 항목 목록을 작성하고 Codex 에이전트에게
   수정 지시. 잔존 항목이 0건이 될 때까지 리뷰-수정 반복.
8. **[Gate 2 — perf 정책 준수 리뷰 + smoke 검증]** Claude 에이전트가
   `doc/perf/PERF_POLICY.md`, `PERF_SINGLE_TEST_POLICY.md`,
   `PERF_MULTI_TEST_POLICY.md` 기준으로 `bindings/<lang>/perf/` 파일 전체를
   리뷰한다. 미적용/오적용 항목 목록을 작성하고 Codex 에이전트에게 수정 지시.
   잔존 항목이 0건이 될 때까지 리뷰-수정 반복.
   이후 `PERF_POLICY.md §3.2` 기준으로 single + multi smoke 테스트
   (`--pattern ALL --msg-sizes 64`)를 실행하여 전 조합 `fail` 0건을 확인한다.
   fail 이 있으면 rework 처리 후 재실행.
9. **[Gate 3 — sample 정책 준수 리뷰]** Claude 에이전트가 `doc/spec/sample/SAMPLE_POLICY.md`
   기준으로 `bindings/<lang>/samples/` 파일 전체를 리뷰한다. 미적용/오적용 항목 목록을
   작성하고 Codex 에이전트에게 수정 지시. 잔존 항목이 0건이 될 때까지 리뷰-수정 반복.
10. Gate 1–3 모두 통과 + 잔존 이슈 0건 + 검증 통과 시 해당 언어 완료 (`done`).

## 4. 작업 유형별 에이전트 분담

### 4.1 에이전트 분담 원칙

| 작업 유형 | 담당 에이전트 | 예시 |
|-----------|--------------|------|
| 코드 구현, 파일 이동, 빌드 배선, 테스트 수정 | **Codex 에이전트** | `bindings/c/` 생성, `.cpp`/`.cs`/`.go`/`.rs`/`.py`/`.java` 수정, CMakeLists 갱신 |
| 문서 작성/수정 (doc/guide, doc/spec, 계획 문서) | **Claude 에이전트** | `doc/guide/` C API 가이드 업데이트, spec 문서 승격, 이 계획 문서 갱신 |

### 4.2 언어별 코드 담당 (Codex 에이전트)

| 대상 | 담당 에이전트 | 주 검토 범위 | 기본 검증 진입점 |
|------|----------------|--------------|------------------|
| `core` + `bindings/c` | `codex-core-c-split-agent` | aggregate 이동, `*_part` 루프, 빌드 배선, samples/perf 이동 | core unit/integration tests, `bindings/c` 샘플 빌드 |
| `bindings/cpp`    | `codex-cpp-binding-agent`    | `base_socket.hpp`, `socket_types.hpp`, `spot.hpp`, 자체 `zlink.h` 복사본 | `bindings/cpp/tests/run_tests.sh` |
| `bindings/dotnet` | `codex-dotnet-binding-agent` | `NativeMethods.*`, `SocketKernel.cs`, Service layer | `bindings/dotnet/tests/run_tests.sh` |
| `bindings/go`     | `codex-go-binding-agent`     | `ffi.go`, `request_reply.go`, `spot.go`, 자체 `zlink.h` 복사본 | `go test ./...` |
| `bindings/java`   | `codex-java-binding-agent`   | `Native.java` 잔존 aggregate MethodHandle 제거, Socket.java 오류 문자열 | `bindings/java/tests/run_tests.sh` + perf 스모크 |
| `bindings/node`   | `codex-node-binding-agent`   | `addon_spot.cc`, addon 심볼 바인딩 | `bindings/node/tests/run_tests.sh` |
| `bindings/python` | `codex-python-binding-agent` | `_ffi.py` 심볼 목록, `_socket_base.py`, `_socket_types.py`, `_spot.py`, `_core.py` | `bindings/python/tests/run_tests.sh` |
| `bindings/rust`   | `codex-rust-binding-agent`   | `ffi.rs` extern 선언, `socket/mod.rs`, `socket/router.rs`, `socket/dealer.rs`, `service.rs`, `send_handle.rs`, 자체 `zlink.h` 복사본 | `cargo test` |

### 4.3 문서 담당 (Claude 에이전트)

| 대상 | 담당 에이전트 | 주 작업 범위 |
|------|----------------|--------------|
| `doc/guide/` C API 가이드 | `claude-doc-agent` | `bindings/c/` 기준 aggregate API 예제로 재작성 |
| `doc/spec/bindings/c/README.md` | `claude-doc-agent` | helper substrate 추가 및 계층 분리 반영 |
| 이 계획 문서 상태표 갱신 | main Claude (감독) | 각 단계 완료/rework 시 직접 갱신 |

## 5. 감독과 하위 에이전트의 역할 분리

### 5.1 main Claude 감독 역할

- Phase A, Phase B 각 단계별 작업 요청을 생성한다.
- 하위 에이전트가 제출한 변경 목록이 draft spec 을 실제로 충족하는지 본다.
- 각 바인딩에서 aggregate 심볼이 빌드 산출물/링크 과정에서 완전히 사라졌는지
  (grep + build log) 확인한다.
- hot path 에 불필요한 allocation/indirection 추가 여부를 확인한다.
- 누락/과잉/잔재가 보이면 재지시를 내린다.
- 상태표를 직접 갱신한다.

### 5.2 하위 에이전트 역할

- 지시받은 범위를 draft spec 기준으로 수정한다.
- 변경 전후의 aggregate 심볼 grep 결과를 보고한다.
- 기본 검증 진입점을 실행하고 결과를 첨부한다.
- hot path 성능에 영향 가능한 변경이면 간단한 대비 결과를 첨부한다.
- draft spec 해석이 애매하면 감독에게 확인을 요청한다. 임의 해석 금지.

## 6. 파일별 체크리스트 (감독 리뷰 기준)

### 6.1 `core/include/zlink.h`
- [ ] `/* ========== C binding convenience layer (aggregate) ========== */`
      블록 전부 제거
- [ ] `/* ========== Helper substrate layer (*_part) ========== */` 블록 유지
- [ ] `zlink_send`, `zlink_send_rid`, `zlink_recv`, `zlink_router_recv`,
      `zlink_publish`, `zlink_subscribe`, `zlink_subscription_event`,
      `zlink_spot_send_*`, `zlink_spot_request_*`, `zlink_spot_reply_*`,
      `zlink_spot_subscribe`, `zlink_spot_subscription_event`,
      `zlink_dealer_request`, `zlink_router_request`, `zlink_router_reply`,
      `zlink_router_request_spot`, `zlink_router_reply_spot`,
      `zlink_router_send_spot` 선언 전부 제거
- [ ] 비-multipart public 함수 (context, socket lifecycle, 옵션, monitor,
      timer, poller, registry, discovery, atomic counter 등)는 그대로 유지
- [ ] `zlink_java_*` bridge 심볼은 그대로 유지

### 6.2 `bindings/c/`
- [ ] `bindings/c/include/zlink_c.h` 생성 — aggregate 선언 모음
- [ ] `bindings/c/src/zlink_c.c` 생성 — `*_part` 위 구현
- [ ] `bindings/c/CMakeLists.txt` 생성 — `zlink_c` 라이브러리 빌드
- [ ] 각 aggregate 구현은 §5.1.1 open 시퀀스 규칙 준수
      (루프 마지막에 `ZLINK_PART_FINAL`, 중간은 `ZLINK_PART_MORE`)
- [ ] recv 계열은 §6.3.1 metadata lifetime 을 지키는 aggregate 복원
- [ ] `bindings/c/samples/` — `core/samples/` 에서 이동, `zlink_c.h` include 로 재배선
- [ ] `bindings/c/perf/` — `core/perf/` 에서 이동, `zlink_c.h` include 로 재배선
- [ ] `core/samples/`, `core/perf/` 원본 제거 (CMakeLists.txt 포함)
- [ ] `doc/guide/` C API 관련 가이드가 있다면 `bindings/c/` 기준 aggregate API 로 업데이트

### 6.3 `core/src/api/`
- [ ] aggregate wrapper 구현 파일 제거 또는 `bindings/c/src/` 이동
- [ ] `part_helper_api.cpp` 유지
- [ ] context/socket/monitor/timer/poller/service api 구현 유지

### 6.4 `bindings/cpp`
- [ ] `bindings/cpp/include/zlink.h` → core 와 동일하게 `*_part` 만 남김
- [ ] `base_socket.hpp`, `socket_types.hpp`, `spot.hpp` 의 모든
      `zlink_send*`, `zlink_recv*`, `zlink_publish`, `zlink_subscribe`,
      `zlink_router_recv`, `zlink_*_request`, `zlink_*_reply`,
      `zlink_spot_*` aggregate 호출을 `*_part` 루프로 치환
- [ ] grep 결과 aggregate 심볼 잔존 0건

#### Gate 1 — spec 준수 리뷰 (`bindings/cpp`)
- [ ] Claude 에이전트가 `doc/spec/bindings/cpp/` 스펙 전체를 현재 구현과 대조
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인 후 Gate 2 진행

#### Gate 2 — perf 정책 준수 리뷰 + smoke 검증 (`bindings/cpp`)
- [ ] Claude 에이전트가 `doc/perf/PERF_POLICY.md`, `PERF_SINGLE_TEST_POLICY.md`,
      `PERF_MULTI_TEST_POLICY.md` 기준으로 `bindings/cpp/perf/` 리뷰
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인
- [ ] single smoke: `bindings/cpp/perf/run_benchmarks.sh --pattern ALL --msg-sizes 64`
      — 전 조합 `fail` 0건 확인
- [ ] multi smoke: `bindings/cpp/perf/run_benchmarks_multi.sh --pattern ALL --msg-sizes 64`
      — 전 조합 `fail` 0건 확인
- [ ] fail 있으면 rework → 재실행 반복 → 0건 후 Gate 3 진행

#### Gate 3 — sample 정책 준수 리뷰 (`bindings/cpp`)
- [ ] Claude 에이전트가 `doc/spec/sample/SAMPLE_POLICY.md` 기준으로
      `bindings/cpp/samples/` 리뷰
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인 후 완료 처리

### 6.5 `bindings/dotnet`
- [ ] `NativeMethods.Socket.cs`, `NativeMethods.Service.cs`,
      `NativeMethods.Core.cs` 의 DllImport 엔트리에서 aggregate 제거,
      `*_part` 엔트리 추가
- [ ] `SocketKernel.cs` send/recv 경로를 part-by-part 호출로 재작성
- [ ] service layer (publish/subscribe/spot) 도 동일하게 재배선

#### Gate 1 — spec 준수 리뷰 (`bindings/dotnet`)
- [ ] Claude 에이전트가 `doc/spec/bindings/dotnet/` 스펙 전체를 현재 구현과 대조
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인 후 Gate 2 진행

#### Gate 2 — perf 정책 준수 리뷰 + smoke 검증 (`bindings/dotnet`)
- [ ] Claude 에이전트가 `doc/perf/PERF_POLICY.md`, `PERF_SINGLE_TEST_POLICY.md`,
      `PERF_MULTI_TEST_POLICY.md` 기준으로 `bindings/dotnet/perf/` 리뷰
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인
- [ ] single smoke: `bindings/dotnet/perf/run_benchmarks.sh --pattern ALL --msg-sizes 64`
      — 전 조합 `fail` 0건 확인
- [ ] multi smoke: `bindings/dotnet/perf/run_benchmarks_multi.sh --pattern ALL --msg-sizes 64`
      — 전 조합 `fail` 0건 확인
- [ ] fail 있으면 rework → 재실행 반복 → 0건 후 Gate 3 진행

#### Gate 3 — sample 정책 준수 리뷰 (`bindings/dotnet`)
- [ ] Claude 에이전트가 `doc/spec/sample/SAMPLE_POLICY.md` 기준으로
      `bindings/dotnet/samples/` 리뷰
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인 후 완료 처리

### 6.6 `bindings/go`
- [ ] `bindings/go/include/zlink.h` 동기화
- [ ] `ffi.go` 의 inline C trampoline 에서 aggregate 호출을 `*_part` 시퀀스로 변경
- [ ] `request_reply.go`, `spot.go` 의 `C.zlink_*` 호출 치환
- [ ] cgo 헤더와 심볼 불일치 없음 확인

#### Gate 1 — spec 준수 리뷰 (`bindings/go`)
- [ ] Claude 에이전트가 `doc/spec/bindings/go/` 스펙 전체를 현재 구현과 대조
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인 후 Gate 2 진행

#### Gate 2 — perf 정책 준수 리뷰 + smoke 검증 (`bindings/go`)
- [ ] Claude 에이전트가 `doc/perf/PERF_POLICY.md`, `PERF_SINGLE_TEST_POLICY.md`,
      `PERF_MULTI_TEST_POLICY.md` 기준으로 `bindings/go/perf/` 리뷰
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인
- [ ] single smoke: `bindings/go/perf/run_benchmarks.sh --pattern ALL --msg-sizes 64`
      — 전 조합 `fail` 0건 확인
- [ ] multi smoke: `bindings/go/perf/run_benchmarks_multi.sh --pattern ALL --msg-sizes 64`
      — 전 조합 `fail` 0건 확인
- [ ] fail 있으면 rework → 재실행 반복 → 0건 후 Gate 3 진행

#### Gate 3 — sample 정책 준수 리뷰 (`bindings/go`)
- [ ] Claude 에이전트가 `doc/spec/sample/SAMPLE_POLICY.md` 기준으로
      `bindings/go/samples/` 리뷰
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인 후 완료 처리

### 6.7 `bindings/java`
- [ ] `Native.java` 에서 `MH_SEND`, `MH_SEND_RID`, `MH_RECV`, `MH_PUBLISH`,
      `MH_SUBSCRIBE`, `MH_ROUTER_RECV` 및 대응하는 invoke 메소드 제거
- [ ] `Socket.java` 의 오류 문자열 중 aggregate 이름을 `*_part` 이름으로 수정
- [ ] `zlink_java_*` bridge 함수 유지 (dealer/router request sync)
- [ ] Perf/Bench (`bindings/java/perf/*`) 재측정 — regression 없음

#### Gate 1 — spec 준수 리뷰 (`bindings/java`)
- [ ] Claude 에이전트가 `doc/spec/bindings/java/` 스펙 전체를 현재 구현과 대조
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인 후 Gate 2 진행

#### Gate 2 — perf 정책 준수 리뷰 + smoke 검증 (`bindings/java`)
- [ ] Claude 에이전트가 `doc/perf/PERF_POLICY.md`, `PERF_SINGLE_TEST_POLICY.md`,
      `PERF_MULTI_TEST_POLICY.md` 기준으로 `bindings/java/perf/` 리뷰
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인
- [ ] single smoke: `bindings/java/perf/run_benchmarks.sh --pattern ALL --msg-sizes 64`
      — 전 조합 `fail` 0건 확인
- [ ] multi smoke: `bindings/java/perf/run_benchmarks_multi.sh --pattern ALL --msg-sizes 64`
      — 전 조합 `fail` 0건 확인
- [ ] fail 있으면 rework → 재실행 반복 → 0건 후 Gate 3 진행

#### Gate 3 — sample 정책 준수 리뷰 (`bindings/java`)
- [ ] Claude 에이전트가 `doc/spec/sample/SAMPLE_POLICY.md` 기준으로
      `bindings/java/samples/` 리뷰
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인 후 완료 처리

### 6.8 `bindings/node`
- [ ] `addon_spot.cc` 및 관련 native addon 에서 aggregate 호출 치환
- [ ] 노출된 JS/TS API 는 그대로 유지
- [ ] native 빌드 산출물에서 aggregate 심볼 참조 없음 확인

#### Gate 1 — spec 준수 리뷰 (`bindings/node`)
- [ ] Claude 에이전트가 `doc/spec/bindings/node/` 스펙 전체를 현재 구현과 대조
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인 후 Gate 2 진행

#### Gate 2 — perf 정책 준수 리뷰 + smoke 검증 (`bindings/node`)
- [ ] Claude 에이전트가 `doc/perf/PERF_POLICY.md`, `PERF_SINGLE_TEST_POLICY.md`,
      `PERF_MULTI_TEST_POLICY.md` 기준으로 `bindings/node/perf/` 리뷰
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인
- [ ] single smoke: `bindings/node/perf/run_benchmarks.sh --pattern ALL --msg-sizes 64`
      — 전 조합 `fail` 0건 확인
- [ ] multi smoke: `bindings/node/perf/run_benchmarks_multi.sh --pattern ALL --msg-sizes 64`
      — 전 조합 `fail` 0건 확인
- [ ] fail 있으면 rework → 재실행 반복 → 0건 후 Gate 3 진행

#### Gate 3 — sample 정책 준수 리뷰 (`bindings/node`)
- [ ] Claude 에이전트가 `doc/spec/sample/SAMPLE_POLICY.md` 기준으로
      `bindings/node/samples/` 리뷰
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인 후 완료 처리

### 6.9 `bindings/python`
- [ ] `_ffi.py` 의 심볼 목록에서 aggregate 제거, `*_part` 추가
- [ ] `_socket_base.py` 의 `zlink_send`, `zlink_send_rid`, `zlink_publish`,
      `zlink_subscribe`, `zlink_recv` 호출 치환
- [ ] `_socket_types.py` 의 `zlink_dealer_request`, `zlink_router_request`,
      `zlink_router_reply`, `zlink_router_recv` 치환
- [ ] `_spot.py` 의 `zlink_spot_*` 치환
- [ ] `_core.py` 의 recv 경로 치환

#### Gate 1 — spec 준수 리뷰 (`bindings/python`)
- [ ] Claude 에이전트가 `doc/spec/bindings/python/` 스펙 전체를 현재 구현과 대조
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인 후 Gate 2 진행

#### Gate 2 — perf 정책 준수 리뷰 + smoke 검증 (`bindings/python`)
- [ ] Claude 에이전트가 `doc/perf/PERF_POLICY.md`, `PERF_SINGLE_TEST_POLICY.md`,
      `PERF_MULTI_TEST_POLICY.md` 기준으로 `bindings/python/perf/` 리뷰
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인
- [ ] single smoke: `bindings/python/perf/run_benchmarks.sh --pattern ALL --msg-sizes 64`
      — 전 조합 `fail` 0건 확인
- [ ] multi smoke: `bindings/python/perf/run_benchmarks_multi.sh --pattern ALL --msg-sizes 64`
      — 전 조합 `fail` 0건 확인
- [ ] fail 있으면 rework → 재실행 반복 → 0건 후 Gate 3 진행

#### Gate 3 — sample 정책 준수 리뷰 (`bindings/python`)
- [ ] Claude 에이전트가 `doc/spec/sample/SAMPLE_POLICY.md` 기준으로
      `bindings/python/samples/` 리뷰
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인 후 완료 처리

### 6.10 `bindings/rust`
- [ ] `bindings/rust/include/zlink.h` 동기화
- [ ] `src/ffi.rs` extern 선언에서 aggregate 제거, `*_part` 추가
- [ ] `src/socket/mod.rs` 의 `zlink_send`/`zlink_recv`/`zlink_publish`/
      `zlink_subscribe`/`zlink_send_rid` 호출 치환
- [ ] `src/socket/router.rs`, `src/socket/dealer.rs`, `src/socket/send_handle.rs`
      치환
- [ ] `src/service.rs`, `src/domain.rs` 의 `zlink_spot_*`, `zlink_router_reply`
      치환
- [ ] `cargo test`, `cargo clippy` 통과

#### Gate 1 — spec 준수 리뷰 (`bindings/rust`)
- [ ] Claude 에이전트가 `doc/spec/bindings/rust/` 스펙 전체를 현재 구현과 대조
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인 후 Gate 2 진행

#### Gate 2 — perf 정책 준수 리뷰 + smoke 검증 (`bindings/rust`)
- [ ] Claude 에이전트가 `doc/perf/PERF_POLICY.md`, `PERF_SINGLE_TEST_POLICY.md`,
      `PERF_MULTI_TEST_POLICY.md` 기준으로 `bindings/rust/perf/` 리뷰
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인
- [ ] single smoke: `bindings/rust/perf/run_benchmarks.sh --pattern ALL --msg-sizes 64`
      — 전 조합 `fail` 0건 확인
- [ ] multi smoke: `bindings/rust/perf/run_benchmarks_multi.sh --pattern ALL --msg-sizes 64`
      — 전 조합 `fail` 0건 확인
- [ ] fail 있으면 rework → 재실행 반복 → 0건 후 Gate 3 진행

#### Gate 3 — sample 정책 준수 리뷰 (`bindings/rust`)
- [ ] Claude 에이전트가 `doc/spec/sample/SAMPLE_POLICY.md` 기준으로
      `bindings/rust/samples/` 리뷰
- [ ] 미적용/오적용 항목 Codex 에이전트에게 수정 지시
- [ ] 리뷰-수정 반복 → 잔존 항목 **0건** 확인 후 완료 처리

## 7. 공통 검증 기준

- 각 언어별 기본 검증 진입점 통과.
- `nm`/`dumpbin`/`go tool nm`/`objdump` 로 aggregate 심볼 import 0건 확인
  가능한 언어는 확인까지 수행.
- 기존 샘플(app-level demo) 이 그대로 동작.
- perf/bench 회귀 없음. Java, .NET 은 A-0 에서 저장한 baseline 수치와 직접 비교.

### 7.1 툴체인 미설치 시 처리 방침

각 언어 검증 단계에서 해당 툴체인(Go, Rust, Java/Gradle, Node/npm, .NET SDK, Python)이
환경에 없으면 아래 규칙을 따른다.

- 에이전트는 `[SKIP - toolchain not available: <tool>]` 으로 보고하고 다음 언어로 넘어간다.
- 감독은 해당 언어 상태를 `done` 이 아닌 `blocked` 로 표시한다.
- `blocked` 상태 언어는 툴체인 설치 후 별도 재검증 라운드에서 처리한다.
- 툴체인 설치 자체는 사람이 개입해야 하는 항목이므로 에이전트가 임의로 설치하지 않는다.

### 7.2 native 라이브러리 배포 확인

A-7 완료 후 각 바인딩의 native 라이브러리 디렉토리에 `libzlink_c.so` (또는 플랫폼별 확장자)가
존재하는지 확인한다. 없으면 런타임에 심볼을 찾지 못해 검증이 실패하므로 A-7 을 rework 처리한다.

## 8. 완료 정의

- Phase A-0 ~ A-8 모두 `done`.
- Phase B 모든 언어가 `done` 또는 `blocked` (툴체인 미설치 사유 명시 시).
- 각 언어 Phase B 에서 Gate 1 (spec 준수), Gate 2 (perf 정책 준수),
  Gate 3 (sample 정책 준수) 모두 잔존 항목 **0건** 확인.
- `core/samples/`, `core/perf/` 디렉토리가 제거되거나 비어 있음.
- `bindings/c/samples/`, `bindings/c/perf/` 가 정상 빌드/실행됨.
- `doc/guide/` 에 C API 예제가 있다면 `zlink_c.h` 기준 aggregate API 만 사용.
- `grep -rn "zlink_send\b\|zlink_recv\b\|zlink_send_rid\b\|zlink_publish\b\|
  zlink_subscribe\b\|zlink_router_recv\b\|zlink_spot_send\|zlink_spot_request\|
  zlink_spot_reply\b\|zlink_dealer_request\b\|zlink_router_request\b\|
  zlink_router_reply\b"` 결과 — `bindings/c/` 내부와 문서를 제외하고 0건.
- 모든 기본 검증 진입점 통과.
- hot path 성능 regression 보고 없음.

## 9. 이 문서의 갱신 규칙

- 감독은 각 단계 완료/rework/blocked 시마다 §2 상태표를 즉시 갱신한다.
- 단계별 상세 작업 로그는 이 문서에 남기지 않고, 에이전트 응답을 통해 추적한다.
- draft spec 해석이 바뀌면 먼저 `doc/spec/draft/*` 를 갱신한 뒤 이 문서를 갱신한다.
