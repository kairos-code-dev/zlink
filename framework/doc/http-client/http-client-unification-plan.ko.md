# HTTP Client 통일·개선 진행 계획 (임시 문서)

> **임시 문서다. 모든 Phase 완료 후 삭제한다.** 계약 내용은 이 문서가 아니라
> [spec/](../framework/spec/http-client/README.ko.md)이 정본이다. 이 문서는 진행 순서·결함 목록·확인표만
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

## Phase 4 — 언어별 결함 수정 ✅ (2026-07-12)

| 언어 | 항목 | 결과 |
| --- | --- | --- |
| node | 동기 zlib → async(promisify + `chunkSize` 1MiB) + **body lazy UTF-8 변환** | ✅ 실측: 64MiB gzip, body 미접근 median 478→105ms(−78%), event loop 블로킹 461→35ms(−92%); body 접근 시 median 동등(±2%). 계약 32/32 |
| dotnet | 버퍼드 응답 무조건 `UTF8.GetString` → `RawHttpResponse.Body` lazy 변환 | ✅ 54/54 |
| dotnet | `IsPackable` | ✅ Phase 2에서 처리 |
| dotnet | 코덱 레지스트리: 매 요청 LINQ 해석 → **타입 캐시**(ConcurrentDictionary) | ✅. framework 빌더와의 **중복 통합은 보류** — framework `ZLinkCodecRegistryBuilder`가 internal이라 어셈블리 경계상 재사용 불가(공개화 필요). `AddStreamCodec` no-op은 인터페이스 계약상 유지 |
| cpp | 동기 경로 총 데드라인 | ✅ `execute()`가 `execute_with_deadline()`으로 위임 — 두 경로 timeout 의미 통일(총 예산, 언어 편차로 spec 6.2 기재) |
| cpp | 기본 스케줄러 | ✅ execute 4스레드 + resume 전용 1스레드 분리(직렬화·상호 굶김 데드락 해소). continuation 예외 무음은 유지(소유자 없는 detached 스레드 — 주석 명문화) |
| cpp | connection pool | ✅ idle TTL 30초 lazy 축출 추가. 전체 상한은 보류(키당 4 + TTL로 충분, 필요 시 재평가) |
| cpp | dead code | ✅ `http_response_metadata_t` 제거(공개 목록에 원래 없음), `version_*`는 UA 파생으로 사용처 연결(Phase 2) |
| kotlin | 취소 전파 | → Phase 5 R5/R9 결정으로 이관 |
| java | read-loop 3중복 | ✅ `BoundedRead.copy` 단일 헬퍼로 통합 |
| java | CookieJar 락 | **유지 결정** — 병목 실측 증거 없음(host당 128개 선형 스캔, 임계구역 짧음). perf 하네스(H2) 기록 후 재평가 |
| java | virtual threads | **보류 결정** — 동작·성능 변화가 커서 perf 기록(H2/H4) 없이 전환 금지. Phase 6 이후 재평가 |

전 언어 게이트: cpp 54/54 + contract headers, dotnet 54/54, java/kotlin 그린,
node 계약 32/32.

## Phase 5 — 개정 후보 결정 (R1~R14) ✅ 결정 완료 (2026-07-12)

**승격·구현 완료 (2건)**:
- [x] **R3 승격**: retry 지연을 지수 백오프 + full jitter로(상한 =
  min(1초, 50ms×2^attempt), 지연 = [0,상한] 무작위). 4언어 구현 + spec 6.2 +
  언어 가이드 10장 5개 개정. 잔여 쟁점(총 데드라인)은 R3′로 분리
- [x] **R6 승격**: cpp 응답 헤더 소문자 정규화(4장 §4.3 편차 해소). cpp 문서
  예제 3곳 + framework app-host 테스트 10곳 소문자 갱신. 전 언어 게이트 그린.
  주의: cpp 소비자는 소스 참조라 즉시 영향 — 원표기 조회 코드는 깨짐(의도)

**차기 버전(0.3.0) 구현 트랙으로 승격 예고 (2건)**:
- [ ] R1(에러 바디 노출) — 가치 최대. 설계 요점: 4언어는
  `ZLinkFrameworkException` 파생(status/headers/rawBody 보유) 1개 도입,
  cpp는 `result_t` 실패에 응답 봉투를 싣는 방법 조사 필요(별도 설계)
- [ ] R4(multipart 바이너리) — 시그니처는 자명하나 4언어 multipart 조립부가
  문자열 기반이라 바이너리 조립로 재작업 필요

