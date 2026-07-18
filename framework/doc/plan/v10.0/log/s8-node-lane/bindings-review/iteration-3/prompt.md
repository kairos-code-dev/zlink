# RouteMesh 10.0.0 S8 NODE bindings 전환 리뷰 — iteration 3 공통 prompt

너는 S8 NODE bindings 전환 리뷰 iteration 3의 독립 리뷰어다. byte 단위 동일 prompt(R1·R2). 다른 리뷰어 결과·coordinator 해석을 판정 근거로 쓰지 마라.

## Snapshot
- 대상 commit: `bc409293a` (iter-1·iter-2 finding 수정 반영)
- Scope: `git ls-files bindings/node/src bindings/node/native/src bindings/node/samples bindings/node/binding.gyp bindings/node/package.json` 중 `/build/`·`node_modules`·`prebuilds` 제외
- 파일 수: 140
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `4c772436d48795958da6e8cdf8e071962f716b9d33c391a1874c382892ccfdf6`
- 시작·종료 파일수·hash 확인·기록, 파일 수정 금지.

## 우선 검증: iter-1·iter-2 finding 해소
- iter-1(`../iteration-1/finding-ledger.ko.md`): NF1 wire enum·NF2 RouterSocket.sendToSpot·NF3 kind_data·NF4 ready-handler·NF5 close-busy·NF6 count·NF7 transfer.
- iter-2(R1 opus 발견, `../iteration-2/codex/review.ko.md`): dead stream bind-actor 선언(binding_socket.ts) 제거·result enum 최신 Core 코드 추가(RequestResult.Backpressured=113·RecvResult 207/208·ConnectResult 608·ConfigResult 707/708/709)·MonitorSourceKind를 Core의 SOCKET=1만으로 축소.
소스 대조로 해소 판정. 해소된 finding은 새 반례 없이 재개 금지.

## 전체 scope 재검토(3축)
I1 계약 일치(enum 값 Core 정확 일치·pull dispatch 수명·transfer·kind_data·raw-layer 드리프트), I2 POSD·DDD, I3 정리(폐기 no-hit·dead code·미등록 native 선언).

## 절차
산출물 progress.md·review.ko.md만. build/실행 금지(정적 대조). 실행 증거는 manifest(addon node-gyp green·tsc src+samples green·no-hit 0)만. iteration 3: 각 축 CLEAN=finding 0.

## 출력
1. Scope 확인 2. iter-1·iter-2 해소 판정 3. I1/I2/I3 Finding·Evidence·Verdict 4. 폐기 no-hit 판정 5. 마지막 줄 정확히 `BINDINGS REVIEW CLEAN` 또는 `BINDINGS REVIEW NOT CLEAN`.
