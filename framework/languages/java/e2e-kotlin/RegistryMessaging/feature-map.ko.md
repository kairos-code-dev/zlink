# Kotlin Registry Messaging E2E feature map

이 앱은 공통 E2E `config-1-registry-messaging.ko.md`를 Kotlin framework 표면으로 검증한다.

## 구현한 시나리오

- `RM-A1` registry 기반 client/server resolve
- `RM-A2` 명시 endpoint 기반 client/server
- `RM-A4` provider failover 후 registry 재 resolve
- `RM-A6` 여러 channel 동시 discovery
- `RM-B1` provider scale-out
- `RM-B2` provider scale-in
- `RM-C1` request/reply와 send 기본 경로
- `RM-C2` route/dealer mesh request
- `RM-C3` 명시 endpoint 다중 client 분산
- `RM-C4` timeout 후 후속 request 회복
- `RM-C5` 미등록 packet negative path
- `RM-C8` payload size variation roundtrip

## 미구현 항목

- `RM-C7` max-size reject와 high water mark는 현재 Java/Kotlin framework config 표면에서
  공통 E2E가 직접 조절할 수 있는 옵션으로 노출되어 있지 않아 별도 harness가 필요하다.
