# 저장소 전체 재검토 — 보안·버그·계약·테스트 리뷰

- 작성일: 2026-06-14
- 검토자: 코드 리뷰어 (독립 재검토, read-only)
- 대상: `core/` 런타임 + `framework/languages/{cpp,node,java,dotnet}` + `bindings/{c,cpp,go,rust,node,python,java,dotnet}`
- 방식: 기존 리포트의 "종결" 주장을 신뢰하지 않고 실제 소스/헤더/테스트를 직접 열어 코드 흐름과 재현 가능성 기준으로 재검증. 이번 패스는 **(A) 직접 코드 흐름 추적으로 새 결함을 찾고, (B) 이전 실행의 테스트 게이트 실패를 정적으로 재확인**하는 두 축으로 진행했다.
- **결론 요약: 추가 이슈 있음.** 직접 코드 추적으로 새로 확정한 결함 3건(High 1 / Medium 2)과, 테스트 게이트 실패 2건(Medium, 정적으로 재확인), 보강 항목들을 아래에 정리한다. 기존 리포트가 닫았다고 한 core 오버플로/길이 클램프, 헤더 버전 정합성, Node/Java/.NET framework 수신·압축 상한은 실제 코드에서 정상 동작을 확인했다.

---

## 1. Findings

### F-1 (High) — C++ 바인딩: 핸들러 등록 후 move 시 콜백 userdata use-after-free

- **상태(2026-06-14 구현 패스)**: 수정 완료. send-ready/packet handler 상태를 socket 객체 주소가 아니라 move 뒤에도 주소가 안정적인 힙 상태(`socket_callback_state_t`)에 저장하고, 네이티브 `userdata`도 그 상태 포인터로 등록하도록 바꿨다. "핸들러 등록 → `std::move` → 옛 객체 소멸 → 이벤트 발화" 경로를 send-ready와 packet 양쪽 회귀 테스트로 고정했다.
- **위치**:
  - `bindings/cpp/src/Runtime/Sockets/socket.cpp:46-48` — move ctor/assign `= default`
  - `bindings/cpp/src/Runtime/Sockets/socket.cpp:423-437` — `set_send_ready_handler`, 네이티브 `userdata = this`
  - `bindings/cpp/src/Runtime/Sockets/stream.cpp:35-54` — `set_packet_handler`, 네이티브 `userdata = this`
  - 멤버/계약: `bindings/cpp/include/zlink/Contracts/Sockets/socket_contracts.hpp:44-47,168,171`, `stream_socket.hpp:24-25,52`

- **문제 설명**: `socket_t`는 공개 계약상 move 가능(`socket_t(socket_t&&) noexcept;`, copy는 `= delete`)이다. `set_send_ready_handler`는 네이티브 콜백 트램펄린의 `userdata`로 **`this`(=`socket_t*`)** 를 등록하고, 실제 핸들러 `std::function`은 `socket_t`의 멤버 `_send_ready_handler`에 둔다. `stream_socket_t::set_packet_handler`도 동일(`userdata=this`, `_packet_handler` 멤버). move 연산이 `= default`이므로 move 시 내부 `unique_ptr<socket_handle_t>`(네이티브 소켓 소유권)와 `std::function` 멤버는 새 객체로 옮겨가지만, **네이티브 콜백에 박힌 `userdata`는 여전히 옛 `socket_t` 주소를 가리킨다.**

- **실제 영향**:
  ```cpp
  stream_socket_t s = ctx.open_stream(...);
  s.set_packet_handler(on_packet);
  registry.push_back(std::move(s));   // s의 this가 네이티브 userdata에 박제됨
  // 지역 s 파괴(빈 unique_ptr라 네이티브 소켓은 registry가 소유, 살아 있음)
  // 패킷 도착 -> 트램펄린이 self(=옛 s 주소, 소멸됨) 역참조 -> use-after-free
  ```
  - move 직후 옛 객체가 살아 있어도 `self->_packet_handler`가 비어(옮겨감) **핸들러가 조용히 호출되지 않는 정합성 버그**가 먼저 발생한다.
  - 옛 객체가 소멸되면 살아 있는 네이티브 소켓이 콜백 발화 시 해제된 주소를 역참조 → **UAF**(읽기 후 잠재적 임의 함수 호출). 소켓을 컨테이너에 move-insert 하는 것은 자연스러운 C++ 패턴이므로 안전 공개 API만으로 도달 가능하다.

