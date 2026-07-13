# TypeScript Stream Connector 브라우저 전용 전환 계획

## 1. 목적

이 계획은 `@zlink-systems/stream-connector`에서 Node 전용 client connector 구현을 완전히
제거하고, TypeScript Stream Connector를 브라우저 계열 client 전용 package로 정리하기 위한
실행 문서다.

전환 뒤 connector는 플랫폼이 제공하는 `WebSocket`을 사용하며 `ws://`와 `wss://` endpoint만
지원한다. Node.js는 connector의 실행 환경이 아니다. Node.js를 사용하는 runner는 서버
프로세스, 정적 파일 서버와 headless 브라우저를 실행하는 자동화 도구 역할만 맡는다.

Node framework sample에서는 브라우저 client가 직접 접속하는 외부 STREAM endpoint만
`ws://` 또는 `wss://`로 바꾼다. 서버 사이의 channel, route, Spot mesh, pub/sub와 Redis 연결은
현재 transport를 유지한다.

이 문서는 계획과 진행 상태를 함께 관리한다. 구현과 검증이 끝난 항목만 체크하고, source가
존재하거나 build가 성공했다는 이유만으로 완료 표시하지 않는다.

## 2. 확정 결정

이번 작업에서는 다음 결정을 다시 선택지로 열어 두지 않는다.

1. TypeScript Stream Connector의 제품 실행 환경은 브라우저와 브라우저가 호스팅하는 WebGL·WASM
   build다. 엔진별 interop adapter는 connector runtime과 별도 산출물이다.
2. Node용 TCP, TLS와 직접 구현한 WebSocket transport는 제거한다.
3. package root인 `@zlink-systems/stream-connector`가 브라우저 구현을 직접 내보낸다.
4. `/browser`는 별도 구현이나 장기 호환 진입점으로 유지하지 않는다.
5. connector가 지원하는 endpoint scheme은 `ws`와 `wss`뿐이다.
6. 브라우저가 TLS handshake와 인증서 검증을 소유한다. connector에 인증서 검증 우회 option을
   제공하지 않는다.
7. 실제 Chromium에서 실행한 E2E가 없으면 브라우저 지원을 완료로 판정하지 않는다.
8. sample runner는 서버 실행을 담당하고, sample application code 안에서 서버 프로세스를
   시작하지 않는다.
9. 브라우저 비동기 flow 계약은 정식 spec을 충족하거나, 구현 가능한 새 계약을 정식 spec에
   확정한 뒤 구현해야 한다. 미해결 gap인 상태로 이 계획을 완료 처리하지 않는다.
10. browser sample의 HTTP 호출은 runner의 same-origin reverse proxy를 통과한다. sample server에
    테스트 편의를 위한 광범위한 CORS 허용 설정을 반복해서 추가하지 않는다.
11. connector와 codec을 분리하고 connector 생성 시 codec을 주입하는 현재 구조를 유지한다.
    브라우저 지원을 위해 병렬 connector API나 별도 codec 등록 구조를 만들지 않는다.
12. TypeScript connector source는 기존 Node framework workspace의 package 경로에 유지한다. 이 경로는
    build와 package 관리를 위한 저장소 위치이며 Node runtime 지원을 뜻하지 않는다.
13. 언어별 공개 계약은 `languages/typescript/03-stream-connector.ko.md`로 옮긴다. Node framework
    공개 계약에 browser client connector를 계속 함께 기록하지 않는다.

현재 package가 아직 정식 호환성을 보장하지 않는 개발 버전이더라도, 삭제 전에 package
소비자와 저장소 내부 사용처를 모두 검색한다. 사용처를 확인하지 않고 파일만 제거하지 않는다.

## 3. 범위

### 3.1 포함 범위

- Stream Connector 공통 spec과 TypeScript 공개 계약 변경
- Node 전용 connector source, export, option과 test 제거
- package root를 브라우저 connector로 단일화
- browser bundle의 Node 전용 module과 `Buffer` 의존성 부재 검증
- 실제 브라우저에서 request/reply, push, dispatch, reconnect와 종료 동작 검증
- Node framework sample의 client-facing STREAM endpoint를 `ws/wss`로 변경
- Node 프로세스로 실행하던 sample client를 browser bundle과 Chromium 실행으로 변경
- Node connector를 사용하던 framework E2E와 cross-language smoke의 브라우저 실행 전환
- 중앙 guide, internals, implementation gap, sample 문서와 검증 matrix 갱신
- Stream Connector와 codec npm tarball의 export 및 실제 browser/server consumer 검증

### 3.2 제외 범위

- Node framework server runtime 제거
- Node/NestJS server sample을 다른 언어로 변경
- 서버 사이 channel, route와 Spot mesh transport 변경
- Redis endpoint 변경
- HTTP API를 WebSocket API로 변경
- `.NET`, Java와 C++ native connector의 TCP/TLS 지원 제거
- 브라우저 client가 사용하지 않는 framework E2E를 강제로 browser test로 변경
- sample이나 E2E만 통과시키는 connector 전용 우회 transport 추가
- Unity `jslib`, Godot와 Cocos용 엔진별 interop adapter 신규 구현

## 4. 목표 구조

전환 뒤 package 내부 책임은 다음과 같이 나눈다.

| 책임 | 소유 모듈 | 실행 환경 |
|------|-----------|-----------|
| public contract와 call builder | Stream Connector 공용 runtime | 브라우저 |
| header, frame, metadata와 control packet | `stream-wire`와 connector protocol | 브라우저 |
| codec과 compression 연결 | connector 공용 runtime | 브라우저 |
| 연결과 binary message 송수신 | 플랫폼 `WebSocket` adapter | 브라우저 |
| E2E 서버와 정적 파일 서버 실행 | runner | Node.js 또는 shell |
| sample scenario 실행 | browser bundle | Chromium |
| PASS/FAIL 수집과 process 종료 | runner | Node.js 또는 shell |

connector는 연결과 STREAM protocol을 담당하고, 애플리케이션은 필요한 payload codec을 connector
생성 option으로 전달한다. 이 책임 분리는 현재 구현에도 있으므로 새 구조로 교체하지 않는다. 최종
browser bundle에는 애플리케이션이 import한 connector, 선택한 codec, 생성된 message codec과 그
codec에 필요한 browser runtime만 포함한다. JSON만 사용하는 client에 Protobuf 구현이 포함되도록
connector가 모든 codec을 직접 의존해서는 안 된다.

