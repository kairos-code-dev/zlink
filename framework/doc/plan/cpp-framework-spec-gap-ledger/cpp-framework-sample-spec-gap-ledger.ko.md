# C++ Framework sample spec gap audit ledger

> 상태: 미완료. C++ Framework production contract와 process E2E 선행 조건을 닫은 뒤에만 이 문서의
> sample gap을 완료한다.
>
> 기준 시점: 2026-08-02 working tree. 공통 sample 문서와 C++ source에 사용자 변경이 함께 있으므로,
> 각 구현을 시작할 때 source·문서 기준 commit과 package manifest를 다시 고정한다.
>
> 이 문서는 public API 계약이 아니다. 공통 sample 문서와 C++ exact interface에 없는 기능을 다른
> 언어 구현이나 E2E 문서만으로 C++ public API에 추가하지 않는다.

## 1. 목적과 완료 조건

이 ledger의 목적은 C++ sample 디렉토리와 실행 파일이 존재하는지 확인하는 데 있지 않다. 공통
Framework sample의 message·field·transport·상태 전이와 C++ `Shared/Contracts`, server role,
client scenario, runner의 실제 호출 경로를 대조한다. 또한 client가 실제 role server를 호출하는지,
role server가 owner·generation·cleanup 결과를 기록하는지, runner가 그 결과를 같은 실행에서 확인하는지
판정한다.

C++ 공통 sample의 현재 계약 범위는 `Bingo`, `TicTacToe`, `SupportChat`, `DeliveryDispatch`,
`ShoppingMall`, `GameQuest` 6종이다. 공통 sample index는 `ZoneWorld`를 .NET과 Node.js 범위로
구분하므로 C++에 ZoneWorld public API나 sample을 추가하는 gap으로 판정하지 않는다. C++에서
ZoneWorld를 요구하려면 별도의 공통 범위·계약 review가 먼저 필요하다.

다음 조건을 모두 만족해야 이 ledger를 완료로 표시한다.

1. 6개 sample의 공통 message inventory를 C++ shared contract와 한 행씩 대조하고, 이름·field 타입,
   optional/null, request/reply·one-way send·publish·notify 의미를 exact하게 고정한다.
2. 공통 문서에 없는 C++ message와 field를 `internal-only`, `test/evidence-only`, `contract 선행`
   가운데 하나로 분류한다. 분류되지 않은 extra public message가 0개여야 한다.
3. C++ sample client가 `zlink::http_client`, `zlink::stream_connector`와 exact public interface만
   사용하고, private header, raw frame, Core C ABI, Framework 내부 ABI, symbol lookup 또는 test-only
   adapter를 업무 경로에 사용하지 않는다.
4. `Client → Shared → Server` 경계와 `Domain → Application → Infrastructure` 책임이 공통 sample
   계약과 일치한다. `NodeRid`, endpoint, session route, `ActorRef`와 connection id를 client-visible
   업무 message에 넣지 않는다.
5. shell·PowerShell individual runner와 aggregate runner가 같은 6개 sample inventory, 선택 방식,
   readiness, 전용 Redis, client self-check, role-server evidence, process cleanup을 제공한다.
6. sample별 process 실행에서 client-visible 결과와 role-server evidence를 함께 확인한다. 성공 로그,
   source type 존재, static parity test만으로 sample 완료를 표시하지 않는다.
7. CMake sample target, install/export, package version, local package provenance와 clean consumer가
   같은 C++ Framework package를 사용한다. Debug/Release와 다른 build cache가 섞이지 않는다.
8. regression test와 CI path가 6개 sample의 exact inventory와 process gate를 강제하고, `부분`,
   `diagnostic_only`, `source-only`, `historical log` 항목을 aggregate 성공으로 계산하지 않는다.

이 문서는 C++ Framework spec gap ledger의 후속 문서다. 전체 C++ audit는 다음 순서를 따른다.

```text
S0  cpp-framework-spec-gap-ledger.ko.md
    public contract -> runtime -> package/ABI -> common E2E
    -> production regression -> process E2E
                         |
                         v
S1  cpp-framework-sample-spec-gap-ledger.ko.md
    sample contract -> role path -> runner -> sample regression
    -> six-sample process evidence
                         |
                         v
S2  C++ audit closure
    두 ledger의 unresolved gap과 blocker가 모두 0일 때만 완료
```

S0가 닫히기 전에는 S1의 sample code를 Framework gap의 우회 수단으로 고치지 않는다. S1이
완료되지 않은 상태에서 S0를 최종 완료로 표시하지 않는다.

## 2. 조사 범위와 authoritative source

### 2.1 계약과 sample 기준

| 구분 | authoritative source | 확인 내용 |
|---|---|---|
| 공통 sample index | `framework/doc/framework/common/sample/README.ko.md:1-38` | C++ 지원 6종, ZoneWorld의 언어 범위, codec·handler 등록 방식 |
| 공통 sample 규칙 | `framework/doc/framework/common/sample/README.ko.md:280-322,375-449,524-550` | manual topology 금지, C++ compile-time registration, Redis 격리, runner와 client self-check |
| Bingo | `framework/doc/framework/common/sample/bingo/README.ko.md` | Protobuf message, matching, Actor/Spot lifecycle, reward publish와 cleanup |
| TicTacToe | `framework/doc/framework/common/sample/tictactoe/README.ko.md` | HTTP create, manual endpoint scale-out, `LeaveGameMsg`, timeout, actor destroy |
| SupportChat | `framework/doc/framework/common/sample/supportchat/README.ko.md` | conversation state, `SetTypingMsg`, reconnect, idle/close와 bound push |
| DeliveryDispatch | `framework/doc/framework/common/sample/deliverydispatch/README.ko.md` | `attempt`, optional/null, `occurredAtUnixMs`, offer/decision/status 순서 |
| ShoppingMall | `framework/doc/framework/common/sample/event/shoppingmall.ko.md` | `OrderState`, workflow command, event stream, compensation과 projection rebuild |
| GameQuest | `framework/doc/framework/common/sample/event/gamequest.ko.md` | typed JSON `GameplayMsg`, `ClosePlayerQuestMsg`, quest projection과 reconcile |
| 언어별 정확한 표현 | `framework/doc/framework/common/spec/server/languages/cpp/`, `http-client/languages/cpp/`, `stream-connector/languages/cpp/` | C++ public header, value/reference/move, callback, error와 lifetime |
| 공통 E2E | `framework/doc/framework/common/e2e/`와 `README.ko.md` | sample이 사용하는 Framework 동작의 scenario ID와 process evidence 기준 |

공통 E2E 문서는 sample에 새 public API를 추가하는 근거가 아니다. E2E가 요구하는 동작을 현재
C++ public surface로 표현할 수 없는 경우 S0의 contract gap 또는 별도 contract 선행으로 보낸다.

### 2.2 C++ 구현과 검증 자료

