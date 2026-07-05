# Framework 샘플 parity 포팅 계획 — 5개 언어 동일 샘플 세트

> dotnet 샘플이 POSD 재설계([framework-public-contract-posd-redesign.ko.md](framework-public-contract-posd-redesign.ko.md))
> 완료로 레퍼런스 확정됨에 따라, **5개 언어(dotnet·node·java·kotlin·cpp)가 동일한 샘플 세트를
> 갖도록** 갭을 포팅한다(사용자 지시 2026-07-04). 정본 샘플 스펙은
> `framework/doc/framework/common/sample/`의 공통 샘플 문서이고, 코드 형태의 최종 기준은 dotnet
> 샘플이다.

## 1. 현황 매트릭스 (2026-07-04 실측)

| 샘플 | dotnet | node | java | kotlin | cpp |
|------|:---:|:---:|:---:|:---:|:---:|
| TicTacToe | ✅ | ✅ | ✅ | ✅ | ✅ |
| Bingo | ✅ | ✅ | ✅ | ✅ | ✅ |
| DeliveryDispatch | ✅ | ✅ | ✅ | ✅ | ✅ |
| SupportChat | ✅ | ✅ | ✅ | ✅ | ✅ |
| GameQuest | ✅ | **⬜ P-N2** | ✅ | ✅ | **⬜ P-C2** |
| ShoppingMall | ✅ | ✅ | ✅ | ✅ | ✅ |

갭 합계: **8건 포팅 + 1건 완성도 확인**(P-J1: java GameQuest는 디렉터리·빌드는 있으나
[GameQuest 포팅 트랙](../../doc 참조)의 잔여 모듈 완성 여부를 S0에서 확인).

## 2. 포팅 원칙

1. **정본**: 공통 샘플 문서(`framework/doc/framework/common/sample/<sample>/README.ko.md`)가 시나리오
   계약의 정본, dotnet 코드가 형태의 정본. 두 정본이 어긋나면 공통 문서 기준(코드가 따라간다).
2. **언어 idiom 매핑**: 각 언어 wave 문서 2절의 매핑 규칙을 그대로 사용(attribute↔decorator↔명시
   등록, CompletionStage↔Promise↔suspend↔콜백/코루틴). 언어 능력 차이는 유지하고 기계 복사하지
   않는다. kotlin 샘플은 java 런타임 공유 + suspend idiom 레이어(기존 kotlin 샘플 인프라 규약).
3. **HTTP 클라이언트 규약**: 샘플의 HTTP 호출은 각 언어의 zlink http client를 쓴다(raw http client
   금지 — dotnet은 SampleRegression 가드로 강제, 타 언어도 동등 가드 추가).
4. **L13 표면 포함**: DeliveryDispatch Tracking의 actor 전송은 `SendToActor`/`RequestToActor` 새
   표면 기준(L13 e2e 그린 이후의 dotnet 형태).
5. **빌드/러너 등록 gotcha**: 신규 샘플은 언어별 빌드 등록 필수 — node(package/워크스페이스),
   java·kotlin(settings.gradle.kts), cpp(CMakeLists). run_samples.sh 러너 등록과
   doc tab 체커(check_doc_tabs.py) 통과 확인.
6. **검증 게이트(샘플별)**: ⑴ 언어 빌드 그린 ⑵ 샘플 러너(run_sample/run_samples) 실행 PASS ⑶ 해당
   언어의 샘플 회귀 검사(있는 언어) 갱신 ⑷ 공통 샘플 문서와 시나리오 대조 ⑸ 구 표면 grep 0.

## 3. 작업 항목 (언어별 병렬, 언어 안에서는 샘플 순차)

| ID | 언어 | 샘플 | 상태 | 비고 |
|----|------|------|:---:|------|
| P-J1 | java | GameQuest **포팅(재분류)** | ✅ | 2026-07-05 완료 — dotnet 미러, run_sample 3연속 PASS, 릴리즈 게이트 그린 |
| P-N1 | node | SupportChat | ✅ | 2026-07-05 완료 — 2연속 PASS, sample-regression 35/35 |
| P-N2 | node | GameQuest | 🟨 | 구조 완성·빌드 그린, 클라 requestTimeout 6왕복 미결(동결) — 다음: java 정본과 flow trace hop 비교. 로그 scratchpad/run_ngq5 |
| P-N3 | node | ShoppingMall | ✅ | 2026-07-05 완료 — 3연속 PASS |
| P-K1 | kotlin | GameQuest | ✅ | 2026-07-05 완료 — java 정본 미러, run_sample 3연속 PASS |
| P-K2 | kotlin | ShoppingMall | ✅ | 2026-07-05 완료 — 구 registry 제거·location store 전환, 3연속 PASS |
| P-C1 | cpp | SupportChat | ✅ | 2026-07-05 완료 — 3연속 PASS |
| P-C2 | cpp | GameQuest | 🟨 | segfault·잔존quest 해소, 잔여="first kill event id mismatch"(동결) — 다음: dotnet 정본 이벤트 순서 대조 |
| P-C3 | cpp | ShoppingMall | ✅ | 2026-07-05 완료 — 5연속 PASS |
| SAMP-V | dotnet | 6종 샘플 ↔ 공통 샘플 문서 갭 검증·수정 | ✅ | 2026-07-04 갭 0 — 코드·문서 일치 |
| SAMP-GN | node | 기존 ✅ 샘플(TicTacToe·Bingo) dotnet 대비 갭 수정 | ✅ | 2026-07-04 갭 1건(Bingo reward id drift) 수정 |
| SAMP-GJ | java+kotlin | 기존 ✅ 샘플(TTT·Bingo·SupportChat·ShoppingMall) dotnet 대비 갭 수정 | ✅ | 2026-07-04 갭 0 — P-J1은 포팅으로 재분류 |
| SAMP-GC | cpp | 기존 ✅ 샘플(TicTacToe·Bingo) dotnet 대비 갭 수정 | 🟨 | TicTacToe ✅(2026-07-05 3연속 PASS — remote join·notify·send/request kind 보존·재접속 재바인딩·destroy 노드 경계 전달·route rid 결부 등 framework 결함 6종 수정). Bingo ⬜(errno=113 두 원천(route rid·채널 수렴) 해소 후에도 auth/match 간헐 flake — coroutine 예외, 원인 미확정). DD ⬜(courier offer wait failed — 미착수) |
| P-G | 전체 | 최종 게이트 | ⬜ | 매트릭스 전 칸 ✅ + 5언어 러너 일괄 PASS + codex 리뷰 `이슈 없음` |

착수 조건: 각 언어의 L13(actor client) 작업이 해당 언어 트리에서 완료된 후(빌드 등록 파일 충돌
방지). 실행·검증 운영은 POSD 재설계와 동일(codex 발주+감독 실측, 진행 상태는 이 문서에만 기록).

## 4. 완료 판정

- 1절 매트릭스 전 칸 ✅(P-J1 포함), 3절 전 항목 ✅.
- 5개 언어 샘플 러너 전부 실측 PASS.
- 샘플별 공통 문서와 시나리오 일치(codex 교차 리뷰 `이슈 없음`).
