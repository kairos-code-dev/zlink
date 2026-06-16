# Framework / Bindings object messaging 정렬 구현 계획

이 문서는 아래 두 초안을 실제 코드, 샘플, 정식 spec, guide에 반영하기 위한 실행 계획이다.

- [`framework-object-messaging-surface.ko.md`](./framework-object-messaging-surface.ko.md)
- [`bindings-message-boundary-alignment.ko.md`](./bindings-message-boundary-alignment.ko.md)

대상 draft 문서는 **구현 전 초안**이며 **현재 공개 계약이 아니다**. 구현 전 단계에서는
정식 spec, guide, internals 문서에 새 계약을 섞어 쓰지 않는다. 이 문서는 구현 순서,
수정 범위, 검증 기준, 리뷰 절차를 고정하기 위한 진행 문서다.

## 목적

이 계획의 목적은 object messaging 정렬 작업이 아래 세 갈래로 분리되어 따로 흘러가지
않게 막는 것이다.

1. framework high-level API 표면 정리
2. bindings `Message` / codec extension 책임 경계 정리
3. sample / guide / spec를 최종 public contract 예시로 다시 맞추는 작업

이 세 갈래는 한 언어 안에서 동시에 닫아야 한다. 한 슬라이스를 끝낼 때는
`bindings -> framework -> sample -> doc -> review`가 한 묶음으로 마무리되어야 한다.

## 대상 설계 기준

### Framework 기준

[`framework-object-messaging-surface.ko.md`](./framework-object-messaging-surface.ko.md)
에서 확정한 기준을 따른다.

- high-level outbound API는 `Message`가 아니라 업무 객체를 받는다.
- codec 선택, payload 직렬화, packet name 추론, typed reply decode는 framework 내부에서
  처리한다.
- `joinSpot`과 explicit `reply(...)`도 같은 원칙으로 맞춘다.
- high-level sample과 guide에서 `Message.from(...)`, `.ToJson()`, `.ToProto()`,
  `submit<Buffer>().then(decode...)` 같은 경로를 제거한다.

### Bindings 기준

[`bindings-message-boundary-alignment.ko.md`](./bindings-message-boundary-alignment.ko.md)
에서 확정한 기준을 따른다.

- bindings base `Message`는 bytes container로 유지한다.
- codec extension은 object <-> `Message` encode/decode helper만 담당한다.
- packet name 정책, serializer lookup, typed request/reply decode는 framework가 맡는다.
- bindings public contract에서 object-aware `Message` shortcut을 없앤다.

## 대상 범위

### 코드

- `framework/languages/node/packages`
- `framework/languages/node/samples`
- `framework/languages/node/doc`
- `framework/languages/node/test`
- `framework/languages/node/cross-language`
- `framework/languages/java/zlink-framework-core`
- `framework/languages/java/zlink-framework-kotlin`
- `framework/languages/java/zlink-framework-testkit`
- `framework/languages/java/zlink-stream-connector`
- `framework/languages/java/zlink-stream-connector-codecs`
- `framework/languages/java/zlink-stream-connector-json`
- `framework/languages/java/zlink-stream-connector-msgpack`
- `framework/languages/java/zlink-stream-connector-protobuf`
- `framework/languages/java/samples/java`
- `framework/languages/java/samples/kotlin`
- `framework/languages/java/doc`
- `framework/languages/dotnet/src`
- `framework/languages/dotnet/samples`
- `framework/languages/dotnet/doc`
- `framework/languages/dotnet/tests`
- `framework/languages/cpp/framework`
- `framework/languages/cpp/extensions`
- `framework/languages/cpp/samples`
- `framework/languages/cpp/doc`
- `framework/languages/cpp/tests`
- `bindings/node/src`
- `bindings/node/packages`
- `bindings/node/tests`
- `bindings/java/src`
- `bindings/java/codec`
- `bindings/java/tests`
- `bindings/dotnet/src`
- `bindings/dotnet/codecs`
- `bindings/dotnet/tests`
- `bindings/cpp/include`
- `bindings/cpp/src`
- `bindings/cpp/codecs`
- `bindings/cpp/tests`
- `bindings/go/contracts`
- `bindings/go/codec`
- `bindings/go/tests`
- `bindings/python/src`
- `bindings/python/codecs`
- `bindings/python/tests`
- `bindings/rust/src`
- `bindings/rust/crates`
- `bindings/rust/tests`