| 구분 | C++ 경로 | 확인 내용 |
|---|---|---|
| sample source | `framework/languages/cpp/samples/{Bingo,DeliveryDispatch,GameQuest,ShoppingMall,SupportChat,TicTacToe}/` | `Client`, `Shared/Contracts`, server role, Domain/Application/Infrastructure와 실제 호출 |
| sample build/export | `framework/languages/cpp/CMakeLists.txt:1850-2060,618-760` | executable, CTest smoke, install header, CMake target와 package export |
| aggregate shell | `framework/languages/cpp/samples/run_samples.sh` | sample selector, retry, 순서와 최종 결과 표식 |
| aggregate PowerShell | `framework/languages/cpp/samples/run_samples.ps1` | Windows sample inventory와 shell parity |
| individual runner | 각 sample의 `run_sample.sh`, 존재하는 `run_sample.ps1` | build, Redis, role readiness, client self-check, role evidence와 cleanup |
| C++ sample guide | `framework/languages/cpp/samples/README.ko.md` | 사용자에게 보이는 sample 범위, version, 실행 방법 |
| porting inventory | 각 sample의 `sample-porting-inventory.ko.md`, `DeliveryDispatch/feature-map.ko.md` | 참고용 매핑. `done`과 과거 PASS는 현재 완료 evidence가 아님 |
| static contract test | `framework/languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_sample_parity.cpp` | 일부 message·field·source structure의 정적 gate |
| layout/target test | `test_cpp_framework_layout_contract.cpp`, `test_cpp_framework_target_contract.cpp` | public sample tree, target와 private/raw route 금지 |
| package test | `framework/languages/cpp/scripts/verify_packaged_contract.sh`, `test_cpp_framework_install_consumer` | install tree와 clean consumer |
| version policy | `scripts/local-package/README.ko.md`, `framework/languages/cpp/CMakeLists.txt:41-87` | C++ package와 Core package pin, local package 사용 방식 |

### 2.3 판정 용어와 ID

- `충족(정적)`: source, declaration 또는 좁은 test가 조건을 확인했지만 process·package 전체를
  증명하지는 않는다.
- `gap`: 공통 sample 계약과 C++ 실제 경로가 다르거나, 완료를 직접 판정하는 gate가 없다.
- `contract 선행`: public message 범위, exact C++ type 또는 package ownership을 먼저 확정해야 한다.
- `historical`: 이전 branch·log·snapshot의 결과다. 현재 working tree의 완료 evidence가 아니다.
- `N/A`: 현재 공통 문서가 C++에 요구하지 않는 범위다. 새 API를 추가하지 않고 이유와 재검토 조건을
  기록한다.

이 문서의 ID는 C++ Framework ledger의 ID와 충돌하지 않도록 `CPP-SAMPLE-*` 접두사를 사용한다.
`CPP-SAMPLE-IMP-*`는 sample production source·contract gap, `CPP-SAMPLE-E2E-IMP-*`는 sample
runner/process evidence gap, `CPP-SAMPLE-TEST-*`는 기존 audit gate gap, `CPP-SAMPLE-REG-*`는
새로 추가하거나 변경할 회귀 test다.

## 3. 현재 검증 결과

### 3.1 working tree와 진행 log

조사 시작 시점의 working tree에는 다음 사용자 변경이 함께 있었다. 이 sample audit은 이 파일들을
수정하지 않는다.

```text
 M framework/doc/framework/common/sample/bingo/README.ko.md
 M framework/doc/framework/common/sample/deliverydispatch/README.ko.md
 M framework/doc/framework/common/sample/event/gamequest.ko.md
 M framework/doc/framework/common/sample/event/shoppingmall.ko.md
 M framework/doc/framework/common/sample/supportchat/README.ko.md
 M framework/doc/framework/common/sample/tictactoe/README.ko.md
 M framework/doc/framework/common/sample/zoneworld/README.ko.md
```

최종 status 재확인에서는 `framework/languages/cpp/framework/src/runtime/http/http_listener.cpp`가
더 이상 working-tree diff로 표시되지 않았고 현재 source와 `HEAD`가 일치했다. audit은 이 source를
수정하거나 이전 상태로 되돌리지 않았다. 공통 sample 문서의 사용자 변경과 다른 언어의 사용자 변경은
현재 tree에 남아 있으므로 계속 보호한다.

실제 조사와 검증을 수행한 단계는 [`log/2026-08-02-progress.log`](log/2026-08-02-progress.log)에
각 단계가 끝난 직후 기록한다. 이 log는 사후에 command를 모은 snapshot이 아니며, source/test/
runner/spec의 실행 결과를 대신하지 않는다.

### 3.2 새로 실행한 검증

| 실행 | 현재 결과 | sample audit 해석 |
|---|---|---|
| `ctest --test-dir framework/languages/cpp/build-v11-tests -R 'test_cpp_framework_sample_parity\|test_cpp_framework_target_contract\|test_cpp_framework_layout_contract\|test_cpp_framework_install_consumer' --output-on-failure --timeout 30` | 4/4 passed, exit 0, 14.04초 | static parity/layout/target와 install consumer의 좁은 조건만 증명한다. |
| `timeout 120s framework/languages/cpp/samples/TicTacToe/run_sample.sh` | preflight 3/3 passed 뒤 exit 1 | 실제 process에서 `invalid application payload version`, `Connection reset by peer`, process status 134가 발생했다. sample 완료가 아니다. |
| `timeout 120s framework/languages/cpp/samples/run_samples.sh --max-attempts=1` | 첫 `TicTacToe` 실패로 exit 1 | aggregate가 뒤의 5개 sample을 실행하지 못했다. `sample all result=passed`는 출력되지 않았다. |
| `cmake --build ... --target zlink_framework zlink_http_client zlink_stream_connector -j2` | current `build-v11-tests`에서 성공 | sample process가 같은 package provenance를 사용했다는 뜻은 아니다. |
| `framework/languages/cpp/scripts/verify_packaged_contract.sh framework/languages/cpp/build-v11-tests` | 실패 | install manifest에서 `include/zlink/framework/contracts/locations/spot_handle.hpp`가 누락되었다. S0 package gap과 연결한다. |
| 전체 CTest와 C++ E2E aggregate | 각각 41/49 passed, 8 failed / bounded exit 124 | S0 ledger의 current blocker다. sample completion으로 승격하지 않는다. |

위 process 실행은 sample runner 기본값인 `framework/languages/cpp/build`를 사용했다. 이 cache는
현재 pinned `build-v11-tests`와 다른 Debug/package version provenance를 가질 수 있으므로, 실패 원인을
sample source 하나로 단정하지 않는다. 같은 package를 clean하게 고정한 뒤 process gate를 재실행한다.

### 3.3 현재 inventory 관찰

- shell aggregate에는 6개 runner가 등록되어 있다: `TicTacToe`, `Bingo`, `DeliveryDispatch`,
  `SupportChat`, `GameQuest`, `ShoppingMall`.
