plugins {
    `java-library`
    `maven-publish`
}

description = "ZLink Framework Java core contracts, runtime, handler scanner, and backend adapter"

dependencies {
    api(zlinkLibs.zlink.bindings)
    compileOnlyApi("org.jspecify:jspecify:1.0.0")
    api("io.netty:netty-buffer:4.1.100.Final")
    api("com.fasterxml.jackson.core:jackson-databind:2.17.2")
    implementation("org.lz4:lz4-java:1.8.0")
}
