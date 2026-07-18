# S8 JVM bindings 리뷰 manifest — iteration 3
| iteration | S8-JVM / 3 | commit | `39b1edee8` | 파일 수 | 251 |
| hash | `f35dd2fe28be90088b482a799e45389d4fab259c80366b527fa5d9e38a94af95` |
| R1/R2 | opus / Sonnet |
## 2. Coordinator 실행 증거
- iter-2 JV2-1(descriptor 5 ADDRESS 복원)·JV2-2(router_handler→recv_handler) 수정. compileJava+samples+kotlin ALL GREEN. no-hit 8종 0. **제거심볼 게이트 EMPTY**. router_recv_part descriptor 5 ADDRESS+1 INT=6param(각 변형). recv_handler∈nm -D, router_handler=0.
- **모든 FFI downcall descriptor arity가 invokeExact call site와 대조 검증됨**(iter-2 R2가 발견한 버그 클래스).
## 3. Session 기록
| | R1 opus | R2 Sonnet | codex/·claude-sonnet/ |