- PowerShell aggregate `run_samples.ps1`는 현재 `TicTacToe/run_sample.ps1`과
  `Bingo/run_sample.ps1`만 호출한다. 나머지 4개 sample에는 PowerShell individual runner가 없다.
- CMake에는 6개 sample의 role/client target과 6개 shell smoke test가 등록되어 있다. C++ common
  sample 범위 자체는 정적 기준에서 맞지만, aggregate OS parity는 맞지 않는다.
- 기존 `sample-porting-inventory.ko.md`의 `done`, 과거 `PASS`와 `sample all result=passed`는 현재
  process evidence가 아니다. 특히 current `TicTacToe` process 실패 때문에 historical status를
  완료로 사용할 수 없다.

## 4. 현재 충족 판정

전체 sample 완료가 아니라 현재 source와 좁은 gate에서 확인된 조건만 표시한다.

| 항목 | 판정 | 근거와 제한 |
|---|---|---|
| C++ sample 지원 범위 | 충족(정적) | common sample index의 C++ 6종과 CMake의 6종 target이 대응한다. PowerShell aggregate와 process evidence는 별도 gap이다. |
| ZoneWorld 범위 | N/A | common index가 .NET/Node.js 제공 범위로 명시한다. C++ public API·sample을 다른 언어만으로 추가하지 않는다. |
| C++ compile-time handler registration | 충족(정적) | C++ sample host가 compile-time registration을 사용하고 common 기준도 이를 허용한다. 모든 handler invocation과 lifecycle semantics는 process gate가 필요하다. |
| public client package boundary | 충족(정적) | C++ client source에서 `zlink::http_client`, `zlink::stream_connector`, `zlink::stream_e2e_client` 경계를 사용한다. package export와 process runtime은 실패/미완료다. |
| sample static parity | 충족(정적) | `test_cpp_framework_sample_parity`가 current `build-v11-tests`와 default `build`에서 각각 좁은 source/contract gate로 통과했다. 전체 message inventory는 비교하지 않는다. |
| role별 executable 분리 | 충족(정적) | CMake와 각 runner가 client와 role executable을 분리한다. 실제 owner, generation, callback count와 cleanup은 process evidence가 없다. |
| 전용 Redis와 cleanup 구현 | 충족(정적) | individual shell runner와 Redis helper에 scoped container·cleanup 경로가 있다. current runner가 process failure 때 모든 역할 evidence를 통과했다는 뜻은 아니다. |
| 공통 message exact parity | 미충족 | `SetTypingReq`, `LeaveGameReq`, Delivery timestamp type, GameQuest payload와 ShoppingMall response shape의 차이가 확인됐다. |
| aggregate sample completion | 미충족 | current `TicTacToe` process가 payload version 오류로 abort했고 6개 전체를 실행하지 못했다. |

## 5. `CPP-SAMPLE-IMP-*` production implementation gap

### CPP-SAMPLE-IMP-001 — 공통 message·field·transport inventory가 C++ shared contract와 다름

- 상태: `gap`, 일부 항목은 `contract 선행`.
- 공통 spec/E2E 근거: `framework/doc/framework/common/sample/README.ko.md:204-228`,
  `framework/doc/framework/common/sample/tictactoe/README.ko.md:170-324`,
  `supportchat/README.ko.md:170-315`, `deliverydispatch/README.ko.md:193-320`,
  `event/gamequest.ko.md:174-277`, `event/shoppingmall.ko.md:234-350`,
  관련 `framework/doc/framework/common/e2e/` scenario 문서.
- C++ 경로: `samples/TicTacToe/Shared/Contracts/messages.hpp:117-236`,
  `samples/SupportChat/Shared/Contracts/messages.hpp:104-286`,
  `samples/DeliveryDispatch/Shared/Contracts/messages.hpp:24-177`,
  `samples/GameQuest/Shared/Contracts/messages.hpp:30-275`,
  `samples/ShoppingMall/Shared/Contracts/messages.hpp:120-335`,
  `samples/Bingo/Shared/Contracts/messages.hpp:60-316`와 각 sample client/server handler.
- 확인한 실제 동작과 기대 동작:
  - SupportChat C++는 `OpenConversationApiRes`에 `conversationId`와 `status`를 두고,
    `ConversationCreateReq`에 공통 `createdAtUnixMs`가 없다. 공통 계약은 state 전체와 생성 시각을
    요구한다. C++는 `SetTypingReq`로 등록하지만 공통 one-way message 이름은 `SetTypingMsg`다.
    `JoinConversationFailedNotify`에는 공통에 없는 `isRetriable`도 있다.
  - TicTacToe C++는 `CreateGameHttpReq.game_name`을 비 optional 값으로 표현하고,
    `CreateGameHttpRes`에 공통 계약에 없는 `owner_play_endpoint`를 추가한다. 공통 one-way send는
    `LeaveGameMsg`인데 C++ packet은 `LeaveGameReq`이며, `JoinGameFailedNotify` declaration도
    현재 shared contract에서 확인되지 않는다.
  - DeliveryDispatch C++는 `occurred_at`과 optional이어야 하는 `courier_id`, `reason`을
    `std::string`으로 표현한다. 공통은 `occurredAtUnixMs: int64`와 null 허용을 요구한다.
  - GameQuest C++의 `JoinSessionRes`에는 `playerId`가 없고, push message에
    `targetConnectionId`가 들어간다. 공통은 current session binding이 target을 정하고 이 field를
    client contract에 노출하지 않는다. 공통 `ClosePlayerQuestMsg`는 현재 C++ packet declaration에서
    확인되지 않는다.
  - ShoppingMall C++의 `StartOrderRes`는 `orderId`와 `status`만 반환하지만 공통은 `state: OrderState`
    전체를 반환한다. `StartOrderWorkflowReq`에는 공통 `sourceCommandId`가 없고, `OrderState`의
    optional field를 빈 문자열로 표현한다.
  - Bingo는 Protobuf message 이름과 주요 field가 대부분 대응하지만 `BingoRoomState.lastDrawnNumber`
    같은 optional field의 C++ value 표현과 `EnsurePlayerActorRes` 같은 extra internal message를
    public/shared inventory에서 구분해야 한다.
- gap 판정 근거: 공통 문서의 wire 이름·type·호출 방식과 current C++ declaration이 직접 다르다.
  Static parity test가 일부 sample surface를 검사해도 6개 전체 declaration과 serialized optional/null
  결과를 비교하지 않으므로 충족으로 볼 수 없다.