- **수정 제안** (택1):
  1. 핸들러 상태와 `userdata`를 **안정된 힙 위치(pImpl `socket_handle_t`)** 로 옮기고 `userdata`를 그 포인터로 등록 → move-safe.
  2. move 시 핸들러를 새 `this`로 **재등록**하는 사용자 정의 move 연산.
  3. 최소 조치로 핸들러 등록 후 move 금지(가드/문서) 또는 socket을 non-movable로 계약 변경.

- **검증/재현**: 위 코드 흐름. "핸들러 등록 → move → 옛 객체 소멸 → 이벤트 발화" ASan 회귀 테스트로 고정 필요. `bindings/cpp/tests/`에 해당 시나리오 없음. 기존 보고서 묶음에서 이 C++ 바인딩의 move-after-handler 문제를 다룬 항목은 찾지 못했다 — **신규 발견.**

---

### F-2 (Medium) — C++ framework: LZ4 압축 해제 결과 길이 무제한 할당(압축 폭탄); README의 "안전" 기술은 사실 오류

- **상태(2026-06-14 구현 패스)**: 수정 완료. `decompress`가 `max_decompressed_size`를 받아 `original_size`를 할당 전에 거부하고, 두 수신 decode 경로가 `max_receive_payload_size`를 전달한다. 합성 LZ4 payload 회귀 테스트와 정상 roundtrip을 `test_cpp_stream_connector.cpp`에 고정했다.
- **위치**: `framework/languages/cpp/connector/core/src/runtime/protocol/compression/lz4_compression_codec.cpp:86-93`
- **문제 설명**:
  ```cpp
  const auto original_size = read_u32 (input);            // 공격자 제어(압축 해제 후 크기)
  if (original_size > INT32_MAX) { throw ... }            // 2GB 상한만 검사
  ...
  std::string output (original_size, '\0');               // ★ 최대 2GB 선할당
  LZ4_decompress_safe (input+4, output.data(), ...);
  ```
  코덱 시그니처(`framework/languages/cpp/connector/core/src/runtime/protocol/compression/lz4_compression_codec.hpp:13-14`)에 `max_*` 인자가 없고, **수신 decode 경로 두 곳**(`zlink_stream_calls.cpp:107`의 `decode_packet`, `framing.cpp:63`)이 모두 무인자 `decompress(payload)`로 부른다. 와이어 `payload_size`(=압축된 입력)는 `frame_codec`이 `max_receive_payload_size`(기본 64KiB)까지 차단하지만 이는 **압축 입력 크기**일 뿐 출력과 무관하다. 64KiB 압축 프레임에 `original_size=2GB`를 실으면 `LZ4_decompress_safe`가 실패로 throw 하기 **이전에** 2GB `std::string`이 할당된다.