### 문서

- `doc/spec/draft/framework-object-messaging-surface.ko.md`
- `doc/spec/draft/bindings-message-boundary-alignment.ko.md`
- `doc/spec/bindings/README.md`
- `doc/spec/bindings/README.ko.md`
- `doc/spec/bindings/{node,java,dotnet,cpp,go,python,rust}/codec.md`
- 언어별 framework spec / guide / sample guide 문서

### 테스트와 샘플

- 언어별 contract test
- 언어별 framework unit / integration test
- 언어별 sample gate
- connector / e2e 시나리오

## 비목표

- low-level raw transport API 자체를 제거하지 않는다.
- core C `zlink_msg_t` / C++ `message_t` 기반 public contract를 object messaging 표면으로
  바꾸지 않는다.
- Spot create / admission lifecycle callback의 raw payload 표면까지 이번 1차 rollout에서
  함께 바꾸지 않는다.
- low-level binding sample에서 `Message`를 직접 다루는 사용법을 금지하지 않는다.

## 반드시 유지할 설계 결정

- high-level API의 canonical 입력은 업무 객체다.
- bindings base `Message`는 object payload를 저장하거나 복원하는 표면이 아니다.
- codec extension은 packet name을 자동 부착하지 않는다.
- packet name 추론 실패 시에만 builder `.packetName(...)` override를 허용한다.
- sample은 단순 테스트 코드가 아니라 최종 public contract 예시다.
- 한 언어만 다른 호출 표면을 남기는 우회 구현을 허용하지 않는다.
- Kotlin wrapper는 Java runtime 위 thin wrapper로 유지하고 별도 의미를 만들지 않는다.
- Go / Python / Rust는 현재 framework가 없더라도 bindings 경계는 같은 방향으로 맞춘다.
- C binding은 raw byte/message contract를 유지하고 별도 codec helper를 강제하지 않는다.

## 작업 원칙

### 1. 두 초안을 동시에 기준으로 사용한다

작업 중 어느 한쪽 문서만 근거로 구현하지 않는다.

- framework 코드를 바꿀 때는 framework 초안과 bindings 경계 초안을 둘 다 확인한다.
- bindings 코드를 바꿀 때도 high-level framework 표면이 최종적으로 무엇을 요구하는지
  같이 확인한다.

### 2. 언어 슬라이스 단위로 끝낸다

예를 들어 Node를 정리할 때는 아래를 한 번에 끝낸다.

1. bindings `Message` / codec extension 정리
2. framework high-level API 정리
3. sample 정리
4. 정식 spec / guide 정리
5. test / sample gate 실행
6. 독립 리뷰

Node bindings만 먼저 끝내고 framework를 나중에 두는 식으로 오래 벌려 두지 않는다.

### 3. sample을 임시 우회로 고치지 않는다

sample에 raw bytes decode, `then(decode...)`, helper chain, 언어별 예외 surface를 넣어
빌드만 통과시키지 않는다. sample이 이상해 보이면 sample이 아니라 공개 표면 설계가
문제라고 보고 원래 책임 계층을 수정한다.

### 4. 문서와 구현을 따로 오래 유지하지 않는다

한 슬라이스 구현이 끝나면 해당 슬라이스의 정식 spec / guide / sample 문서까지 같이 맞춘다.

### 5. low-level 검증 코드와 high-level sample 검증을 구분한다

아래 코드는 high-level sample 정렬 대상과 성격이 다르므로 같은 grep gate로 묶지 않는다.

- framework testkit
- contract test
- connector 내부 test
- cross-language smoke
- low-level bindings sample

이 경로들은 raw `Message`, explicit `packetName(...)`, encoded payload helper를
계속 사용할 수 있다. 다만 그 경우에도 왜 low-level 검증이 필요한지 코드와 문서에서
설명 가능해야 한다.

## 전체 순서

### 0단계. 기준선 재확인

아래를 먼저 다시 확인한다.

- 현재 bindings public surface
- 현재 framework public surface
- 현재 sample이 보여 주는 실제 표면
- 현재 정식 spec과 guide의 설명

이 단계의 출력은 언어별 차이 목록이다.

### 1단계. 공통 bindings 경계 정리

