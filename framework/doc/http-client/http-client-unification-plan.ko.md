# HTTP Client 통일·개선 진행 계획 (임시 문서)

> **임시 문서다. 모든 Phase 완료 후 삭제한다.** 계약 내용은 이 문서가 아니라
> [spec/](spec/README.ko.md)이 정본이다. 이 문서는 진행 순서·결함 목록·확인표만
> 담는다.
>
> 작성: 2026-07-12. 근거: 5개 언어 구현 + 문서 5세트 전수 조사(같은 날).

## 배경 요약

- 5개 모듈 모두 POSD 구조는 양호(god file 없음, 공개/runtime 분리 일관).
  대형 구조 리팩토링은 불필요.
- 진짜 문제: ① 언어 중립 공통 계약 부재(기본값이 cpp 문서에만 존재)
  ② 에러 모델 발산(java/kotlin kind 부재, dotnet timeout 이탈)
  ③ 언어별 실결함·성능 이슈 ④ 배포/버전 정합 문제.

## Phase 0 — 공통 spec 신설 ✅ (2026-07-12)

- [x] `spec/README.ko.md` + 01~11장 + `language-interfaces.ko.md` 작성
- [x] `perf/README.ko.md` (시나리오 매트릭스 정본) 작성
- [x] 개정 후보 R1~R8 등재 (`spec/10-revision-candidates.ko.md`)

## Phase 1 — 문서 정비 (코드 무변경) ✅ (2026-07-12)

- [x] 언어별 spec 5개를 "공통 계약 참조 + 언어 편차만"으로 재편
  (dotnet/java/kotlin/node §5·§6 교체, cpp는 참조 헤더 + 상세 유지)
- [x] dotnet/java/kotlin/node 03장에 기본값 표 반영(공통 spec 인용)
- [x] 07장 파일명·제목 정리: dotnet/java/node `07-async.ko.md`(비동기),
  kotlin `07-coroutines`·cpp `07-async-coroutines` 유지 — 링크 일괄 수정,
  저장소 전체 참조·링크 무결성 검사 통과
- [x] 스텁 챕터 보강: 3언어 11장(CONNECT tunnel/end-to-end TLS/인증 비유출),
  09장(trust 추가 의미·hostname 검증 명시, dotnet 09 "custom root trust"
  오해 표현 교정 — java composite TrustManager 코드로 검증). 12장은 이미
  핵심 의미론 보유로 보강 불요 판정
- [x] cpp 10장 + cpp spec에 cookie host당 128개 상한 추가
- [x] `json()`/`Json()` drift 전량 제거 — node spec만이 아니라
  **5언어 문서 전반**(dotnet·java·node spec §3, 03장 5개, kotlin 02/03/12장
  예제, cpp 03장 표)에 잔존했음. 코드는 커밋 `a69810f1e`에서 제거된 옵션
- [x] spec 상단 통일: 5개 spec 모두 "공통 spec이 정본 + 이 문서는 언어
  편차만" 헤더로 통일(cpp adapter-nav 체인은 cpp 고유 유지)

## Phase 2 — 소비자 격리: 로컬 패키지 스냅숏 + 버전 도입

http-client 수정 전에 e2e/샘플을 현행 스냅숏에 고정한다. bindings local
package 정책(`scripts/local-package/README.ko.md`)을 http-client에 그대로
확장: 소비자는 소스가 아니라 **명시한 버전의 로컬 패키지**만 참조하고,
새 버전을 배포해도 참조 버전을 올리기 전까지 기존 버전을 쓴다.

- [x] 버전 baseline `0.2.0` 확정 — 언어별 단일 소스에서 UA 파생:
  dotnet csproj `<Version>`→assembly version, java `HttpClientVersion.java`
  (gradle 버전도 이 파일에서 regex 파생), node `package.json`(runtime require),
  cpp `types.hpp` `version_*` 상수(dead code였던 것을 사용처 연결)
- [x] dotnet `IsPackable=true` + `SuppressDependenciesWhenPacking`(Framework
  의존은 패키지 의존으로 선언하지 않음 — 소비자가 framework를 직접 소유)
