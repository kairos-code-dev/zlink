# ASP.NET Core Registry

Core C API의 Discovery/Registry 표면은 제거되었다. 이 문서는 예전 ASP.NET Core Registry
계약을 더 이상 정식 공개 계약으로 정의하지 않는다.

현재 공개 계약에서 사용할 수 있는 연결 방식은 각 역할 등록에서 endpoint 를 명시하는 방식이다.
channel 이름이나 SPOT 이름으로 endpoint 를 찾는 자동 연결은 location runtime 과 location
store 설계가 정식 계약으로 확정된 뒤 별도 spec 에서 다룬다.

## 1. 회귀 테스트

이 문서는 제거된 Registry 계약을 다시 공개 표면으로 설명하지 않도록 지키기 위한 자리다.
Registry 또는 Discovery 기반 API 를 되살리려면 먼저 별도 draft/spec 에서 공개 계약을 확정해야
한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RegressionTests.DotNet_Samples_Do_Not_Use_Legacy_Registry_Discovery` | .NET sample 이 제거된 Registry/Discovery API 를 다시 사용하지 않는다. |
| `ScaffoldSmokeTests.PublicSurface_DoesNotExpose_BackendConcreteTypes` | framework public API 가 backend 구현 세부사항을 직접 노출하지 않는다. |