MessagePack과 Protobuf package는 browser-safe payload codec과 Node framework 등록 adapter를 같은
파일에서 만들지 않는다. package root는 browser-safe payload codec을 제공하고, Node framework가
serializer를 등록할 때만 명시적인 `./framework` subpath를 사용한다. 따라서 browser application의
module graph는 `@zlink-systems/framework`를 통과하지 않는다. 이 분리는 connector를 두 버전으로
유지한다는 뜻이 아니라, payload wire 처리와 server framework 등록이라는 서로 다른 책임을 나누는
것이다.

Node framework server는 CommonJS build를 유지하므로 server adapter가 ESM 전용 connector runtime을
`require`해서는 안 된다. codec 번호처럼 양쪽이 공유하는 wire 상수는 `stream-wire`가 소유하고
connector가 다시 export한다. codec package는 connector의 runtime 값을 import하지 않고 필요한
connector type만 type-only import한다. `stream-wire`는 같은 source에서 browser용 ESM과 server용
CommonJS output을 만들고, 숫자 상수를 codec마다 복제하지 않는다.

브라우저 connector는 `net`, `tls`, `async_hooks`, Node `crypto`와 `Buffer`를 import하지 않는다.
UUID나 browser crypto가 필요하면 플랫폼의 `globalThis.crypto`를 사용한다. 플랫폼 API가 없으면
구성 오류로 명확하게 실패하며 Node module을 fallback으로 불러오지 않는다.

## 5. 공개 계약과 문서 변경

Framework public contract는 구현보다 먼저 정식 spec에 반영한다. 현재 구현이 새 계약과 다른
기간에는 차이를 implementation gap에 기록한다.

### 5.1 공통 spec

다음 문서를 변경한다.

| 문서 | 변경 내용 |
|------|-----------|
| `framework/doc/framework/common/spec/32-stream-connector.ko.md` | Node(E2E·도구·봇)를 TypeScript connector 대상에서 제거하고 브라우저 계열만 남긴다. Node의 4개 transport 지원, 이중 진입점과 Node 배포 행을 제거한다. TypeScript connector의 지원 scheme을 `ws/wss`로 고정한다. |
| `framework/doc/framework/common/spec/90-implementation-gap.ko.md` | Node/browser 진입점 분리 완료 설명과 Node connector test 증거를 제거한다. browser transport, 실제 Chromium E2E와 비동기 flow 차이를 현재 상태에 맞게 다시 기록한다. |
| `framework/doc/framework/common/spec/53-flow-correlation.ko.md` | 브라우저에 `AsyncLocalStorage`가 없다는 사실을 반영해 ambient flow 계약이 브라우저에서 실제로 구현 가능한지 재검토한다. 현재 계약을 구현하거나, 명시적 context 전달처럼 브라우저에서 구현 가능한 계약으로 정식 spec을 먼저 바꾼다. 미해결 gap은 완료로 판정하지 않는다. |
| `framework/doc/framework/common/spec/05-framework-api.ko.md` | Node codec extension package에서 browser-safe payload codec과 framework 등록 adapter의 공개 진입점을 구분한다. |
| `framework/doc/framework/common/spec/00-public-contract-governance.ko.md` | TypeScript browser projection의 소유 문서 위치와 환경 차이에 따른 transport 제한을 언어별 계약 목록에 반영한다. |
| `framework/doc/framework/common/spec/languages/node/01-system-structure.ko.md` | codec package root와 `./framework` 진입점의 dependency 경계를 기록하고 browser bundle이 Node framework package에 의존하지 않음을 명시한다. |

`framework/doc/framework/common/draft/browser-stream-connector.ko.md`는 현재 정식 spec 및 구현 상태와
중복되거나 과거 Node connector 전제를 포함한다. 필요한 계약을 정식 spec으로 옮긴 뒤 별도 계약
후보가 남지 않으면 제거한다. 과거 설명을 정식 계약과 나란히 유지하지 않는다.

### 5.2 언어별 public interface 문서

현재 문서인
`framework/doc/framework/common/spec/languages/node/03-stream-connector.ko.md`는 Node framework와
browser TypeScript connector를 한 문서에 함께 두고 있다. 이 파일을
`framework/doc/framework/common/spec/languages/typescript/03-stream-connector.ko.md`로 옮겨
브라우저 전용 TypeScript 계약으로 고정한다.

작업 순서는 다음과 같다.

1. `languages/typescript/`를 공통 spec index와 navigation의 정식 언어별 계약 위치에 추가한다.
2. connector 계약을 `languages/typescript/` 아래로 옮기고 모든 링크를 갱신한다.
3. 기존 Node 언어 계약과 목차에서 client connector 계약을 제거한다.
4. TypeScript public interface에서 `Tcp`, `Tls`, `skipServerCertificateValidation`과 Node
   socket/TLS 전용 option을 제거한다. 공통 enum 문서는 브라우저 projection이 `WebSocket`과
   `WebSocketSecure`만 노출하는 환경별 차이를 명시한다.
5. package root 하나와 플랫폼 `WebSocket` 사용법만 정식 signature 문서에 남긴다.
6. 현재 구현과 같은 `codec?: ZlinkStreamPayloadCodec` 단일 주입 계약을 정확히 기록한다. 실제 public
   API에 없는 codec registry 표현은 제거하고 별도 등록 API를 추가하지 않는다.
7. `framework/languages/node/test/contract/documentation-regression.test.js`가 새 TypeScript 문서와
   browser package declaration을 비교하도록 경로와 기대값을 갱신한다.

문서 이동과 같은 변경에서 공통 spec index와 public contract governance도 갱신해 계약 소유권을
한 위치로 고정한다.

### 5.3 비동기 flow 계약 결정

현재 `BrowserZlinkFlowContext`는 handler가 반환한 Promise가 끝날 때까지 connector instance의
현재 flow를 유지한다. 이 방식은 같은 instance에서 실행되는 관련 없는 callback에 inbound flow를
노출할 수 있으므로 그대로 완료 처리하지 않는다.

G1에서 최소 두 설계를 비교한다.

1. handler와 후속 send/request 호출에 flow context를 명시적으로 전달하고 ambient context에
   의존하지 않는 계약
2. 브라우저에서 지원되는 표준 또는 검증된 runtime을 사용해 비동기 작업별 context를 격리하는 계약

호출자가 알아야 하는 상태와 public signature가 더 적고, unrelated callback 격리를 실제
Chromium에서 증명할 수 있는 설계를 선택한다. 전역 Promise, timer와 event callback을 monkey
patch하는 방식은 사용하지 않는다. 선택한 계약을 `53-flow-correlation.ko.md`와 TypeScript public
interface에 먼저 반영한 뒤 runtime과 E2E를 구현한다.

