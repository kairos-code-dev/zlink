plugins {
    application
}

dependencies {
    implementation(project(":Shared"))
    implementation(files("../../../../zlink-framework-core/build/libs/zlink-framework-core-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../zlink-stream-connector/build/libs/zlink-stream-connector-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../zlink-stream-connector-json/build/libs/zlink-stream-connector-json-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../../../../bindings/java/build/libs/zlink-java-6.0.4.jar"))
    implementation("com.fasterxml.jackson.core:jackson-databind:2.17.2")
    implementation("io.netty:netty-buffer:4.1.100.Final")
}

java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(22))
    }
}

application {
    mainClass.set("systems.zlink.samples.bingo.client.Program")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