공통 policy와 언어별 codec spec을 먼저 고정한다.

- `doc/spec/bindings/README*.md`
- `doc/spec/bindings/*/codec.md`

이 단계에서 확정할 것:

- codec extension은 object <-> `Message` helper만 담당한다.
- packet name / serializer lookup / typed request/reply decode는 codec spec에 쓰지 않는다.
- framework가 있는 언어는 그 정책을 framework spec에서 설명한다.

### 2단계. framework 공통 구현 전략 정리

언어별 구현에 앞서 framework 쪽에서 공통으로 필요한 내부 책임을 정리한다.

- payload object -> packet name resolution
- payload object -> serializer lookup
- serializer -> low-level `Message` 생성
- low-level reply -> typed object decode
- explicit `reply(replyObject)` 처리

이 단계에서는 언어별 내부 extension point 이름이 달라도 역할이 같아야 한다.

### 3단계. Node 슬라이스

대상:

- `bindings/node/src`
- `bindings/node/packages`
- `framework/languages/node/packages`
- `framework/languages/node/samples`
- 관련 test / doc

해야 할 일:

- bindings `Message.from(object)`, `value()`, `packetName()`, `withPacketName()` 제거
- codec package가 packet 정책을 갖지 않게 정리
- framework outbound API가 업무 객체를 직접 받게 정리
- Node packet name 추론 규칙을 framework 내부로 이동
- sample에서 `Message.from(...)`, explicit decode chain 제거
- connector e2e는 low-level handler 등록 대신 `waitFor(...)` 중심으로 맞출 수 있는지 정리

완료 기준:

- Node high-level sample에서 codec helper와 raw decode chain이 사라진다.
- bindings `Message`는 bytes container 계약만 남는다.
- `.packetName(...)`은 plain object 같은 예외 경로에서만 남는다.

### 4단계. Java / Kotlin 슬라이스

대상:

- `bindings/java/src`
- `bindings/java/codec`
- `bindings/java/tests`
- `framework/languages/java/zlink-framework-core`
- `framework/languages/java/zlink-framework-kotlin`
- `framework/languages/java/zlink-framework-testkit`
- `framework/languages/java/zlink-stream-connector`
- `framework/languages/java/zlink-stream-connector-codecs`
- `framework/languages/java/zlink-stream-connector-json`
- `framework/languages/java/zlink-stream-connector-msgpack`
- `framework/languages/java/zlink-stream-connector-protobuf`
- `framework/languages/java/samples/java`
- `framework/languages/java/samples/kotlin`
- 관련 doc / tests

해야 할 일:

- bindings `Message.packetName()` / `withPacketName()` 제거
- codec encode 시 packet name 자동 부착 제거
- framework Java core가 object messaging policy를 맡게 이동
- stream connector 계열의 packet name 결정, typed codec helper, encoded payload 경계도
  같은 기준으로 정리
- Kotlin wrapper는 Java core 위 thin wrapper로 유지
- Java/Kotlin sample을 같은 high-level 호출 모양으로 정리

완료 기준:

- Java/Kotlin sample이 같은 메시징 규칙을 따른다.
- Kotlin wrapper가 Java와 다른 codec / packet 정책을 새로 만들지 않는다.
- bindings codec은 encode/decode helper만 남는다.

### 5단계. .NET 슬라이스

대상:

- `bindings/dotnet/src`
- `bindings/dotnet/codecs`
- `framework/languages/dotnet/src`
- `framework/languages/dotnet/samples`
- 관련 doc / tests

해야 할 일:

- 현재 object형 표면을 기준으로 `JoinSpot`, explicit reply, sample 표면을 다시 점검
- high-level sample에서 `.ToJson()`, `.ToProto()` 제거
- packet name 자동 추론 규칙을 framework 내부에서 일관되게 정리

완료 기준:

- .NET은 object messaging 기준선으로 유지되며 다른 언어가 여기에 맞춰진다.
- bindings `Message`에 framework 정책이 추가되지 않는다.

### 6단계. C++ 슬라이스

대상:

- `bindings/cpp/include`
- `bindings/cpp/src`
- `bindings/cpp/codecs`
- `framework/languages/cpp/framework`
- `framework/languages/cpp/extensions`
- `framework/languages/cpp/samples`
- 관련 doc / tests

해야 할 일:

- typed high-level wrapper가 object를 직접 받게 정리
- `join_spot` high-level 표면 정리
- `to_stream_payload(...)`, `from_stream_payload(...)`는 low-level helper로만 남김
- packet name 추론 규칙을 framework typed wrapper 쪽으로 고정

완료 기준:

- high-level sample에서 `message_t`나 stream payload helper가 직접 보이지 않는다.
- low-level binding helper는 계속 남지만 high-level sample의 기본 경로가 아니다.

### 7단계. Go / Python / Rust bindings 정리

이 언어들은 현재 framework 작업보다 bindings 경계 정리가 중심이다.

대상:

- `bindings/go/contracts`, `bindings/go/codec`
- `bindings/python/src`, `bindings/python/codecs`
- `bindings/rust/src`, `bindings/rust/crates`
- 관련 spec / tests

해야 할 일:

- base `Message` public contract가 bytes 중심인지 재확인
- codec extension이 object-aware policy를 갖지 않도록 정리
- 정식 spec과 tests가 같은 책임 경계를 설명하도록 맞춤

완료 기준:

- 모든 non-C bindings가 같은 `Message` / codec boundary를 가진다.

### 8단계. sample / guide / 정식 spec 일괄 반영

각 언어 슬라이스가 끝난 뒤에도 마지막으로 한 번 더 공통 검토를 한다.

- sample이 최종 public contract와 같은 표면을 보여 주는가
- framework guide가 object messaging 표면을 설명하는가
- bindings guide가 low-level `Message` 표면을 설명하는가
- 같은 sample family가 언어별로 다른 규칙을 보여 주지 않는가

### 9단계. conformance test와 독립 리뷰

마지막 단계에서는 구현과 문서를 다시 드리프트하지 않게 막아야 한다.

- 언어별 contract test 추가 또는 보강
- framework conformance test 추가
- sample gate 실행
- independent review 수행

independent review는 최소 두 갈래다.

1. 구현자가 아닌 시선에서 파일/line 기준으로 읽는 code review
2. Codex 에이전트를 사용한 별도 리뷰 패스

리뷰 질문은 아래를 기준으로 고정한다.

- high-level API가 정말 업무 객체를 직접 받는가
- codec helper가 sample과 guide 기본 경로에서 사라졌는가
- bindings `Message`에 object-aware policy가 남아 있지 않은가
- packet name override가 예외 경로로만 남는가
- Java/Kotlin, Node, .NET, C++가 같은 의미를 보여 주는가

## 언어별 체크리스트

### Node

- bindings `Message` object-aware public API 제거
- framework packet name resolver 이동
- framework serializer lookup 이동
- explicit reply object 경로 점검
- connector e2e 표면 정리
- sample / guide / spec 갱신
- tests / sample gate / independent review

### Java / Kotlin

- Java bindings packet metadata 제거
- codec auto packet-name policy 제거
- Java framework object messaging 정리
- Kotlin thin wrapper 보존 확인
- Java/Kotlin sample 동형성 확인
- doc / tests / independent review

### .NET

- high-level object 표면 유지 확인
- `JoinSpot` / explicit reply / sample 점검
- codec helper 제거 범위 적용
- packet name 추론 policy 정리
- doc / tests / independent review

### C++

- high-level typed wrapper 입력 정리
- `join_spot` 표면 정리
- stream payload helper low-level 잔류 확인
- sample / doc / tests / independent review

### Go / Python / Rust

- base `Message` bytes-only 확인
- codec spec / test 정렬
- object-aware shortcut 부재 확인

## 구현 중 금지 패턴

- sample에서 raw bytes를 직접 decode해서 우회
- `submit<Buffer>().then(decode...)` 같은 체인 추가
- handler마다 codec helper 반복 주입
- bindings `Message`에 packet metadata를 다시 추가
- framework가 아니라 codec extension에 serializer lookup 정책 추가
- 한 언어만 다른 호출 모양을 남기는 임시 adapter 추가

## 검증 기준

아래가 모두 충족될 때만 완료로 본다.

