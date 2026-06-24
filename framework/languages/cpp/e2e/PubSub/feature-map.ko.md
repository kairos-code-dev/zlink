# C++ Pub/Sub E2E feature map

이 문서는 Config 3 Pub/Sub 공통 시나리오 중 C++ framework E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다.

## 구현한 시나리오

- `PS-A1`: warm-up 이후 세 subscriber가 같은 fanout 측정 sequence를 수신하는지 검증한다.
- `PS-A2`: subscriber handler가 publish context의 topic을 보고 관심 topic만 evidence에
  기록하는지 검증한다.
- `PS-A3`: late subscriber가 합류 이후 발행분만 받고 합류 전 발행분은 replay되지 않는지
  검증한다.
- `PS-A4`: subscriber 프로세스를 종료했다가 같은 topic으로 다시 띄우고, 종료 중 발행분은
  replay되지 않으며 재구독 이후 발행분만 다시 받는지 검증한다. 다른 subscriber가 그 사이에도
  계속 수신하는지도 함께 확인한다.
- `PS-B1`: 한 subscriber handler에 지연을 주입한 상태에서 다른 두 subscriber가 같은 발행
  sequence를 계속 수신하는지 확인해 subscriber 간 격리를 검증한다.
- `PS-B2`: publisher 역할을 하는 client process를 종료한 뒤 새 client process로 다시 발행해,
  기존 subscriber가 재시작 이후 발행분을 다시 받는지 검증한다.
- `PS-C1`: handler 없는 message name으로 publish하면 subscriber dispatch observer에
  `handlerMissing`/`drop` marker가 남고 후속 정상 publish가 오염되지 않는지 검증한다.
