# Kotlin YieldDispatch YD-E3 route recovery bug

이 문서는 구현 완료 보고가 아니라 재현 기록이다. `YD-E3` shutdown/recovery를 Kotlin E2E에 추가하려는
과정에서, 기존 stream session이 같은 routing id로 재시작한 Play route에 request를 보내지 못하는 현상이
확인되었다.

## 재현 절차

```bash
cd framework/languages/java/e2e-kotlin/YieldDispatch
nice -n 10 timeout 240s ./run_e2e.sh YD-E3
```

runner는 자신이 시작한 `play-a` PID만 종료한다. 외부 process를 pattern으로 종료하지 않는다. Redis가
지정되지 않으면 runner가 `zlink-e2e-yielddispatch-kotlin-<pid>` 컨테이너를 만들고 cleanup에서 그
컨테이너만 제거한다.

## 관찰 로그

- `logs/20260707-192818-3296525/client-e3.stderr.log`
  - 재시작 후 recovery `ProbeReq`가 `request timed out after PT15S`로 실패한다.
- `logs/20260707-192818-3296525/session-flow.log`
  - `ShutdownYieldReq`와 shutdown-ready 확인용 `EvidenceReq`는 session에 도착한다.
  - 재시작 신호 이후 `ProbeReq`도 session에는 도착하지만, 이후 route send/reply marker가 남지 않는다.
- `logs/20260707-192818-3296525/play-a-flow.log`
  - 최초 `ShutdownYieldReq`와 `EvidenceReq`는 기존 `play-a`에서 처리된다.
  - 재시작 후 `ProbeReq`가 Play handler까지 도달한 marker는 없다.
- `logs/20260707-192818-3296525/play-restarted.stdout.log`
  - 같은 `play-a` routing id의 route/spot row가 `generation=2`로 다시 claim된다.

## 현재 판단

Kotlin scenario만 통과시키기 위해 별도 우회 route, raw frame, sleep 기반 retry를 넣지 않는다.
증상은 Kotlin client code 자체보다 Java 공용 framework의 route mesh/session route recovery 쪽에 가깝다.
다만 같은 현상이 Java E2E, Node/C++, 또는 binding 수준에서 재현되는지는 아직 확인하지 않았다.

## 후속 확인

1. Java `YieldDispatch` 또는 같은 route mesh recovery scenario에서 기존 stream session이 같은 routing id로
   재시작한 route peer에 request를 다시 보낼 수 있는지 확인한다.
2. Node/C++에서 같은 의미의 `YD-E3`가 통과한다면 Java/Kotlin route mesh 또는 stream session bridge 문제로
   좁힌다.
3. 원인이 route mesh recovery이면 Kotlin E2E에는 bug report 링크만 남기고, framework/binding 계층에서
   수정한 뒤 `YD-E3` focused runner를 다시 실행한다.
