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

## Phase 1 — 문서 정비 (코드 무변경)

- [ ] 언어별 spec 5개(`<lang>/spec/<lang>-http-client.ko.md`)를
  "공통 계약 참조 + 언어 편차만"으로 재편, 공통 spec 링크 추가
- [ ] dotnet/java/kotlin/node 03장에 기본값 표 반영(공통 spec 인용)
- [ ] 07장 파일명·제목 정리: 코루틴 없는 언어(dotnet/java/node)는
  `07-async`(비동기)로, kotlin은 현행 `07-coroutines` 유지 — 링크 일괄 수정
- [ ] 스텁 챕터 보강: dotnet/java/kotlin의 09(auth)·11(proxy)·12(압축)에
  CONNECT 의미론/보안 서술 추가(cpp 수준으로)
- [ ] cpp 10장에 cookie host당 128개 상한 추가(유일 누락)
- [ ] node spec §3의 builder 목록에서 `json` 제거 — 코드에 없는 메서드
  (`client.ts` builder에 미존재, 문서 drift)
- [ ] 언어별 spec 상단 nav 블록 통일(cpp만 있음)

## Phase 2 — 소비자 격리: 로컬 패키지 스냅숏 + 버전 도입

http-client 수정 전에 e2e/샘플을 현행 스냅숏에 고정한다. bindings local
package 정책(`scripts/local-package/README.ko.md`)을 http-client에 그대로
확장: 소비자는 소스가 아니라 **명시한 버전의 로컬 패키지**만 참조하고,
새 버전을 배포해도 참조 버전을 올리기 전까지 기존 버전을 쓴다.

- [ ] 버전 baseline `0.2.0` 확정 — 현행(=spec 동결본)을 0.2.0으로 스냅숏.
  UA `zlink-http-client/0.2` 하드코딩(java/node)과 정합. 버전 정본을 한 곳에
  두고 아티팩트 버전·User-Agent를 파생(cpp `version_*` 상수는 이 정본과
  정합시키거나 제거)
- [ ] dotnet `IsPackable=true` (Phase 6에서 당김 — nupkg 생성 전제)
- [ ] `scripts/local-package`에 http-client 트랙 추가, 산출물은
  `.artifacts/wsl` 아래(nupkg 피드 / maven repo / npm tarball / cpp install
  export). README 정책 문구 갱신(bindings 전용 → +framework http-client)
- [ ] 소비자 전환 (java/kotlin은 소비자 없음 — 제외):
  - dotnet: e2e 9 + 샘플 7 csproj — ProjectReference → PackageReference 0.2.0 고정
  - node: 샘플 4 + e2e 1 — workspace 참조 → `.artifacts` tarball 참조
  - cpp: 샘플 1 + e2e 2 (+PackageTests) — 소스 타깃 → `find_package` 설치본
- [ ] 게이트: 전환 직후 해당 e2e/샘플 전량 그린(수정 전 baseline 증명)
- [ ] 이후 규칙: http-client 수정 → 새 버전(0.3.0…) 로컬 배포 → 소비자
  참조 버전은 의도적으로만 상향. 계약 영향 변경(R 승격)은 minor 상향 +
  plan에 소비자 반영 항목 동반

## Phase 3 — 에러 모델 parity (spec 9장 구현 갭 해소)

- [ ] java/kotlin: framework 공용 error kind + `isRetriable` 노출
  (`ZLinkFrameworkException`에 kind 접근 경로 — framework-core 조율 필요)
- [ ] dotnet: timeout의 `TimeoutException` 이탈을 zlink 에러 모델로 회수
  (R2 결정과 연동 — R2 승격 전에는 최소한 kind 식별 가능하게)
- [ ] cpp: 일반 예외 일괄 retriable=true 매핑 제거
  (`runtime_errors.cpp:21-25`) — retriable은 전송 실패·timeout만
- [ ] 계약 테스트: kind/isRetriable 검증 케이스를 4언어에 추가(spec 11장 매트릭스 갱신)

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
| 2 | Phase 1 문서 정비 | ⬜ |
| 3 | Phase 2 소비자 격리(로컬 패키지 0.2.0 + 전환 + 그린 게이트) | ⬜ |
| 4 | Phase 3 에러 모델 parity | ⬜ |
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