- 구체적인 수정 목록:
  1. 공통 문서의 각 message를 `Client`, `server-internal`, `domain event`, `test/evidence-only`로
     분류하고 C++ `messages.hpp`의 packet declaration을 한 inventory로 정리한다.
  2. 공통 이름·field·optional/null·int64 표현과 one-way/request/reply 호출 방식을 exact하게 맞춘다.
  3. 공통에 없는 extra message는 public wire로 유지하지 말고 internal contract 또는 evidence DTO로
     이동한다. 공통 계약 변경이 필요하면 구현 전에 문서 review를 받는다.
  4. generated/typed serializer가 동일한 JSON key와 null omission을 출력하도록 고정한다. 호출부에
     별도 encode/decode workaround를 추가하지 않는다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-001` exact inventory, `002` optional/null·numeric wire,
  `003` TicTacToe one-way leave, `004` SupportChat typing, `005` DeliveryDispatch timestamp,
  `006` GameQuest action/payload, `007` ShoppingMall state response, `008` Bingo Protobuf.
- Core·bindings·package 선행 조건: Core C ABI와 bindings public API는 변경하지 않는다. C++ exact
  interface가 message serializer와 public codec의 ownership을 먼저 확정하고, 동일 package version의
  generated/typed codec으로 compile한다. sample-only codec registry를 새 public API로 만들지 않는다.
- 완료 evidence: 6개 shared contract와 공통 문서의 machine-readable inventory diff가 0이며,
  serialized request/reply/send/notify의 field, optional/null, numeric type과 packet kind가 일치한다.
  각 sample process에서 client 수신 결과와 role handler evidence가 같은 message name·sequence를
  기록한다.

### CPP-SAMPLE-IMP-002 — C++ ownership·routing identity와 codec 책임이 sample contract 밖으로 노출됨

- 상태: `gap`, `contract 선행`.
- 공통 spec/E2E 근거: `framework/doc/framework/common/sample/README.ko.md:192-204,280-322,451-520`,
  `framework/doc/framework/common/sample/event/gamequest.ko.md:255-277`,
  `framework/doc/framework/common/sample/deliverydispatch/README.ko.md:316-320`.
- C++ 경로: `samples/GameQuest/Shared/Contracts/messages.hpp:179-210,473-538`,
  `samples/SupportChat/Shared/Contracts/messages.hpp:1-72,140-160`,
  `samples/TicTacToe/Shared/Contracts/messages.hpp:1-38`,
  `samples/{Bingo,DeliveryDispatch,GameQuest,ShoppingMall,SupportChat,TicTacToe}/Server/common_codecs.hpp`,
  각 `Client/*_scenario.hpp`.
- 확인한 실제 동작과 기대 동작: GameQuest `GameplayMsg.payload`는
  `std::vector<std::uint8_t>`에 `nlohmann::json::to_msgpack/from_msgpack` 결과를 넣는다. 공통
  contract는 typed JSON `object`이며 app code가 raw payload codec을 직접 책임지지 않는다.
  GameQuest push에는 `targetConnectionId`가 들어가고, SupportChat/TicTacToe shared contract는
  `actor_ref_snapshot_t`를 alias한다. 공통 sample은 ActorRef, NodeRid, session route와 connection
  identity를 업무 message에 노출하지 않고 Framework binding이 현재 target을 결정해야 한다.
- gap 판정 근거: `std::vector<uint8_t>`와 MessagePack 변환은 payload buffer lifetime을 value로
  보존하더라도 공통 typed JSON contract와 다르다. Actor reference와 connection identity가 Shared
  contract에 있으면 호출자가 Framework 내부 owner/session 정보를 알아야 하며 public/internal 경계가
  흐려진다.
- 구체적인 수정 목록: `GameplayMsg.payload`의 public 표현을 typed JSON object로 확정하고
  message codec 책임을 Framework typed codec 경로에 둔다. `targetConnectionId`와 ActorRef snapshot은
  client-visible message에서 제거하고 server-internal lifecycle message인지 먼저 분류한다. 필요한
  server-internal type은 Client shared contract와 분리한다. C++에서는 borrowed reference를 message에
  저장하지 않고, callback capture가 process shutdown 뒤 객체를 참조하지 않도록 owner-managed value와
  completion barrier를 사용한다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-006`, `009` domain/framework type boundary, `010` session
  reconnect routing, `011` payload buffer lifetime와 callback cleanup.
- Core·bindings·package 선행 조건: Framework exact interface의 typed JSON codec, session binding과
  actor reference public 표현을 먼저 확인한다. Core raw frame 또는 Framework 내부 ABI를 sample에
  노출하지 않는다. package consumer에서 public header만 include해 compile해야 한다.
- 완료 evidence: app source에 MessagePack/raw buffer 변환이 없고, client wire에 `targetConnectionId`,
  ActorRef, NodeRid, session route가 없다. reconnect 후 새 stream에 notify가 전달되며 이전 connection
  id를 수동 선택하지 않는다. sanitizer 또는 bounded process test에서 callback count 1, buffer lifetime,
  shutdown cleanup을 확인한다.

### CPP-SAMPLE-IMP-003 — 역할별 logical structure와 production call path를 공통 sample 책임에 맞게 고정하지 않음

- 상태: `gap`, 일부는 `test gap`.
- 공통 spec/E2E 근거: 각 공통 sample 문서의 `## 4 역할과 책임`, `## 8 구현 구조`,
  `framework/doc/framework/common/sample/README.ko.md:451-520,552-584`.
- C++ 경로: `samples/DeliveryDispatch/Server/Dispatch/main.cpp`,
  `samples/GameQuest/Server/GameApi/main.cpp`, `samples/GameQuest/Server/QuestMission/main.cpp`,
  `samples/ShoppingMall/Server/CommerceApi/main.cpp`, `samples/ShoppingMall/Server/OrderWorkflow/main.cpp`,
  각 sample의 `Domain`, `Application`, `Infrastructure` 경로와
  `test_cpp_framework_layout_contract.cpp`.
- 확인한 실제 동작과 기대 동작: C++는 role executable을 분리하고 일부 sample은 named Domain/Application
  type을 둔다. 그러나 DeliveryDispatch와 event sample의 여러 production handler·workflow가 role
  `main.cpp`와 shared header에 함께 있어 공통 문서의 state owner, application service, infrastructure
  adapter 경계를 파일 구조만으로 일관되게 판정하기 어렵다. 기대 동작은 client가 domain state나
  Framework object를 직접 변경하지 않고, role server의 public handler가 application/domain을 호출하며
  Framework 배선은 Infrastructure에만 남는 것이다.
- gap 판정 근거: C++ porting inventory의 `done`은 과거 mapping 결과이고 current role process가
  실제로 해당 owner·adapter·handler sequence를 사용했다는 증거가 아니다. static layout test가
  directory와 일부 banned pattern만 확인해 전체 production call path를 증명하지 않는다.
- 구체적인 수정 목록: 6개 sample의 role별 call graph를 작성하고, `Client`, `Shared/Contracts`,
  `Server/Api|Session|Support|Play|Workflow`와 Domain/Application/Infrastructure owner를 명시한다.
  state mutation을 client 또는 route helper로 이동하지 않고 원래 owner role에 둔다. 공통 문서에 없는
  역할을 추가해야 하면 sample contract review를 먼저 한다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-009` layer boundary, `012` role process topology,
  `013` owner/generation transition, `014` domain rule no Framework include.
- Core·bindings·package 선행 조건: Framework public Actor/Spot/Channel/Stream interface가 S0에서
  완료되어야 한다. Core service C ABI나 internal Framework helper를 production sample에 추가하지 않는다.
- 완료 evidence: 각 role의 clean build에서 call graph가 public handler → application/domain → Framework
  adapter 순서로 확인되고, process evidence가 owner, generation, callback count, terminal state와
  cleanup을 기록한다. client binary는 server implementation header와 link하지 않는다.

### CPP-SAMPLE-IMP-004 — shell·PowerShell runner inventory와 완료 판정이 일치하지 않음

- 상태: `gap`.
- 공통 spec/E2E 근거: `framework/doc/framework/common/sample/README.ko.md:375-449,524-550`, 각
  sample 문서 `## 9 Client self-check`, `## 10 Smoke 실행`.
- C++ 경로: `framework/languages/cpp/samples/run_samples.sh:1-86`,
  `framework/languages/cpp/samples/run_samples.ps1:1-7`,
  `Bingo/run_sample.ps1`, `TicTacToe/run_sample.ps1`, 각 6개 `run_sample.sh`.
- 확인한 실제 동작과 기대 동작: shell aggregate는 6개를 등록하고 selector를 지원하지만 bind
  failure output을 찾아 개별 sample을 재시도한다. 공통 runner 기준은 준비된 port의 bind 실패를
  재시도하지 않고 원인을 보존해야 한다. PowerShell aggregate는 현재 Bingo와 TicTacToe만 호출하며
  4개 sample의 Windows runner가 없다. 각 individual runner가 일부 server log와 client marker를
  검사하더라도 aggregate 결과가 6개 client-visible result·role evidence·cleanup의 conjunction을
  직접 표현하지 않는다.
- gap 판정 근거: OS별 sample inventory가 다르고, retry와 단일 `sample all result=passed` 표식이
  공통 completion semantics와 다르다. current aggregate는 첫 TicTacToe process 실패에서 중단되어
  뒤의 sample을 실행하지 못했다.
- 구체적인 수정 목록: 6개 sample 모두에 동일한 책임을 가진 PowerShell runner를 제공하거나
  C++ 지원 OS 범위를 공통 문서에서 명시적으로 분리해 review한다. aggregate는 선택한 sample 목록을
  출력하고 각 결과·log path·cleanup status를 누적하며 하나라도 실패하면 실패한다. 공통 기준에
  맞게 bind retry를 제거하고 readiness와 bind failure를 구분한다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-011` canonical inventory, `015` shell/PowerShell selector,
  `016` no retry, `017` marker/evidence aggregation.
- Core·bindings·package 선행 조건: runner가 고정 package의 CMake build를 사용하도록
  `ZLINK_FRAMEWORK_CPP_ZLINK_CPP_VERSION`과 build directory를 외부 변수로 명시한다. Docker Redis는
  공통 helper와 pinned image를 사용하고 host Redis 또는 다른 sample endpoint로 fallback하지 않는다.
- 완료 evidence: shell과 PowerShell의 6종 inventory가 동일하고, 각 선택 실행은 readiness → client
  self-check → role evidence → cleanup을 모두 기록한다. aggregate 로그에 sample별 결과가 남고, 실패
  sample은 전체 성공으로 집계되지 않는다.

### CPP-SAMPLE-IMP-005 — sample package·build cache·문서 version이 현재 CMake pin과 다름

- 상태: `gap`, `contract 선행`.
- 공통 spec/E2E 근거: `framework/doc/framework/common/sample/README.ko.md:375-449`,
  `framework/doc/framework/common/spec/00-public-contract-governance.ko.md`,
  `scripts/local-package/README.ko.md`.
- C++ 경로: `framework/languages/cpp/samples/README.ko.md:1-56`,
  `framework/languages/cpp/CMakeLists.txt:41-87,618-760,2028-2060`,
  `framework/languages/cpp/build/CMakeCache.txt`,
  `framework/languages/cpp/build-v11-tests/CMakeCache.txt`,
  `framework/languages/cpp/scripts/verify_packaged_contract.sh`, `samples/*/run_sample.sh`.
- 확인한 실제 동작과 기대 동작: sample README는 Framework `10.0.0`을 설명한다. CMake는
  `ZLINK_FRAMEWORK_CPP_ZLINK_CPP_VERSION=11.1.0`, Core `11.0.0`을 pin한다. `build-v11-tests`는
  Release 11.1.0 provenance를 가지지만 sample runner 기본값 `build`는 Debug와 `11.0.2` cache를
  사용할 수 있다. current TicTacToe process는 이 기본 cache에서 `invalid application payload version`
  abort를 기록했다. 기대 동작은 모든 sample runner, CTest smoke와 package consumer가 같은 pinned
  package와 compiler standard/ABI를 사용하고, README가 실제 version을 설명하는 것이다.
- gap 판정 근거: source target build가 성공해도 runner가 다른 cache와 package를 사용하면 실제
  Framework 실행을 증명하지 않는다. package verifier도 `spot_handle.hpp` install 누락에서 실패했다.
  version drift와 install manifest 차이를 sample 완료로 숨길 수 없다.
- 구체적인 수정 목록: sample README와 runner의 version·build directory 정책을 CMake pin과
  `scripts/local-package/README.ko.md`에 맞춘다. clean build path를 선택하고 cache provenance를
  출력한다. `verify_packaged_contract.sh`의 manifest와 실제 install/export target을 exact interface에
  맞춰 검토한다. Debug/Release와 compiler standard를 consumer gate에서 명시한다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-014` version/provenance, `018` clean sample build,
  `019` install/export consumer, `020` ABI/symbol visibility.
