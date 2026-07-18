# S8 JVM bindings 리뷰 iteration-2 — 병합 finding ledger

두 리뷰어(R1 opus, R2 Sonnet) iteration-2. 둘 다 `BINDINGS REVIEW NOT CLEAN`. JV-F2~F5 해소 확인,
**JV-F1이 iter-1 fix에서 over-correct되어 신규 blocker화** + R1이 2번째 발견.

## JV2-1. router_recv_part descriptor over-correct [blocker] (두 리뷰어 합의)
iter-1 fix가 `Native.java`의 `MH_ROUTER_RECV_PART`(+`_CRITICAL`) FunctionDescriptor에서 ADDRESS를
**2개** 지워 4 ADDRESS+1 INT=5파라미터가 됨. 그러나 Core는 5 ADDRESS+1 INT=6파라미터
(`socket/api.h:271-277`)이고 invokeExact call site는 6인자 그대로 → `WrongMethodTypeException`(런타임,
매 RouterSocket.recv()). 근본원인=`zlink_recv_part`(4 ADDRESS)와 `zlink_router_recv_part`(5 ADDRESS,
request_seq_out 여분) 혼동. compileJava 못 잡음(FFM 링크타임). **coordinator 직접 수정 완료**: 두
descriptor를 `of(JAVA_INT, ADDRESS×5, JAVA_INT)`로 복원.

## JV2-2. zlink_router_handler Core 부재 [high] (R1)
`NativeMessage.java:37,142`가 `zlink_router_handler` downcall — Core 10.0.0에 **완전 부재**(headers/
libzlink.vers/nm -D/removed-identifiers 모두 0). `RouterSocket.onReceive`→SocketCore.onReceive→
NativeRouterReceiveSupport.onReceive→NativeMessage.routerHandler 경로에서 런타임 실패.
**Core 사실**: 10.0.0의 소켓 콜백 수신은 `zlink_recv_handler(void* s, zlink_socket_msg_handler_fn, void*)`
이며 **raw STREAM 전용**(다른 subject는 ENOTSUP). 콜백은 `zlink_socket_msg_handler_fn(source_rid,
parts, part_count, userdata)`. router 콜백 수신은 미지원(폴링 recv만). dotnet은 이미 `zlink_recv_handler`+
`ZlinkSocketMsgHandlerDelegate`로 일반화(EnsureSupports 게이팅).
→ `zlink_router_handler`를 `zlink_recv_handler`로 재매핑하고 콜백을 `zlink_socket_msg_handler_fn`
시그니처로 변경. onReceive는 STREAM 지원(router는 Core ENOTSUP). dotnet 패턴과 정합.

## 처리
JV2-1 coordinator 직접 수정 완료. JV2-2는 콜백 시그니처 변경이 얽혀 전용 에이전트로. compileJava+
samples+kotlin green, 제거심볼 게이트 EMPTY, no-hit 유지. 완료 후 iteration-3.