- **실제 영향**: LZ4가 활성화된 stream 연결에서 악의적/탈취 피어(평문 `tcp://`/`ws://` MITM 포함)가 프레임당 최대 2GB 일시 할당을 강제 → 반복 시 메모리 압박/OOM/크래시(증폭 ~64KiB→2GB). `LZ4_decompress_safe`는 오버런만 막고 **할당 상한은 없다.** Node/Java/.NET 3개 언어는 동일 경로에서 출력 길이를 할당 전 클램프하므로, **같은 결함이 C++에만 남은 교차언어 불일치**(중점 확인 #10).
- **문서 불일치**: `core/doc/report/odl/README.ko.md:104` 및 공통 #2 표가 "C++ lz4 codec 자체는 `LZ4_decompress_safe`로 안전"이라 단언 → 메모리 안전성과 할당 상한을 혼동한 **사실 오류**, 정정 필요.
- **수정 제안**: `decompress`에 `max_decompressed_size`(= `max_receive_payload_size`) 인자를 추가하고 `original_size > max`이면 **할당 이전에** 거부. 호출부 **두 곳**(`zlink_stream_calls.cpp:107`, `framing.cpp:63`)에서 연결 수신 상한 전달.
- **검증/재현**: `original_size`에 큰 값을 넣은 합성 프레임 단위 테스트(현재 부재). 정상 회귀는 상한 이하 정상 페이로드로 확인.

---

### F-3 (Medium) — Go 바인딩: `Spot.Close()` 시 콜백 dispatcher goroutine 누수

- **상태(2026-06-14 구현 패스)**: 수정 완료. `spotCore.Close()`가 세 callback handle을 모두 `releaseCallbackHandle(...)`로 정리하도록 바꿨고, `TestSpotCallbackDispatchersStopOnClose`로 반복 close 후 goroutine 수가 baseline 근처로 돌아오는지 검증한다.
- **위치**: `bindings/go/internal/native/spot_core.go:62-70`
- **문제 설명**: spot의 `sendReadyHandle`(`bindings/go/internal/native/spot.go:356-359`)·`dispatchHandle`(`bindings/go/internal/native/spot.go:424-427`)은 `cgo.Handle`로 감싼 콜백 state를 가리키며, 각 state는 `newCallbackDispatcher()`로 **워커 goroutine(`loop()`)을 띄운다**(`callbacks.go:43-51,190-216`). 올바른 정리는 `releaseCallbackHandle()` → `registration.close()` → `dispatcher.close()`로 goroutine을 종료한 뒤 `Delete()` 한다(`callbacks.go:263-271`). 그러나 `spotCore.Close()`는 **bare `.Delete()`** 만 호출한다:
  ```go
  if sendReadyHandle != 0 { sendReadyHandle.Delete() }   // dispatcher.close() 생략
  if dispatchHandle  != 0 { dispatchHandle.Delete() }    // dispatcher.close() 생략
  ```
  `.Delete()`는 cgo 핸들 매핑만 제거할 뿐 dispatcher를 닫지 않으므로 `loop()` goroutine이 `cond.Wait()`에서 영원히 블록되어 GC되지 않는다(실행 중 goroutine은 GC 루트).
- **실제 영향**: 핸들러가 등록된 spot을 닫을 때마다 dispatcher goroutine이 **최대 2개씩 누수**(sendReady + dispatch). spot을 반복 생성/파기하는 장기 프로세스에서 goroutine·메모리 누수 누적. (참고: dispatch 트램펄린은 동기 실행이라 그 dispatcher는 쓰이지도 않으면서 누수된다.)
- **일관성 결함**: 동일 정리를 `socketCore.Close()`(`socket_core.go:91-103`), `monitor.Close()`(`monitor.go:191,206`), timer(`poller_timer.go:196,211`)는 모두 `releaseCallbackHandle()`로 올바르게 한다. spot 자신의 **재등록 경로**(`bindings/go/internal/native/spot.go:356-357`, `:424-425`)조차 `releaseCallbackHandle()`을 쓴다. 즉 패턴 수정이 socket/monitor/timer엔 적용됐는데 **`spotCore.Close()`만 누락**(중점 확인 #6, #10).
- **수정 제안**: `spot_core.go`의 `sendReadyHandle.Delete()`/`dispatchHandle.Delete()`(및 항상 0이라 무해한 `subscribeHandle`)를 `releaseCallbackHandle(...)` 호출로 교체. 잠금 밖 호출 구조 유지 가능.
- **검증/재현**: `runtime.NumGoroutine()` 회귀 테스트로 "spot 생성 → SetSendReadyHandler/OnDispatchEvent → Close" 반복 후 goroutine 수 안정 확인(현재 부재).

---

### F-4 (Medium) — core: thread-safe 공개 계약 문서 회귀 테스트 실패 (이전 게이트 실행, 정적 재확인)

- **상태(2026-06-14 구현 패스)**: 수정 완료. `doc/internals/threading-model.ko.md`의 동시 접근 섹션에 공개 socket handle의 계층형 thread-safe 계약을 복원했고, `test_thread_safe_contract_policy`가 통과했다.
- **위치**: `core/tests/integration/test_thread_safe_contract_policy.cpp:176-186` ↔ `doc/internals/threading-model.ko.md`
- **문제 설명**: 테스트가 `threading_doc_ko = doc/internals/threading-model.ko.md`를 읽어 `assert_text_present(threading_doc_ko, "여러 스레드에서 동시 사용 가능")`를 요구한다(`:186`). 직접 grep 결과 이 토큰은 **해당 파일은 물론 `doc/` 어디에도 없다**(`rg "여러 스레드에서 동시 사용 가능" doc/` 무결과). 결정론적 실패.
- **실제 영향**: thread-safe 공개 계약(callback/monitor/stream/spot/discovery close·race 판단 기준)이 문서·테스트에서 같은 의미로 유지되는지를 현재 게이트로 증명할 수 없다. 단순 문구 누락으로 취급하면 안 된다.
- **수정 제안**: 실제 공개 계약을 확인한 뒤 한국어 threading 문서와 테스트 기대값을 함께 맞춘다. 계약이 안 바뀌었으면 문서에서 빠진 설명 복원, 바뀌었으면 테스트 기대값+spec을 같은 변경으로 수정.
- **검증**: `core/tests/integration/test_thread_safe_contract_policy.cpp:176-186` 열람 + `rg` 토큰 부재 확인.

---

### F-5 (Medium) — .NET framework: regression matrix 문서 불일치(정적 재확인) + E2E test host native crash(이전 게이트 실행)

- **상태(2026-06-14 구현 패스)**: 문서 불일치 수정 완료. `regression-test-matrix.ko.md`의 Entry Spot actor 항목명을 테스트와 실제 mailbox 계약에 맞춰 `Entry Spot actor mailbox dispatch`로 고쳤다. E2E native crash는 현재 checkout에서 재현되지 않았고, `Zlink.Framework.E2ETests` 전체 96개가 통과했다.
- **위치 / 문제**:
  1. **문서 불일치(결정론적, 정적 재확인)**: `framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Documentation/Regression.cs:237`이 `Assert.Contains("Entry Spot actor mailbox dispatch", matrix)`를 요구한다. `framework/languages/dotnet/doc/internals/regression-test-matrix.ko.md:156`에는 비슷한 `Entry Spot actor dispatch serialization` 항목이 있지만, 테스트가 요구하는 정확한 문자열 `Entry Spot actor mailbox dispatch`는 **없다**(grep 확인). 테스트 실패.
  2. **E2E native crash(이전 게이트 실행에서 기록)**: `dotnet test Zlink.Framework.sln` 실행 시 `Zlink.Framework.E2ETests` test host가 `core/src/runtime/utils/mutex.hpp:108`의 `pthread_mutex_lock` 실패로 abort. 47개 통과 후 프로세스 종료 → 전체 실패.
- **실제 영향**: actor mailbox dispatch/Entry Spot 직렬화 관련 문서·테스트 정합성을 증명 불가. E2E crash는 close 경로·native lifecycle·공유 runtime 사용 방식의 문제를 숨길 수 있어 원인 분리 필요. 이번 검토만으로 crash를 UAF/double close로 단정할 수는 없다.
- **수정 제안**: 먼저 matrix 기대 항목/문서를 맞춘다. 그 뒤 E2E crash를 단일 테스트 필터로 좁히고 native mutex 소유 객체의 생성/close/dispose 순서를 추적, 재현 시 ASAN/TSAN 또는 core debug 빌드로 close race 확인.
- **검증**: matrix 문서 수정 후 `DotNetRegressionMatrix_Includes_ExecutionSerialization_Guards`가 통과했다. `Zlink.Framework.E2ETests` 단독 실행은 96개 모두 통과해 이전 crash는 현재 checkout에서 재현되지 않았다. 다만 `dotnet test Zlink.Framework.sln` 전체 실행은 E2E testhost crash 또는 장기 정지로 별도 관찰되어, solution-level 병렬/호스트 격리 문제는 후속 추적이 필요하다.

---

### F-6 (Low~Medium) — native buffer view 수명: 문서화는 됐으나 테스트로 고정되지 않음 (Python `memoryview`는 misuse 시 UAF 도달)

- **상태(2026-06-14 구현 패스)**: 보강 완료. Python 수신 part의 `data`는 native receive buffer alias 대신 Python-owned snapshot memoryview를 반환하도록 pure Python owner와 C extension `NativeReceivedPartsOwner`를 함께 수정했다. Go `Data()`와 .NET `AsSpan`은 기존 public 계약처럼 message lifetime 안에서만 view를 만들 수 있게 두고, close 뒤 새 view 접근 차단과 snapshot copy 보존을 회귀 테스트로 고정했다.
- **위치 / 성격**:
  - Python: `bindings/python/src/zlink/_runtime/messaging/message_materializer.py:71`(`data`), `:479`, `bindings/python/src/zlink/_runtime/handles/native_support.py:521` — `memoryview((c_ubyte*size).from_address(ptr))`로 네이티브 버퍼 **alias** 반환. `close()`(`message_materializer.py:78-87`)는 `zlink_msg_close`로 버퍼를 해제하지만 **이미 반환된 memoryview를 무효화하거나 close에 묶지 않는다.** docstring("valid while ... open")만 존재 → `view=msg.data; msg.close(); bytes(view)`는 UAF/segfault에 도달.
  - Go: `bindings/go/internal/native/message.go:239-249`(`Data()`)는 `unsafe.Slice` alias. 무효화 테스트 없음(`message_test.go`는 복사본 `Bytes()`만 검증).
  - .NET: `Message.Native.cs:99,116`(`AsSpan/AsReadOnlySpan`)는 alias. `EnsureValid()`가 취득 시점만 막음(단 `ref struct` 수명 규칙이 사고를 어렵게 함).
- **문제 설명**: 중점 확인 #5("native buffer view 수명 규칙이 public contract와 **테스트**로 고정") 관점에서 현재는 **doc 주석 수준**이며 close 후 접근을 잡는 테스트가 없다. 기존 리포트들은 "문서화 완료"로만 기술했고 "테스트로 고정"은 검증되지 않는다.
- **반대 확인(오탐 방지)**: Node는 `addon_message_values.h`에서 `zlink_msg_move`로 소유권을 external buffer로 이전+GC finalizer close → dangling 없음. Rust는 `as_bytes/data_mut`가 `&self` 수명에 묶여 borrow checker가 컴파일 타임에 UAF 차단. Java는 FFM `MemorySegment`+Arena 스코프 관리. 이 3개는 실제 안전.
- **수정 제안**: 각 언어에 "close 후 view 접근은 정의되지 않음"을 잠그는 회귀 테스트 추가, 가능하면 Python은 close 시 보유 view keepalive/무효화 또는 copy fallback 검토.

---

### F-7 (Low) — Node framework: workspace dependency 해석 실패로 게이트 미실행 (이전 게이트 실행)

- **상태(2026-06-14 구현 패스)**: 현재 checkout에서는 추가 준비 없이 재현되지 않는다. `packages/framework`와 `packages/stream-connector`가 `@zlink-systems/zlink`를 `file:../../../../../bindings/node`로 참조하고, `npm run build && npm run typecheck && npm test`가 통과했다. 수신 한도, WebSocket 한도, LZ4 압축 해제 한도 테스트도 같은 `npm test` 실행에서 통과했다.
- **위치**: `framework/languages/node`에서 `npm run build && npm run typecheck && npm test`가 `@zlink-systems/zlink` 모듈 해석 실패로 중단. 실패 지점 예: `packages/framework/src/runtime/actors/index.ts:27` import, `tsconfig.base.json:40` 매핑.
- **실제 영향**: Node framework actor/stream/compression/close 경로 테스트를 현재 workspace 명령으로 검증 못 함. binding 자체 `npm test`는 통과했으나 framework 보안 회귀 결과는 비어 있다. (보안 결함 아님 — 게이트 재현성 문제.)
- **수정 제안**: workspace dependency 연결/타입 해석 조건을 명시해 같은 checkout에서 추가 준비 없이 framework Node 게이트가 재현되게 정리한 뒤 수신 한도·WS 누적 한도·LZ4 한도 테스트 재실행.

---

### F-8 (Low) — Java 바인딩: 전체 runner 첫 실행 `:integrationTest` 일시 실패(단독 재실행 통과)

- **상태(2026-06-14 구현 패스)**: runner 보강 완료. 각 Gradle task와 sample smoke의 stdout/stderr를 `bindings/java/build/test-runner-logs/` 아래 task별 로그로 남기고, 실패 시 해당 로그와 Gradle HTML/JUnit report 위치를 출력하도록 바꿨다.
- **위치**: `bindings/java/tests/run_tests.sh` 첫 실행 `:integrationTest` 실패(요약 `1 failed`), 단독 `./gradlew :integrationTest`는 통과.
- **실제 영향**: 현재 증거만으로 Java binding 보안 결함을 주장할 수 없다. 다만 전체 runner 첫 실패에 별도 failure report가 없어 CI 재발 시 원인 분석이 어렵다.
- **수정 제안**: runner가 실패 task의 stdout/JUnit report 위치를 보존하도록 정리. 재현 시 native library 복사·daemon 격리·test filter 우선 확인.

---

### F-9 (Low) — Rust 바인딩: 콜백 `part_count`/버퍼 `len` 경계 미검증(방어적 깊이)

- **상태(2026-06-14 구현 패스)**: 방어적 보강 완료. native part count는 Rust 바인딩에서 1024개로 상한을 두고, fixed buffer 문자열 변환은 native `len`을 실제 buffer 길이로 clamp한다. core가 정상 값을 주는 기존 공개 동작은 유지한다.
- **위치**: `bindings/rust/src/runtime/service/spot_receive.rs:26-40`(`borrowed_parts_to_messages`), `bindings/rust/src/runtime/sockets/socket/socket_parts_runtime.rs:86-102`(`cstr_buf_to_string`)
- **문제 설명**: 네이티브가 준 `part_count`로 `Vec::with_capacity`/`parts.add(i)` 루프, `len`으로 `from_raw_parts`를 상한 검증 없이 수행. 신뢰된 core 계약에 의존.
- **실제 영향**: core가 올바르면 안전. core가 잘못된 값을 주면 OOB read. 원격 직접 트리거 아님(actor join 콜백 경로). 방어적 보강 항목.
- **수정 제안**: 바인딩에서 `part_count`·`len`에 합리적 상한(또는 `len <= buf.len()` 대조) 추가.

---

## 2. Open Questions

1. **F-2 심각도**: C++ stream에서 LZ4 압축이 기본 활성/협상 범위가 어디까지인지에 따라 노출면이 달라진다. 항상 opt-in이면 Medium, 기본 경로면 High 상향 검토.
2. **Java 코덱 명명(기존 README 부록)**: `ZLinkStreamMessagePack`/`ZLinkStreamProtobuf`가 실제로는 JSON `JsonMapper`를 쓰는 placeholder인지 — 와이어 `codec` 태그와 실제 직렬화 불일치가 의도인지 확인 필요(정확성 항목).

---

## 3. Verification

### 구현 패스에서 실행한 주요 검증

| 항목 | 명령·방법 | 결과 |
|------|-----------|------|
| F-1 C++ binding move-safe callbacks | `bindings/cpp/tests/run_tests.sh` | PASS |
| F-1 C++ binding ASan regression | ASan flags로 `bindings/cpp/build-asan` 구성 후 `ctest -R '^test_cpp_contract_behavior$'` | PASS |
| F-2 C++ framework LZ4 clamp | `ctest --test-dir framework/languages/cpp/build --output-on-failure` | PASS, 42개 통과 |
| F-3 Go callback dispatcher close | `bindings/go/tests/run_tests.sh` | PASS |
| F-4 core threading 문서 계약 | `ctest --test-dir core/build --output-on-failure -R '^test_thread_safe_contract_policy$'` | PASS |
| F-5 .NET regression matrix | `dotnet test Zlink.Framework.sln --filter FullyQualifiedName~DotNetRegressionMatrix_Includes_ExecutionSerialization_Guards` | PASS |
| F-5 .NET E2E crash 재확인 | `timeout 180s dotnet test tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --no-build --logger "console;verbosity=minimal"` | PASS, 96개 통과 |
| F-5 .NET solution 전체 | `dotnet test Zlink.Framework.sln --logger "console;verbosity=minimal"` 및 `--no-build -m:1` 재시도 | 프로젝트 단위 테스트는 통과했지만 E2E testhost crash 또는 장기 정지로 solution-level gate는 미종결 |
| F-6 Python/Go/.NET view lifetime | `bindings/python/tests/run_tests.sh`, Go runner, .NET `test_message` filter | PASS |
| F-7 Node framework gate | `npm run build && npm run typecheck && npm test` | PASS |
| F-8 Java runner | `bindings/java/tests/run_tests.sh` | PASS, 전체 task와 sample smoke 통과 |
| F-9 Rust defensive bounds | `bindings/rust/tests/run_tests.sh && cargo test --manifest-path bindings/rust/Cargo.toml` | PASS |
| core 전체 ctest | `cmake --build core/build --parallel && ctest --test-dir core/build --output-on-failure` | 119개 중 `test_reconnect_ivl` 1회 실패, 같은 테스트 단독 반복 재실행은 통과 |

---

## 4. 결론

**F-1~F-4, F-6~F-9 처리 완료. F-5는 matrix와 단독 E2E crash 재현을 닫았고, solution-level gate는 후속 추적이 필요하다.**

- F-1/F-2/F-3/F-4는 코드·문서·테스트를 수정해 해당 결함을 닫았다.
- F-5의 matrix 불일치와 단독 E2E crash 재현은 닫혔다. solution-level `dotnet test`에서 E2E testhost crash/정지가 남아 있어 별도 후속 추적이 필요하다.
- F-6/F-8/F-9는 방어적 보강과 회귀 테스트/runner 개선을 반영했다.
- F-7은 현재 checkout에서 workspace 해석 실패가 재현되지 않았고 Node framework 전체 gate가 통과했다.
- 남은 별도 확인 사항은 Java 코덱 명명(`ZLinkStreamMessagePack`/`ZLinkStreamProtobuf`)의 의도 확인뿐이며, 이번 보안 finding 처리 범위 밖 정확성 항목이다.
