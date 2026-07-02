[English](README.md) | [한국어](README.ko.md)

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md)

# 서비스 API

현재 core가 공개 C API로 제공하는 서비스 계약은 SPOT 계층이다. Discovery와
Registry C API는 core 공개 계약에서 제거되었다. 응용과 바인딩은
제거된 header나 함수 prefix를 현재 공개 API처럼 다시 노출하지 않는다.

SPOT publish/subscribe, route bridge, publisher, Actor, direct routed messaging
계약은 [spot.ko.md](spot.ko.md)를 읽는다.
