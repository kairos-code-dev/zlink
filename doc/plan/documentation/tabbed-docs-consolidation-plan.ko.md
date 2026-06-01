# zlink 문서 탭 통합 개편 계획 (v2)

> 이 문서는 [documentation-overhaul-plan](./documentation-overhaul-plan.ko.md)(v1)의
> **개정**이다. v1은 "언어별 바인딩 가이드 6문서 × 7언어"를 만드는 방향이었으나,
> 실제 사용 경험에서 두 가지 문제가 드러났다:
>
> 1. 개념 문서(코어 07-*)가 C API 코드 투성이라 다른 언어 사용자가 졸린다.
> 2. 코어 개념 + 언어별 가이드가 같은 내용을 중복 설명한다(actor를 두 곳에서).
>
> v2는 **코어 가이드를 7+언어 코드 탭으로 통합**하고, **언어별 바인딩 가이드를
> 설치·고유점 1장으로 축소**한다. 원래 mkdocs Material 사이트에 있던 언어 탭
> 방식(`ec1603d8c` 커밋, 이후 유실)을 복원·확장하는 것이기도 하다.
>
> source of truth:
> - 문서 측 원칙: [documentation-principles](../../principal/documentation/documentation-principles.ko.md)
> - 샘플 측 정책: [SAMPLE_POLICY](../../spec/sample/SAMPLE_POLICY.md)
> - 탭 인프라: `doc/site/mkdocs.yml`(`pymdownx.tabbed` 등)

---

## 용어 — 언어 분류 (수치 혼동 방지)

이 문서에서 "언어"는 맥락마다 대상이 다르다. 아래로 고정한다.

| 분류 | 목록 | 수 |
|------|------|----|
| **네이티브 바인딩** | C, C++, C#/.NET, Java, Node/TypeScript, Python, Go, Rust | 8 |
| **런타임 공유 언어** | Kotlin(JVM=Java런타임), JavaScript(Node런타임) | 2 |
| **전체 표면 언어** | 네이티브 8 + 공유 2 | 10 |
| **C 코어** | C는 코어 그 자체(별도 wrapper 가이드 없음) | — |

탭 칸 수는 **문서마다 다르다**:
- **소켓 패턴 문서**(pair/dealer 등)는 C 샘플이 있어 **C 탭 포함 → 최대 10칸**.
- **서비스 문서**(actor/spot 등 일부)는 해당 C 샘플이 없으면 **C 탭 제외 → 9칸**.
- 탭은 "그 문서에 해당하는 샘플이 존재하는 언어"만 넣는다. 없는 언어 탭을 비워
  두지 않는다.

"v1의 7언어 가이드"는 C를 제외한 7개 wrapper 바인딩 가이드를 뜻한다(C는 코어).

---

## 0. 핵심 결정 (확정)

| # | 결정 | 이유 |
|---|------|------|
| D1 | **코어 guide의 코드를 언어 탭으로** | 개념 1번 + 각 언어 사용자는 자기 탭만 봄(졸림 해소). zguide 패턴 |
| D2 | **언어별 바인딩 가이드 → 언어당 1장 축소** | 6문서×7언어(42개)는 탭과 중복. 단 설치·소유권·배포는 탭이 못 담음 → 1장 잔류 |
| D3 | **bindings 가이드 0 삭제는 안 함** | 설치(`npm`/`dotnet`/`cargo`)·소유권(`using`/`defer`/`Drop`)·배포(AOT 등)는 코드 탭에도 코어 본문에도 안 맞음 |
| D4 | **Kotlin/JS = 런타임 공유 언어** | Kotlin은 java 바인딩(`systems.zlink.*`), JS는 node 바인딩(`@zlink-systems/zlink`)을 그대로 씀. 새 네이티브 바인딩 없음 |
| D5 | **Kotlin/JS 샘플 = 별도 디렉토리, 샘플만** | `bindings/kotlin/samples/`, `bindings/javascript/samples/`. 네이티브 바인딩 소스(`src/`)는 만들지 않고 기존 java/node 패키지를 의존. 단 샘플 빌드용 `build.gradle`(kotlin)·러너 설정(js)은 필요 |
| D6 | **internals는 2분류** | 순수 코어 내부(reaper/YPipe)는 C-only, 바인딩이 표면화하는 내부(소유권/zerocopy)는 언어 비교 탭 |
| D7 | **spec은 탭 통합 안 함** | 언어별 계약은 시그니처·반환·오류가 달라 `spec/bindings/<lang>` 분리 유지 |

