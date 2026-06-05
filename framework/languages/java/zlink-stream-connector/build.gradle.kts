plugins {
    `java-library`
    `maven-publish`
}

description = "ZLink Java STREAM connector core"

dependencies {
    api(files(rootProject.file("../../../bindings/java/build/libs/zlink-java-6.0.4.jar")))
    api("io.netty:netty-buffer:4.1.100.Final")
    api("io.netty:netty-codec-http:4.1.100.Final")
    api("io.netty:netty-handler:4.1.100.Final")
    api("io.netty:netty-transport:4.1.100.Final")
    implementation("org.lz4:lz4-java:1.8.0")
}