- Core·bindings·package 선행 조건: S0의 C++ Framework package gap과 Core/bindings local package
  handoff가 먼저 닫혀야 한다. Core package를 sample source에 직접 include하거나 source tree를
  link하는 방식으로 해결하지 않는다.
- 완료 evidence: clean Release/Debug matrix가 같은 declared package version으로 configure·build되고,
  install tree의 모든 public header/target/config/version이 verifier와 consumer에서 통과한다. 6개
  runner가 실행한 binary와 `ldd`/package provenance가 같은 package를 가리키며 process smoke가
  payload version 오류 없이 끝난다.

## 6. `CPP-SAMPLE-E2E-IMP-*` sample runner·process evidence gap

### CPP-SAMPLE-E2E-IMP-001 — client-visible 결과와 role-server evidence를 한 process 실행에서 완료하지 못함

- 상태: `gap`; 현재 process blocker.
- 공통 spec/E2E 근거: 각 공통 sample 문서 `## 7 업무 흐름`, `## 9 Client self-check`,
  `## 10 Smoke 실행`, `framework/doc/framework/common/e2e/`의 해당 routing·lifecycle·failure
  scenario와 `README.ko.md`의 evidence 기준.
- C++ 경로: `samples/TicTacToe/Client/tictactoe_client_scenario.hpp`,
  `samples/TicTacToe/run_sample.sh`, `samples/{Bingo,DeliveryDispatch,GameQuest,ShoppingMall,SupportChat}/Client/*_scenario.hpp`,
  각 server `main.cpp`의 `/self-check/assert` 또는 evidence handler,
  `framework/languages/cpp/samples/run_samples.sh`.
