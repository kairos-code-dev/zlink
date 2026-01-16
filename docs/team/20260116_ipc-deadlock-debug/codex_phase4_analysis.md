# Phase 4 실패 원인 분석 (Codex)

팀장님,

## 요약 판단
Phase 4의 실패율 악화는 **Complete Strand 자체의 직렬화가 IPC의 초저지연 패턴과 충돌**했고, 그 중에서도 **`restart_input()`의 `post()` 큐잉이 즉시 재시작을 막아 Head-of-line blocking을 만든 것**이 핵심 촉발 요인으로 보입니다. Phase 3에서는 일부 핸들러가 strand 밖에서 병렬로 진행되며 “운 좋게” 진행이 되었지만, Phase 4는 모든 경로가 단일 큐에 묶이면서 지연이 누적되고 타임아웃으로 이어졌습니다.

## 핵심 질문별 분석

### 1. 왜 Complete Strand (Phase 4)가 Partial Strand (Phase 3)보다 2배 더 나쁜가?
- **단일 큐 강제**로 인해 IPC의 동시 read/write/timer/handshake 이벤트가 순차 대기열에 쌓임.
- Phase 3의 “부분 직렬화”는 오히려 **핸들러가 우회 실행되는 탈출구**를 남겨, 일부 런에서는 교착을 피할 수 있었음.
- Phase 4는 모든 완료 이벤트가 strand에 묶여 **Head-of-line blocking이 결정적으로 강화**됨.

### 2. post() 변경이 문제인가? dispatch로 되돌려야 하는가?
- **문제 가능성이 매우 높음.**
- `dispatch`는 이미 strand 안에서 호출될 때 **즉시 실행**되어 read 재개 타이밍을 보장함.
- `post`는 항상 큐에 넣기 때문에 **restart_input_internal()이 후순위로 밀림** → backpressure 해제가 지연 → 읽기 정지 상태가 장기화.
- 결론: **`post → dispatch` 복귀는 필수적인 확인 실험**이며, 현재 관측된 실패율 급락과 가장 정합적임.

### 3. Strand 오버헤드가 IPC 처리량을 임계점 아래로 떨어뜨린 것인가?
- **보조 원인 가능성.**
- IPC의 초저지연 환경에서는 strand의 큐잉/atomic 비용이 체감될 수 있으나, 단독으로 “성공률 30%”까지 떨어뜨리기에는 설명력이 낮음.
- 성능 저하 자체보다 **“진행 순서/타이밍 역전”**이 실패율 급락과 더 일치함.

### 4. session의 restart_input() 호출이 post로 큐잉되는 것이 핵심 문제인가?
- **예, 가장 가능성이 높음.**
- session이 backpressure 해제 후 즉시 재시작을 기대하지만, post는 이를 **strand 큐 후순위로 밀어** 재시작 지연을 유발.
- 이 지연 동안 read/write 완료 핸들러가 더 쌓이면서 **교착 조건이 강화**됨.

## 권장 옵션
**Option B: dispatch만 되돌리기 (post → dispatch)**

이유:
- 현재 실패율 급락과 가장 직접적으로 연결된 변경점은 `restart_input()`의 `post` 전환입니다.
- Complete Strand의 타당성을 판단하려면 **“완전 직렬화 + dispatch”**를 먼저 확인해야 합니다.
- 이 실험 결과가 개선되지 않으면, 그때 **Strand 자체를 포기(Option A/C)**하는 결론이 더 설득력을 가집니다.

## 다음 단계 (제안)
1. `restart_input()`만 `dispatch`로 되돌린 Phase 4b 수행.
2. 동일 조건(2K/10K 반복)에서 재측정.
3. 개선 없다면 Strand 전체 포기(Option A 또는 C)로 전환.

