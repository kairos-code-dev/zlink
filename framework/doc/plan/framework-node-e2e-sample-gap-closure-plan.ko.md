# Node Framework E2E/Sample 문서 갭 제거 계획

## 목적

이 문서는 Node 담당 에이전트가 공통 framework e2e 문서와 공통 sample 문서에 적힌 내용을
`framework/languages/node`에 빠짐없이 구현하도록 안내한다.

E2E는 공통 e2e 문서의 모든 scenario를 Node public framework 표면으로 검증하는 것이 목표다. Sample은
공통 sample 문서를 계약 기준으로 삼고, `.NET` sample 구현을 포팅 기준으로 삼아 Node sample을 같은
사용자 체감 동작으로 맞춘다.

기존 계획은 아래 문서를 따른다.

- `framework/doc/plan/framework-node-e2e-dotnet-porting-plan.ko.md`
- `framework/doc/plan/framework-node-sample-dotnet-porting-plan.ko.md`
- `framework/doc/plan/framework-ref-target-unification-plan.ko.md`
- `framework/doc/plan/framework-ref-target-unification-node-worker-prompt.ko.md`

이 문서는 위 계획들을 한 작업 흐름으로 묶는 완료 계획이다.

## 담당 범위

- E2E 대상: `framework/languages/node/e2e/`
- Sample 대상: `framework/languages/node/samples/`
- Node framework 대상: `framework/languages/node/packages/`
- Node 검증 대상: `framework/languages/node/test/`, `framework/languages/node/package.json`
- 공통 E2E 기준: `framework/doc/framework/common/e2e/`
- 공통 sample 기준: `framework/doc/framework/common/sample/`
- `.NET` E2E 기준 구현: `framework/languages/dotnet/e2e/`
- `.NET` sample 기준 구현: `framework/languages/dotnet/samples/`

Core 성능 작업과 충돌하지 않도록 `core/`는 수정하지 않는다. Node framework에서 발견한 문제가 core
버그로 의심되면 Node만 우회하지 말고, C++/Java/Kotlin 또는 바인딩 수준에서 같은 현상이 재현되는지
확인한 뒤 버그 리포트로 분리한다.

## ActorRef / SpotRef 전송 대상 통일 포함 범위

Node E2E/Sample 갭 제거 중 actor/spot 전송 API가 드러나면
`framework/doc/plan/framework-ref-target-unification-node-worker-prompt.ko.md`의 내용을 같은 작업 범위에
포함한다. 이 작업은 sample이나 E2E만 통과시키는 표면 조정이 아니라 Node framework public contract
정리다.

적용 기준:

- actor 전송 대상은 `ActorRef`, spot 전송 대상은 `SpotRef`로 맞춘다.
- actor id 또는 spot id만 받아 메시지를 보내는 public API는 완료 표면으로 인정하지 않는다.
- id는 조회 입력이고 ref는 전송 입력이다. 조회 API는 id를 받을 수 있지만, 전송 API는 이미 얻은 ref를
  받아야 한다.
- `ZLinkSpotAddress` 계열 이름은 `SpotRef` 계열 이름으로 바꾼다.
- `IZLinkSpotAddressResolver`, `resolveSpotAddress`, `resolveActorSpotAddress` 계열 이름은
  `ZLinkSpotRefResolver`, `resolveSpotRef`, `resolveActorSpotRef` 의미로 정리한다.
- `ZLinkSpotRemoteAddress` / `ZLinkSpotRemoteAddressResolver` 같은 route bridge 구현 정보는 일반
  application guide, sample, E2E 표준 사용 예시에서 제거한다. 내부 또는 advanced routing extension으로
  남겨야 하면 public 메시징 표면과 분리해 문서화한다.
- `JoinSpot(spotRid, ...)`처럼 lifecycle id 입력이 남아야 하는 API는 일반 메시징 API가 아니라 actor
  이동/admission workflow임을 문서와 예제에서 구분한다.

이 변경은 E2E/Sample 구현 전에 모두 끝내야 한다는 뜻은 아니다. 다만 Node sample/E2E를 새로 고치거나
누락을 닫을 때, 제거 대상 id-only 메시징 API나 `SpotAddress` 표면을 새 예시로 남기지 않는다. 기존
sample/E2E가 그런 표면을 쓰고 있으면 ref 기반 호출로 함께 고친다.

필수 코드 확인 대상:

```text
framework/languages/node/packages/framework/src/contracts/Actors/ZLinkActorClient.ts
framework/languages/node/packages/framework/src/contracts/Common/ActorRef.ts
framework/languages/node/packages/framework/src/contracts/Locations/Resolvers.ts
framework/languages/node/packages/framework/src/contracts/Locations/Rows.ts
framework/languages/node/packages/framework/src/contracts/Spots/Contracts.ts
framework/languages/node/packages/framework/src/contracts/Spots/SpotRoutingContracts.ts
framework/languages/node/packages/framework/src/runtime/actors/actor-client.ts
framework/languages/node/packages/framework/src/runtime/actors/index.ts
framework/languages/node/packages/framework/src/runtime/channels/index.ts
framework/languages/node/packages/framework/src/runtime/locations/index.ts
framework/languages/node/packages/framework/src/runtime/spots/index.ts
framework/languages/node/packages/framework/src/runtime/streams/index.ts
```

필수 테스트 확인 대상:

```text
framework/languages/node/test/contract/contract-surface.test.js
framework/languages/node/test/contract/actor-manager.test.js
framework/languages/node/test/contract/location-runtime.test.js
framework/languages/node/test/contract/entry-spot-dispatch.test.js
framework/languages/node/test/contract/stream-runtime.test.js
```

문서 변경은 코드와 테스트를 바꾸는 같은 작업 안에서 처리한다. 최소 대상은 아래와 같다.

```text
framework/doc/contract-inventory/framework-public-contract-inventory.json
framework/doc/framework/common
framework/doc/framework/node
```

## 메시지 핸들러 등록 정책 포함 범위

Node E2E/Sample 갭 제거 중 handler 등록 표면을 고치거나 새 sample/E2E handler를 추가할 때는
`framework/doc/framework/common/spec/framework-api.ko.md`의 메시지 핸들러 정책을 같은 작업 범위에
포함한다. 특히 `framework-api.ko.md`의 `3.3 Handler 등록 정책`은 Node/NestJS sample과 E2E의 handler
등록 방식이 따라야 하는 공통 기준이다.