### 5.4 사용자 guide

현재 `framework/doc/stream-connector/node/` 아래에는 Node guide와 browser guide가 함께 있다.
이 디렉터리는 다음 기준으로 정리한다.

- 공식 guide 위치를 `framework/doc/stream-connector/typescript/`로 변경한다.
- `guide/02-node.ko.md`는 제거하고 guide index의 순서와 링크를 갱신한다.
- browser guide를 기본 guide로 승격하고 `/browser` import를 package root import로 바꾼다.
- connector 생성 예제는 package root에서 connector를, 별도 codec package에서 선택한 codec을
  import해 `codec` option으로 전달한다. 해당 호출 옆 주석으로 connector가 codec을 선택적으로
  사용한다는 점을 설명한다.
- 첫 연결 예제는 `ws://`, 운영 예제는 `wss://`를 사용한다.
- 브라우저가 인증서를 검증하므로 connector가 검증을 건너뛸 수 없음을 설명한다.
- `Manual`과 `Immediate` dispatch를 browser main loop 관점에서 설명한다.
- `observeInbound`를 `connect` 전에 등록해야 한다는 순서를 코드 주석으로 보여 준다.
- Node 내부 socket, handshake와 framing 구현 설명은 guide에 남기지 않는다.
- `framework/doc/stream-connector/README.ko.md`와 `.NET`·C++ guide에서 Node/TypeScript guide를
  가리키는 링크를 새 TypeScript guide 위치로 모두 바꾼다.

### 5.5 Node framework와 sample 문서

다음 문서 집합에서 Node connector라는 표현과 `tcp://` client 예제를 검색해 갱신한다.

- `framework/doc/framework/node/README.ko.md`
- `framework/doc/framework/node/guide/`
- `framework/doc/framework/node/internals/regression-test-matrix.ko.md`
- `framework/doc/framework/common/sample/README.ko.md`
- `framework/doc/framework/common/sample/<sample>/README.ko.md`
- `framework/doc/framework/common/sample/zoneworld/README.ko.md`
- `framework/doc/plan/framework-public-contract-gap-implementation.ko.md`
- Java/Node 또는 cross-language stream interop을 설명하는 언어별 regression matrix

sample 문서에는 다음 실행 구조를 설명한다.

- runner가 Node server와 정적 client 파일 서버를 실행한다.
- Chromium 안의 client scenario가 HTTP API 호출과 stream connector 흐름을 소유한다.
- API가 endpoint를 반환하는 sample은 반환값의 `ws/wss` endpoint로 connector를 만든다.
- browser HTTP 호출은 runner가 제공하는 same-origin reverse proxy의 상대 URL을 사용한다.
- server 내부 endpoint는 계속 `tcp`를 사용한다.
- Bingo는 filesystem lookup이 없는 생성된 browser Protobuf codec을 사용한다.
- browser package와 codec package root는 Node ambient type이 없는 ESM build로 검증한다.
- Playwright bootstrap, Chromium cache와 WSS test certificate의 소유 위치를 runner 문서에 기록한다.
- runner는 browser console과 명시적 결과 값을 수집해 실패를 process exit code로 전달한다.

## 6. Connector source와 package 변경

### 6.1 유지할 책임

다음 구현은 browser-safe 여부를 다시 확인한 뒤 유지한다.

- public contracts와 call builder
- connector lifecycle
- pending request와 wait 처리
- inbound observer와 received-message queue
- frame sender와 receive dispatcher
- protocol codec
- compression 연결
- `BrowserWebSocketConnection`
- browser flow context

공용 파일이라는 이름만 보고 유지하지 않는다. 정적 import와 transitive dependency를 확인해 Node
전용 module이 포함되면 책임을 분리하거나 browser-safe 구현으로 바꾼다.

### 6.2 제거 후보

다음 파일과 책임은 repository 전체 참조를 확인한 뒤 제거한다.

- `Runtime/ZlinkStreamConnectorNode.ts`
- `Runtime/NodeZlinkFlowContext.ts`
- `Runtime/Transport/NodeSocketConnector.ts`
- `Runtime/Transport/NodeDuplexStreamConnection.ts`
- `Runtime/Transport/NodeWebSocketConnection.ts`
- Node runtime에서 수행하는 WebSocket handshake와 frame 처리
- Node 기본 transport factory
- TCP/TLS socket option과 인증서 검증 우회 연결

`WebSocketFrameCodec.ts`와 `WebSocketHandshake.ts`처럼 이름만으로 browser 공용 여부를 판단하기
어려운 파일은 참조 graph를 확인한다. 플랫폼 `WebSocket`을 쓰는 browser adapter에서 사용하지
않고 Node transport만 사용하면 함께 제거한다.

### 6.3 package export

`framework/languages/node/packages/stream-connector/package.json`은 package root 하나만 browser
runtime과 type declaration으로 내보내도록 변경한다.

- `.`은 browser 구현을 export한다.
- `./browser` export는 제거한다.
- Node 조건부 export와 Node 전용 type entry가 남지 않았는지 확인한다.
- build 결과에 삭제한 Node 파일이 포함되지 않게 clean build한다.
- package lock과 workspace dependency를 갱신한다.
- codec package가 connector contract type을 import하는 경로를 새 root에 맞춘다.
- browser bundler가 직접 소비할 ESM output과 type declaration을 package root에서 export한다.
- CommonJS output이나 Node 조건부 export를 browser package의 기본 경로로 남기지 않는다.

구현 파일 이름도 실제 책임과 맞춘다. browser 구현이 유일한 구현이 된 뒤에도
`ZlinkStreamConnectorBrowser`와 일반 connector가 중복 계층으로 남아 있으면 합친다. 단순히 한
class가 다른 class에 인자를 전달하는 얕은 wrapper를 유지하지 않는다.

source와 package는 기존 Node framework workspace의 build orchestration을 계속 공유한다.
`framework/languages/node/packages/stream-connector` 경로는 TypeScript package의 저장소 소유 위치일
뿐 Node runtime 지원을 뜻하지 않는다고 workspace 문서에 명시한다. 새 source tree를 만들어 같은
package를 이중 관리하지 않는다.

### 6.4 public option 정리

다음 항목은 browser public interface에서 제거한다.

- `Tcp`, `Tls` transport enum 값
- `skipServerCertificateValidation`
- Node socket과 TLS에만 적용되는 option
- Node transport factory를 전제로 한 error와 lifecycle branch

