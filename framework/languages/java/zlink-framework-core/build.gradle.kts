plugins {
    `java-library`
    `maven-publish`
}

description = "ZLink Framework Java core contracts, runtime, handler scanner, and backend adapter"

dependencies {
    api(files(rootProject.file("../../../bindings/java/build/libs/zlink-java-6.0.4.jar")))
    api("io.netty:netty-buffer:4.1.100.Final")
    api("com.fasterxml.jackson.core:jackson-databind:2.17.2")
    implementation("org.lz4:lz4-java:1.8.0")
}
