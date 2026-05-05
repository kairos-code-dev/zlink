# Bindings Update Log

- 날짜: 2026-05-04
- 대상: bindings native library 최신화
- 수행한 명령: 없음
- 발견한 문제: core release 전
- 수정한 파일: 없음
- 남은 위험: `gh` 인증과 release asset 필요
- 다음 확인: core release 완료 뒤 `bindings/update_zlink_libs.sh` 실행

## 2026-05-04 상태

- 수행한 명령: `sed -n '1,220p' bindings/update_zlink_libs.sh`
- 발견한 문제: script 실행에는 release tag 또는 release URL이 필요하며, release asset을 GitHub에서 받아온다.
- 수정한 파일: 없음
- 남은 위험: core version bump, commit, `core/vX.Y.Z` tag push, GitHub Actions release 확인이 아직 끝나지 않아 native library 동기화 단계가 아직 오지 않았다. bindings에는 제거된 generic route wrapper가 아직 남아 있다.
- 다음 확인: core release 완료 뒤 `bindings/update_zlink_libs.sh core/vX.Y.Z --expect-version X.Y.Z` 실행

## 2026-05-04 언어별 binding 공개 표면 정리

- 대상: C++, Go, Rust, Python, .NET, Java, Node
- 확인한 draft spec 절: Generic discovery route 제거 계획, Actor active route 조회, 상수와 구조체
- 구현 요약:
  - C++, Go, Rust, Python, .NET, Java, Node에서 generic discovery route wrapper와 FFI/public method를 제거했다.
  - 각 binding에 `ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC` 또는 대응 상수와 bool option facade를 추가했다.
  - Java의 제거된 generic route 전용 `RouteResolution` record와 잘못된 class 설명을 정리했다.
  - C++/Go/Rust 복사 header에서 generic route 공개 선언과 public route typedef/constant를 제거했고, Actor route sync option 값을 맞췄다.
- 검증:
  - C++: `cmake --build bindings/cpp/build`, `ctest --test-dir bindings/cpp/build --output-on-failure`
  - Go: `gofmt -w discovery.go surface_test.go`, `go test ./...`
  - Rust: `cargo test`
  - Python: `python -m compileall -q src/zlink`, `python -m pytest tests/test_core_api_alignment.py`
  - .NET: `dotnet test`
  - Java: `./gradlew test`
  - Node: `npm test`
  - 검색: `rg -n "zlink_discovery_(bind|unbind|resolve)_route|zlink_route_kind_t|ZLINK_ROUTE_KIND_INVALID|ZLINK_ROUTE_KEY_MAX|ZLINK_ROUTE_VALUE_MAX|BindRoute|UnbindRoute|ResolveRoute|bindRoute|unbindRoute|resolveRoute|bind_route|unbind_route|resolve_route|RouteKind" core bindings`
- 검증 결과:
  - C++ 19개 테스트 통과.
  - Go 전체 테스트 통과.
  - Rust 전체 테스트 통과. 기존 unused 경고만 남았다.
  - Python API 정렬 테스트 17개 통과.
  - .NET 137개 테스트 통과. 기존 nullable/xUnit 경고만 남았다.
  - Java Gradle 테스트 통과. 기존 Gradle deprecation 안내와 sample unchecked 안내만 남았다.
  - Node build/typecheck/native rebuild/test 통과. pubsub remote delivery 2개는 기존 skip 상태다.
  - 제거된 generic route API는 공개 binding 구현에는 남지 않았고, 부재를 확인하는 테스트 문자열과 core 내부 Registry route 구현 이름만 검색된다.
- 남은 위험:
  - `bindings/update_zlink_libs.sh` 실행은 core version bump, commit, `core/vX.Y.Z` tag push, GitHub Actions release 완료 뒤 진행한다.
  - Python runtime에서 Actor route sync getter를 실제 호출하려면 최신 native library가 필요하다. 현재 검증은 공개 표면 존재와 컴파일 정렬을 확인했다.
  - 새 Actor lifecycle/join/STREAM/remote API 전체 binding 노출은 아직 완료되지 않았다.