적용 기준:

- handler 등록 호출부가 packet 이름, actor 타입, request/send 종류처럼 handler 타입에서 알 수 있는
  정보를 반복해서 받지 않도록 한다.
- 수동 등록은 실행 문맥의 구성 단계에서 이뤄져야 한다. Node에서는 channel handler는 application
  startup/channel builder, session handler는 session 구성, Entry Spot과 user Spot handler는 각 Spot
  구성 문맥에 둔다.
- Spot 메시지 handler는 actor request/send, Spot packet, subscription 책임을 handler 타입과 metadata로
  드러내고, 등록 표면은 가능한 한 `AddHandler<THandler>()`와 같은 단일 의미로 유지한다. Node에서는
  같은 원칙을 decorator/module/provider 등록 표면에 맞춰 적용한다.
- subscription topic처럼 handler interface만으로 알 수 없는 값은 handler metadata, decorator, annotation
  또는 언어별 metadata 선언에 둔다. 등록 호출의 반복 인자로 숨기지 않는다.
- timer는 메시지 dispatch handler가 아니므로 메시지 handler 등록 정책으로 우회하지 않는다. timer 이름,
  주기, overrun 정책처럼 실행 계획에 속한 값은 별도 timer 등록 표면에서 다룬다.
- 자동 등록과 수동 등록이 같은 dispatch key를 만들면 startup validation 오류로 처리한다. 조용히
  덮어쓰거나 수동 등록이 자동 등록을 대신하게 만들지 않는다.
- C++처럼 reflection scan을 전제로 하지 않는 언어 예외는 Node에 그대로 가져오지 않는다. Node/NestJS는
  module/package scan과 decorator/provider metadata를 쓸 수 있으므로 공통 자동 등록 원칙을 따라야 한다.
- sample 등록 방식은 의도적으로 하나만 예외를 둔다. `TicTacToe.Ts`는 manual registration 사용 예시로
  남기고, `Bingo.Ts`, `DeliveryDispatch.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`, `SupportChat.Ts`는 모두
  automatic registration만 사용하도록 정리한다. 이 구분은 사용자가 두 등록 방식을 모두 볼 수 있게 하기
  위한 문서화된 sample 정책이며, 개별 sample 통과를 위한 임시 우회로 쓰지 않는다.

이 정책은 sample이나 E2E를 통과시키기 위한 구조 검사 회피가 아니라 public 사용 예시의 품질 기준이다.
새 sample/E2E가 handler 책임을 shared helper, raw frame adapter, test-only registry로 밀어내면 완료로
인정하지 않는다.

## 버그 처리 원칙

작업 중 버그가 드러나면 scenario나 sample만 통과시키는 우회 코드를 넣지 않는다. 실패 로그, 재현
절차, 영향을 받는 언어와 계층을 먼저 확인하고, 원인이 Node framework, binding, connector, e2e/sample
harness 중 어디에 있는지 좁힌다.

실제 버그로 확인되면 가능한 범위에서 먼저 회귀테스트를 작성하거나 같은 변경에 포함한다. 그 다음 원인
계층에서 버그를 수정하고, 회귀테스트와 해당 e2e/sample runner를 다시 실행한 뒤 원래 작업을 계속
진행한다. 버그 수정 없이 `sleep`, retry-only wrapper, private helper, raw frame 조작, test-only adapter,
sample 코드 변경으로 실패를 숨기지 않는다.

## 완료와 gap 처리 원칙

이 계획의 목표는 문서와 구현 사이의 gap을 없애는 것이다. `partial`이나 `gap` 표기는 작업 중 상태를
보이게 하기 위한 임시 표시일 뿐 완료 판정이 아니다.

공통 e2e 문서나 공통 sample 문서가 요구하는 공개 동작인데 Node에서 바로 구현할 수 없으면, 먼저
`feature-map.ko.md`나 `sample-porting-inventory.ko.md`에 이유를 적고 설계 이슈로 분리한다. 그 뒤
필요한 spec/guide/draft 검토와 public API 설계를 거쳐 다시 구현해야 한다. 설계 이슈로 분리했다는
사실만으로 이 계획을 완료 처리하지 않는다.

## E2E 구현 절차

1. `framework/doc/framework/common/e2e/README.ko.md`와 `config-1`부터 `config-9`까지 모든 문서를
   읽고 scenario ID를 표로 만든다.
2. 각 config마다 `.NET` 기준 구현과 `.NET` `feature-map.ko.md`를 읽는다.
3. Node의 `porting-inventory.ko.md`와 `feature-map.ko.md`를 먼저 갱신한다.
   - 공통 문서의 scenario ID를 모두 행으로 둔다.
   - 공통 문서의 scenario 상태는 `implemented`, `partial`, `gap` 중 하나로 적는다.
   - `partial`과 `gap`은 이유, 필요한 public API, 막힌 계층을 함께 적는다.
   - `.NET` 파일이나 기존 Node 파일을 inventory에서 매핑할 때만 `merged`, `stale`, `not needed` 같은
     보조 상태를 쓸 수 있다. 공통 scenario 자체를 `not applicable`로 닫지 않는다.
4. Node public framework API로 구현 가능한 항목은 실제 역할 프로세스, runner, scenario evidence까지
   구현한다.
5. public API가 없어 구현할 수 없는 항목은 private helper, raw frame, 테스트 전용 adapter로 우회하지
   않는다. 문서에 gap으로 남기고 설계 검토 항목으로 분리한다.
6. 각 config의 `run_e2e.sh`는 standalone으로 실행 가능해야 하며, 성공 시 명확한 최종 pass marker를
   출력해야 한다.
7. config 하나가 끝날 때마다 build, typecheck, test, runner, feature-map, inventory를 맞춘 뒤 다음
   config로 넘어간다.

필수 config 목록:

- `LocationMessaging` 또는 Node에서 같은 의미로 명명된 registry/location messaging config
- `SpotService`
- `PubSub`
- `RegistrationCodec`
- `ResilienceLifecycle`
- `StoreFailure` 또는 Node에서 같은 의미로 명명된 store failure/recovery config
- `RuntimeMonitoring`
- `YieldDispatch`
- `ToActorMessaging`

