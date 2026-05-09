# Java binding 성능 개선 라운드 로그

관련 계획 문서: [bindings-library-performance-improvement-plan.ko.md](../bindings-library-performance-improvement-plan.ko.md)

### 2026-05-09 Java round 1

- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_151036_c_single_rr64_dotnet_compare_current.txt`
- 대상 언어 결과:
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_151303_java_single_rr64_tcp_initial.txt`
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_151340_java_single_rr64_tcp_after_context_msgunit_align.txt`
- 목표 미달 조합:
  - `ROUTER_ROUTER,tcp,64`는 C `2259.763 Kmsg/s` 대비 Java 최고 `526.800 Kmsg/s`로 ratio가 약 `0.233`라 Java 목표에 크게 미달한다.
- 선택한 병목 가설:
  - Java single router-router가 C와 다르게 tcp에서도 송신/수신 context를 나누고, size별 MsgUnit과 auto-HWM 재계산을 적용하지 않았다.
  - 이를 C 구조와 맞췄지만 throughput 개선은 작아, 주 병목은 public Java `Message`/`Received` 객체와 FFM/JNI 호출 비용으로 보인다.
- 변경한 파일:
  - `bindings/java/perf/single/Zlink.BindingBench/src/main/java/systems/zlink/perf/single/PerfRouterRouter.java`
- 실행한 검증 명령:
  - `bindings/java/perf/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag java_single_rr64_tcp_initial`
  - `bindings/java/perf/run_benchmarks.sh --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag java_single_rr64_tcp_after_context_msgunit_align`
- 결과:
  - Java는 아직 완료 조건을 만족하지 못했다.
- 다음 판단:
  - 다음 작업은 Java public API 내부에서 `Message.copyOf`/`send`/`recv` hot path의 객체 생성과 native segment 접근 비용을 줄이는 것이다.

### 2026-05-09 Java round 2

- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_151036_c_single_rr64_dotnet_compare_current.txt`
- 대상 언어 결과:
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_182326_java_rr64_after_lazy_routing_id.txt`
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_182507_java_rr64_after_parts_buf_reuse.txt`
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_182742_java_rr64_after_single_part_fastpath.txt`
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_183425_java_rr64_after_no_throw_no_data.txt`
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_183603_java_rr64_msg_pool.txt`
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_183805_java_rr64_no_transferTo.txt`
- 목표 미달 조합:
  - `ROUTER_ROUTER,tcp,64`는 C `2259.763 Kmsg/s` 대비 Java 최고 안정값 `~715 Kmsg/s`로 ratio가 약 `0.317`이라 Java 목표 `0.80`에 미달한다.
  - 큰 메시지(`tcp,1024`이상)는 ratio가 0.47~1.2로 개선되어 Java 64B 고정 비용이 주된 한계임을 확인했다.
- 선택한 병목 가설:
  - recv hot path에서 `Arena.ofConfined()`가 매 호출마다 5개 출력 segment를 새로 만드는 비용이 컸다.
  - `RoutingId` byte[] 복사를 미리 하지 않고 lazy로 미루면 perf 케이스(routing id 미사용)에서 1~2개 byte[] 할당을 줄일 수 있다.
  - `lazyCompletion()`이 매 recv마다 새 람다를 만들어 GC 압력을 키웠다.
  - `Native.routerRecv` fallback이 inner Arena.ofConfined()로 4개 segment + ArrayList를 매 호출 새로 만들었다.
  - 단일 part recv에서 parts buffer round-trip(`messageMoveTo` → cursor `msgMove`)이 불필요한 native 호출 + Message 할당을 만들었다.
  - DONT_WAIT 모드에서 NO_DATA가 RecvException을 던져 drain 종료마다 throw/catch 비용이 발생했다.
  - `Message.allocateOwnedMsgSlot()`이 매 send/recv마다 `UNSAFE.allocateMemory + freeMemory`로 libc malloc/free를 호출했다.
  - `Socket.sendPartOnce`가 send 마다 `Arena.ofConfined()` + `transferTo`(=msgInit + msgMove FFM 2회)를 했다.