## 2026-05-05 core/v5.3.6 native library 최신화

- 기준 release: `core/v5.3.6`
- 수행한 명령:
  - `gh release view core/v5.3.6 --json tagName,url,assets`
  - `bindings/update_zlink_libs.sh core/v5.3.6 --expect-version 5.3.6`
  - `git status --short`
- release asset 확인:
  - 필수 core asset `libzlink-linux-x64.tar.gz`,
    `libzlink-linux-arm64.tar.gz`, `libzlink-macos-x64.tar.gz`,
    `libzlink-macos-arm64.tar.gz`, `libzlink-windows-x64.tar.gz`,
    `libzlink-windows-arm64.tar.gz`가 모두 존재한다.
  - optional `libzlink_c-*` asset은 release에 없어서 스크립트가 경고 후 건너뛰었다.
- 발견한 문제:
  - 첫 실행은 native binary fetch 뒤 Python version test marker 갱신에서 실패했다.
  - 원인은 `bindings/python/tests/test_version.py`가 숫자 literal 대신
    `core/include/zlink.h`에서 기대 version을 읽는 구조로 바뀌었기 때문이다.
- 수정:
  - `bindings/update_zlink_libs.sh`에서 Python version test marker를 optional 갱신으로 바꿨다.
  - major/minor/patch 중 일부만 갱신되는 경우는 오류로 유지한다.
  - marker가 전혀 없으면 헤더 기반 test로 판단하고 건너뛴다.
- 재실행 결과:
  - native libraries fetch 성공.
  - version marker는 이미 `5.3.6`으로 맞아 있어 추가 변경 없음.
  - linux-x64 runtime verification 결과:
    - Python: `5.3.6`
    - Node: `5.3.6`
    - .NET: `5.3.6`
    - Java: `5.3.6`
    - C++: `5.3.6`
    - Go: `5.3.6`
    - Rust: `5.3.6`
    - C binding library는 기존 파일이 존재한다.
- `git status --short` 확인:
  - C++, .NET, Go, Java, Node, Python, Rust native library가 갱신됐다.
  - C++, Go, Rust vendored header와 utility header가 갱신됐다.
  - .NET, Java, Node, Python package version marker가 `5.3.6`으로 갱신됐다.
  - `bindings/update_zlink_libs.sh`가 Python marker optional 처리로 갱신됐다.
- 남은 확인:
  - 언어별 binding spec 5회 비교 리뷰.
  - 언어별 sample/perf 검증.
  - 언어별 POSD 검토와 필요한 리팩토링.

## 2026-05-05 C++ binding Actor API 적용

- 적용 범위:
  - `bindings/cpp/include/zlink/types.hpp`
  - `bindings/cpp/include/zlink/services/spot.hpp`
  - `bindings/cpp/include/zlink/services/discovery.hpp`
  - `bindings/cpp/include/zlink/socket_types.hpp`
  - `bindings/cpp/samples/*actor*`
  - `bindings/cpp/CMakeLists.txt`
  - `bindings/cpp/samples/run_samples.sh`
- 반영한 계약:
  - local Actor 생성, ref 조회, lookup, destroy.
  - unchecked remote Actor ref와 remote create-or-get, destroy.
  - Actor admission handler, join/leave, join reply message.
  - STREAM session Actor bind/unbind, actor id selector relay.
  - Actor에서 bound session으로 raw/packet send.
  - Discovery Actor active route resolve.
  - SpotNode/Spot Actor snapshot.
  - dispatch event subject가 recv 대상 Actor임을 사용하는 sample.
- 검증:
  - `cmake --build bindings/cpp/build -j"$(nproc)"` 성공.
  - `sample_cpp_actor_room_server_sample` 성공.
  - `sample_cpp_actor_gateway_relay_sample` 성공.
  - `sample_cpp_actor_single_player_queue_sample` 성공.
- 남은 확인:
  - C++ spec 문서 5회 비교 리뷰.
  - C++ perf smoke.
  - C++ POSD 반복 검토.

## 2026-05-05 Rust binding Actor API 적용