`transportFactory`는 in-memory test 대역이나 플랫폼 adapter 확장점이라는 공통 계약 근거가 있으면
유지한다. Node transport 호환 지점으로 설명하거나 Node 구현을 되살리는 통로로 사용하지 않는다.

### 6.5 browser 전용 TypeScript build

Stream Connector와 `stream-wire`는 Node workspace 공통 tsconfig의 Node ambient type에 의존하지
않는 browser build를 가져야 한다. browser production build의 최소 compiler 조건은 다음과 같다.

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "lib": ["ES2022", "DOM", "DOM.Iterable"],
    "types": [],
    "module": "ESNext",
    "moduleResolution": "Bundler"
  }
}
```

실제 파일은 package별 `tsconfig.browser.json` 또는 같은 의미의 중앙 browser config로 둔다.
Node contract test용 config와 production browser config를 섞지 않는다. production source에
`Buffer`, `process`, `__dirname`, `require`와 `node:*` import를 넣으면 TypeScript compile 단계에서
실패해야 한다.

Stream Connector package export는 이 ESM build를 가리킨다. Node test runner에서 빠른 contract
test를 실행할 때는 빌드 산출물을 dynamic `import()`로 읽거나 test 전용 변환을 사용한다. 테스트
편의를 위해 connector production output을 CommonJS로 되돌리지 않는다.

`stream-wire`는 browser connector뿐 아니라 CommonJS Node framework server도 사용하는 환경 중립
module이다. 같은 TypeScript source에서 ESM `import`와 CommonJS `require` export를 만들되 public
wire 상수와 구현을 두 source로 복제하지 않는다. 두 output의 byte fixture 결과가 같은지 contract
test로 검증한다.

### 6.6 browser codec 호환성

Browser sample이 사용하는 codec도 connector와 같은 browser dependency 규칙을 충족해야 한다.
connector package만 browser-safe이고 codec을 포함한 consumer bundle에 Node module이 포함되는
상태는 완료가 아니다.

현재의 `ZlinkStreamConnectorOptions.codec` 주입과 `ZlinkStreamPayloadCodec` 계약은 유지한다.
browser 전환은 connector와 codec 사이에 새 계층을 추가하는 작업이 아니라, 기존 codec 구현에서
Node 전용 dependency를 제거하는 작업이다. Bingo는 기존
`createZlinkStreamProtobufEnvelopeCodec()`에 생성된 encode/decode option을 전달한다. 새 public
factory나 별도 browser codec interface를 추가하지 않는다. 이후 다른 요구로 새 API가 필요해지면
기존 interface로 해결할 수 없는 이유를 기록하고 public contract 변경 절차를 먼저 거친다.

현재 MessagePack과 Protobuf package root는 `@zlink-systems/framework`의 runtime 값과 connector
payload codec을 한 module에서 함께 import한다. 이 상태에서는 codec만 선택한 browser bundle에도
server framework dependency가 유입될 수 있다. 다음 package 경계를 두 codec에 같은 방식으로
적용한다.

- package root는 `ZlinkStreamPayloadCodec` 구현과 browser-safe wire 함수만 export한다.
- `./framework` subpath는 `ZLinkCodecExtension`, serializer 등록과 `ZLinkEncodedPayload` adapter를
  export하며 Node framework server만 import한다.
- package root의 browser module graph에는 `@zlink-systems/framework`가 없어야 한다. package manifest는
  server adapter가 요구하는 framework를 optional peer dependency로 선언해 browser consumer가 server
  framework를 함께 설치하도록 강제하지 않는다.
- connector의 type만 필요한 의존성도 runtime dependency로 두지 않는다. root와 `./framework`의
  declaration을 분리하고, 필요한 경우 connector를 optional peer dependency로 선언해 server adapter
  consumer가 browser connector를 설치하도록 강제하지 않는다.
- connector는 codec package를 dependency로 추가하지 않는다. application이 선택한 codec만 import한다.
- browser와 server adapter가 같은 wire encode/decode 구현을 사용하며 복제하지 않는다.
- package root는 ESM browser output을, `./framework`는 현재 Node framework가 소비할 CommonJS
  output을 제공한다. server adapter가 ESM connector runtime을 `require`하지 않게 한다.
- package export map은 `.`의 `types`와 ESM `import`를 browser root로 연결하고,
  `./framework`의 별도 `types`와 CommonJS `require`를 server adapter로 연결한다. `default` 조건이
  Node 전용 파일을 browser root로 선택하게 두지 않는다.
- stream codec enum과 wire 상수는 `stream-wire`에서 가져오고 connector package가 이를 다시
  export한다. codec package 안에 같은 숫자 값을 별도로 정의하지 않는다.
- 기존 server framework 사용처는 `./framework` import로 일괄 변경하고 contract test로 보호한다.

특히 Bingo의 Protobuf 경로는 다음과 같이 변경한다.

- `framework-codec-protobuf`의 `Buffer` 기반 encode/decode를 `Uint8Array`, `DataView`,
  `TextEncoder`와 `TextDecoder` 기반으로 변경한다.
- browser client에서 `node:path`, `__dirname`, filesystem과 runtime `protoPath` lookup을 제거한다.
- runtime `.proto` lookup을 전제로 하는 `createZlinkProtobufJsEnvelopeCodec()`은 production public
  surface에서 제거한다. repository 사용처를 먼저 확인하고, 필요한 schema 처리는 build-time
  generator가 맡는다.
- `.proto` 파일로부터 message type과 `encode`/`decode` 구현을 build 단계에서 생성한다. 브라우저는
  `.proto` 파일을 읽거나 schema를 runtime에 해석하지 않는다.
- 현재 `framework/languages/node/samples/Bingo.Ts/scripts/generate-protobuf-types.js`는 message
  class만 생성하므로 schema별 정적
  `encode`/`decode`와 envelope codec까지 생성하도록 바꾼다.
- 생성된 Bingo message codec을 기존 payload codec 계약에 정적으로 연결한다.
- 최종 bundle에는 선택한 Protobuf codec, 생성된 Bingo message codec과 필요한 최소 browser runtime만
  포함한다. connector package가 Protobuf package를 필수 dependency로 가져서는 안 된다.
- `.proto`를 읽는 `protobufjs` 도구는 build-time generator에서만 사용할 수 있다. 생성 코드가
  browser용 최소 Protobuf runtime을 사용한다면 그 runtime만 production dependency와 bundle에
  포함하고 `loadSync`, filesystem loader와 Node fallback은 포함하지 않는다.
- generator에 `--check` mode를 두어 새 내용을 임시 메모리나 임시 파일에 만들고 대상 generated
  file과 바이트 단위로 비교한다. 저장소 전체 `git diff`에 의존해 무관한 변경 때문에 실패하게
  만들지 않는다. 생성 파일을 사람이 직접 수정하거나 runner 실행 때마다 내용이 달라지는 상태를
  허용하지 않는다.
- server와 browser client가 같은 packet name, Protobuf message type과 response mapping을 사용한다.
- server와 browser client는 같은 생성 codec을 공유해 wire format 차이가 생기지 않게 한다.
- codec package 자체와 Bingo의 실제 production bundle을 각각 browser config로 compile한다.

MessagePack과 JSON 경로도 같은 기준으로 검사한다. polyfill을 자동 주입해 `Buffer`, `process`나
Node stream을 숨기는 bundler 설정은 허용하지 않는다. codec 전체를 포함한 module graph와 최종
bundle text에서 Node 전용 dependency가 없음을 검증한다.

## 7. 실제 브라우저 테스트 기반

### 7.1 test runner

Playwright와 Chromium을 browser E2E runner로 사용한다. Node.js는 Playwright, server process와
정적 파일 서버를 실행할 뿐 connector instance를 만들지 않는다.

Playwright 실행 기반은 `framework/languages/node` workspace가 소유한다. 공통 browser runner와
Playwright spec은 `framework/languages/node/test/browser/`, server 실행·proxy·정리 helper는
`framework/languages/node/scripts/browser-e2e/` 아래에 두어 sample마다 복제하지 않는다.

- root `devDependencies`에 버전을 고정한 `@playwright/test`를 추가한다.
- 공통 bootstrap script가 browser 설치 여부를 검사한다.
- Linux CI는 `playwright install --with-deps chromium`으로 system dependency와 Chromium을
  준비한다.
- Windows와 개발자용 bootstrap은 `playwright install chromium`을 사용한다.
- `PLAYWRIGHT_BROWSERS_PATH`를 workspace 또는 CI cache 위치로 고정해 sample마다 browser를 다시
  설치하지 않는다.
- sample runner는 dependency를 자동 설치하지 않고, 누락 시 실행해야 할 bootstrap 명령을 포함한
  오류로 즉시 실패한다.
- CI workflow는 browser cache key에 Playwright version과 운영체제를 포함한다.
- sample browser bundle은 workspace에 이미 있는 `esbuild`를 공통 bundler로 사용한다. sample마다
  다른 bundler나 암묵적인 Node polyfill 설정을 추가하지 않는다.
- 실제 Chromium E2E는 CI matrix의 `linux-x64`와 `win-x64` 대표 job에서 실행한다. 다른 architecture와
  macOS job은 browser ESM compile, package와 빠른 contract test를 실행하되 Chromium을 반복 설치하지
  않는다.

runner는 다음 순서를 지킨다.

1. 필요한 package와 sample을 clean build한다.
2. Node framework server를 실행한다.
3. readiness endpoint 또는 관찰 가능한 port 상태를 확인한다.
4. browser client bundle, runtime config와 same-origin API proxy를 HTTP로 제공한다.
5. Chromium page를 열고 scenario 완료 신호를 기다린다.
6. page error, unhandled rejection과 console error를 즉시 실패로 수집한다.
7. scenario의 구조화된 PASS 결과와 필수 evidence marker를 확인한다.
8. browser, 정적 파일 서버와 sample server를 종료한다.

고정 sleep 뒤 성공으로 간주하지 않는다. server readiness, page load, connector state와 scenario
결과를 각각 관찰한다.

### 7.2 browser config와 결과 전달

브라우저 bundle에서는 `process.env`, `node:fs`와 runner의 임시 파일을 읽을 수 없다. sample
설정은 다음 중 하나의 저장소 공통 방식으로 전달한다.

- runner가 제공하는 `/config.json`
- HTML에 삽입한 읽기 전용 초기 설정 객체
- page URL의 query parameter

샘플마다 서로 다른 임시 방식으로 만들지 않는다. endpoint 목록, HTTP API URL과 scenario 선택을
담을 수 있는 작은 browser runner contract를 하나 정하고 모든 대상 sample이 공유한다.

완료 결과는 console 문자열 하나에만 의존하지 않는다. page가 성공 여부, scenario 이름과 evidence를
구조화된 객체로 노출하고 runner가 이를 읽는다. 기존 grep 가능한 `stream-inbound`와 `PASS` marker는
사람이 로그를 확인할 수 있도록 함께 유지한다.

공통 결과 객체는 `window.__zlinkSampleResult` 하나로 고정하며 `status`, `scenario`, `evidence`와 실패
시 `error`를 담는다. runner는 이 값이 `passed` 또는 `failed`가 될 때까지 timeout과 함께 기다리고,
값이 없거나 알 수 없는 상태면 실패한다. sample마다 다른 전역 이름이나 console parser를 만들지
않는다.

### 7.3 same-origin HTTP와 WebSocket Origin

Browser page는 runner가 제공하는 origin 하나만 사용한다. runner HTTP server는 sample별 route를
명시적으로 등록해 기존 sample API로 reverse proxy한다.

예를 들어 client는 `http://127.0.0.1:<runner-port>/api/tictactoe/games`를 호출하고, runner는 이를
실제 TicTacToe API의 `/games`로 전달한다. DeliveryDispatch와 GameQuest처럼 API가 여러 개인
sample은 `/api/<role>/...` route로 구분한다. proxy는 method, body와 필요한 response header만
전달하며 임의의 외부 URL을 받는 open proxy로 만들지 않는다.