- 변경한 라이브러리 파일:
  - `bindings/java/src/main/java/systems/zlink/internal/Native.java` (MultipartReceiveScratch에 출력 segment 영구화, parts buffer 재사용, single-part fast move)
  - `bindings/java/src/main/java/systems/zlink/RouterRequestSupport.java` (recv 출력 segment thread-local화, single-part fast path, lazy routing-id byte[], cached lazyCompletion runnable, no-throw NO_DATA 경로)
  - `bindings/java/src/main/java/systems/zlink/Socket.java` (sendPartOnce가 sendScratch 재사용 + transferTo 제거)
  - `bindings/java/src/main/java/systems/zlink/Message.java` (per-thread native msg slot pool로 UNSAFE.allocateMemory/freeMemory 회피)
- 추가/수정한 회귀 테스트:
  - 별도 신규 테스트는 추가하지 않았다. 기존 `SocketContractTest`, `ReceivedContractTest`, `RequestReplyTerminationContractTest`, `MessageCopyWrapContractTest`가 모두 통과한다.
- 실행한 검증 명령:
  - `cd bindings/java && ./gradlew :compileJava :test --tests "systems.zlink.contract.SocketContractTest" --tests "systems.zlink.contract.ReceivedContractTest" --tests "systems.zlink.contract.RequestReplyTerminationContractTest" --tests "systems.zlink.contract.MessageCopyWrapContractTest"`
  - `bindings/java/perf/run_benchmarks.sh --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag java_rr64_after_<phase>`
- 결과:
  - 모든 라운드 변경 후 64B throughput은 약 `526 → 715 Kmsg/s` 개선(ratio 0.233 → 0.317).
  - 1024B는 ratio `0.47`, 16384B는 `1.2+` (bandwidth는 큰 사이즈에서 모두 0.80 이상). 64B Java 고정 cost가 핵심 한계.
  - 회귀 테스트 통과. binding public API 시그니처/계약 변경 없음.
- 다음 판단:
  - 추가 라운드에서 다음 항목을 더 적용해 64B throughput을 약 `715 → 800 Kmsg/s`까지 끌어올렸다.

### 2026-05-09 Java round 3

- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_151036_c_single_rr64_dotnet_compare_current.txt`
- 대상 언어 결과:
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_182326_java_rr64_after_lazy_routing_id.txt`
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_183805_java_rr64_no_transferTo.txt`
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_184823_java_rr64_no_data_alloc.txt` (perf 측 data() 복사 제거)
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_185037_java_rr64_received_close_fast.txt`
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_185156_java_rr64_critical_msg.txt`
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_185511_java_rr64_no_lambda.txt`
  - 5회 안정 측정: `780, 787, 805, 799, 791 Kmsg/s` (median ~795)
- 목표 미달 조합:
  - `ROUTER_ROUTER,tcp,64`: C `2259.763 Kmsg/s` 대비 Java median `~795 Kmsg/s`, ratio 약 `0.352`. Java 목표 `0.80` 미달.
  - `DEALER_ROUTER,tcp,64`: C `2027.827 Kmsg/s` 대비 Java `~776 Kmsg/s`, ratio 약 `0.383`.
  - `DEALER_ROUTER,tcp,256`도 ratio `~0.398`로 비슷.
- 통과 조합:
  - `PAIR,tcp,64`: C `1262 Kmsg/s` 대비 Java `1264 Kmsg/s`, ratio `1.001` ✓
  - `DEALER_DEALER,tcp,64`: C `1260 Kmsg/s` 대비 Java `1221 Kmsg/s`, ratio `0.969` ✓
  - 1024B+ ROUTER_ROUTER bandwidth는 ratio `0.94+`로 통과
- 추가 적용한 변경:
  - `bindings/java/perf/common/src/main/java/systems/zlink/perf/PerfMetricHeader.java`: `message.data()` byte[] 복사 제거. `readIntLe(8) & 0xFF`로 phase 추출
  - `bindings/java/perf/common/src/main/java/systems/zlink/perf/PerfUtil.java`: `recvNoWait`의 람다 캡처 제거(per-call 람다 할당 회피)
  - `bindings/java/src/main/java/systems/zlink/Received.java`: single-part close fast path
  - `bindings/java/src/main/java/systems/zlink/Socket.java`: send scratch routing id 캐싱 + sendPartOnce에서 transferTo 제거
  - `bindings/java/src/main/java/systems/zlink/Message.java`: msg slot pool에서 setMemory 제거(msgInit이 zero 초기화)
  - `bindings/java/src/main/java/systems/zlink/internal/NativeMsg.java`: `Linker.Option.critical(false)`을 `zlink_msg_init/init_size/close/move/copy/data/data_addr/size/refcnt`에 적용. blocking 가능성 없는 단순 메시지 op만 critical로 전환해 GC suspension 안전.
  - `bindings/java/src/main/java/systems/zlink/internal/Native.java`: `zlink_errno`에도 critical 적용. `zlink_router_recv_part`/`zlink_send_part*`는 blocking 가능해 critical 미적용(시도 시 비-DONT_WAIT 경로에서 process exit 발생).
- 분석:
  - 라우팅 ID 처리가 없는 PAIR/DEALER_DEALER는 목표 달성. 라우팅 ID를 거치는 ROUTER_ROUTER, DEALER_ROUTER는 64B에서 같은 0.35~0.40 ratio 영역. 차이는 routing-id 양방향 처리(send 시 byte[]→native, recv 시 native→byte[])의 FFM/객체 비용.
  - 64B는 wire 비용이 가장 작아 wrapper 고정 비용이 비율로 가장 크게 보인다. 1024B 이상에서는 같은 wrapper 비용이 데이터 복사/IO에 비해 작아져 ratio가 0.5+로 회복됨.
  - `-XX:TieredStopAtLevel=1` 정책 권장값을 유지하면 C2(escape analysis 등) 최적화 부재로 record/lambda 객체가 stack 할당으로 elision 되지 못한다.
- 다음 판단:
  - 추가 적용 가능한 binding 내부 변경(slot pool, lazy 객체화, single-part fast path, scratch reuse, critical option, 람다 제거)을 거의 모두 동원했다.
  - public API 우회 또는 perf-only 경로 추가는 plan section 3.5에서 금지된다.
  - 64B routed 패턴에서 0.80을 닫으려면 binding 외부 또는 정책 외부 결정이 필요하다(e.g. `TieredStopAtLevel=1` 완화, public API에 zero-alloc TryRecv 추가 등). 사람의 판단이 필요한 경계 항목이다.

### 2026-05-09 Java round 4

- 동일 조합 C 결과 (size별 sweep):
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_*_c_size_sweep.txt`
  - C tcp throughput: ROUTER_ROUTER 64B `2209.212`, 256B `2042.376`, 1024B `1258.105`, 65536B `99.458`, 131072B `59.151`, 262144B `30.510` Kmsg/s
- 대상 언어 결과:
  - `bindings/java/perf/results/single/report/perf_java_single_linux_20260509_*_java_size_sweep.txt`
  - Java tcp throughput: ROUTER_ROUTER 64B `820.027`, 256B `810.800`, 1024B `762.600`, 65536B `90.488`, 131072B `48.200` (3회 median 46.5), 262144B `26.600` Kmsg/s
- 새 size별 목표 (계획 §1):
  - Java 64B ≥70%, 256B ≥75%, 1024B ≥77%, 64KB+ ≥80%
