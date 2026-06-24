# Kotlin RegistrationCodec E2E feature map

이 문서는 Config 4 Registration/Codec 공통 시나리오 중 Kotlin 전용 E2E 상태를 정리한다. 현재 추적된
Kotlin RegistrationCodec runner/source가 없으므로 Kotlin 전용 stdout marker로 구현 완료를 주장하지
않는다.

## 구현됨

- 없음.

## public API/harness 대기

- `RC-A1`: Kotlin handler 자동 등록 runner와 marker가 아직 없다.
- `RC-A2`: Kotlin annotation/suspend handler 등록 runner와 marker가 아직 없다.
- `RC-A3`: Kotlin 수동 handler 등록 runner와 marker가 아직 없다.
- `RC-A4`: Kotlin DI lifecycle runner와 marker가 아직 없다.
- `RC-A5`: Kotlin filter ordering runner와 marker가 아직 없다.
- `RC-A6`: 잘못된 등록 차단 Kotlin runner와 marker가 아직 없다.
- `RC-B1`: JSON codec Kotlin runner와 marker가 아직 없다.
- `RC-B2`: Protobuf codec Kotlin runner와 marker가 아직 없다.
- `RC-B3`: MessagePack codec Kotlin runner와 marker가 아직 없다.
- `RC-B4`: codec 공존 Kotlin runner와 marker가 아직 없다.
- `RC-B5`: peer codec mismatch Kotlin runner와 marker가 아직 없다.