**보류 (사유와 재검토 조건)**:
- R2(timeout kind): framework 공용 enum 변경은 HTTP 밖 영향 — R3′와 함께 재검토
- R3′(총 데드라인 옵션): cpp만 총 예산 강제(언어 편차) — 수요 확인 후
- R5+R9+R12(kotlin coroutine·취소 표면·스트리밍 관용화): 같은 뿌리(비동기
  수명 제어) — 단일 설계 트랙으로 묶어 진행해야 하며 부분 구현 금지
- R10(관찰성 훅): 보류 중 **우선 검토 1순위** — flow-correlation 헤더 표준을
  framework 쪽과 함께 정해야 해서 http-client 단독 결정 불가
- R11(다중값 헤더): 응답 타입 변경 — R1 구현 시 응답 모델 개정과 일괄
- R13(DI 통합): 사용성 트랙, 별도 진행 가능
- R14(poll terminal): R9(취소) 선행 필요
- R8(cpp async I/O): 대형 — 별도 draft 문서로 분리(향후 착수 시
  `framework/doc/draft/` 관례)

**현상 유지로 종결 (1건)**:
- [x] R7(one-shot): 편의 계약으로 유지. 반복 사용 경고는 공통 spec 5.4에
  명문화돼 있음. dotnet 핸들러 재생성 비용은 문서 경고로 충분하다고 판단

승격된 것만 spec 본문 이동 + 5언어 동시 구현(README 변경 절차).

## Phase 6 — 배포·관리 체계 (2026-07-12 부분 완료 — 잔여 명시)

- [x] **0.3.0 릴리즈 사이클 완주** — Phase 3~5 변경분(에러 parity, lazy
  body, async zlib, 백오프, 헤더 정규화)을 4언어 0.3.0으로 상향, 로컬
  재배포(nupkg/maven/tarball), 소비자 참조 상향(dotnet CPM·node tarball),
  전체 그린 게이트(dotnet 16/16 소비자 + `Zlink.HttpClient/0.3.0` 해석 확증,
  node 워크스페이스+계약 32/32+소비자 7/7). 격리 파이프라인 왕복 검증 완료
- [x] cpp `nlohmann_json` PUBLIC 전이 — **유지 결정**: `body<T>()`/`submit<T>()`
  공개 템플릿이 nlohmann ADL 직렬화를 소비하므로 제거하려면 직렬화 훅
  재설계(R급)가 필요. 현행 유지, 재설계는 codec 확장 논의와 함께
- [x] perf 하네스 최초분 + 기록 — `perf/harness/node-h7-compression.js`(H7),
  `perf/node-results.ko.md`(0.2.0→0.3.0 실측). 재현 검증 완료
- [ ] **잔여**: H1~H6·H8 및 타 언어 하네스, 교차 언어 계약 게이트 확장
  (spec 11.3), spec 11.4 커버리지 갭 케이스(cpp pool TTL/cookie 128 축출,
  dotnet 호출자 취소) — 후속 작업 트랙

## 진행 확인표

| # | 항목 | 상태 |
| --- | --- | --- |
| 1 | Phase 0 공통 spec + perf 문서 | ✅ 2026-07-12 |
| 2 | Phase 1 문서 정비 | ✅ 2026-07-12 |
| 3 | Phase 2 소비자 격리(로컬 패키지 0.2.0 + 전환 + 그린 게이트) | ✅ 2026-07-12 (cpp는 소스 참조+계약 게이트로 편차 확정) |
| 4 | Phase 3 에러 모델 parity | ✅ 2026-07-12 |
| 5 | Phase 4 언어별 결함 | ✅ 2026-07-12 |
| 6 | Phase 5 R-항목 결정 | ✅ 2026-07-12 (R3·R6 승격 구현, R1·R4 0.3.0 트랙, R7 종결, 나머지 보류) |
| 7 | Phase 6 배포·perf 체계 | 🔶 2026-07-12 부분 완료(0.3.0 사이클·H7 기록 완료, perf 하네스 잔여·교차 게이트·커버리지 갭은 후속) |
| 8 | 이 문서 삭제 | ⬜ (Phase 6 잔여 + R1/R4 0.3.0 트랙 종료 후) |

## 완료 기준

- 5개 언어가 spec 2~9장 계약과 11장 매트릭스를 전량 그린으로 통과.
- e2e/샘플 소비자가 고정 버전 로컬 패키지를 경유하고, 최종 버전으로
  상향된 상태에서 전량 그린.
- 언어별 spec이 공통 spec을 참조하는 구조로 재편.
- perf 최초 기록이 존재하고 회귀 판정 기준이 작동.
- 이 문서 삭제(내용은 spec/커밋 이력으로 대체).
