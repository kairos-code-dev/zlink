[← 목차](README.ko.md)

# 11. Proxy

## HTTP proxy

`proxy(url)`로 HTTP proxy를 지정한다. URL은 `http://`로 시작해야 한다. 내부적으로
`ProxySelector`로 `HttpClient`에 구성된다.

```java
ZLinkHttpClient.create("https://api.internal")
    .proxy("http://proxy.internal:3128")
    .build();
```

## proxy 인증

```java
ZLinkHttpClient.create("https://api.internal")
    .proxy("http://proxy.internal:3128")
    .proxyBasicAuth("proxy-user", "proxy-secret")
    .build();
```

`proxyBasicAuth`는 `Proxy-Authorization: Basic ...` 헤더를 구성한다.

[다음: 압축 →](12-compression.ko.md)
