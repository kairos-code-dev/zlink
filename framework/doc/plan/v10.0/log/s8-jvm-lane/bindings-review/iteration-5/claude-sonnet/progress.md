# S8 JVM bindings 리뷰 iteration 5 — R2(Claude Sonnet) progress

## 절차
1. prompt.md 정독(byte-identical 배포, iteration 5 규칙: 4회차+ — 각 축 CLEAN=blocker/high/medium 0, low는 별도 기록·비차단). 다른 리뷰어(R1)·coordinator 해석은 판정 근거로 사용하지 않음.
2. Scope 재현: `git ls-tree -r --name-only fbd35a1ef -- bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle` → `/native/(linux|darwin|win)`·`resources/native`·`/build/` 제외 → 251 files.
   - 대상 commit `fbd35a1ef`는 현재 `HEAD`(`2e8164c17`)의 조상이며, `git diff --stat fbd35a1ef HEAD -- <scope paths>` = empty → working tree scope 파일이 대상 commit과 완전히 동일함을 확인 후 working tree 파일로 해시 계산.
   - aggregate SHA-256(파일 목록 `LC_ALL=C sort` → 파일별 `sha256sum "$f"`(파일명 포함 출력) → 그 출력 리스트 자체를 `sha256sum`) = `1351d5dabce124a8cefea369437cc23bd9f1eecf0ce59079056b6add2ad54192` — prompt 기대값과 일치.
3. iter-4 finding 해소 검증:
   - JV4-1(blocker): `git diff 41660e37b fbd35a1ef -- bindings/java/native/src/zlink_java_reqrep_bridge.c` 확인 → dead+broken `zlink_java_router_recv`(88줄, 6-param `zlink_router_recv_part`를 7인자로 오호출) + 전용 `extern "C" zlink_router_enable_spot_receive` 선언 + helper 2개 완전 삭제(127→39줄). scope 전체 `zlink_java_router_recv`·`zlink_router_enable_spot_receive`·`zlink_router_recv_part`(native/src 내) grep = 0건.
   - 3 low(SPOT_TOPIC/SPOT_PAYLOAD/ERRNO_EFSM): `git diff 41660e37b fbd35a1ef` 확인 → `SampleSupport.java`에서 SPOT_TOPIC/SPOT_PAYLOAD 2줄, `NativeSocketRuntime.java`에서 ERRNO_EFSM 1줄 삭제. scope 전체 grep = 0건(테스트 트리의 독립 `ERRNO_EFSM` 로컬 선언 1건은 scope 밖 `src/test`이므로 무관).
4. native/src(C bridge) 전체 재검토: 파일이 39줄로 축소됨을 확인, 잔존 Core 호출부 2개(`zlink_msg_data`, `zlink_send_part_rid`) 전부 `core/include/zlink/{message,socket}/api.h` 시그니처와 arity/타입 일치 확인. `zlink_routing_id_t`/`zlink_msg_t`/`ZLINK_SUBMIT_*`/`ZLINK_PART_*` 심볼도 Core 헤더에 실존.
5. Java FFI descriptor↔callsite↔Core 전면 스윕:
   - `Native.java`(1538줄) 전체 통독: MH_* 디스크립터 85개 전부 `invokeExact` 호출부 인자수 일치, Core `core/api.h`·`socket/api.h`·`eventing/api.h` 시그니처와 1:1 대조(버전/컨텍스트/소켓/옵션/구독/모니터/타이머/스톱워치/스레드/라우터 계열 전부) — 불일치 0.
   - `NativePollerSymbols.java`: poller 계열 13개 디스크립터 전부 `eventing/api.h`와 일치.
   - `NativeMessage.java`: 메시지 계열 12개 디스크립터 전부 `message/api.h`와 일치(`zlink_multipart_close`/`zlink_msgv_close` fallback명 처리는 의도된 downcallAny 패턴).
   - `NativeServiceSymbols.java`(877줄): Python 스크립트로 mesh_node.h/actor.h/spot.h/dispatch.h/stream_session.h/common.h에서 정규식 추출한 75개 Core 함수 arity와 파일 내 `dc("name", FunctionDescriptor.of(...))` 75건을 전수 대조 — 불일치 0, Core 부재 심볼 0. 이어서 각 MethodHandle 변수의 `invokeExact` 호출부 인자수를 디스크립터 인자수와 전수 대조(75/75 일치, dead 디스크립터 0).
   - Upcall(콜백) 디스크립터 6곳(`NativeZlinkThread`/`RoutedRequestSupport`/`SocketCore`/`NativeRouterReceiveSupport`/`NativeTimer`/`NativeMonitorSocket`) 전부 Core의 `zlink_thread_fn`/`zlink_reply_handler_fn`/`zlink_socket_msg_handler_fn`/`zlink_send_ready_handler_fn`/`zlink_stream_packet_handler_fn`/`zlink_timer_handler_fn`/`zlink_socket_monitor_handler_fn`typedef와 파라미터 타입·개수 일치. `NativeServiceSymbols.READY_HANDLER_DESCRIPTOR`(mesh ready handler)도 `zlink_mesh_ready_handler_fn` 시그니처와 일치.