이 방식으로 browser sample code에는 CORS 우회 option이나 wildcard
`Access-Control-Allow-Origin`을 추가하지 않는다. `fetch`는 같은 origin의 상대 URL만 사용한다.
API 자체의 CORS가 제품 계약으로 필요해지는 경우에는 이 sample 전환과 분리해 HTTP spec에서
먼저 설계한다.

WebSocket endpoint는 browser config로 받은 `ws/wss` 주소를 사용한다. server가 `Origin` 검증을
지원하면 runner origin을 명시적으로 허용한다. 아직 공개 Origin policy가 없다면 현재 동작과 보안
영향을 internals 또는 implementation gap에 기록하고, sample 전용 raw handshake 우회를 만들지
않는다.

### 7.4 WS와 WSS 검증

- 모든 local sample runner의 기본 연결은 `ws://127.0.0.1:<port>`를 사용한다.
- 최소 한 개의 focused E2E에서 실제 Chromium과 `wss://` server를 연결한다.
- focused WSS server는 기존
  `framework/languages/node/test/fixtures/tls/server-cert.pem`과 `server-key.pem`을
  `setTlsServer(certificatePath, keyPath)`로 stream node에 설정한다. 같은 목적의 인증서를 새로
  만들지 않는다.
- fixture certificate에는 test host의 SAN을 포함하고 만료일을 contract test에서 검사한다.
- WSS transport E2E는 격리한 Chromium context의 `ignoreHTTPSErrors`를 runner에서만 사용한다.
  이는 암호화된 WebSocket transport 검증일 뿐 인증서 신뢰 성공 증거로 기록하지 않는다.