1. high-level framework API는 업무 객체를 직접 받는다.
2. reply는 typed object로 직접 돌아온다.
3. explicit `reply(...)`도 업무 객체를 직접 받는다.
4. `joinSpot`도 업무 객체 request / reply 표면으로 맞춰진다.
5. packet name 자동 추론이 표준 sample에서 동작한다.
6. `.packetName(...)`은 추론 불가능한 예외 경로에만 남는다.
7. bindings base `Message`는 bytes container 계약만 남는다.
8. codec extension은 object <-> `Message` encode/decode helper만 남는다.
9. sample과 guide의 기본 경로에서 `Message.from(...)`, `.ToJson()`, `.ToProto()`,
   `then(decode...)`, raw `Buffer` reply 경로가 사라진다.
10. Node, Java/Kotlin, .NET, C++ sample이 같은 high-level 호출 모양을 유지한다.
11. Go, Python, Rust bindings spec도 같은 책임 경계를 설명한다.
12. independent review에서 material issue가 남지 않는다.

## 권장 검증 명령

작업 중과 마지막에 아래 검사를 반복한다.

```bash
rg -n "Message\\.from\\(|\\.ToJson\\(|\\.ToProto\\(|submit<Buffer>|then\\(decode" \
  framework/languages/node/samples \
  framework/languages/java/samples/java \
  framework/languages/java/samples/kotlin \
  framework/languages/dotnet/samples \
  framework/languages/cpp/samples
```

high-level sample source에서는 위 helper 경로가 기본 호출 모양으로 남지 않아야 한다.

```bash
rg -n "\\.packetName\\(" \
  framework/languages/node/doc/guide \
  framework/languages/java/doc/guide \
  framework/languages/dotnet/doc/guide \
  framework/languages/cpp/doc/guide
```

guide 문서에서 `.packetName(...)`은 예외 경로 설명에만 남아야 한다. 기본 사용 예시에서는
남지 않아야 한다.

```bash
rg -n "\\.packetName\\(" \
  framework/languages/node/samples \
  framework/languages/java/samples/java \
  framework/languages/java/samples/kotlin \
  framework/languages/dotnet/samples \
  framework/languages/cpp/samples
```

sample source에서 나온 `.packetName(...)` 결과는 모두 high-level outbound call site인지,
아니면 `header.packetName()` 같은 low-level runtime/session code인지 분류해서 본다.
high-level outbound call site라면 추론 불가능한 예외 경로에만 남아야 한다.

testkit, contract test, connector 내부 test, cross-language smoke는 위 grep gate 대상이 아니다.

```bash
rg -n "value\\(|withPacketName\\(|Message\\.from\\(object|auto.*packet" \
  bindings/node/src \
  bindings/java/src \
  bindings/dotnet/src \
  bindings/cpp/include \
  bindings/cpp/src \
  bindings/go/contracts \
  bindings/python/src \
  bindings/rust/src
```

제거 대상 경로가 남지 않아야 한다.

```bash
(cd framework/languages/node && npm run verify:p0 && npm run verify:samples)
./framework/languages/java/samples/run_samples.sh
(cd framework/languages/java && ./gradlew :zlink-framework-java-samples:buildAllSamples)
./framework/languages/dotnet/samples/run_samples.sh
./framework/languages/cpp/samples/run_samples.sh
```

현재 저장소의 표준 sample gate와 빌드 검증은 위 runner를 기준으로 사용한다.

```bash
git diff --check -- \
  doc/spec/draft/framework-object-messaging-surface.ko.md \
  doc/spec/draft/bindings-message-boundary-alignment.ko.md \
  doc/spec/draft/framework-bindings-object-messaging-rollout-plan.ko.md
```

통과해야 한다.

슬라이스별 추가 unit / contract test 명령은 해당 작업 시점에 실제 실행한 명령을 완료
보고에 덧붙인다.

## 완료 보고 형식

각 슬라이스 완료 보고는 아래 형식으로 남긴다.

1. 변경 파일 목록
2. bindings 변경 요약
3. framework 변경 요약
4. sample / doc 변경 요약
5. 실행한 검증 명령
6. 독립 리뷰 결과
7. 남은 리스크 또는 다음 슬라이스 진입 조건

## 한 문장 요약

이 작업은 두 초안을 동시에 기준으로 삼아, 각 언어 슬라이스에서 `bindings -> framework
-> sample -> doc -> review`를 한 묶음으로 끝내면서 object messaging 표면과 codec
책임 경계를 전 저장소에 같은 의미로 정렬하는 rollout이다.
