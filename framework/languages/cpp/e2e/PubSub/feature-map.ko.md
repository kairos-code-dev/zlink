# C++ Pub/Sub E2E feature map

이 문서는 Config 3 Pub/Sub 공통 시나리오 중 C++ framework E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다.

## 구현한 시나리오

- `PS-A1`: warm-up 이후 세 subscriber가 같은 fanout 측정 sequence를 수신하는지 검증한다.
- `PS-A2`: subscriber handler가 publish context의 topic을 보고 관심 topic만 evidence에
  기록하는지 검증한다.
- `PS-A3`: late subscriber가 합류 이후 발행분만 받고 합류 전 발행분은 replay되지 않는지
  검증한다.
- `PS-C1`: handler 없는 message name으로 publish하면 subscriber dispatch observer에
  `handlerMissing`/`drop` marker가 남고 후속 정상 publish가 오염되지 않는지 검증한다.

## public API/harness 대기

- `PS-A4`: subscriber process restart와 재구독 이후의 비replay 관측은 가능하지만, 현재 runner는
  재접속 시점과 발행 구간을 안정적으로 고정하는 전용 restart 단계가 없다.
- `PS-B1`: 느린 subscriber handler와 빠른 subscriber 격리를 동시에 단언하려면 subscriber별 처리
  지연을 주입하는 harness 옵션이 필요하다.
- `PS-B2`: publisher process restart 뒤 기존 subscriber의 재수신을 검증하려면 publisher 역할을
  별도 장기 실행 프로세스로 띄우고 재기동하는 runner 구성이 필요하다. 현재 C++ Pub/Sub runner는
  client process가 scenario별 publish를 수행한 뒤 종료된다.