- 별도 기본 Chromium context는 같은 신뢰되지 않은 fixture 접속이 실패하는지 확인한다. 운영
  인증서 신뢰는 플랫폼 browser가 소유하며 connector가 변경하지 않는다.
- connector public option으로 browser 인증서 오류를 우회하지 않는다.
- HTTPS에서 실행한 page가 `ws://`를 사용하는 mixed content 구성을 정상 예제로 기록하지 않는다.

## 8. Node sample 전환 범위

여기서 “Session server”는 디렉터리 이름이 아니라 브라우저 client session을 받는
`addStreamNode(...).bind(...)` endpoint를 뜻한다. sample에 따라 역할 이름이 `Session`, `Play`,
`GameApi` 또는 `CourierSession`일 수 있다.

| sample | `ws/wss`로 바꿀 외부 STREAM endpoint | 유지할 내부 endpoint | 비고 |
|--------|-------------------------------------------|-----------------------|------|
| Bingo | Session A/B의 `sessionEndpoint` | session route, session Spot, Play route/Spot/pub-sub, API channel, Redis | drain probe가 Node file과 connector에 의존하므로 browser scenario와 runner control로 분리한다. |
| TicTacToe | Play A/B의 `playStreamEndpoint` | API channel, Play channel, Spot route/pub-sub, Redis | `POST /games` 응답의 Play endpoint가 browser에서 접근 가능한 `ws/wss` 주소여야 한다. |
| SupportChat | Session의 `sessionStreamEndpoint` | API/Support channel, Spot route/pub-sub, Redis | 여러 browser client session과 notification 흐름을 Chromium에서 검증한다. |
| DeliveryDispatch | Customer Session과 CourierSession의 외부 stream endpoint | Dispatch API/channel, workflow와 Spot endpoint, Redis | customer와 courier가 모두 외부 client 역할이므로 두 stream endpoint가 대상이다. |
| GameQuest | GameApi A/B의 player stream endpoint | mission HTTP, route/Spot endpoint, Redis | 이름은 GameApi지만 browser player session을 받는 `addStreamNode`이므로 대상이다. |
| ShoppingMall | 없음 | 전체 | 현재 browser stream connector 사용처가 없으므로 transport 변경을 만들지 않는다. |

각 sample의 `.sh`와 `.ps1` runner를 같은 의미로 변경한다. 한 운영체제 runner만 browser E2E를
실행하고 다른 runner는 이전 Node client를 유지하는 상태를 완료로 보지 않는다.

### 8.1 sample client source

대상 sample client는 다음 기준으로 변경한다.

- connector import는 package root를 사용한다.
- `process.env`와 filesystem config를 browser config contract로 교체한다.
- Node 전용 HTTP client를 same-origin 상대 URL에 대한 browser `fetch` 호출로 교체한다.
- scenario의 업무 흐름과 assertion은 browser-safe module로 유지한다.
- `main.ts`는 config를 읽고 scenario를 실행하며 결과를 보고하는 얇은 진입점으로 둔다.
- packet encode/decode나 raw WebSocket frame 처리를 sample에 추가하지 않는다.
- 기본 typed JSON 또는 해당 sample의 정식 codec 경로를 그대로 사용한다.
- Bingo browser client는 filesystem을 사용하지 않는 생성된 Protobuf codec entry를 사용한다.
- server와 browser client를 한 CommonJS tsconfig로 compile하지 않는다. 각 sample에 Node server용
  config와 `types: []`, DOM library, ESM을 사용하는 browser client용 config를 분리한다.
- browser client config의 module graph에는 `Client`, browser-safe `Shared` contract와 선택한 codec만
  포함한다. server entrypoint와 Node 전용 configuration file은 포함하지 않는다.
- `Client/main.ts`의 `process.exitCode`는 browser runner 결과 contract로 교체하고, browser graph의
  `process.env`, `node:fs`, `node:path`, `Buffer`와 `__dirname`을 모두 제거한다.
- GameQuest의 player id 처리처럼 `Buffer`를 문자열 byte 변환에 사용한 공용 helper는
  `TextEncoder` 기반 구현으로 바꾸고 server와 browser에서 같은 결과를 검증한다.
- shared contract의 type-only import까지 검사한다. browser compile이
  `@zlink-systems/framework`의 Node declaration에 의존하면 serializable sample contract 또는
  browser-safe 공개 contract로 경계를 바로잡는다. runtime package를 type 우회로 가져오지 않는다.

Node 전용 control 작업이 필요한 경우 connector client에 다시 섞지 않는다. runner control endpoint나
sample server의 기존 공개 제어 경로로 분리한다.

## 9. Framework E2E와 cross-language 영향

Node connector 제거는 sample 외 사용처에도 영향을 준다. package root를 browser 전용으로 바꾼 뒤
Node process가 기존 TCP endpoint로 connector를 만드는 E2E를 남길 수 없다.

대상은 repository 전체 exact import 검색으로 확정하며, 현재 확인한 주요 묶음은 다음과 같다.

- `framework/languages/node/e2e/SpotActorTransfer`
- `framework/languages/node/e2e/ToActorMessaging`
- `framework/languages/node/e2e/AutomaticTurnDispatch`
- `framework/languages/node/e2e/SpotService`
- `framework/languages/node/e2e/RegistrationCodec`
- `framework/languages/node/cross-language/node_dotnet_smoke.js`
- `framework/languages/cpp/cross-language/node_peer_host.js`

각 scenario는 다음 세 유형으로 분류한다.

| 유형 | 처리 |
|------|------|
| browser client 공개 동작 검증 | server의 외부 STREAM endpoint를 `ws/wss`로 열고 Chromium scenario로 전환한다. |
| protocol byte 호환성 검증 | browser-safe protocol module의 unit/interop test로 유지한다. Node connector runtime을 만들지 않는다. |
| Node framework server 자체 검증 | connector를 사용하지 않으면 현재 Node E2E 구조를 유지한다. |

