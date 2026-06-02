plugins {
    application
}

dependencies {
    implementation(files("../../zlink-stream-connector/build/libs/zlink-stream-connector-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../../bindings/java/build/libs/zlink-java-6.0.4.jar"))
    implementation("io.netty:netty-buffer:4.1.100.Final")
}

java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(22))
    }
}

application {
    mainClass.set("systems.zlink.samples.bingo.BingoSample")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