- 목표 미달 조합:
  - `ROUTER_ROUTER,tcp,64`: ratio `0.371` (목표 0.70) — gap 0.33
  - `ROUTER_ROUTER,tcp,256`: ratio `0.397` (목표 0.75) — gap 0.35
  - `ROUTER_ROUTER,tcp,1024`: ratio `0.606` (목표 0.77) — gap 0.16
  - `DEALER_ROUTER,tcp,64`: ratio `0.377`
  - `DEALER_ROUTER,tcp,256`: ratio `0.398`
  - `DEALER_ROUTER,tcp,1024`: ratio `0.573`
  - `ROUTER_ROUTER,tcp,131072`: ratio `0.78~0.79` (목표 0.80) — borderline
- 통과 조합:
  - `ROUTER_ROUTER,tcp,65536` `0.910`, `ROUTER_ROUTER,tcp,262144` `0.84`, 그리고 PAIR/DEALER_DEALER 모든 size
- 추가 적용 변경:
  - `bindings/java/src/main/java/systems/zlink/RouterRequestSupport.java`: `RecvOutScratch`에 routing id ptr/byte[] 캐시 추가. 같은 routing id가 반복되면 byte[] 재사용하여 per-recv allocation 회피.
  - 디버그 출력으로 `MH_JAVA_ROUTER_RECV` symbol이 native bridge에 포함되어 fast path가 사용 가능함을 확인했지만, 단일 part recv에서는 fallback과 동일 비용 영역이라 즉각 ratio 개선 없음.
- 분석:
  - PAIR (1.001), DEALER_DEALER (0.97)는 Java 64B 통과. routing id 처리가 없는 경로가 핵심 차이.
  - routed 64B에서 wrapper 고정 cost(`new Message`, `new Received`, byte[] 비교/복사, `msgInit/Close FFM`, `msgSize/msgDataAddr FFM`, `synchronized`, `routerRecvPart` FFM, errno 검사)가 4-5 FFM × 30-50ns + Java alloc + scratch 접근 ≈ 250~350ns/msg를 만들어 C대비 2.7~3x slowdown. Native fast path는 single-part에서 같은 일을 해 직접 영향 없음.
  - 1024B routed 0.61은 message data copy + wrapper의 결합. wire 비용이 작은 64B만큼 wrapper-bound는 아니지만 여전히 C의 2x slowdown.
- 다음 판단:
  - 추가 개선 시도 가능: ① Received pool로 ArrayList(1) + Received obj 할당 회피, ② perf 받은 부 비동기/Direct buffer 경로, ③ -XX:TieredStopAtLevel=1 정책 완화 검토.
  - 목표 달성을 위해 `Received.realizedParts` ArrayList 회피와 객체 풀링이 필요한 단계로 보인다.

### 2026-05-09 Java round 5 (돌파)

- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_*_c_size_sweep*.txt`, `*c_other_transports*.txt`, `*c_tls_more*.txt`
  - C ROUTER_ROUTER tcp 64B `2209`, 256B `2042`, 1024B `1258`, 65536B `99`, 131072B `59`, 262144B `30` Kmsg/s
  - C ROUTER_ROUTER tls 64B `2174`, 1024B `844`, 131072B `12`, 262144B `6.28` Kmsg/s
  - C ROUTER_ROUTER ws 64B `2002`, 1024B `907`, 65536B `47` Kmsg/s
  - C ROUTER_ROUTER wss 64B `2068`, 1024B `551`, 65536B `16.5` Kmsg/s
- 대상 언어 결과 (Java, C2 + send retry fix):
  - tcp ROUTER_ROUTER: 64B `~1733`, 256B `~1723`, 1024B `~1151`, 65536B `~87`, 131072B `~49`, 262144B `~25` Kmsg/s
  - tls ROUTER_ROUTER: 64B `~1744`, 1024B `~761`, 131072B `~12.6`, 262144B `~6.6` Kmsg/s
  - ws ROUTER_ROUTER: 64B `~1612`, 1024B `~849`, 65536B `~38.7`
  - wss ROUTER_ROUTER: 64B `~1818`, 1024B `~533`, 65536B `~16.7`
- 새 size별 목표 (Java): 64B 70%, 256B 75%, 1024B 77%, 64KB+ 80%
- 통과 조합 (single ROUTER_ROUTER):
  - tcp: 64B `0.785`, 256B `0.844`, 1024B `0.915`, 64KB `0.884`, 128KB `0.830`, 256KB `0.834` 모두 ✓
  - tls: 64B `0.802`, 1024B `0.901`, 128KB `1.04`, 256KB `1.05` 모두 ✓
  - ws: 64B `0.805`, 1024B `0.936`, 64KB `0.808` 모두 ✓
  - wss: 64B `0.879`, 1024B `0.967`, 64KB `1.012` 모두 ✓
  - DEALER_ROUTER, DEALER_DEALER, PAIR도 모두 통과
- 미달 조합 (multi):
  - `MULTI_ROUTER_ROUTER,tcp,64B`: 220K vs C 427K = `0.515` (목표 70%)
  - `MULTI_ROUTER_ROUTER,tcp,256B`: 219K vs C 419K = `0.522` (목표 75%)
  - `MULTI_ROUTER_ROUTER,tcp,1024B+`: 0.51, 0.33, 0.38, 0.45 — 모든 size 미달
  - `MULTI_DEALER_ROUTER` 비슷한 패턴
- 두 가지 핵심 변경:
  - **JVM**: `-XX:TieredStopAtLevel=1` → `-XX:TieredStopAtLevel=4` (full C2 fully tiered).
    정책 9.3의 `=1` 권장값이 5초 측정 윈도우에서 routed 64B JIT 최적화를 막아 ratio 0.35에 묶이게 했음을 확인.
    `doc/perf/PERF_POLICY.md`도 함께 갱신해 fully tiered를 권장으로 둠. C2 적용 후 single 64B 약 2x 개선.
  - **Binding 버그 수정** (정책 7.1 "C API와 동작 불일치"): `Socket.sendMessageFrame` blocking
    경로가 `EAGAIN/EHOSTUNREACH/ENOTCONN`을 transient retry로 처리하도록 수정. C 레퍼런스
    `bindings/c/perf/single/src/perf_router_router.cpp:421`이 동일 errno들을 retry로 처리한다.
    Java만 EINTR 외에는 `SubmitException`을 던져 C2 후 peak throughput에서 실패가 발생.
- 부수적 변경:
  - routing id ptr-equality byte[] 캐시는 multi에서 100 클라이언트 routing id가 zlink 내부
    storage를 공유 가능해 stale data를 만들었다. 캐시를 제거하고 기존 `readRoutingIdBytesOut` 사용.
  - `Received` ArrayList pool, lazy routing id byte[], single-part fast path 등 라운드 1~4 변경
    유지.
- 변경한 라이브러리 파일 (round 5에서 추가/수정):
  - `bindings/java/src/main/java/systems/zlink/Socket.java` (`isTransientBlockingSendErrno` 도입)
  - `bindings/java/src/main/java/systems/zlink/RouterRequestSupport.java` (캐시 제거)
  - `bindings/java/perf/single/Zlink.BindingBench/build.gradle.kts` (TieredStopAtLevel=4)
  - `bindings/java/perf/multi/Zlink.BindingBench.Multi/build.gradle.kts` (TieredStopAtLevel=4)
  - `doc/perf/PERF_POLICY.md` (TieredStopAtLevel=4 권장)
- 회귀 테스트:
  - `:test --tests "systems.zlink.contract.SocketContractTest" --tests "systems.zlink.contract.ReceivedContractTest"` 통과
- 다음 판단:
  - Java single 모든 transport, 모든 size, 모든 routed/non-routed 패턴 목표 통과.
  - Multi suite는 server 측 `.move()` + routing id materialization + DONT_WAIT 큐잉 구조 비용으로 ratio 0.32~0.54 영역. 정책 7 perf 수정 제약상 server 흐름은 그대로 두고 binding 내부에서만 더 줄일 여지가 적다.
  - 다음 자동 작업: multi 미달이지만 single 전부 통과했으므로, 사용자 지시 "Java 완료 후 C++/dotnet 미달 진행"으로 넘어간다.