`RegistrationCodec`은 browser connector scenario가 아니라 codec package의 `./framework` adapter가
기존 serializer와 stream codec 등록 동작을 유지하는지 검증하는 Node server 회귀 테스트로 둔다.

cross-language smoke의 “Node connector → .NET stream server” 항목은 “Browser TypeScript connector →
.NET WebSocket stream server”로 바꾼다. 실제 Chromium을 거치지 않는 protocol fixture는 browser
connector E2E라는 이름을 사용하지 않는다.

C++ cross-language runner의 `node_peer_host.js`도 Node connector를 직접 불러오므로 같은 inventory에
포함한다. 이 runner에서 Node process가 connector 역할을 계속 맡게 하지 않는다. C++ server와의
client interop이 목적이면 Chromium scenario로 옮기고, Node process가 필요한 server/framework 역할은
runner control로만 유지한다.

## 10. 테스트 재구성

### 10.1 제거할 테스트

- Node TCP connector 연결과 reconnect test
- Node TLS connector와 인증서 우회 test
- Node runtime에서 수행하는 WebSocket handshake와 fragmentation test
- Node/browser 두 entrypoint의 export가 같다는 test
- Node 기본 진입점 tarball consumer test

삭제한 동작을 검증하던 fixture, 인증서와 helper가 다른 테스트에서 사용되지 않으면 함께 제거한다.

### 10.2 유지하거나 강화할 테스트

- header, frame, metadata와 control packet codec
- request sequence와 pending request lifecycle
- send, request, wait와 event dispatch
- manual/immediate dispatch
- reconnect, heartbeat와 close reason
- inbound observer 등록 시점, queue와 overflow
- payload size와 receive queue 한도
- JSON, MessagePack와 Protobuf codec 연결
- package root가 `tcp/tls` endpoint를 거부하는 검증
- browser bundle module graph와 `Buffer` 부재 검증
- Node ambient type이 없는 browser TypeScript compile
- connector, `stream-wire`, codec과 sample을 포함한 최종 consumer bundle 검사

package root가 browser 전용이 되면 테스트 이름도 `browser entrypoint`보다 실제 계약 동작을
설명하도록 바꾼다.

### 10.3 추가할 실제 browser test

최소 검증 항목은 다음과 같다.

- 실제 `ws` connect와 close
- typed JSON request/reply
- server push와 `waitFor`/`on`
- `Manual` dispatch 전후 처리 차이
- `Immediate` dispatch
- reconnect 뒤 request/reply
- server drain과 `closeReason`
- binary payload와 최대 크기 경계
- malformed frame 또는 protocol close
- 실제 `wss` connect
- page error와 unhandled rejection 부재

fake `WebSocket` test는 빠른 contract test로 유지할 수 있지만 실제 browser E2E를 대신하지 않는다.

## 11. Package와 배포 검증

source tree build만으로 package 완료를 판정하지 않는다. Stream Connector, `stream-wire`, Protobuf와
MessagePack codec을 같은 version의 artifact 묶음으로 검증한다.

1. 네 package를 clean build하고 각각 npm tarball을 만든다.
2. Stream Connector tarball 목록에서 Node transport source와 build output이 없는지 확인한다.
3. 각 tarball의 export map, declaration과 ESM/CommonJS 산출물이 계획한 진입점과 일치하는지 확인한다.
4. 빈 browser consumer project에 같은 artifact 묶음의 connector, `stream-wire`와 선택한 codec tarball을
   설치한다.
5. package root import의 TypeScript type 해석을 확인한다.
6. Node ambient type이 없는 browser tsconfig로 consumer를 compile한다.
7. ESM browser production bundle을 만든다.
8. connector와 codec을 포함한 bundle graph에 Node module, server framework와 `Buffer`가 없는지
   확인한다.
9. 별도 빈 CommonJS server consumer에 codec tarball과 framework package를 설치하고
   `./framework` adapter를 실제 package export로 불러와 serializer를 등록한다.
10. Bingo처럼 생성 codec을 쓰는 실제 sample production bundle도 같은 검사를 통과한다.
11. 같은 artifact 묶음을 실제 Chromium E2E에서 사용한다.
12. `/browser`와 삭제한 Node export가 더 이상 public surface에 없는지 확인한다.

local package script와 package version을 바꿔야 하면
`scripts/local-package/README.ko.md`를 먼저 확인하고, 중앙 version과 실제 artifact를 함께
갱신한다.

## 12. 실행 순서와 gate

### G0. 현재 사용처 inventory

- [ ] Node connector source와 간접 참조 목록 작성
- [ ] package export와 codec package type dependency 확인
- [ ] 기존 package 경로 유지에 따른 workspace, project reference와 local-package 영향 확인
- [ ] sample별 외부 STREAM endpoint 목록 확정
- [ ] framework E2E와 cross-language 사용처 분류
- [ ] 관련 spec, guide, draft와 regression 문서 목록 확정

### G1. 정식 계약 변경

- [ ] 공통 Stream Connector spec에서 Node 실행 환경 제거
- [ ] TypeScript browser public interface 문서 확정
- [ ] documentation regression test를 TypeScript 계약 문서 위치로 전환
- [ ] `ws/wss` 전용 option과 error 계약 확정
- [ ] package root 단일 진입점 확정
- [ ] codec package root와 `./framework` adapter의 공개 dependency 계약 확정
- [ ] browser flow 계약 대안 두 가지 비교와 정식 계약 확정
- [ ] 현재 구현과 차이를 implementation gap에 기록

### G2. Browser-only connector 구현

- [ ] package root를 browser runtime으로 변경
- [ ] Node transport와 `NodeZlinkFlowContext` 제거
- [ ] Node 전용 option, enum과 lifecycle branch 제거
- [ ] 얕은 browser wrapper와 중복 factory 정리
- [ ] Node ambient type이 없는 browser ESM build 추가
- [ ] `stream-wire`의 동일 source 기반 ESM/CommonJS export와 공용 codec 상수 정리
- [ ] Protobuf와 MessagePack package root에서 server framework runtime import 제거
- [ ] Protobuf와 MessagePack의 server 등록 adapter를 `./framework` subpath로 분리
- [ ] JSON codec과 선택한 외부 codec의 browser dependency 검사
- [ ] runtime proto lookup factory와 `protoPath` public option 제거
- [ ] Bingo 생성 Protobuf codec에 정적 `encode`/`decode`를 포함하고 filesystem 경로 제거
- [ ] Protobuf 생성 결과 결정성 검사 통과
- [ ] clean build와 static import 검사 통과

