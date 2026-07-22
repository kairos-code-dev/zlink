# Java Location Store 문서 위치

Java Location Store와 maintenance public signature는
[Location과 maintenance](interfaces/location-maintenance.ko.md)에서 제공한다. 공통 동작은
[Location runtime](../../40-location-runtime.ko.md)과 [Redis Location Store](../../41-location-store-redis.ko.md)를
따른다. Location provider는 descriptor·location 기능과 opaque authority CAS capability를 함께 제공하며,
Transfer Store만 별도 provider로 등록한다.
