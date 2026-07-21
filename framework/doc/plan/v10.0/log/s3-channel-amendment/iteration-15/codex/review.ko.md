# Codex 독립 문서 review — iteration 15

## Finding

[계약][major] `framework/doc/framework/spec/server/languages/java/01-system-structure.ko.md:338` — Instance
Spot 예제는 `submit()`이 activation까지 기다린다고 설명한다 — 공통 async 계약과 같은 Java exact interface의
§2.16은 target-side claim·activation을 완료 조건에서 제외한다 — 주석을 address resolve·eligible target 선택과
source local outbound admission까지만 기다린다고 수정해야 한다.

## 판정

Open finding이 있으므로 이 snapshot은 종료 gate를 통과하지 못했다.

DOC REVIEW NOT CLEAN
