# Kotlin RegistryMessaging E2E feature map

이 문서는 Config 1 Registry Messaging 공통 시나리오 중 Kotlin 전용 E2E 상태를 정리한다. Kotlin은
Java runtime 위의 wrapper를 공유하지만, 이 디렉터리에는 아직 추적된 Kotlin runner/source가 없다.
따라서 현재 Kotlin 전용 stdout marker로 구현 완료를 주장하지 않는다.

## 구현됨

- 없음.

## public API/harness 대기

- `RM-A1`: Kotlin 전용 registry discovery runner와 marker가 아직 없다.
- `RM-A2`: Kotlin 전용 수동 endpoint 연결 runner와 marker가 아직 없다.
- `RM-A4`: 같은 rid endpoint 교체를 Kotlin consumer로 확인하는 runner가 아직 없다.
- `RM-A6`: cross-channel discovery를 Kotlin 코드로 확인하는 runner가 아직 없다.
- `RM-B1`: provider scale-out을 Kotlin consumer로 확인하는 runner가 아직 없다.
- `RM-B2`: provider scale-in과 graceful drain을 Kotlin consumer로 확인하는 runner가 아직 없다.
- `RM-C1`: request/send happy path를 Kotlin API로 확인하는 runner가 아직 없다.
- `RM-C2`: target rid route request와 없는 rid 실패를 Kotlin API로 확인하는 runner가 아직 없다.
- `RM-C3`: multi-endpoint 분산을 Kotlin consumer로 확인하는 runner가 아직 없다.
- `RM-C4`: timeout 뒤 late reply 비오염을 Kotlin marker로 확인하는 runner가 아직 없다.
- `RM-C5`: 미등록 packet negative path를 Kotlin marker로 확인하는 runner가 아직 없다.
- `RM-C6`: dealer mesh peer request를 Kotlin public API로 띄우는 runner가 아직 없다.
- `RM-C7`: server socket weight 설정 API와 Kotlin runner가 아직 없다.
- `RM-C8`: payload 크기 다양성과 상한 초과 거부를 Kotlin marker로 확인하는 runner가 아직 없다.
- `RM-C9`: HWM 포화와 backpressure를 안정적으로 유도하는 Kotlin harness가 아직 없다.