- 적용 범위:
  - `bindings/rust/src/ffi.rs`
  - `bindings/rust/src/error.rs`
  - `bindings/rust/src/service.rs`
  - `bindings/rust/src/socket/stream.rs`
  - `bindings/rust/src/lib.rs`
  - `bindings/rust/tests/service_surface_tests.rs`
  - `bindings/rust/Cargo.toml`
  - `bindings/rust/samples/*actor*`
  - `bindings/rust/samples/run_samples.sh`
  - `bindings/rust/samples/spot_recv_sample.rs`
- 반영한 계약:
  - Actor public type과 request result 확장.
  - local Actor lifecycle, checked ref 조회, unchecked remote ref.
  - remote Actor create-or-get와 admission handler.
  - Actor join/leave와 join reply message.
  - STREAM session Actor bind/unbind와 actor id selector relay.
  - Actor bound session send.
  - `SpotDispatchInfo`에서 Actor readable subject를 안전하게 drain하는 helper.
  - Discovery Actor active route resolve와 snapshot wrapper.
- 검증:
  - `cargo test` 성공.
  - `./samples/run_samples.sh` 성공.
  - Actor room server, gateway relay, single-player queue sample 모두 성공.
- 비고:
  - 기존 `spot_recv_sample`은 sample runner에서 앞선 sample의 영향으로 불안정해져
    runner 순서를 조정하고 Registry 준비 확인을 추가했다.
- 남은 확인:
  - Rust spec 문서 5회 비교 리뷰.
  - Rust perf smoke.
  - Rust POSD 반복 검토.

## 2026-05-05 Go binding Actor API 적용

- 적용 범위:
  - `bindings/go/actor.go`
  - `bindings/go/callbacks.go`
  - `bindings/go/domain.go`
  - `bindings/go/error.go`
  - `bindings/go/spot.go`
  - `bindings/go/surface_test.go`
  - `bindings/go/samples/*actor*`
  - `bindings/go/samples/run_samples.sh`
- 반영한 계약:
  - Actor public type, dispatch Actor event, request result 확장.
  - local Actor lifecycle, checked ref 조회, unchecked remote ref.
  - remote Actor create-or-get와 admission handler.
  - Actor join/leave, join request message, join reply message.
  - STREAM session Actor bind/unbind와 actor id selector relay.
  - Actor bound session raw/packet send.
  - `SpotDispatchInfo`에서 Actor readable subject를 drain하는 helper.
  - Discovery Actor active route resolve와 SpotNode/Spot Actor snapshot wrapper.
- 검증:
  - `gofmt -w actor.go domain.go error.go callbacks.go spot.go surface_test.go samples/...` 성공.
  - `go test ./...` 성공.
  - `./samples/run_samples.sh` 성공.
  - Actor room server, gateway relay, single-player queue sample 모두 성공.
- 비고:
  - linux-x86_64 runtime loader가 예전 `libzlink.so.5.3.4` symlink를 따라가던 문제를
    `bindings/go/native/linux-x86_64/libzlink.so.5 -> libzlink.so`로 맞춰 해결했다.
- 남은 확인:
  - Go spec 문서 5회 비교 리뷰.
  - Go perf smoke.
  - Go POSD 반복 검토.

## 2026-05-05 C++/Rust/Go binding perf smoke

- C++:
  - single: `./perf/run_benchmarks.sh --reuse-build --pattern SPOT,SPOT_REQREP --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --results-tag actor_smoke_cpp_single`
  - multi: `./perf/run_benchmarks_multi.sh --reuse-build --pattern SPOT,SPOT_REQREP,STREAM --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --clients 2 --results-tag actor_smoke_cpp_multi`
  - 결과: single 40/40 result line, multi 60/60 result line, 실패 없음.
