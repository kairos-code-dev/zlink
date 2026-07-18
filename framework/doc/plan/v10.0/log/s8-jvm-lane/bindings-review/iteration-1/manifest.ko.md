# S8 JVM bindings 전환 리뷰 manifest — iteration 1

| 항목 | 값 |
|---|---|
| Stage/iteration | S8-JVM / 1 |
| 대상 commit | `5c2eb2acc` |
| Scope | java/src/main + java/native/src + java·kotlin samples + build.gradle (native binaries/build/resources native 제외) |
| 파일 수 | 255 |
| hash | `8af1d48c9ebc4c67d6ee90ba48a6350228300e4f819e03c68840611933dcdf12` |
| R1/R2 | opus / Claude Sonnet |

## 2. Coordinator 실행 증거
- `./gradlew :compileJava :samples:compileJava :kotlin-samples:compileKotlin -x buildZlinkJavaBridge`: ALL GREEN(rc=0).
- no-hit 8종(SpotNode/SpotRouteBridge/spot_node/route_bridge/subjects/internal_sockets/dispatch_workers/msg_gets) 전량 0.

## 3. 알려진 관찰(독립 판정)
- raw-layer 드리프트(`log/s8-common-raw-layer-drift.ko.md`): router_recv_part 파라미터·stream_detach/attach_raw·msg_refcnt·subscribe_handler. jvm이 raw path를 유지했으므로 FFI downcall 시그니처가 Core 10.0.0과 정합인지 확인 필요.
- tests는 scope 밖(pre-broken).

## 4. Session 기록
| | R1 opus | R2 Sonnet |
|---|---|---|
| 결과 | `codex/review.ko.md` | `claude-sonnet/review.ko.md` |