6. `ServiceLayouts.java`/`NativeLayouts.java` 구조체 레이아웃: 대표 구조체(`MESH_NODE_OPTIONS`/`MESH_PEER_CONNECTION_OPTIONS`/`MESH_NODE_STATUS`/`ACTOR_REF_LAYOUT`) 필드 순서·타입·패딩을 Core 구조체 정의와 대조, `ZLINK_ACTOR_ID_MAX`(255)/`ZLINK_MESH_NAME_MAX`(255)/`ZLINK_MESH_ENDPOINT_MAX`(511) 매크로 값과 seq() 크기(256/256/512) 일치 확인. `git log fbd35a1ef..HEAD -- core/include/` = empty이므로 현재 working tree의 Core 헤더가 대상 commit 시점과 동일함을 먼저 확인 후 대조.
7. no-hit(사용되지 않는 MethodHandle) 전수 확인: `Native.java`(85개)·`NativePollerSymbols.java`(13개)·`NativeMessage.java`(12개)·`NativeServiceSymbols.java`(75개) 전부 선언=호출 1:1, 미사용 0.
8. I3 dead-code 스윕 중 신규 발견: JV4-1 삭제 후 `zlink_java_reqrep_bridge.c` 상단의 `<errno.h>`/`<stdlib.h>`/`<vector>` include 3개가 더 이상 어떤 심볼도 참조하지 않는 고아 include로 남음(삭제된 함수가 `errno`/`EAGAIN` 등/`std::vector`를 사용했음). 컴파일은 깨지지 않으나(coordinator manifest: buildZlinkJavaBridge ALL GREEN) 정리 대상 → LOW로 기록.
9. 기존 관찰(신규 아님, 참고용 low): `zlink_has`는 Core에서 C++ `bool`(1바이트) 반환이지만 `Native.java`의 `MH_HAS` 디스크립터는 `JAVA_INT`(4바이트) 반환으로 선언됨. x86-64 SysV/Windows x64 ABI에서 컴파일러가 bool 반환 시 레지스터 상위 바이트를 통상 clear하므로 실무상 안전하나, 엄밀한 FFI 서술 정확성 관점에서 LOW로 기록(scope 전체에서 `bool` 반환 Core 함수는 이 1건뿐).
10. 최종 판정: I1/I2/I3 3축 모두 blocker/high/medium 0. low 2건(비차단). `BINDINGS REVIEW CLEAN`.

## 결론
review.ko.md 작성 완료. 빌드/실행 없이 정적 소스 대조만 수행(REVIEWER 역할 준수). coordinator manifest의 `compileJava+buildZlinkJavaBridge+samples+kotlin ALL GREEN`·no-hit 0·제거심볼 게이트 EMPTY를 실행 증거로 인용(재실행하지 않음).