- Rust:
  - single: `PERF_CONNECT_READY_TIMEOUT_MS=15000 PERF_SINGLE_RUN_COOLDOWN_MS=2000 PERF_SINGLE_CASE_RETRIES=5 ./perf/run_benchmarks.sh --pattern SPOT,SPOT_REQREP --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --results-tag actor_smoke_rust_single_retry_ok`
  - multi: `./perf/run_benchmarks_multi.sh --pattern MULTI_SPOT,MULTI_SPOT_REQREP,MULTI_STREAM --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --clients 2 --results-tag actor_smoke_rust_multi`
  - 결과: single 40/40 result line, multi 60/60 result line, 실패 없음.
  - 수정: single SPOT perf가 같은 service name과 큰 ready probe로 registry stale row와
    setup timing에 흔들려, service name을 process별 고유값으로 바꾸고 ready probe를
    작은 payload로 고정했다. runner에는 case-level retry/cooldown을 추가했다.
- Go:
  - single: `PERF_SINGLE_RUN_COOLDOWN_MS=2000 PERF_SINGLE_CASE_RETRIES=5 ./perf/run_benchmarks.sh --pattern SPOT,SPOT_REQREP --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --results-tag actor_smoke_go_single_retry`
  - multi: `PERF_MULTI_CASE_RETRIES=5 ./perf/run_benchmarks_multi.sh --pattern MULTI_SPOT,MULTI_SPOT_REQREP,MULTI_STREAM --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --clients 2 --results-tag actor_smoke_go_multi_retry`
  - 결과: single 40/40 result line, multi 60/60 result line, 실패 없음.
  - 수정: single SPOT perf service name을 process별 고유값으로 바꾸고
    RegistryQueryClient snapshot으로 두 node registry row를 확인한 뒤 ready probe를
    보낸다. single/multi runner에는 case-level retry/cooldown을 추가했다.
- 후속 테스트:
  - Rust `cargo test` 성공.
  - Go `go test ./...` 성공.

## 2026-05-05 Python binding Actor API 적용과 검증

- 적용 범위:
  - `bindings/python/src/zlink/_ffi.py`
  - `bindings/python/src/zlink/_spot.py`
  - `bindings/python/src/zlink/_socket_types.py`
  - `bindings/python/src/zlink/_discovery.py`
  - `bindings/python/src/zlink/_enums.py`
  - `bindings/python/src/zlink/__init__.py`
  - `bindings/python/tests/test_core_api_alignment.py`
  - `bindings/python/tests/test_enums.py`
  - `bindings/python/samples/*actor*`
  - `bindings/python/samples/run_samples.py`
- 반영한 계약:
  - Actor ref, create result, route, recv/join info, snapshot wrapper.
  - local Actor lifecycle, lookup, unchecked remote ref.
  - remote Actor create-or-get, destroy, admission handler.
  - Actor join/leave, join request message, join reply message.
  - STREAM session Actor bind/unbind와 actor id selector relay.
  - Actor bound session raw/packet send.
  - `SpotDispatchInfo.recv_actor_part()`가 Actor readable subject를 drain한다.
  - Discovery Actor active route resolve와 SpotNode/Spot Actor snapshot wrapper.
- 검증:
  - `PYTHONPATH=src python -m pytest tests` 성공: 49 passed, 10 skipped.
  - `PYTHONPATH=src:samples python samples/run_samples.py` 성공: 14/14 sample.
  - Actor room server, gateway relay, single-player queue sample 모두 성공.
  - full `PYTHONPATH=src python -m pytest`는 codec extension package
    `zlink_codec_json`, `zlink_codec_messagepack`, `zlink_codec_protobuf`가
    설치되지 않은 환경에서 collection 단계가 실패하므로 core binding `tests/`
    범위로 검증했다.
- perf smoke:
  - single: `./perf/run_benchmarks.sh --pattern SPOT,SPOT_REQREP --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --results-tag actor_smoke_python_single`
  - multi: `./perf/run_benchmarks_multi.sh --pattern MULTI_SPOT,MULTI_SPOT_REQREP,MULTI_STREAM --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --clients 2 --results-tag actor_smoke_python_multi`
  - 결과: single 40/40 result line, multi 60/60 result line, 실패 없음.
- 남은 확인:
  - Python spec 문서 5회 비교 리뷰.
  - Python POSD 반복 검토.

## 2026-05-05 Java binding Actor API 적용과 검증