현재 트리에 `.NET` 기준 이름과 다른 `RegistryMessaging`, `DiscoveryRegistryHa` 같은 디렉터리가 있으면
바로 삭제하거나 완료로 인정하지 않는다. 먼저 공통 config 문서와 `.NET` config에 어느 scenario가
대응되는지 inventory에 매핑하고, 중복·stale·rename 대상 여부를 리뷰로 확인한 뒤 정리한다.

## Sample 포팅 절차

1. `framework/doc/framework/common/sample/README.ko.md`와 sample별 문서를 모두 읽는다.
   - event sample은 `framework/doc/framework/common/sample/event/*.ko.md`도 함께 읽는다.
2. `.NET` sample 6종의 실제 코드, runner, README를 읽고 Node sample에 대응시킨다.
3. 각 Node sample에 `sample-porting-inventory.ko.md`를 유지한다.
   - `.NET`의 역할, shared contract, client self-check, runner evidence를 빠짐없이 매핑한다.
   - Node/NestJS idiom 때문에 파일명이나 decorator 구조가 달라도 책임은 누락하지 않는다.
4. Sample 코드는 사용자가 따라 할 public API 예시다. 내부 helper, raw buffer 처리, codec 수동 우회,
   테스트 전용 hook을 sample 코드에 넣지 않는다.
5. `Shared/`에는 contracts와 shared configuration만 둔다. 서버 역할 구현이나 test helper를 shared로
   밀어 넣어 구조 검사를 우회하지 않는다.
6. 각 sample의 `run_sample.sh`는 standalone으로 실행 가능해야 하며, client success뿐 아니라 서버 역할
   로그와 scenario evidence도 확인해야 한다.
7. 모든 sample이 끝난 뒤 Node sample 전체 runner와 sample regression test를 실행한다.

필수 sample 목록:

- `TicTacToe.Ts`
- `Bingo.Ts`
- `DeliveryDispatch.Ts`
- `SupportChat.Ts`
- `GameQuest.Ts`
- `ShoppingMall.Ts`

## 검증 명령

담당 에이전트는 실제 checkout 상태에 맞게 script 이름과 package script를 확인한 뒤 실행한다.

```bash
cd framework/languages/node
npm run build
npm run typecheck
npm test
npm run verify:samples
for f in e2e/*/run_e2e.sh; do timeout 420s "$f"; done

rg -n "ZLinkSpotAddress|resolveSpotAddress|resolveActorSpotAddress|sendToActor\\([^)]*actorId|requestToActor\\([^)]*actorId|sendToSpot\\([^)]*spotRid|requestToSpot\\([^)]*spotRid" \
  . \
  -S -g '!**/node_modules/**'

rg -n "SpotAddress|spot address|SpotRemoteAddress|spot remote address|sendToActor\\([^)]*actorId|requestToActor\\([^)]*actorId|sendToSpot\\([^)]*spotRid|requestToSpot\\([^)]*spotRid" \
  ../../doc/contract-inventory ../../doc/framework/common ../../doc/framework/node \
  -S -g '!../../doc/plan/**' -g '!../../doc/**/draft/**'
```

실행 환경이나 port 충돌 때문에 전체 루프가 실패하면 실패 config/sample을 먼저 단독 재현하고, 단독 pass
후 전체 루프를 다시 실행한다.

## 현재 적용 및 검증 기록

2026-07-08 Node 작업에서 확인한 내용은 아래와 같다.

- retry로 local 실패를 숨기지 않는다. `ResilienceLifecycle` `RL-D5`는 실패 메시지에 invalid reply 내용을
  포함하도록 진단만 강화했고, 판정 조건은 그대로 유지했다.
- `SpotService` `SM-D6`는 같은 actor id를 재사용하지 않도록 bound actor와 shadow actor id를 분리하고,
  shadow stream을 먼저 인증한 뒤 bound stream push 경로를 검증한다. 이는 재시도가 아니라 scenario가
  의도한 bound-only push 대상과 unbound 관찰 대상을 분명하게 만드는 변경이다.
- `DeliveryDispatch.Ts` runner는 Redis container를 `docker create`와 `docker start`로 시작한다. Docker
  시작 지점의 exit code와 container 상태를 확인하므로 container 시작 실패를 background 실행으로 숨기지
  않는다.
- `DeliveryDispatch.Ts` cleanup은 background server가 `SIGSEGV`로 종료되면 runner 실패로 올린다. 정상
  종료 신호와 cleanup용 강제 종료는 기존처럼 cleanup 절차로 처리하지만, native crash는 sample pass로
  감추지 않는다.
- `DeliveryDispatch.Ts`의 `probe` role은 단순히 `topology=ready`를 출력하지 않는다. Redis location
  store의 peer row와 owner lease를 확인해 sample topology가 준비됐는지 검증한 뒤 success marker를
  출력한다. sample business handler의 channel request retry는 제거했다.
- `ToActorMessaging`은 `porting-inventory.ko.md`에 TA-A1~TA-B3를 모두 `implemented`로 매핑한다. runner는
  고정 `sleep`으로 route 전파를 기다리지 않고 Redis location store에서 `to-actor` SpotMesh row와 owner
  lease를 확인한 뒤 client를 실행한다.
- `YieldDispatch`는 retry로 실패를 숨기지 않도록 session role의 Spot relay retry loop를 제거했다.
  YD-E3 shutdown 경로는 `core/v8.6.3` 릴리스와 Node native binding 갱신 뒤 다시 검증했다.
  `timeout 420s ./run_e2e.sh YD-E3`가 `logs/20260708-141507-206725`에서
  `yield-dispatch shutdown wait result=passed`, `yield-dispatch shutdown recovery result=passed`,
  `scenario YD-E3 passed`, `yield-dispatch e2e result=passed`를 출력했다. 실행 중 `session-a`가
  한동안 높은 CPU를 사용했지만 runner timeout이나 retry로 성공을 만든 것이 아니라 graceful shutdown이
  정상 종료되며 pass marker가 출력되었다. 이 CPU 관찰은 별도 성능/종료 시간 개선 후보로 남긴다.