### G3. Contract와 package 검증

- [ ] 공용 protocol/connector contract test 통과
- [ ] fake WebSocket 기반 빠른 browser test 통과
- [ ] browser bundle graph 검사 통과
- [ ] codec을 포함한 sample production bundle graph 검사 통과
- [ ] codec package root graph의 server framework dependency 부재 검사 통과
- [ ] codec `./framework` adapter의 Node server registration contract test 통과
- [ ] `stream-wire` ESM/CommonJS byte fixture 동등성 test 통과
- [ ] CommonJS Node server가 codec `./framework` adapter를 실제 package export로 불러와 등록
- [ ] Node ambient type 없는 browser compile 통과
- [ ] npm tarball browser consumer compile 통과
- [ ] codec tarball CommonJS server consumer 등록 통과
- [ ] 삭제한 Node surface의 absence test 통과

### G4. 실제 browser E2E 기반

- [ ] Chromium runner 추가
- [ ] Playwright bootstrap과 Linux/Windows CI browser cache 추가
- [ ] `linux-x64`와 `win-x64` 대표 CI job에 실제 Chromium E2E 배치
- [ ] browser config/result contract 추가
- [ ] same-origin API reverse proxy 추가
- [ ] 실제 `ws` request/reply와 push 통과
- [ ] 실제 `wss` focused E2E 통과
- [ ] 기본 browser trust에서 test certificate 거부 확인
- [ ] reconnect, drain과 close reason 통과

### G5. Node sample 전환

- [ ] Bingo
- [ ] TicTacToe
- [ ] SupportChat
- [ ] DeliveryDispatch
- [ ] GameQuest
- [ ] ShoppingMall 비대상 재확인
- [ ] 대상 sample별 Node server와 browser client TypeScript build 분리
- [ ] 각 대상의 `.sh`와 `.ps1` runner 통과

### G6. Framework E2E와 cross-language 전환

- [ ] Node process가 connector instance를 만들어 client 역할을 하는 production/E2E 경로 제거
- [ ] browser client 동작을 Chromium scenario로 전환
- [ ] protocol-only test와 browser E2E 이름 구분
- [ ] Browser TypeScript connector → .NET stream server smoke 통과
- [ ] Browser TypeScript connector → C++ stream server smoke 통과
- [ ] Node framework server E2E regression 통과

### G7. 문서와 최종 재검토

- [ ] Stream Connector spec과 TypeScript interface가 구현과 일치
- [ ] Node/browser 이중 진입점 설명이 정식 문서에 남지 않음
- [ ] Node용 connector guide와 TCP/TLS client 예제가 남지 않음
- [ ] sample 문서가 browser client 실행과 `ws/wss` endpoint를 설명
- [ ] implementation gap이 실제 Chromium 결과와 남은 flow 차이를 정직하게 기록
- [ ] browser flow 계약과 runtime/E2E가 일치하며 미해결 public gap이 없음
- [ ] packaged surface, sample, E2E와 문서 전체 재검토 완료

## 13. 완료 판정

다음 조건을 모두 충족해야 이 계획을 완료로 표시한다.

- repository 전체에서 Node connector production implementation과 export가 발견되지 않는다.
- `@zlink-systems/stream-connector` package root가 browser runtime 하나만 제공한다.
- connector는 `ws/wss`만 허용하고 플랫폼 `WebSocket`을 사용한다.
- browser 전용 TypeScript build는 Node ambient type 없이 ESM output을 만든다.
- connector와 codec의 기존 주입 계약을 유지하며 connector package가 Protobuf나 MessagePack을 필수
  dependency로 포함하지 않는다.
- TypeScript 공개 계약과 guide는 실제 `codec?: ZlinkStreamPayloadCodec` option을 설명하며 존재하지
  않는 codec registry나 별도 browser codec API를 요구하지 않는다.
- Protobuf와 MessagePack package root의 browser module graph에는 Node framework runtime이 없고,
  server 등록 adapter만 `./framework`에서 framework를 참조한다.
- CommonJS Node server는 ESM connector runtime을 `require`하지 않으며, `stream-wire`의 ESM과
  CommonJS output은 같은 wire 상수와 byte 결과를 제공한다.
- connector와 `stream-wire` source 및 Stream Connector tarball에 Node socket/crypto/flow implementation,
  `Buffer`, `process`, `__dirname`과 `node:*` import가 없다.
- codec package root를 사용한 browser module graph와 최종 sample bundle에는 codec의 `./framework`
  adapter, Node framework runtime, `Buffer`, `process`, `__dirname`과 `node:*` import가 없다.
- Bingo Protobuf client는 생성된 browser codec을 사용하며 runtime filesystem lookup을 하지 않는다.
- 각 sample의 Node server build와 browser client build가 분리되고 browser graph에 server entrypoint와
  Node configuration이 포함되지 않는다.
- 실제 Chromium에서 Node sample의 외부 STREAM endpoint와 연결한 sample runner가 통과한다.
- browser의 HTTP 호출은 runner의 제한된 same-origin reverse proxy를 사용한다.
- Playwright/Chromium bootstrap과 CI cache가 `linux-x64`와 `win-x64` 대표 runner에서 재현된다.
- WSS transport 성공과 신뢰되지 않은 test certificate 거부를 서로 다른 증거로 검증한다.
- 서버 내부 channel, route, Spot와 Redis endpoint는 의도하지 않게 변경되지 않았다.
- framework E2E와 cross-language test가 삭제한 Node connector에 의존하지 않는다.
- `.sh`와 `.ps1` runner가 같은 sample 계약을 검증한다.
- browser flow 계약이 정식 spec, runtime과 실제 Chromium E2E에서 일치하며 미해결 public contract
  gap이 없다.
- 공통 spec, TypeScript public interface, guide, sample 문서, regression matrix와 implementation gap이
  실제 구현 및 테스트 결과와 일치한다.
- `git diff --check`와 관련 전체 검증 gate가 통과한다.

브라우저 비동기 flow 계약이 해결되지 않으면 현재 상태와 사용자 영향을 implementation gap에
기록하고 이 계획을 `진행` 또는 `차단` 상태로 유지한다. 그렇더라도 Node transport를 임시 호환
구현으로 남기지 않는다. flow 계약과 Node transport 제거는 별개 책임이며, 미해결 계약을 이전
runtime 유지로 숨기지 않는다.