- 확인한 실제 동작과 기대 동작: current TicTacToe 실행은 세 connector가 authenticate 단계까지
  갔지만 server process가 `service_wire_error_t: invalid application payload version`으로 abort했다.
  client는 connection reset을 확인했고 runner cleanup은 status 134를 기록했다. aggregate는 첫
  실패 뒤 중단되어 다른 5개 sample의 client result, role owner/generation, callback count, terminal
  reason과 cleanup evidence를 수집하지 못했다. 기대 동작은 client가 public HTTP/STREAM API로 role
  server를 호출하고, 같은 실행에서 server-side evidence endpoint/log가 state transition, owner,
  generation, callback exactly-once, cleanup과 terminal reason을 검증하는 것이다.
- gap 판정 근거: preflight CTest 3/3 또는 static parity 4/4는 process call path를 실행하지 않는다.
  historical sample inventory의 PASS는 current tree와 다른 cache일 수 있다. 현재 sample aggregate
  성공을 직접 증명하는 결과가 없다.
- 구체적인 수정 목록: S0 package/runtime blocker를 닫은 뒤 6개 sample별 process sequence와
  evidence contract를 고정한다. client에 private status query나 raw frame을 추가하지 않고, role server가
  공통 sample에 필요한 evidence를 public sample HTTP/typed message 경계로 제공하는지 먼저 확인한다.
  process 실패와 timeout은 bounded log와 child process cleanup으로 남긴다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-013` role evidence, `021` lifecycle owner/generation,
  `022` callback count/terminal mapping, `023` concurrent shutdown/cleanup, `024` reconnect/replay,
  `025` aggregate all result.
- Core·bindings·package 선행 조건: `CPP-IMP-*`, `CPP-E2E-IMP-*`와 package verifier가 먼저 닫혀야
  한다. Core recovery 또는 Framework internal runtime을 sample client가 직접 호출하지 않는다.
- 완료 evidence: 각 sample의 role server process가 readiness를 넘고, client-visible response/push와
  role evidence가 같은 run id로 연결된다. owner·generation·terminal reason·callback count·cleanup이
  직접 assertion되고, 6개 aggregate가 bounded timeout 없이 모두 통과한다.

### CPP-SAMPLE-E2E-IMP-002 — sample 이름과 common E2E scenario ID를 완료로 혼동할 수 있음

- 상태: `test gap`, `contract 선행`.
- 공통 spec/E2E 근거: `framework/doc/framework/common/e2e/README.ko.md`, 모든
  `config-*.ko.md`, C++ `framework/languages/cpp/e2e/*/feature-map.ko.md`.
- C++ 경로: `framework/languages/cpp/e2e/`의 12 config와 feature-map, `run_e2e_all.sh`,
  `framework/languages/cpp/samples/`의 6 sample runner 및 `CMakeLists.txt` CTest 등록.
- 확인한 실제 동작과 기대 동작: C++ E2E aggregate는 Framework scenario ID를 관리하고 sample
  aggregate는 sample 이름만 관리한다. 현재 shell aggregate 6종과 C++ E2E 12 config를 하나의
  completion count로 연결하는 inventory가 없으며, `ZoneWorld`는 sample 이름으로 C++에 포함되지
  않는다. 기대 동작은 sample scenario와 Framework E2E scenario를 별도 namespace로 두고, 각
  공통 scenario ID가 어느 C++ feature-map·selector·role server·assertion으로 검증되는지 추적하는
  것이다.
- gap 판정 근거: source file 존재나 sample 이름이 common E2E ID의 실행 증거가 아니다. `부분`,
  `diagnostic_only`, `N/A`, historical 항목이 aggregate 성공 수에 포함되면 누락을 숨긴다.
- 구체적인 수정 목록: sample inventory와 E2E inventory를 분리한 manifest를 만들고, C++에 요구되는
  scenario만 exact ID로 연결한다. C++에 요구하지 않는 ZoneWorld는 N/A 이유를 명시한다. selector가
  실제 process runner와 연결되는지 machine-check한다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-026` sample/E2E manifest, `027` selector dispatch,
  `028` aggregate exclusion of partial/diagnostic/N/A.
- Core·bindings·package 선행 조건: 공통 E2E scenario가 요구하는 Framework public contract는
  S0 exact interface와 package gate를 먼저 통과해야 한다. sample 이름만으로 새 API를 만들지 않는다.
- 완료 evidence: 6 sample inventory와 C++ common E2E feature-map의 cross-reference가 모두 존재하고,
  각 row가 실제 runner/selector/role server/client assertion으로 실행된다. N/A와 partial 항목은
  aggregate success에서 제외된다.

## 7. Core·bindings·package 선행 조건

sample 구현을 시작하기 전에 다음 선행 조건을 닫는다.

| 선행 조건 | 현재 상태 | sample에서 금지되는 대응 |
|---|---|---|
| C++ Framework public contract | S0의 `CPP-IMP-*`와 `CPP-E2E-IMP-*` 미완료 | sample에 private helper, raw frame, internal ABI를 추가하지 않는다. |
| C++ runtime test | 전체 CTest 41/49, 8 fail | sample smoke만 green으로 만들어 Framework failure를 가리지 않는다. |
| package install/export | `verify_packaged_contract.sh`가 `spot_handle.hpp` 누락으로 실패 | source include path나 old build cache를 사용해 우회하지 않는다. |
| local package version | CMake 11.1.0/11.0.0 pin과 default `build` cache가 다름 | sample runner가 다른 package를 자동 fallback하지 않는다. |
| Core·bindings handoff | current evidence로 clean provenance 미확정 | Core C ABI, binding raw operation, Framework private ABI를 sample 업무 경로에 호출하지 않는다. |
| common sample contract | 현재 공통 sample 문서에도 사용자 변경이 있음 | C++ sample이 다른 언어 source를 계약 근거로 삼지 않는다. 공통 문서 review를 먼저 수행한다. |

Core 또는 bindings 변경이 필요한 경우 이 sample ledger에서 임의로 수정하지 않고, 해당 모듈의
ledger와 package handoff를 선행시킨다. Framework sample은 public package를 통해서만 Core·bindings
기능을 사용한다.

## 8. 작업 순서

| 단계 | 작업 | gate |
|---|---|---|
| G0 | working tree와 공통 sample 문서 기준 commit을 고정하고, 실제 작업 단계마다 progress log를 남긴다. | 사용자 변경 보존, source/test/runner/spec 미수정 |
| G1 | S0 C++ Framework ledger의 public contract, runtime, package/ABI와 common E2E gap을 닫는다. | S0 checklist와 current evidence가 complete |
| G2 | 6 sample message inventory를 공통 문서와 exact diff하고 internal/test/evidence-only 범위를 분리한다. | `CPP-SAMPLE-IMP-001`, contract 선행 항목 종료 |
| G3 | codec, ownership, session binding, Actor/Spot routing과 Domain/Application/Infrastructure call path를 수정한다. | raw/private/connection identity 위반 0 |
| G4 | CMake target·install/export·package version을 고정하고 clean consumer를 통과시킨다. | `CPP-SAMPLE-IMP-005` 종료 |
| G5 | shell·PowerShell individual runner와 aggregate inventory, readiness, Redis, marker, evidence, cleanup을 정렬한다. | `CPP-SAMPLE-IMP-004` 종료 |
| G6 | 6 sample을 선택 실행한 뒤 aggregate `all`을 실행한다. 실패 sample을 성공으로 집계하지 않는다. | `CPP-SAMPLE-E2E-IMP-001` 종료 |
| G7 | static contract, unit/integration, package consumer, process E2E와 CI path filter를 재검증한다. | `CPP-SAMPLE-TEST-*`와 `CPP-SAMPLE-REG-*` 종료 |
| G8 | 독립 review에서 공통 message, 실제 call path, owner/generation, evidence, cleanup과 N/A 범위를 다시 확인한다. | unresolved sample gap 0, S0와 S1 모두 완료 |

G2에서 공통 문서에 없는 기능을 발견하면 바로 C++ public API를 추가하지 않는다. 필요한 계약 변경은
`contract 선행`으로 남기고 review 결과를 기다린다.

## 9. 기존 회귀 test의 유지·변경·추가 목록

### 9.1 기존 test gap

#### CPP-SAMPLE-TEST-001 — 기존 sample parity test가 전체 common inventory를 비교하지 않음

- 상태: `test gap`.
- 공통 spec/E2E 근거: 2.1의 6개 sample 문서와 `framework/doc/framework/common/e2e/`.
- C++ 경로: `framework/languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_sample_parity.cpp`.
- 확인한 실제 동작과 기대 동작: 현재 test는 Bingo, TicTacToe, SupportChat, DeliveryDispatch,
  GameQuest, ShoppingMall의 일부 packet name·field·source pattern과 layering을 검사하며 current
  build에서 통과했다. 전체 message set, JSON null/optional, serialized payload, process owner와
  common sample 문서의 모든 field를 비교하지 않는다. 기대 동작은 공통 inventory의 변경이 C++ test
  실패로 드러나는 것이다.
- gap 판정 근거: narrow static test가 green이어도 `SetTypingReq`, `LeaveGameReq`, GameQuest
  MessagePack payload, ShoppingMall `StartOrderRes`와 같은 미충족 항목을 전체적으로 판정하지 못한다.
- 구체적인 수정 목록: 공통 sample inventory를 중복 선언하지 않는 manifest 또는 generated check로
  연결하고, packet kind·field type·optional/null·codec을 compile/runtime serialization test로
  비교한다. source string search만으로 완료를 표시하지 않는다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-001`부터 `008`, negative extra-message cases.
- Core·bindings·package 선행 조건: test는 installed public C++ header와 pinned package를 우선
  사용해야 하며 Core private header를 include하지 않는다.
- 완료 evidence: 공통 6종의 contract diff가 current test에서 자동으로 실패·복구되고, serialization
  fixture와 packet kind assertion이 통과한다.

#### CPP-SAMPLE-TEST-002 — 기존 CTest sample gate가 process evidence와 aggregate completeness를 판정하지 않음

- 상태: `test gap`.
- 공통 spec/E2E 근거: 공통 sample runner 기준 `README.ko.md:375-449`와 6개 sample smoke 기준.
- C++ 경로: `framework/languages/cpp/CMakeLists.txt:2028-2060`,
  `framework/languages/cpp/samples/run_samples.sh`, `run_samples.ps1`, 각 runner.
- 확인한 실제 동작과 기대 동작: CTest sample smoke target은 6개 shell script를 등록하지만,
  current bounded aggregate는 첫 TicTacToe failure에서 중단했다. PowerShell aggregate는 2개만
  실행한다. CTest static gate 4/4 passed는 role server evidence, callback count, terminal reason,
  owner/generation, cleanup과 6개 aggregate 결과를 직접 판정하지 않는다.
- gap 판정 근거: CTest 등록 수가 sample process가 실제로 모두 성공했다는 뜻이 아니며, runner exit
  code만으로 client-visible/role evidence의 completeness를 보장하지 않는다.
- 구체적인 수정 목록: process gate를 sample별 결과 schema와 run id로 확장하고, aggregate가 모든
  선택 sample의 결과를 수집한다. bounded timeout, child process status, Redis cleanup과 log path를
  실패 assertion에 포함한다. CI path filter가 sample source·common sample 문서·runner 변경을
  해당 gate로 보낸다.
- 필요한 회귀 검증: `CPP-SAMPLE-REG-011`, `013`, `015-017`, `021-028`.
- Core·bindings·package 선행 조건: process gate는 pinned package와 clean build를 사용하고,
  Core/bindings blocker를 sample result로 표시하지 않는다.
- 완료 evidence: 6개 individual CTest와 aggregate selection/all runner가 bounded timeout 안에
  client·role evidence·cleanup을 모두 확인하고, CI에서 skip되지 않는다.

### 9.2 유지할 기존 test

- `test_cpp_framework_sample_parity`: 현재 확인한 static ownership/layering, Protobuf, DTO serializer,
  deferred join과 timeout rule assertion을 유지한다. 전체 inventory gate가 추가되어도 기존의
  private/raw route 금지 assertion을 삭제하지 않는다.
- `test_cpp_framework_layout_contract`: `Client`, `Shared`, server role 구조와 client/server link
  경계 검사를 유지한다. directory 존재만으로 완료를 판정하지 않도록 process gate와 구분한다.
- `test_cpp_framework_target_contract`: public target, sample target, private API 금지 검사를 유지한다.
- `test_cpp_framework_install_consumer`: clean install consumer를 유지하고 package verifier와 같은
  header/target manifest를 비교한다.
- 각 sample의 domain/unit test와 CTest smoke: 기존 상태 전이, timeout, retry, cleanup assertion은
  유지한다. sample code를 단순화하기 위해 삭제하지 않는다.

### 9.3 추가·변경할 regression ID

| ID | 대상 | 판정 |
|---|---|---|
| `CPP-SAMPLE-REG-001` | 6개 common message name inventory | 공통 문서와 C++ packet name one-to-one |
| `CPP-SAMPLE-REG-002` | JSON/Protobuf field type와 optional/null | `int64`, nullable string, optional field와 omission 확인 |
| `CPP-SAMPLE-REG-003` | TicTacToe `LeaveGameMsg` | one-way send, reply 없음, Entry Spot 복귀·destroy |
| `CPP-SAMPLE-REG-004` | SupportChat `SetTypingMsg` | one-way send, typing push, response를 기다리지 않음 |
| `CPP-SAMPLE-REG-005` | DeliveryDispatch status | `occurredAtUnixMs`, optional courier/reason, attempt와 late decision |
| `CPP-SAMPLE-REG-006` | GameQuest action/payload | typed JSON object, `ClosePlayerQuestMsg`, playerId와 projection |
| `CPP-SAMPLE-REG-007` | ShoppingMall workflow | `StartOrderRes.state`, sourceCommandId, event order와 compensation |
| `CPP-SAMPLE-REG-008` | Bingo Protobuf | schema tag/cardinality, optional `lastDrawnNumber`, reward publish |
| `CPP-SAMPLE-REG-009` | Domain/Application/Infrastructure boundary | Domain이 Framework type와 route identity를 include하지 않음 |
| `CPP-SAMPLE-REG-010` | session reconnect routing | connection id를 client가 지정하지 않고 binding이 current target을 선택 |
| `CPP-SAMPLE-REG-011` | aggregate inventory | shell·PowerShell의 6종 목록, selector와 duplicate 방지 |
| `CPP-SAMPLE-REG-012` | per-run resource cleanup | Redis container, child process, temp config와 log cleanup |
| `CPP-SAMPLE-REG-013` | role-server evidence | client result와 owner/generation/terminal/cleanup evidence 연결 |
| `CPP-SAMPLE-REG-014` | package/version provenance | CMake pin, README, binary, local package와 clean consumer 일치 |
| `CPP-SAMPLE-REG-015` | shell/PowerShell parity | Windows와 Linux runner가 같은 sample 책임을 수행 |
| `CPP-SAMPLE-REG-016` | no retry/false success | bind failure retry 금지, partial failure가 aggregate success가 아님 |
| `CPP-SAMPLE-REG-017` | public boundary | HTTP/STREAM public API만 사용하고 Core/private/raw 우회 없음 |
| `CPP-SAMPLE-REG-018` | clean sample build | Debug/Release와 compiler standard matrix, old cache 혼입 방지 |
| `CPP-SAMPLE-REG-019` | install/export consumer | public header, target, package config와 symbol visibility |
| `CPP-SAMPLE-REG-020` | ABI matrix | shared/static, C++ standard, package version과 compiler ABI |
| `CPP-SAMPLE-REG-021` | lifecycle evidence | owner, generation, actor/Spot join/leave/destroy 순서 |
| `CPP-SAMPLE-REG-022` | exactly-once terminal | callback count, terminal reason, duplicate completion 차단 |
| `CPP-SAMPLE-REG-023` | shutdown cleanup | in-flight operation, worker, stream close와 child process cleanup |
| `CPP-SAMPLE-REG-024` | reconnect/replay | session binding 변경 뒤 push와 event/projection replay |
| `CPP-SAMPLE-REG-025` | aggregate all result | 6개 모두 실행되고 결과·log·cleanup status를 수집 |
| `CPP-SAMPLE-REG-026` | sample/E2E manifest | sample 이름과 common E2E scenario ID를 별도로 추적 |
| `CPP-SAMPLE-REG-027` | selector dispatch | selector가 실제 client/role runner에 연결됨 |
| `CPP-SAMPLE-REG-028` | status exclusion | `부분`, `diagnostic_only`, `N/A`, historical을 성공 수에서 제외 |

`CPP-SAMPLE-REG-*`는 구현 완료를 뜻하지 않는다. 각 ID는 G2~G8 순서에 따라 test와 process
evidence가 추가된 뒤에만 `충족`으로 바꾼다.

## 10. 완료 판정 checklist

### 선행 Framework ledger

- [ ] `cpp-framework-spec-gap-ledger.ko.md`의 public contract와 exact C++ interface gap이 0이다.
- [ ] Framework production runtime의 lifecycle, admission, owner, queue, callback, deadline,
  cancellation, shutdown과 recovery evidence가 통과한다.
- [ ] 14개 common E2E config와 374개 scenario ID가 C++ feature-map, selector와 aggregate runner에서
  추적되며 partial/source-only/historical 항목을 성공으로 세지 않는다.
- [ ] CMake package, install/export, local package, Core/bindings version과 clean consumer가 같은
  provenance로 통과한다.

### C++ sample contract와 call path

- [ ] 6개 sample의 message·field·transport inventory가 공통 문서와 exact하게 대응한다.
- [ ] `SetTypingMsg`, `LeaveGameMsg`, Delivery timestamp/null, GameQuest typed payload와
  ShoppingMall `OrderState` 차이가 해소됐다.
- [ ] extra message와 field가 internal-only 또는 test/evidence-only로 명시됐고, unresolved
  public extra가 없다.
- [ ] C++ value/reference/move ownership, optional lifetime, callback capture와 buffer lifetime가
  공통 계약을 위반하지 않는다.
- [ ] client에는 ActorRef, NodeRid, session route, connection id, raw frame와 Core C ABI가 없다.
- [ ] Domain/Application/Infrastructure와 role owner가 실제 production call path로 확인된다.

### Runner와 process E2E

- [ ] shell·PowerShell individual/aggregate runner가 같은 6개 sample inventory와 selector 의미를
  제공한다.
- [ ] build directory와 package version이 명시되고 old Debug/Release cache가 섞이지 않는다.
- [ ] 각 실행이 전용 Redis, readiness, role start, public client call, client self-check, server
  evidence, terminal reason, owner/generation, callback count와 cleanup을 확인한다.
- [ ] bind failure를 retry하지 않으며, 한 sample failure가 aggregate 성공으로 집계되지 않는다.
- [ ] 6개 aggregate가 bounded timeout 안에 완료되고 각 sample log path와 process exit status가
  보존된다.

### Test·package·CI

- [ ] `CPP-SAMPLE-TEST-001`, `CPP-SAMPLE-TEST-002`가 닫혔다.
- [ ] `CPP-SAMPLE-REG-001`~`028` 결과가 current source와 current package 실행으로 증명된다.
- [ ] static parity green과 process E2E green이 별도 결과로 기록된다.
- [ ] install header, CMake target export, package config, ABI/symbol visibility와 clean consumer가
  통과한다.
- [ ] sample source, common sample doc, runner와 CMake path 변경이 CI에서 skip되지 않는다.
- [ ] 현재 working tree의 사용자 변경을 건드리지 않았고, 구현 코드·public header·test·E2E runner·
  정식 spec을 수정하지 않았다.
- [ ] 이 문서와 선행 spec ledger의 unresolved gap, Core·bindings·package blocker가 0이다.

최종 완료는 이 sample checklist만으로 판정하지 않는다. 선행 `cpp-framework-spec-gap-ledger.ko.md`
완료와 이 문서의 완료가 모두 확인된 뒤 C++ audit closure를 기록한다.