- Node framework receive loop의 CPU 사용량 조사를 시작했다. channel/Spot idle drain 경로에 짧은 idle
  backoff를 넣었고, YD-E3의 client-visible shutdown/recovery 판정은 core 8.6.3 기반에서 통과했다.
  다만 `YieldDispatch`의 Session 역할은 play shutdown 뒤 종료될 때까지 높은 CPU를 보일 수 있어,
  pass 판정과 별개로 종료 시간/idle drain 개선 후보로 남긴다.
- `TicTacToe.Ts`의 guest leave marker 실패는 Node framework의 remote actor one-way relay와 Spot handler
  leave workflow 문제로 분리했다. bound remote actor `Send` frame은 reply를 기다리는 route request가
  아니라 route command로 보내도록 바꾸고, route command 수신 쪽도 actor packet relay를 처리하게 했다.
  또한 Spot actor handler 안에서 `leaveActor()`가 Entry Spot rejoin을 기다릴 때 현재 Spot serial turn을
  붙잡지 않도록 내부 `joinEntrySpot(...).yield()` 경로를 사용한다. 이 수정은 sample sleep/retry가 아니라
  one-way send와 actor lifecycle의 framework 책임을 맞춘 것이다.
- Node top-level sample runner는 sample 실패를 retry로 가리지 않고 첫 실패에서 멈춘다. 실패 지점이
  조용히 사라지지 않도록 각 sample 시작, 완료, 실패 status를 출력한다. `sample-regression.test.js`는
  이 runner에 retry wrapper가 없는지 검사하고, `run_samples.sh`가 모든 maintained sample self-check를
  실행하는지 확인한다.
- `channel-client.test.js`의 `DSC-008` scale-out/scale-in 검증은 retry로 덮지 않고 runtime cleanup 경계를
  수정했다. receive loop가 실제 `run()` 종료를 기다리도록 했고, auto-connect loop stop에 runtime abort
  signal을 전달했으며, framework stop에서 backend context shutdown을 먼저 호출해 native poll을 깨우도록
  했다. shutdown 뒤 socket cleanup에서 발생하는 context-terminated 오류는 dispose idempotency로만
  처리한다. `DSC-008` cleanup은 traffic client를 provider보다 먼저 종료하고 provider stop을 병렬로
  모은다. 최종 상태에서 `npm run build && timeout 180s node --test test/contract/channel-client.test.js`가
  54개 테스트 모두 pass했다.

현재 checkout에서 실행한 검증:

```bash
cd framework/languages/node
npm run build
node --test test/contract/entry-spot-serial-dispatch.test.js test/contract/stream-runtime.test.js
timeout 180s samples/TicTacToe.Ts/run_sample.sh
timeout 420s samples/run_samples.sh
npm run typecheck -- --pretty false
npm test
npm run verify:samples
for f in e2e/*/run_e2e.sh; do timeout 420s "$f"; done
timeout 420s e2e/SpotService/run_e2e.sh
timeout 420s e2e/ResilienceLifecycle/run_e2e.sh
timeout 420s e2e/ToActorMessaging/run_e2e.sh
timeout 240s samples/DeliveryDispatch.Ts/run_sample.sh
node --test test/contract/sample-regression.test.js
```

2026-07-08 추가 확인에서 `npm run build`, `npm run typecheck -- --pretty false`,
`node --test test/contract/entry-spot-serial-dispatch.test.js test/contract/stream-runtime.test.js`,
`timeout 180s samples/TicTacToe.Ts/run_sample.sh`, `timeout 420s samples/run_samples.sh`,
`node --test test/contract/sample-regression.test.js`가 pass했다. `verify:samples` 중 한 번
`DeliveryDispatch.Ts` PASS 이후 cleanup 시점의 `Segmentation fault` 출력이 있었으나, 단독 재현과 전체
재실행에서는 재현되지 않았다. 이 출력은 성공으로 묻지 않고 `DeliveryDispatch.Ts` runner가 `SIGSEGV`를
실패로 올리도록 보강했다. 이후 `core/v8.6.3`과 Node native binding 갱신 뒤 YD-E3를 다시 실행해
현재 checkout의 완료 증거로 갱신했다.

2026-07-08 이후 추가 확인:

```bash
cd framework/languages/node
npm run build
timeout 60s node --test --test-name-pattern 'DSC-008' test/contract/channel-client.test.js
timeout 90s node --test --test-name-pattern 'REG-003|CH-002|DSC-008' test/contract/channel-client.test.js
npm run build && timeout 180s node --test test/contract/channel-client.test.js
cd e2e/YieldDispatch && timeout 420s ./run_e2e.sh YD-E3
```

위 `channel-client` focused 실행과 전체 실행은 모두 pass했다. YD-E3 선택 실행도 pass했으며, 실행 중
`session-a`의 높은 CPU 사용은 별도 종료 시간/idle drain 개선 후보로 남긴다.

2026-07-08 추가 마감 검증:

```bash
cd framework/languages/node
node --test test/contract/sample-regression.test.js
npm run typecheck -- --pretty false
npm run verify:ci
timeout 420s e2e/SpotService/run_e2e.sh
set -euo pipefail
for dir in e2e/SpotService e2e/DiscoveryRegistryHa; do
  timeout 420s "${dir}/run_e2e.sh"
done
set -euo pipefail
for dir in e2e/RegistryMessaging e2e/YieldDispatch e2e/SpotService; do
  timeout 420s "${dir}/run_e2e.sh"
done
npm run build
npm run typecheck -- --pretty false
```

위 실행은 모두 pass했다. `sample-regression.test.js`는 44개 테스트가 모두 pass했고,
`node run_samples.sh executes every sample self-check`도 skip 없이 실제 `samples/run_samples.sh`를
실행했다. `npm run verify:ci`에서도 같은 self-check가 skip 없이 실행되어 pass했다. 이 때문에 Node
framework CI gate가 sample 전체 self-check를 환경변수로 건너뛰던 문제는 닫혔다.

2026-07-08 최종 정리에서는 `scripts/local-package/README.ko.md`의 변경된 배포 정책도 함께 확인했다.
Node framework root `package.json`은 `.artifacts/wsl/npm/zlink-systems-zlink-8.6.3.tgz`를 참조하고,
설치된 `@zlink-systems/zlink` binding version도 `8.6.3`으로 확인했다. 따라서 이번 Node 검증은 core
`8.6.3` native library가 반영된 local npm package 기준으로 진행했다.