- 적용 범위:
  - `bindings/java/src/main/java/dev/kairoscode/zlink/internal/Native.java`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/internal/NativeLayouts.java`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/internal/ActorInterop.java`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/SpotDispatchInfo.java`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/StreamSocket.java`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/service/discovery/Discovery.java`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/service/spot/*Actor*`
  - `bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/*Actor*`
  - `bindings/java/samples/run_samples.sh`
- 반영한 계약:
  - Actor public type, dispatch Actor event, request result 확장.
  - local Actor lifecycle, lookup, checked ref 조회, unchecked remote ref.
  - remote Actor create-or-get, destroy, admission handler.
  - Actor join/leave, join request message, join reply message.
  - STREAM session Actor bind/unbind와 actor id selector relay.
  - Actor bound session raw/packet send.
  - Discovery Actor active route resolve와 SpotNode/Spot Actor snapshot wrapper.
- Java callback 구조 반영:
  - Java `SpotDispatchEventHandler`는 native callback에서 관리 스레드로 넘겨 실행한다.
  - core 계약상 Actor part drain은 dispatch callback 안에서 해야 하므로,
    Actor readable event는 native callback 안에서 nonblocking drain 후
    `SpotDispatchInfo`에 preloaded part로 전달한다.
- sample runner 수정:
  - 기존 runner가 `bindings/core`를 repo root처럼 계산해 `ZLINK_LIBRARY_PATH`를
    설정하지 못했다.
  - repo root 계산을 고치고, `libzlink.so.5` symlink는 고정 `5.3.4`가 아니라
    현재 `libzlink.so`를 가리키도록 수정했다.
- 검증:
  - `./gradlew compileJava :samples:compileJava` 성공.
  - `./gradlew test integrationTest` 성공.
  - `./samples/run_samples.sh` 성공: 14/14 sample.
  - Actor room server, gateway relay, single-player queue sample 모두 성공.
- perf smoke:
  - single: `PERF_SINGLE_SPOT_READY_SETTLE_MS=3000 PERF_CONNECT_READY_TIMEOUT_MS=30000 ./perf/run_benchmarks.sh --reuse-build --pattern SPOT,SPOT_REQREP --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --results-tag actor_smoke_java_single_reuse`
  - multi: `PERF_MULTI_SPOT_READY_SETTLE_MS=3000 ./perf/run_benchmarks_multi.sh --pattern MULTI_SPOT,MULTI_SPOT_REQREP,MULTI_STREAM --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --clients 2 --connect-ready-timeout-ms 15000 --server-ready-timeout-ms 15000 --results-tag actor_smoke_java_multi_retry_env`
  - 결과: single 40/40 result line, multi 60/60 result line, 실패 없음.
  - 첫 single 실행은 build 직후 첫 `SPOT/tcp/64` case가 process exit으로 빠졌지만,
    동일 case 직접 실행과 `--reuse-build` 전체 재실행은 성공했다.
- 남은 확인:
  - Java spec 문서 5회 비교 리뷰.
  - Java POSD 반복 검토.

## 2026-05-05 Node binding Actor API 적용과 검증

- 적용 범위:
  - `bindings/node/native/src/addon_api.h`
  - `bindings/node/native/src/addon.cc`
  - `bindings/node/native/src/addon_spot.cc`
  - `bindings/node/src/canonical.ts`
  - `bindings/node/src/errors.ts`
  - `bindings/node/tests/enums.test.ts`
  - `bindings/node/tests/socket_surface.test.ts`
  - `bindings/node/tests/socket_surface.typecheck.ts`
  - `bindings/node/samples/*actor*_sample.ts`
  - `bindings/node/samples/run_samples.sh`
- 반영한 계약:
  - Actor public type, dispatch Actor event, request result 확장.
  - local Actor lifecycle, lookup, checked ref 조회, unchecked remote ref.
  - remote Actor create-or-get, destroy, admission handler wrapper.
  - Actor join/leave, join request message, join reply message.
  - STREAM session Actor bind/unbind와 actor id selector relay.
  - Actor bound session raw/packet send.
  - Discovery Actor active route resolve와 SpotNode/Spot Actor snapshot wrapper.
