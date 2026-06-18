[← 목차](README.ko.md)

# 11. Proxy

## HTTP proxy

`proxy(url)`로 HTTP proxy를 지정한다. URL은 `http://`로 시작해야 한다.

```kotlin
zlinkHttpClient("https://api.internal") {
    proxy("http://proxy.internal:3128")
}
```

## proxy 인증

```kotlin
zlinkHttpClient("https://api.internal") {
    proxy("http://proxy.internal:3128")
    proxyBasicAuth("proxy-user", "proxy-secret")
}
```

`proxyBasicAuth`는 `Proxy-Authorization: Basic ...` 헤더를 구성한다.

[다음: 압축 →](12-compression.ko.md)