`SpotService`, `DiscoveryRegistryHa`, `RegistryMessaging`, `ResilienceLifecycle`, `PubSub`,
`RegistrationCodec`, `RuntimeMonitoring`, `YieldDispatch`의 E2E evidence 저장소는
`framework/languages/node/e2e/evidence-store.js`와 `evidence-store.d.ts`로 모았다. 각 E2E 가족 root의
`evidence-store.ts`는 이 공통 구현을 re-export한다. 각 package의 `tsconfig.json`이 `rootDir`을 자기 E2E
가족 디렉터리로 제한하므로, source import는 package 경계를 넘지 않는다. runtime에서는 runner가
`ZLINK_NODE_E2E_ROOT`를 Node E2E root로 넘기고, 직접 실행처럼 환경 변수가 없으면 현재 작업 디렉터리의
`e2e` 하위 경로를 fallback으로 사용해 공통 JS 모듈을 찾는다. 역할별
`Server/*/Infrastructure/evidence-store.ts`는 같은 가족 root의 re-export를 다시 내보낸다.

`SpotService`, `DiscoveryRegistryHa`, `ResilienceLifecycle` 일부 E2E endpoint/spot handler 안에 있던
broad catch 기반 retry는 제거했다. handler는 준비된 대상에 대해 한 번 public framework 호출을 수행하고,
실패하면 원래 오류를 그대로 드러낸다. `RegistryMessaging` `RM-B1`, `RM-B2`, `RM-C7`은 reply payload 값이
아니라 실제 request value를 provider별 evidence wait 기준으로 사용하도록 고쳤다. 이 변경은 timeout을
늘리는 retry가 아니라 scenario oracle이 handler evidence 형식과 같은 값을 보게 하는 수정이다.

`DiscoveryRegistryHa` runner는 `SF-A2`, `SF-D1`, `SF-D2`, `SF-D3`에서 client가 출력하는
`scenario-control ...` marker를 기다린 뒤 provider 시작, provider 종료, Redis pause/unpause, Redis stop을
수행한다. 고정 대기 뒤 장애를 주입하지 않고, client가 실제로 해당 단계에 들어간 것을 marker로 확인한 뒤
runner가 제어한다.

`ToActorMessaging`은 `TA-A1`부터 `TA-B3`까지 scenario별 client 함수와 runner 선택 실행을 지원한다.
`run_e2e.sh TA-B2`처럼 공통 scenario ID를 첫 인자로 주면 해당 scenario만 실행하고, 인자를 주지 않으면
전체 scenario를 순서대로 실행한다. `feature-map.ko.md`도 각 공통 ID를 `implemented` 상태와 runner 증거
열로 매핑했다.

POSD/DDD 리뷰에서 지적된 구조 문제도 같은 마감 작업에서 정리했다. `Bingo.Ts` business handler에 있던
startup route retry는 제거했고, runner의 Redis/location readiness 확인 뒤 handler는 public framework
호출을 한 번만 수행한다. `@zlink-systems/nestjs`는 `framework/dist/internal` 전체 barrel 대신
`framework/dist/nest-integration`의 좁은 integration module만 로드한다. stream connector에는
기존 `submit<TReply>()`가 plain object request reply decoding을 맡게 했고, raw encoded 응답이 필요한
테스트는 `submitEncoded()`를 명시적으로 사용하게 했다. 별도 `submitDecoded<TReply>()` public API는
남기지 않았다. `SpotService`, `YieldDispatch`, `ToActorMessaging` E2E client의 수동 decode helper도
제거했다.
`ShoppingMall.Ts` Commerce API application layer는 `OrderWorkflowRouterPort`에만 의존하고, ZLink 구현은
`Infrastructure/ZLink`에서 Nest provider로 바인딩한다.

여러 E2E runner에 반복되던 port allocation, package build, readiness polling, process start, Redis cleanup,
실패 로그 tail 정책은 `framework/languages/node/e2e/runner-common.sh`로 모았다. `SpotService`의
background role status 판정처럼 scenario별 의미가 있는 분기는 각 runner에 남겼고, 공통 lifecycle helper만
공유한다.

2026-07-08 추가 정리에서는 local package 정책과 최신 runtime 수정 뒤의 검증 상태를 다시 분리했다.
`ZLinkAsyncSubmitter`는 callback 기반 request를 socket별로 하나씩만 outstanding 상태로 둔다. 이 수정은
`ResilienceLifecycle` `RL-D1` high fanout 실행에서 handler는 모두 request를 처리했지만 한 channel reply가
JSON payload 대신 제어 문자처럼 보이는 값으로 돌아오던 문제를 retry 없이 framework 제출 순서에서 닫기
위한 것이다. `channel-client.test.js`에는 두 request가 같은 dealer socket에서 동시에 제출되지 않는
계약 테스트를 추가했다.

`SpotService` `SM-D12`는 첫 stream close 뒤 `entry-disconnected` evidence를 확인한 다음 다음 session의
bind/transfer를 진행한다. 이는 실패 후 재시도나 고정 sleep이 아니라, 앞선 close lifecycle이 완료됐다는
관찰 가능한 evidence를 scenario 단계 경계로 사용하는 수정이다.

`ResilienceLifecycle` `RL-A1`은 provider B shutdown 뒤 topology에서 `api-b`가 빠진 것을 확인한 다음
surviving provider 검증 request를 보낸다. 이 대기는 실패한 request를 다시 보내는 retry가 아니라, 종료된
provider가 location routing 대상에서 사라졌다는 관찰 가능한 상태 전이를 scenario 단계 경계로 삼는
수정이다.

최신 수정 뒤 현재 checkout에서 아래 명령이 pass했다.

```bash
cd framework/languages/node
npm run build
npm run typecheck -- --pretty false
node --test test/contract/channel-client.test.js
npm run verify:samples
timeout 420s e2e/ResilienceLifecycle/run_e2e.sh
timeout 1200s e2e/SpotService/run_e2e.sh
timeout 420s e2e/ToActorMessaging/run_e2e.sh
timeout 420s e2e/YieldDispatch/run_e2e.sh
```