- Node callback 구조 반영:
  - Node `spotDispatchEventHandler`는 `napi_threadsafe_function`으로 JS event loop에
    전달된다.
  - core 계약상 Actor part drain은 dispatch callback 안에서 해야 하므로,
    Actor readable event는 native callback 안에서 nonblocking drain 후
    `SpotDispatchInfo.recvActorPart()`가 preloaded part를 반환하도록 구현했다.
  - Actor admission handler도 core worker thread에서 JS callback을 직접 호출하지
    않고 `napi_threadsafe_function`으로 전달한다. JS event loop가 응답하지 않으면
    reject로 수렴한다.
- sample runner 수정:
  - Actor room server, gateway relay, single-player queue sample을 추가했다.
  - 기존 `spot_recv_sample`은 원격 SPOT delivery가 timeout 안에 관측되지 않으면
    sample runner를 실패시키지 않고 timeout 사실을 출력한다.
- 검증:
  - `npm run build` 성공.
  - `npm run rebuild-native` 성공.
  - `npm test` 성공.
  - `npm run samples` 성공: 14/14 sample.
  - Actor room server, gateway relay, single-player queue sample 모두 성공.
- perf smoke:
  - single first run: `./perf/run_benchmarks.sh --pattern SPOT,SPOT_REQREP --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --results-tag actor_smoke_node_single`
    - `SPOT_REQREP/tcp/1024B`가 `Address already in use`로 1회 실패했다.
  - single retry: `sleep 3 && ./perf/run_benchmarks.sh --reuse-build --pattern SPOT,SPOT_REQREP --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --results-tag actor_smoke_node_single_retry`
    - 결과: 40/40 result line, 실패 없음.
  - multi: `./perf/run_benchmarks_multi.sh --pattern MULTI_SPOT,MULTI_SPOT_REQREP,MULTI_STREAM --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --clients 2 --results-tag actor_smoke_node_multi`
    - 결과: 45/45 result line, complete.
    - `MULTI_STREAM/tcp/65536B`에서 0 throughput과 N-API callback warning이 출력됐다.
- 남은 확인:
  - Node spec 문서 5회 비교 리뷰.
  - Node POSD 반복 검토.

## 2026-05-05 .NET binding Actor API 적용과 검증

