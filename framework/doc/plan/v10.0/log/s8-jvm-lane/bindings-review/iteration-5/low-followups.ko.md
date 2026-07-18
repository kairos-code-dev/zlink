# S8 JVM bindings iteration-5 — low finding follow-up
두 리뷰어 iteration-5 `BINDINGS REVIEW CLEAN`(세 축 blocker/high/medium 0). 아래 low는 non-blocking.
- I1-L1 [low] `zlink_java_reqrep_bridge.c` JV4-1 삭제 후 남은 orphan include(`<errno.h>/<stdlib.h>/<vector>`). 빌드 무해.
- I1-L2 [low] `zlink_has` FFI descriptor가 Core `bool`(1B)에 JAVA_INT 반환 선언 — 사용 ABI에서 실무상 안전하나 부정확.
(framework/S11 정리 시 처리.)