그 직전 전체 E2E sweep에서는 `DiscoveryRegistryHa`, `PubSub`, `RegistrationCodec`, `RegistryMessaging`,
`ResilienceLifecycle`, `RuntimeMonitoring`이 pass했고, 이후 `SpotService`의 `SM-D12` lifecycle 경계 수정
뒤 `SpotService` 전체, `ToActorMessaging` 전체, `YieldDispatch` 전체를 다시 pass했다. `RegistryMessaging`
runner를 해당 디렉터리에서 직접 실행하는 `timeout 420s ./run_e2e.sh RM-A1`도 pass해
`ZLINK_NODE_E2E_ROOT` fallback 경로를 확인했다.

최신 수정 뒤 전체 `npm test` runtime gate는 공유 머신 부하가 높은 상태에서 한 번
`channel-client.test.js` 진행 중 출력이 멈춰 완료 증거로 사용하지 않았다. 이 실행은 중단한 뒤 같은 파일을
단독 실행해 `node --test test/contract/channel-client.test.js` 55개 테스트가 모두 pass하는 것을 확인했다.
이후 `npm test`를 다시 실행해 runtime gate 전체가 끝까지 pass했고, 이 최신 실행을 완료 증거로 삼는다.

`npm run verify:samples`는 actor lifecycle sample gate, `TicTacToe.Ts`, `Bingo.Ts`,
`DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`를 모두 pass했다. `SpotService`
전체 runner는 한 번 `SM-G2` 시작 시점에 Node 프로세스가 `SIGSEGV`로 종료됐지만, core dump는 발견되지
않았고 `SM-G2` 단독 실행과 `SpotService` 전체 재실행에서는 재현되지 않았다. 이 현상은 배포 패키지
해석 문제로 단정하지 않고, 재현되면 native startup crash로 별도 추적한다.

2026-07-09 재실행에서는 이전에 미완료로 남겼던 Node runtime gate가 끝까지 pass했다.

```bash
cd framework/languages/node
npm run build && npm run typecheck -- --pretty false && npm test
npm run verify:samples
```

`npm test`는 `channel-client.test.js` 55개 테스트, `sample-regression.test.js` 44개 테스트를 포함해
runtime gate 전체를 완료했다. Redis가 없는 환경에서 `location-redis-store.test.js`의 Redis 연결 테스트
2개는 기존 skip 조건대로 skip됐고, 실패는 없었다. `npm run verify:samples`는 actor lifecycle sample
gate와 `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`,
`ShoppingMall.Ts` 전체 self-check를 다시 pass했다.

같은 날 POSD/DDD 재리뷰 후 남은 두 구조 항목도 정리했다. Nest route mesh builder는 동일한 client
연결 동작을 `connect(...)` 하나로 표현하도록 하고, route mesh 전용 `enableClient(...)` 별칭을 제거했다.
stream connector는 raw encoded reply를 원하는 테스트만 `submitEncoded()`를 쓰게 하고, 일반
`submit<T>()`가 typed JSON decode를 담당한다. 또한 `Bingo.Ts`에서 드러난 종료 race는 retry나 sleep으로
덮지 않고 Node framework의 bound session send 계약을 고쳤다. `ZLinkBoundSessionSendCall.submit(...)`은
`Promise<void>`를 반환하고, 반환된 Promise는 framework가 현재 bound session route에 push frame을 넘긴
뒤 완료된다. client application 처리 완료까지 보장하지는 않지만, actor leave/destroy처럼 같은 handler
turn 안의 후속 lifecycle 정리와는 순서를 맞출 수 있다.

이 수정 뒤 순차 전체 gate를 다시 실행했다.

```bash
cd framework/languages/node
npm run build && npm run typecheck -- --pretty false && npm test && npm run verify:samples
```

이 순차 실행은 build, typecheck, `npm test`, `verify:samples`를 모두 pass했다. `verify:samples`는
actor lifecycle sample gate, `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`,
`GameQuest.Ts`, `ShoppingMall.Ts` 전체 self-check를 통과했다. focused 계약 확인으로
`node --test test/contract/stream-runtime.test.js test/contract/nestjs-module.test.js`와
`node --test test/contract/contract-surface.test.js`도 pass했다.

`RL-A1` topology 경계 수정 뒤에도 같은 gate를 다시 실행해 최신 코드 기준으로 pass를 확인했다.

```bash
cd framework/languages/node
npm run build && npm run typecheck -- --pretty false && npm test
```

같은 날 전체 E2E runner도 최신 코드 기준으로 다시 실행했다.

```bash
cd framework/languages/node
set -euo pipefail
for d in DiscoveryRegistryHa PubSub RegistrationCodec RegistryMessaging ResilienceLifecycle RuntimeMonitoring SpotService ToActorMessaging YieldDispatch; do
  timeout 1200s "e2e/$d/run_e2e.sh"
done
```

첫 sweep에서는 `DiscoveryRegistryHa`, `PubSub`, `RegistrationCodec`, `RegistryMessaging`이 pass한 뒤
`ResilienceLifecycle` `RL-A1`의 provider B shutdown 직후 surviving provider 검증 단계에서
`ZLink async submit timed out`이 발생했다. 로그는 첫 request가 `api-a`에서 reply를 받았고, 다음 request가
provider까지 도달하지 못한 상태를 보였다. `RL-A1`에 topology 이탈 확인을 추가한 뒤
`timeout 420s e2e/ResilienceLifecycle/run_e2e.sh RL-A1`이 pass했고, 이어서
`ResilienceLifecycle`, `RuntimeMonitoring`, `SpotService`, `ToActorMessaging`, `YieldDispatch` 전체
runner가 pass했다. 최종 pass 로그는 2026-07-09 실행의 각 runner log directory에 남아 있다.

현재 남은 종료 조건은 read-only 누락 리뷰와 POSD/DDD 재리뷰다. 누락 리뷰가 `NO MISSING NODE ITEMS`,
POSD/DDD 리뷰가 `NO POSD/DDD NODE REFACTOR ITEMS`를 반환해야 이 계획을 완료로 닫는다.

2026-07-09 read-only 누락 재리뷰는 `NO MISSING NODE ITEMS`를 반환했다. 리뷰는 config-1~9 scenario ID의
Node feature-map/runner 매핑, `ToActorMessaging` scenario 선택 실행, sample 6종과 handler 등록 정책,
ActorRef/SpotRef 통일 grep, 최신 build/typecheck/test/sample/E2E 검증 기록을 확인했다.