---

## 1. 목표 구조

### 코어 가이드 (탭 통합)

```
doc/guide/07-4-actor.ko.md   (단 하나, 서비스 문서 예시 = C 샘플 없어 C 탭 제외)
  [개념: 역할·언제·재접속 이전성]   ← 탭 밖, 언어 무관, 1번
  === "C++"  코드
  === "C#/.NET"  코드
  === "Java" / "Kotlin"  코드     ← JVM 런타임, 언어 2칸
  === "Node/TypeScript" / "JavaScript"  코드  ← Node 런타임, 언어 2칸
  === "Python" / "Go" / "Rust"  코드
```

탭 언어 칸은 문서마다 다르다(위 용어 표 참조). 서비스 문서(actor 등)는
**C++ · C#/.NET · Java · Kotlin · TypeScript · JavaScript · Python · Go ·
Rust**(9칸, 해당 C 샘플 없으면 C 제외). 소켓 패턴 문서는 여기에 **C**를 더해
최대 10칸.

### 바인딩 가이드 (1장 축소)

```
doc/guide/bindings/<lang>.md (+ .ko.md)   (언어당 1장, ko/en 쌍)
  - 설치 (패키지 매니저)
  - 소유권·정리 모델 (그 언어 관용)
  - 패키징/배포 주의점
  - C API ↔ 언어 대응표 (탭에 안 가는 언어 고유 표)
  - "메시징·서비스 사용법 → 코어 가이드" 안내 (탭에서 자기 언어)
```

기존 `bindings/<lang>/`의 6문서(index/01~05) 중:
- **잔류(1장으로 회수)**: 01의 설치·소유권, 05의 C API↔언어 대응표·코덱·배포 —
  탭에도 코어 본문에도 안 들어가는 **언어 고유 내용**.
- **삭제(코어 탭이 흡수)**: 02 messaging, 03 services, 04 operations의 코드 예제,
  05의 샘플 링크.

현재 바인딩 가이드는 ko만 존재(42개 ko, en 0). 1장도 ko 우선, en은 후속.
Kotlin은 java 1장에 "Kotlin 의존성·관용" 절, JS는 node 1장에 절로 추가(별도
디렉토리 아님 — D2/D4).

### 샘플

```
bindings/<lang>/samples/         기존 네이티브 8언어 (C 포함)
bindings/kotlin/samples/         신규 (.kt, gradle, systems.zlink.*)
bindings/javascript/samples/     신규 (.js, node, @zlink-systems/zlink)
```

모든 샘플은 SAMPLE_POLICY의 canonical 시나리오·값을 **동일하게** 따른다.

---

## 2. 실행 로드맵 (순서 = 의존성)

> "정책부터 확장" 원칙. 구조를 먼저 확정하고 그 기준으로 콘텐츠를 만든다.

