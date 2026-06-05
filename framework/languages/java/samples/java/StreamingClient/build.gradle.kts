plugins {
    application
}

dependencies {
    implementation("systems.zlink:zlink-stream-connector:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink:6.0.4")
    implementation("io.netty:netty-buffer:4.1.100.Final")
}

java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(22))
    }
}

application {
    mainClass.set("systems.zlink.samples.streamingclient.StreamingClientSample")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