2026-07-09 배포 정책 확인에서는 `scripts/local-package/README.ko.md`의 현재 정책대로 Node framework가
bindings source를 직접 참조하지 않고 명시 버전의 local npm tarball을 참조하는지 확인했다. Node root
`package.json`의 `@zlink-systems/zlink` 값은
`file:../../../.artifacts/wsl/npm/zlink-systems-zlink-8.6.3.tgz`이고, 실제 설치된 package version도
`8.6.3`이다. 따라서 이후에 본 실패는 배포 package 해석 문제가 아니라 runtime 또는 E2E 경계 문제로
분리해 처리했다.

`SpotService` runner에서 `SM-F6` 뒤 `SM-G2`를 시작할 때 한 번 Node native `SIGSEGV`가 관찰됐다. 단독
`SM-G2`는 반복 통과했고, `default-batch -> SM-F6 -> SM-G2` 순서에서 재현됐다. 실패를 retry로 덮지 않고
role shutdown 경계를 수정했다. runner cleanup은 각 역할의 `/shutdown` endpoint를 먼저 호출하고, 짧은
graceful window 동안 종료를 기다린 뒤에만 signal cleanup으로 넘어간다. 이 수정 뒤
`default-batch -> SM-F6 -> SM-G2` 순서와 `timeout 1200s e2e/SpotService/run_e2e.sh` 전체가 pass했다.

`SpotService` `SM-C4`는 새 Spot subscription 직후 gateway publish를 바로 검증하면 core pub/sub
subscription 전파 경계에서 message evidence가 늦게 보일 수 있었다. 실패한 publish를 재시도하지 않고,
play-a에서 같은 Spot에 local publish를 한 번 수행해 새 Spot subscription이 실제로 topic event를 받을 수
있다는 observable readiness marker를 확인한 다음 gateway publish 검증을 진행한다. 이는 scenario 단계
경계이며, gateway publish 실패를 숨기는 retry가 아니다.

`SpotService` `SM-D6`에서 같은 stream routing id가 disconnect cleanup 완료 전에 재사용되면 Node stream
session runtime이 disconnect가 시작된 기존 session을 새 packet에 재사용할 수 있었다. 그 결과 auth reply
후에도 local actor binding이 이미 정리된 session에서 `ActorPushReq`가 dispatch되어
`No actor is bound for packet 'ActorPushReq'.`가 발생했다. `ZLinkStreamSessionNodeRuntime`은 disconnected
session을 새 packet/connection 대상으로 재사용하지 않고 새 session context를 만들며, 이전 session의
cleanup은 map에 여전히 자신이 들어 있을 때만 제거하도록 고쳤다. 회귀 테스트
`stream node runtime does not reuse a disconnected session for the same routing id`를 추가했다.

이 수정 뒤 최신 Node 검증은 아래와 같다.

```bash
cd framework/languages/node
npm run build
node --test test/contract/stream-runtime.test.js
timeout 420s e2e/SpotService/run_e2e.sh default-batch
timeout 1200s e2e/SpotService/run_e2e.sh
set -euo pipefail
for d in ToActorMessaging YieldDispatch; do
  timeout 1200s "e2e/$d/run_e2e.sh"
done
npm run typecheck -- --pretty false
npm run build
node --test --test-name-pattern 'REG-003' test/contract/channel-client.test.js
timeout 240s node --test test/contract/channel-client.test.js
npm test
npm run verify:samples
```

위 명령은 모두 pass했다. `npm test`는 `channel-client.test.js` 55개, `sample-regression.test.js` 44개,
새 stream runtime 회귀 테스트를 포함해 pass했다. `npm run verify:samples`는 actor lifecycle sample gate,
`TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`
전체 self-check를 pass했다.

POSD/DDD 재리뷰에서 두 구조 문제가 추가로 발견되어 수정했다. `ZLinkFrameworkRuntimeHost`의 remote
bound-session forwarding 경로 두 곳이 `ZLinkBoundSessionSendCall.submit()`이 반환하는 `Promise<void>`를
기다리지 않아, public send 계약과 달리 routed/native delivery가 끝나기 전에 성공을 보고할 수 있었다.
두 경로 모두 `await call.submit()`으로 바꾸고, `stream-runtime.test.js`의 remote receiver 테스트가 route
promise를 수동으로 풀기 전에는 caller-visible operation이 완료되지 않는지 확인하도록 보강했다.

`SpotService` runner의 전역 `sleep 5` route-settle 대기도 제거했다. full topology에서는 session role의
public HTTP control route probe로 `session-a/session-b -> play-a/play-b` route가 실제 request/reply 가능한지
확인한 뒤 client를 시작한다. `SM-F6`, `SM-Q9`처럼 multi-node만 띄우는 child는 scenario 자체의 public
request가 준비 상태를 검증하게 두고, 전역 wall-clock delay를 넣지 않는다.

TicTacToe sample runner는 ready event를 기다릴 때 role process가 먼저 종료되면 즉시 실패하도록 고쳤고,
공유 머신 부하가 높아도 살아 있는 process의 startup ready event를 관찰할 수 있게 readiness budget을
60초로 조정했다. 이는 실패한 sample 동작을 재시도하는 것이 아니라, role process의 실제 ready event 또는
조기 종료를 관찰하는 runner 경계 보강이다.

위 구조 수정 뒤 최신 검증은 아래와 같다.

```bash
cd framework/languages/node
npm run build
node --test test/contract/stream-runtime.test.js
timeout 420s e2e/SpotService/run_e2e.sh default-batch
timeout 1200s e2e/SpotService/run_e2e.sh
npm run typecheck -- --pretty false
npm run build
npm test
timeout 180s samples/TicTacToe.Ts/run_sample.sh
npm run verify:samples
node --test test/contract/sample-regression.test.js
```

위 실행은 모두 pass했다. `npm test`는 host bound-session await 보강 테스트와 stream RID 재사용 회귀
테스트를 포함해 pass했다. `SpotService` 전체 runner는 전역 sleep 제거 뒤에도 pass했고,
`verify:samples`와 `sample-regression.test.js`는 TicTacToe ready 대기 보강 뒤 pass했다.

2026-07-09 POSD/DDD 재리뷰는 `NO POSD/DDD NODE REFACTOR ITEMS`를 반환했다. 리뷰는 host
bound-session forwarding await, stream-runtime await 회귀 테스트, SpotService route readiness probe,
TicTacToe ready/liveness runner 보강을 read-only로 확인했고 파일을 수정하지 않았다.

