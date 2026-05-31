# GOAL: bindings 7개 언어 contracts 주석 POSD 정전화 (dotnet 표준 횡전개)

## 배경 / 기준
- 기준 정책: `doc/principal/software-design-principles.md` §Comments (POSD).
- 정전(canonical) 표준 = 이미 완료된 `bindings/dotnet/src/Zlink/Contracts/` (커밋 `8bd034e60`).
  → 모든 계약 문구의 "출처"는 dotnet contracts다. 새로 지어내지 말고 dotnet에서 가져와 각 언어 관용구로 옮긴다.
- 발견: 나머지 언어는 dotnet 같은 rot이 아니라 주석이 "비어 있음(sparse)". 즉 주로 "고치기"가 아니라 "써넣기".

## 진행 순서 (한 번에 한 언어, 절대 병렬 금지)
Rust → Python → Node → Go → Java → C/C++ 헤더
- 각 언어 contracts 트리: `bindings/<lang>/.../contracts/` (C는 `c/include/zlink/*.h` 가 원천).
- 구조는 dotnet과 1:1 매핑(core/sockets/messaging/eventing/errors/service). 같은 계약을 같은 위치에 이식.

## 언어별 절차 (현재 언어 L)
1. **매핑**: L의 contracts 파일을 dotnet `Contracts/` 대응 파일에 1:1 대응시킨다.
2. **이식**: dotnet 계약 의미를 L 관용 doc 형식으로 작성.
   - Rust: rustdoc `///` (필요시 `# Safety` / `# Panics` / `# Errors` 절)
   - Python: docstring (Args / Returns / Raises)
   - Node: TSDoc (`@param @returns @remarks`)
   - Go: godoc (식별자로 시작하는 문장)
   - Java: Javadoc (`@param @return @throws`)
   - C/C++: Doxygen (`@brief @param @return @note`)
3. **정책(엄수)**:
   - 숨은 계약(소유권/수명/auto-start/스레딩/null/단위/에러/부수효과)이 있으면 그것을 쓴다.
   - 진짜 비자명한 게 없으면 "정확한 한 줄"로만. 코드 재진술·placeholder 금지.
   - No-duplication: 같은 계약은 한 곳에만 쓰고 나머지는 그 언어식 참조(rustdoc `` [`Type`] ``, javadoc `{@link}`, godoc 문장 참조 등)로.
   - 정전 앵커 유지: send/publish/request/reply는 "성공 submit 시 message를 consume(소유권 이전)" 계약을 send 빌더 한 곳에 두고 참조. copy-semantics는 Message 생성/From 계열 한 곳에.
4. **검증(지어내기 금지)**: 각 계약 문구는 L의 runtime 코드를 읽어 실제 동작과 일치하는지 확인한 뒤 쓴다.
   불확실하면 dotnet 대응 구현 + L runtime을 교차 확인. 확인 불가하면 약하게(모호한 단정 금지).
5. **빌드/문서/테스트 게이트(모두 그린이어야 완료)**:
   - L 빌드 0 warning / 0 error (doc 경고 포함).
   - doc 생성 가능하면 생성해 깨진 참조 0 확인.
   - L의 contract 테스트 전부 통과(주석 전용이므로 동작 회귀 없어야 함; flaky는 격리 재실행으로 구분).

## 리뷰 루프 (언어 L 1차 완료 후 — 핵심 요구사항)
- **A. 전수 리뷰**: L의 모든 변경 파일을 다시 읽으며 점검 — 코드와 불일치/거짓 주석, 재진술, placeholder 잔재, 검증 안 된 단정, 단위/null/에러 누락, No-duplication 위반, 빌드/doc 경고.
- **B. 수정**: 발견된 이슈를 고친다.
- **C. 반복**: 이슈가 하나라도 있었으면 A로 돌아가 다시 전수 리뷰.
- **D. 종료 조건**: "이슈 0"인 리뷰가 나올 때까지 A–C를 반복.
- **E. 다음으로**: 이슈 0 확인 + 게이트(빌드/doc/테스트) 그린이면 → L만 별도 커밋 (`docs(<lang>): canonicalize binding contract comments to POSD policy`) → 다음 언어로 진행 → 진행표 + `project_posd_comment_pass` 메모리 갱신.

## 산출/보고 규칙
- 매 언어마다: 무엇을 이식했는지, 리뷰 라운드 수, 게이트 결과를 간단히 보고.
- **절대 한 언어가 게이트·리뷰 0이슈를 통과하기 전에 다음 언어로 넘어가지 않는다.**
- 작업은 main에서. 측정 불필요(주석 전용). 커밋은 언어별로 분리.

---

## 진행표 (체크하며 진행)

상태 범례: ⬜ 미시작 · 🟦 진행중 · ✅ 완료

| 언어 | 매핑 | 이식 | 게이트(빌드/doc/테스트) | 리뷰 루프(이슈0) | 커밋 | 상태 |
|---|---|---|---|---|---|---|
| **dotnet** (기준) | — | — | ✅ 0/0, doc 1109, 테스트 169/170 | ✅ | ✅ `8bd034e60` | ✅ 완료 |
| **1. Rust** | ✅ | ✅ 28파일 전체 | ✅ doc 0/0, build clean, tests 16/16 | ✅ R1회 | ✅ `ab14c1fc2` | ✅ 완료 |
| **2. Python** | ✅ | ✅ 전체 contracts | ✅ compile/import OK, tests 68/68 | ✅ R1회 | ✅ `344354beb` | ✅ 완료 |
| **3. Node** | ✅ | ✅ 전체 contracts | ✅ tsc 0, typecheck 0 | ✅ R1회 | ✅ `11752fce7` | ✅ 완료 |
| **4. Go** | ✅ | ✅ 6파일 contracts/ | ✅ build clean, tests 16/16, doc 0/0 | ✅ R1회 | ✅ `9fdb9c838` | ✅ 완료 |
| **5. Java** | ✅ | ✅ 155파일 contracts/ | ✅ build clean, javadoc 0, tests 86/86 | ✅ R1회 | ✅ `c567b3d87` | ✅ 완료 |
| **6. C/C++ 헤더** | ✅ | ✅ 36 hpp + 13 h | ✅ build 0/0, tests 19/19 | ✅ R1회 | ✅ `f4d2f8467` | ✅ 완료 |

> 리뷰 루프 칸의 `R<n>회` = 이슈 0이 나오기까지 돈 전수 리뷰 라운드 수(기록용).

### 언어별 contracts 매핑 메모 (작업 중 채움)
- **Rust** `bindings/rust/src/contracts/` (28파일) ↔ dotnet `Contracts/`
- **Python** `bindings/python/src/zlink/contracts/` ↔ dotnet `Contracts/`
- **Node** `bindings/node/src/zlink/contracts/` ↔ dotnet `Contracts/`
- **Go** `bindings/go/contracts/` (8파일) ↔ dotnet `Contracts/`
- **Java** `bindings/java/src/main/java/systems/zlink/` ↔ dotnet `Contracts/`
- **C/C++** `bindings/c/include/zlink/*.h`, `bindings/cpp/include/*.hpp` ↔ dotnet `Contracts/`