- [x] `scripts/local-package/http-client/build-wsl.sh` 신설 + README에
  HTTP client 트랙 절 추가. java 루트 gradle의 file:// 게시 credentials
  검증 오류 수정(Gradle 9). **kotlin 모듈 게시는 제외**(Gradle 9 ×
  Kotlin 2.1.0 플러그인 `getDependencyProject` 비호환, 소비자 없음)
- [x] 소비자 전환:
  - dotnet: 16 csproj → CPM `ZLinkHttpClientPackageVersion=0.2.0` 고정
    PackageReference(+순수 클라이언트 13곳에 Framework ProjectReference 추가)
  - node: http-client를 workspace에서 제외, 루트 `package.json`에
    `.artifacts` tarball 고정 참조(기존 `@zlink-systems/zlink` 8.6.4와 동일 패턴)
  - **cpp: 전환 제외 결정** — static lib + in-tree framework PUBLIC 헤더
    의존이라 설치본/소스 혼합 시 ODR 위험. 소스 참조 유지 +
    `test_cpp_http_client` 계약 테스트 게이트로 대체
- [x] 게이트 그린: dotnet 소비자 16/16 빌드(assets에 `Zlink.HttpClient/0.2.0`
  type=package 확증) + UnitTests 54/54, node 워크스페이스 빌드 + 계약 32/32 +
  샘플 4 + e2e 3 빌드, java/kotlin 테스트 그린, cpp 54/54
- [ ] 이후 규칙(상시): http-client 수정 → 새 버전(0.3.0…) 로컬 배포 → 소비자
  참조 버전은 의도적으로만 상향. 계약 영향 변경(R 승격)은 minor 상향 +
  plan에 소비자 반영 항목 동반

## Phase 3 — 에러 모델 parity ✅ (2026-07-12)

- [x] java: framework-core에 `kind()`/`retriable()`가 **이미 존재**했음(조사
  보고와 달리 갭은 http-client가 message-only 생성자만 쓴 것). http-client의
  예외 생성 지점 28곳 전수에 kind 분류 적용(PROTO/FAILED/DECODE), 전송 실패
  래핑에 retriable 플래그 전달. kotlin은 전이로 해소
- [x] dotnet: timeout을 `RequestFailed(IsRetriable=true)` + inner
  `TimeoutException`으로 회수(호출자 취소는 `OperationCanceledException` 유지)
- [x] cpp: `map_exception` 일괄 retriable=true를
  `map_transport_exception`(true) / `map_unexpected_exception`(false)으로 분리
- [x] 계약 테스트: java 4케이스(400 kind/retriable, decode kind, 검증 kind,
  timeout kind+retriable), dotnet timeout 2케이스 갱신. node/cpp는 기존
  테스트가 이미 kind 검증. spec 9.2/9.3·언어별 spec 6절 현황 갱신

## Phase 4 — 언어별 결함 수정

| 언어 | 항목 | 위치 |
| --- | --- | --- |
| node | 동기 zlib(`gunzipSync` 등) → async 전환(event loop 블록 제거) | `runtime/compression.ts` |
| dotnet | 버퍼드 응답 무조건 `UTF8.GetString`(바이너리 응답 낭비/훼손) → lazy 변환 | `Runtime/RequestPerformer.cs:90` |
| dotnet | `IsPackable` 미설정 → nupkg 미생성인데 문서는 "public package". 배포 의도 확정 후 정렬 | `Zlink.HttpClient.csproj` |
| dotnet | `HttpClientCodecRegistry` ↔ framework `ZLinkCodecRegistryBuilder` 중복 통합 + 타입캐시 + `AddStreamCodec` no-op 정리 | `Runtime/HttpClientCodecRegistry.cs` |
| cpp | 동기 경로 재시도 총 데드라인 부재(최악 retry×timeout 블로킹) — 코루틴 경로와 시맨틱 통일 | `http_client_runtime.cpp:30-42` |
| cpp | 기본 스케줄러 1스레드 execute/resume 공용(직렬화·데드락) + continuation 예외 무음 삼킴 | `coroutine_scheduler.cpp` |
| cpp | connection pool: 키당 idle 4 하드코딩, 전체 상한·TTL 없음 | `connection_pool.cpp:23` |
| cpp | dead code 제거: `http_response_metadata_t`, `version_*` 상수 | `contracts/types.hpp` |
| kotlin | 취소 전파(`suspendCancellableCoroutine` + 하부 cancel) — R5 승격과 연동 | `HttpClientCoroutines.kt` |
| java | CookieJar 단일 synchronized 락 완화 검토(활성화 시 전 요청 경유) | `internal/CookieJar.java` |
| java | read-loop/size-limit 3중복 통합 | `ResponseBodyReader` / `ResponseCompression` |
| java | virtual threads 검토(JDK 22 타깃인데 body-read를 플랫폼 풀에 오프로드) | `HttpClientRuntime.java` |