2026-07-09 최종 검증 중 `npm run verify:samples`가 한 번 `TicTacToe.Ts`의 `api-b-channel` readiness에서
실패했다. `api-b.log`가 비어 있었고 단독 실행은 통과했으므로 sample 동작 재시도가 아니라 runner 감시
경계를 보강했다. API role도 Play role처럼 ready JSON과 process 생존을 함께 확인하고, port 대기 중 role
process가 먼저 종료되면 즉시 실패하도록 바꿨다. 이후 `samples/TicTacToe.Ts/run_sample.sh`,
`npm run verify:samples`, `npm test`가 모두 pass했다.

같은 최종 E2E sweep에서 `SpotService` `SM-G2` child가 `multi-node-b` HTTP readiness timeout으로 한 번
멈췄다. 실패 log는 대부분 0바이트였고 scenario body에 들어가기 전 role bootstrap readiness에서 끝났다.
`SpotService` runner의 role readiness budget은 10초였기 때문에, sample runner와 같은 60초 budget으로
맞춰 startup readiness를 관찰하도록 조정했다. 이는 실패한 scenario를 다시 보내는 retry가 아니라 role이
ready가 되거나 조기 종료되는지를 기다리는 runner 경계다. 이후 아래 명령이 pass했다.

```bash
cd framework/languages/node
samples/TicTacToe.Ts/run_sample.sh
npm run verify:samples
npm test
timeout 420s e2e/SpotService/run_e2e.sh SM-G2
timeout 1200s e2e/SpotService/run_e2e.sh
timeout 1200s e2e/ToActorMessaging/run_e2e.sh
timeout 1200s e2e/YieldDispatch/run_e2e.sh
```

최종 E2E loop에서 `DiscoveryRegistryHa`, `PubSub`, `RegistrationCodec`, `RegistryMessaging`,
`ResilienceLifecycle`, `RuntimeMonitoring`은 이미 pass한 뒤 `SpotService`에서 위 readiness timeout을
발견했다. readiness 보강 뒤 실패 지점인 `SpotService` 전체와 중단 뒤 남은 `ToActorMessaging`,
`YieldDispatch`가 모두 pass했으므로 전체 E2E runner 검증을 닫는다.

현재 종료 조건은 모두 충족됐다. read-only 누락 리뷰는 `NO MISSING NODE ITEMS`, POSD/DDD 재리뷰는
`NO POSD/DDD NODE REFACTOR ITEMS`로 닫혔다.

## 완료 전 누락 리뷰

구현 담당 에이전트가 완료를 주장하기 전에 별도 Codex 에이전트로 read-only 리뷰를 요청한다.

리뷰 요청은 아래 범위를 포함해야 한다.

- 공통 e2e 문서의 모든 scenario ID가 Node `feature-map.ko.md`와 runner evidence에 존재하는지
- 공통 sample 문서의 모든 역할, 메시지 흐름, self-check가 Node sample inventory와 runner evidence에
  존재하는지
- `.NET` 기준 구현에 있는 역할과 client 검증이 Node에서 누락되지 않았는지
- public contract gap을 private helper나 테스트 전용 adapter로 숨기지 않았는지
- `ActorRef` / `SpotRef` 전송 대상 통일 계획의 Node 제거 대상 API와 이름이 public contract, sample,
  guide에 남지 않았는지
- `framework-api.ko.md`의 메시지 핸들러 등록 정책을 Node/NestJS sample과 E2E handler 등록 표면이
  따르는지
- `TicTacToe.Ts`를 제외한 Node sample이 handler manual registration을 사용하지 않고 automatic
  registration만 사용하는지
- `run_e2e.sh`, `run_sample.sh`, `verify:samples`, `npm test` 결과가 실제로 pass했는지

리뷰 결과가 `NO MISSING NODE ITEMS`가 아니면 모든 finding을 수정하고 같은 리뷰를 다시 요청한다.

## POSD/DDD 반복 리뷰

누락 리뷰가 깨끗해진 뒤에만 별도 Codex 에이전트로 POSD/DDD 리뷰를 요청한다. 이 리뷰는 동작 누락이
아니라 구조 개선 가능성만 본다.

리뷰 기준:

- public API가 shallow wrapper로 늘어나지 않았는지
- codec, transport, registry, location store, actor/session lifecycle 같은 지식이 호출자나 sample로
  새어나오지 않았는지
- domain role과 NestJS/module infrastructure 책임이 섞이지 않았는지
- handler, runtime, runner, sample 사이에 같은 정책이 반복 구현되지 않았는지
- Node idiom을 따르면서도 `.NET` 기준 domain 흐름과 같은 의미를 유지하는지

의미 있는 refactoring finding이 나오면 구현, 테스트, 문서 갱신을 한 뒤 E2E/sample 검증과 POSD/DDD
리뷰를 다시 실행한다. 리뷰가 `NO POSD/DDD NODE REFACTOR ITEMS`를 반환할 때 종료한다.

## 최종 종료 조건

- 모든 공통 E2E scenario가 Node에서 `implemented`로 남아 있다.
- 공통 sample 문서와 `.NET` sample 기준에 대해 Node sample gap이 없다.
- `framework-ref-target-unification-node-worker-prompt.ko.md`의 Node public contract 정리가 반영되어,
  actor/spot 메시징 표면이 `ActorRef` / `SpotRef` 기반으로 통일되어 있다.
- `framework-api.ko.md`의 메시지 핸들러 등록 정책이 Node public contract, sample, E2E handler 등록
  표면에 반영되어 있다.
- Node sample의 handler 등록 정책이 정리되어 있다. `TicTacToe.Ts`만 manual registration 예시를 유지하고,
  나머지 sample은 automatic registration만 사용한다.
- `partial` 또는 `gap`으로 남은 E2E/sample 항목이 없다. public contract 설계가 필요한 항목이 있으면
  이 계획은 완료가 아니라 blocked 상태로 남긴다.
- 모든 Node E2E runner와 sample runner가 pass했다.
- 누락 리뷰가 `NO MISSING NODE ITEMS`를 반환했다.
- POSD/DDD 반복 리뷰가 `NO POSD/DDD NODE REFACTOR ITEMS`를 반환했다.
