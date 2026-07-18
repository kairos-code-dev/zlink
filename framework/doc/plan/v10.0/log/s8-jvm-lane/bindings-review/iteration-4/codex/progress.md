# S8 JVM bindings 리뷰 iteration 4 — R1 (opus, codex 슬롯) progress

정적 대조만. 빌드·실행·수정 없음. R2·coordinator 해석을 판정 근거로 쓰지 않음.

## Scope
- 대상 commit `7403cb5c9` (freeze HEAD `41660e37b`의 부모). 파일 수 **251** — 일치.
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum) =
  `fa9c1de94a291e3dad452c648896eee9620ad7815050ea6a91dd096ab07837ff` — **일치**.
- 시작·종료 파일 수·hash 동일. 파일 미수정.

## 진행
- [x] Scope hash/파일수 확인
- [x] iter-3 JV3-1 해소 확인 (SERVICE_EVENT_LAYOUT + 22 offset 제거)
- [x] iter-3 R2 low(optionalDowncall) 해소 확인
- [x] 제거로 인한 새 orphan 부재 확인 (import·helper·인접 layout)
- [x] 제거·부재 심볼 게이트 독립 재계산 (178 used ∩ 119 removed = 0)
- [x] 인접 service 계층(NativeServiceSymbols/ServiceLayouts) live 확인
- [x] I1 FFI arity 전수 대조 (subagent 병렬 sweep)
- [x] I2 POSD/DDD
- [x] I3 dead-code / no-hit

## 확인된 사실
- 7403cb5c9 diff = 삭제 55줄 3파일(Native.java −4, NativeLayouts.java −45, NativeSymbols.java −6).
  기능 코드 추가 0. 순수 dead-code 제거.
- SERVICE_EVENT / service_kind / subject_kind: java src/samples/kt 전역 참조 0
  (잔여 매치는 scope 외 resources/native prebuilt .so/.dylib 바이너리 뿐).
- optionalDowncall: 참조 0.
- 삭제 후 인접 심볼(MONITOR_EVENT_LAYOUT+offsets, ACTOR_REF_LAYOUT) 정상 상주·소비.
  NativeSymbols.java 8개 import 전부 여전히 사용. 새 unused import/helper 없음.
- 삭제 자리에 이중 공백 라인 3곳(Native.java, NativeSymbols.java, NativeLayouts.java) —
  순수 whitespace, 식별자·dead code 아님(정보성만, finding 아님).
- I1 arity 전수: 187 handle(185 downcall + 2 upcall) 불일치 0. 넓은 시그니처·_CRITICAL·
  upcall·bridge 심볼 a=b=c 확인. 제거심볼 게이트 0 hit.

## 판정
- I1 CLEAN / I2 CLEAN / I3 CLEAN (세 축 blocker·high·medium 0, low 0).
- 종료 scope hash fa9c1de9... 동일. → BINDINGS REVIEW CLEAN.
