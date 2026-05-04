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
