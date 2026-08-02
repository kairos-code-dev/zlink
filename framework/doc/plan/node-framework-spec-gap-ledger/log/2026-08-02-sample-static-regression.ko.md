# Node sample static regression follow-up

이 기록은 sample 절의 정적 regression만 갱신한다. 실제 sample process·browser 실행은 E2E 범위로
분리하며, 정적 test가 통과했다는 이유로 process evidence를 완료로 표시하지 않는다.

## 확인한 차이

`sample-deliverydispatch-spot-handle-gate.test.js`가 `SampleNames.routeMesh`를 요구했지만,
현재 DeliveryDispatch의 authoritative shared contract와 다른 regression은
`SampleNames.courierMeshName`을 사용한다. 공통 sample 문서도 `deliverydispatch.courier` RouteMesh를
정의하므로 test assertion이 stale 상태였다.

## 수정

test가 `addRouteMesh(SampleNames.courierMeshName)`을 검사하도록 정렬했다. sample source에
호환 alias나 새 public API는 추가하지 않았다.

## 검증

```text
node --test --test-force-exit test/contract/sample-deliverydispatch-spot-handle-gate.test.js
  1/1 PASS

sample*.test.js (sample-regression.test.js 제외)
  35/35 PASS
```

전체 sample contract suite는 `sample-regression.test.js`의 실제
`run_samples.sh` 실행에서 TicTacToe browser stream message timeout으로 실패했다. 이는 static
assertion 수정으로 숨기지 않았으며, E2E/process 범위의 후속 조건으로 유지한다.