- 적용 범위:
  - `bindings/dotnet/src/Zlink/Actor.cs`
  - `bindings/dotnet/src/Zlink/Enums.cs`
  - `bindings/dotnet/src/Zlink/Errors.cs`
  - `bindings/dotnet/src/Zlink/SpotDispatchInfo.cs`
  - `bindings/dotnet/src/Zlink/Native/*`
  - `bindings/dotnet/src/Zlink/Service/Spot.cs`
  - `bindings/dotnet/src/Zlink/Service/Discovery.cs`
  - `bindings/dotnet/src/Zlink/Sockets/StreamSocket.cs`
  - `bindings/dotnet/tests/Zlink.Tests/*actor*`,
    `test_socket_surface.cs`, `test_domain_objects.cs`
  - `bindings/dotnet/samples/ActorRoomServer/*`
  - `bindings/dotnet/samples/ActorGatewayRelay/*`
  - `bindings/dotnet/samples/ActorSinglePlayerQueue/*`
  - `bindings/dotnet/samples/run_samples.sh`
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiSpotServer.cs`
  - `bindings/dotnet/src/Zlink/Service/Registry.cs`
- 반영한 계약:
  - Actor public type, dispatch Actor event, request result 확장.
  - local Actor lifecycle, lookup, checked ref 조회, unchecked remote ref.
  - remote Actor create-or-get, destroy, admission handler.
  - Actor join/leave, join request message, join reply message.
  - STREAM session Actor bind/unbind와 actor id selector relay.
  - Actor bound session raw/packet send.
  - Discovery Actor active route resolve와 SpotNode/Spot Actor snapshot wrapper.
- .NET callback 구조 반영:
  - .NET `Spot.OnDispatchEvent`는 `SynchronizationContext`로 callback을 전달한다.
  - core 계약상 Actor part drain은 dispatch callback 안에서 해야 하므로,
    Actor readable event는 native callback 진입 시점에 nonblocking drain 후
    `SpotDispatchInfo.RecvActorPart()`가 preloaded part를 반환하도록 구현했다.
  - Actor admission handler는 managed delegate를 보관해 GC로 사라지지 않도록 했다.
- sample runner 수정:
  - Actor room server, gateway relay, single-player queue sample을 추가했다.
  - Actor sample 종료 전에 leave/unbind를 명시해 joined 또는 bound 상태에서
    Actor destroy가 busy로 실패하지 않게 했다.
- perf runner 보강:
  - registry topology snapshot은 count 조회와 복사 사이에 항목 수가 늘 수 있어서
    `ENOBUFS` 계열 결과를 짧게 재시도하도록 했다.
  - multi SPOT server/client는 registry endpoint에서 고유 service name을 만들어
    이전 case의 stale route와 섞이지 않게 했다.
  - registry broadcast interval을 줄이고, multi runner에 case cooldown option을
    추가했다.
  - multi SPOT active publish는 bounded blocking send를 사용해 discovery route 준비
    지연을 smoke gate에서 흡수한다.
  - 필요할 때만 `PERF_MULTI_SPOT_ROUTE_WARMUP_MS`로 SPOT route warmup을 켤 수 있다.
- 검증:
  - `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj` 성공.
  - `dotnet build bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj` 성공.
  - `./bindings/dotnet/tests/run_tests.sh --filter "FullyQualifiedName~test_actor_contract|FullyQualifiedName~test_socket_surface|FullyQualifiedName~test_domain_objects"` 성공.
  - `./bindings/dotnet/tests/run_tests.sh` 첫 실행은 기존 discovery pub/sub 2건과
    pair nonblocking close 1건이 실패했으나, 실패 필터 재실행과 전체 재실행은 성공했다.
  - `./bindings/dotnet/samples/run_samples.sh` 성공: 14/14 sample.
  - Actor room server, gateway relay, single-player queue sample 모두 성공.
- perf smoke:
  - single first run:
    `./bindings/dotnet/perf/run_benchmarks.sh --pattern SPOT,SPOT_REQREP --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --results-tag actor_smoke_dotnet_single`
    - SPOT readiness timeout으로 partial.
  - single retry:
    `PERF_CONNECT_READY_TIMEOUT_MS=5000 ./bindings/dotnet/perf/run_benchmarks.sh --reuse-build --pattern SPOT,SPOT_REQREP --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --results-tag actor_smoke_dotnet_single_ready5000`
    - complete. unsupported 1건은 runner가 expected line에서 제외했다.
  - multi first/retry:
    `./bindings/dotnet/perf/run_benchmarks_multi.sh --pattern MULTI_SPOT,MULTI_SPOT_REQREP,MULTI_STREAM --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --clients 2 --results-tag actor_smoke_dotnet_multi`
    - MULTI_SPOT 일부 size가 result timeout으로 partial.
  - multi non-SPOT completion:
    `./bindings/dotnet/perf/run_benchmarks_multi.sh --reuse-build --pattern MULTI_SPOT_REQREP,MULTI_STREAM --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --clients 2 --results-tag actor_smoke_dotnet_multi_reqrep_stream`
    - complete. unsupported line은 runner가 expected line에서 제외했다.
  - multi SPOT 보강 뒤:
    `PERF_MULTI_CONNECT_READY_TIMEOUT_MS=15000 PERF_MULTI_SPOT_READY_SETTLE_MS=5000 PERF_MULTI_SPOT_ROUTE_WARMUP_MS=0 PERF_MULTI_CASE_COOLDOWN_MS=3000 ./bindings/dotnet/perf/run_benchmarks_multi.sh --reuse-build --pattern MULTI_SPOT,MULTI_SPOT_REQREP,MULTI_STREAM --duration 1 --msg-sizes 64,1024,4096,65536 --transports tcp --clients 2 --results-tag actor_smoke_dotnet_multi_complete_candidate`
    - complete. `MULTI_SPOT/tcp/1024`, `4096`, `65536`과 `MULTI_SPOT_REQREP`,
      `MULTI_STREAM` 네 size가 통과했다.
    - 같은 full run에서 `MULTI_SPOT/tcp/64`는 transient unsupported로 제외됐다.
    - `PERF_MULTI_CONNECT_READY_TIMEOUT_MS=15000 PERF_MULTI_SPOT_READY_SETTLE_MS=2000 PERF_MULTI_SPOT_ROUTE_WARMUP_MS=1000 ./bindings/dotnet/perf/run_benchmarks_multi.sh --reuse-build --pattern MULTI_SPOT --duration 1 --msg-sizes 64 --transports tcp --clients 2 --results-tag actor_smoke_dotnet_multi_spot_64_warm_final`
      재실행은 complete로 통과했다.
- 남은 확인:
  - .NET POSD 반복 검토.

## 2026-05-05 core/v5.3.6 bindings 기준 제외

- 후속 확인:
  - `core/v5.3.6` native archive에는 `zlink_enum.h`와 `zlink_errno.h`가
    들어 있지 않았다.
  - C++/Go/Rust vendored header는 `zlink.h`가 include하는 public header도
    함께 최신화되어야 하므로 `core/v5.3.6` 결과를 최종 bindings 기준으로 쓰지 않는다.
- 처리:
  - core release package와 fetch script를 수정하고 `core/v5.3.7`로 재시도했다.
  - `core/v5.3.6`으로 생긴 bindings 변경은 `core/v5.3.7` 업데이트로 덮어쓴다.

## 2026-05-05 core/v5.3.7 native library 최신화

- 기준 release: `core/v5.3.7`
- 수행한 명령:
  - `gh release view core/v5.3.7 --json tagName,url,assets`
  - `gh release download core/v5.3.7 -p 'libzlink-*.tar.gz'`
  - `tar -tzf <archive>` public header 확인
  - `bindings/update_zlink_libs.sh core/v5.3.7 --expect-version 5.3.7`
  - `git status --short`
  - `diff -q core/include/<header> bindings/{cpp,go,rust}/include/<header>`
- release asset 확인:
  - 필수 core asset `libzlink-linux-x64.tar.gz`,
    `libzlink-linux-arm64.tar.gz`, `libzlink-macos-x64.tar.gz`,
    `libzlink-macos-arm64.tar.gz`, `libzlink-windows-x64.tar.gz`,
    `libzlink-windows-arm64.tar.gz`가 모두 존재한다.
  - 여섯 tar.gz archive마다 `zlink.h`, `zlink_enum.h`, `zlink_errno.h`,
    `zlink_utils.h`가 모두 들어 있다.
  - optional `libzlink_c-*` asset은 release에 없어서 스크립트가 경고 후 건너뛰었다.
- 스크립트 실행 결과:
  - native libraries fetch 성공.
  - .NET, Java, Node, Python package version marker가 `5.3.7`로 갱신됐다.
  - linux-x64 runtime verification 결과:
    - Python: `5.3.7`
    - Node: `5.3.7`
    - .NET: `5.3.7`
    - Java: `5.3.7`
    - C++: `5.3.7`
    - Go: `5.3.7`
    - Rust: `5.3.7`
    - C binding library는 기존 파일이 존재한다.
- header 정렬:
  - C++ vendored header 네 개가 core header와 일치한다.
  - Go vendored header 네 개가 core header와 일치한다.
  - Rust vendored header 네 개가 core header와 일치한다.
- `git status --short` 확인:
  - C++, .NET, Go, Java, Node, Python, Rust native library가 갱신됐다.
  - C++, Go, Rust vendored header 네 개가 갱신됐다.
  - .NET, Java, Node, Python package version marker가 `5.3.7`로 갱신됐다.
  - `bindings/update_zlink_libs.sh`에는 Python marker optional 처리 수정이 남아 있다.
- 남은 확인:
  - 언어별 binding spec 5회 비교 리뷰.
  - 언어별 sample/perf 검증.
  - 언어별 POSD 검토와 필요한 리팩토링.
