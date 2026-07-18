# RouteMesh 10.0.0 S8 JVM bindings 전환 리뷰 — iteration 1 공통 prompt

너는 S8 JVM(Java/Kotlin·Java FFI/Panama) bindings 전환 리뷰 iteration 1의 독립 리뷰어다. byte 단위 동일 prompt(R1·R2). 다른 리뷰어 결과·coordinator 해석을 판정 근거로 쓰지 마라.

## Snapshot
- 대상 commit: `5c2eb2acc`
- Scope: `git ls-files bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle` 중 `native/linux|darwin|win`·`resources/native`·`/build/` 제외
- 파일 수: 255 (java 236, kotlin 19)
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `8af1d48c9ebc4c67d6ee90ba48a6350228300e4f819e03c68840611933dcdf12`
- 시작·종료 파일수·hash 확인·기록, 파일 수정 금지.

## 대상과 목적
JVM bindings(Java FFI/Panama — `runtime/nativeapi/`의 MethodHandle downcall, 심볼명 문자열 키; Kotlin은 Java 런타임 공유·samples만)를 zlink Core 10.0.0 공개 C API(`core/include/zlink/service/*.h`, `core/include/zlink/socket/api.h`)로 전환한 결과 검토. Runtime raw-socket 레이어는 존속하되 일부 raw 심볼도 드리프트했다(주의: `zlink_router_recv_part` 파라미터·`zlink_stream_detach`/`attach_raw`·`zlink_msg_refcnt`·`zlink_subscribe_handler`). 폐기 개념: SpotNode/route_bridge/subjects/internal_sockets/pub·sub rid/dispatch_workers/recv_actor_part/msg_gets.

## 절차 규칙
- 시간 제한 없음. 산출물은 review 디렉터리(`codex/`|`claude-sonnet/`)의 `progress.md`·`review.ko.md`만. build/실행 금지(정적 대조; 국소 grep/read 자유). 실행 증거는 manifest(compileJava+samples+kotlin-samples green, no-hit 0)만.
- iteration 1: 각 축 CLEAN=finding 0. 같은 근본원인은 root-cause family로.

## 검토 축(3축)
- **I1 계약 일치**: Core C API와 FFI downcall(`ServiceLayouts`/`NativeServiceSymbols`/`ServiceInterop`, `Native.java`)·공개 표면 계약. 매핑 정확성, FFI 레이아웃(struct_size/version)·downcall 시그니처(파라미터 수·타입)가 Core와 정확히 일치하는지, pull dispatch(ReadyBatch/Claim/ReceiveBatch/ReplyToken) 수명, join-reply route, metadata/transfer. **raw-layer 드리프트**로 인한 파손(제거 심볼 downcall 잔존, router_recv_part 파라미터 불일치 등).
- **I2 POSD·DDD**. **I3 정리 완결성**(폐기 잔재·제거 심볼 FFI downcall·no-hit).

## 출력
1. Scope 확인 2. I1/I2/I3 Finding·Evidence·Verdict 3. 폐기 no-hit 판정(위 목록) 4. 마지막 줄 정확히 `BINDINGS REVIEW CLEAN` 또는 `BINDINGS REVIEW NOT CLEAN`.

주: Java StreamSocket에는 bindActor가 없어 actor 샘플이 StreamSessionService(Java 표면)로 세션 바인딩하는 것은 Java 계약 형태이며 결함 아님(리뷰어 독립 판정).