| 단계 | 작업 | 산출물 | 비고 |
|------|------|--------|------|
| **P1** | 정책·원칙에 v2 구조 반영 | SAMPLE_POLICY + documentation-principles 갱신 | SAMPLE_POLICY: Kotlin/JS 런타임공유 샘플 규칙(현재 없음). documentation-principles: bindings 가이드 1장 축소 규칙·탭 언어 분류(탭 원칙 자체는 이미 있음) |
| **P2** | 샘플 통일 (네이티브 8언어) | actor 3종 + 검증 | 진행 중. 현 상태: 차이 스캔 완료(불일치는 dotnet/java/node/python actor id·payload). cpp/go/rust가 정책 기준 |
| **P3** | Kotlin/JS 샘플 신규 | `bindings/{kotlin,javascript}/samples/` | canonical 세트, 정책 값. 빌드: kotlin gradle / js node 러너 |
| **P4** | 코어 guide 탭 확대 | 07-*, 03-* 등 패턴·서비스 문서 | actor는 site에 파일럿 1개 존재(통일 **전** 값으로 작성됨 → P2 통일 후 파일럿 코드도 갱신). 나머지 문서로 확대. 문서별 탭 칸 수는 용어 표 기준. 탭 코드는 P2/P3 통일 샘플에서 가져옴 |
| **P5** | bindings 가이드 1장 축소 | `bindings/<lang>.md`(+.ko.md) | 6문서→1장. 설치·소유권·대응표·배포 잔류, 02~04+05일부 삭제 |
| **P6** | 탭 CI 강제 | 회귀 테스트 | 탭 언어 누락·탭 소실·API 부재 검출(원칙 5) |
| **P7** | 진입·미러·mkdocs 정리 | README, mkdocs nav, site 미러 | 탭 통합 문서·1장 가이드 반영 |

각 단계는 비교적 독립적이나 P1(정책)이 나머지의 기준이다. P2/P3(샘플)이
P4(탭 코드의 출처)를 선행하는 게 안전하다 — 탭은 샘플에서 가져오므로. 이미 만든
actor 파일럿은 통일 전 값이라, P2 완료 후 그 파일럿 코드도 통일 값으로 맞춘다.

---

## 3. 리스크와 완화

| 리스크 | 완화 |
|--------|------|
| 7언어 6문서 가이드 대거 삭제 = 작업 손실 | git 보존. **회수 명시**: 05의 C API↔언어 대응표·코덱·배포, 01의 소유권 모델은 1장으로 옮긴다(삭제 전 체크) |
| 탭이 또 유실됨 (과거 `03632539b`에서 발생) | P6 CI 강제 필수. 탭 마크업 수 급감 시 빌드 실패 |
| 추측 API (언어 수 증가로 위험 커짐) | 모든 탭 코드는 실제 샘플/contract 대조. 샘플이 단일 출처(P2/P3 선행) |
| 빌드 환경 (dotnet/gradle/cargo/node/python) | 통일·검증 시 언어별 빌드 가능 여부 먼저 확인. 불가하면 코드만 맞추고 CI 위임 |
| 동시 편집 충돌 | 단계별·언어별 순차. 한 번에 다 고치지 않음 |
| Kotlin/JS 빌드 통합 (gradle/node) | Kotlin은 기존 java gradle에 추가, JS는 node 샘플 러너에 추가 |

---

## 4. 완료 판정 기준

- [ ] 코어 guide 패턴·서비스 문서가 언어 탭으로 통합됨 (개념 1번 + 언어별 코드,
  문서별 탭 칸 수는 용어 표 기준 9~10칸).
- [ ] 모든 탭 코드가 실제 샘플/contract와 일치 (추측 0).
- [ ] 네이티브 8언어 + Kotlin/JS 샘플이 canonical 시나리오·값을 동일하게 따름.
- [ ] bindings 가이드가 언어당 1장(설치·소유권·대응표·배포)으로 축소됨.
- [ ] 탭 누락·소실·API 부재를 CI가 잡음.
- [ ] `mkdocs serve`로 탭 렌더(전 언어 칸) 확인.
- [ ] mkdocs nav·미러·README가 새 구조 반영.

---

## 5. v1과의 관계

- v1의 "개념은 한 번만"(2장), "도메인 벤치마크"(9장), Part 구조, persona 진입은
  유지된다. v2는 그 위에서 **전달 형식만 "언어별 문서"→"언어 탭"으로** 바꾼다.
- v1에서 만든 7언어 6문서 가이드는 P5에서 1장으로 축소되며 흡수된다.
- v1에서 만든 코어 신규 문서(reliability/design-rationale/zmp/glossary/scenarios)는
  그대로 유지된다(언어 무관 개념·레퍼런스라 탭 불필요).