perf 영향이 있는 항목(node zlib, dotnet GetString, cpp 스케줄러/풀)은
**perf/README 규칙대로 baseline vs patched 측정 후 커밋**.

## Phase 5 — 개정 후보 결정 (R1~R14)

- [ ] R1(에러 바디 노출) — 우선 검토 권장(실무 함정 최다)
- [ ] R2(timeout kind) — Phase 3(에러 모델)과 연동
- [ ] R3(retry 백오프) / R4(multipart 바이너리) / R6(헤더 정규화) /
  R7(one-shot 재검토) / R11(다중값 헤더)
- [ ] R5(kotlin Flow·취소·fetch 동명이의) — R9(취소 표면)·R12(스트리밍
  관용화)와 묶어서 결정 권장(같은 뿌리)
- [ ] R10(관찰성 훅) — framework 통일성 관점 우선 검토 권장
  (flow-tracing 헤더 전파)
- [ ] R13(호스팅/DI 통합) — 사용성 트랙, 우선 dotnet/node
- [ ] R14(조건 폴링 `poll` terminal) — 무조건 loop 포함 여부는 R14에서 결정,
  취소(R9) 선행 권장
- [ ] R8(cpp async I/O 전환) — 대형, 별도 draft로 분리 권장

승격된 것만 spec 본문 이동 + 5언어 동시 구현(README 변경 절차).

## Phase 6 — 배포·관리 체계

- [ ] 버전 단일 소스화 마무리 — Phase 2에서 도입한 버전 정본을 정식 배포
  파이프라인까지 연결(빌드에서 UA 주입, `0.1.0-SNAPSHOT` 잔재 제거)
- [ ] cpp PUBLIC 링크 전이(`nlohmann_json`) 재검토
- [ ] 교차 언어 계약 게이트: node `verify:cross-language`를 5언어 매트릭스
  대조로 확장(spec 11장 §11.3)
- [ ] perf 하네스 신설 + 최초 기록(`perf/<lang>-results.ko.md`) — H1~H8
- [ ] spec 11.4 커버리지 갭 케이스 보강(cpp pool/cookie 축출, dotnet 취소 등)

## 진행 확인표

| # | 항목 | 상태 |
| --- | --- | --- |
| 1 | Phase 0 공통 spec + perf 문서 | ✅ 2026-07-12 |
| 2 | Phase 1 문서 정비 | ✅ 2026-07-12 |
| 3 | Phase 2 소비자 격리(로컬 패키지 0.2.0 + 전환 + 그린 게이트) | ✅ 2026-07-12 (cpp는 소스 참조+계약 게이트로 편차 확정) |
| 4 | Phase 3 에러 모델 parity | ✅ 2026-07-12 |
| 5 | Phase 4 언어별 결함 | ⬜ |
| 6 | Phase 5 R-항목 결정 | ⬜ |
| 7 | Phase 6 배포·perf 체계 | ⬜ |
| 8 | 이 문서 삭제 | ⬜ |

## 완료 기준

- 5개 언어가 spec 2~9장 계약과 11장 매트릭스를 전량 그린으로 통과.
- e2e/샘플 소비자가 고정 버전 로컬 패키지를 경유하고, 최종 버전으로
  상향된 상태에서 전량 그린.
- 언어별 spec이 공통 spec을 참조하는 구조로 재편.
- perf 최초 기록이 존재하고 회귀 판정 기준이 작동.
- 이 문서 삭제(내용은 spec/커밋 이력으로 대체).
