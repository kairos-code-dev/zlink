plugins {
    application
    id("org.jetbrains.kotlin.jvm") version "2.1.0"
}

dependencies {
    implementation(project(":Shared"))
    implementation(files("../../../../../zlink-framework-core/build/libs/zlink-framework-core-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../../zlink-framework-kotlin/build/libs/zlink-framework-kotlin-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../../../../../bindings/java/build/libs/zlink-java-6.0.4.jar"))
    implementation("io.netty:netty-buffer:4.1.100.Final")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.9.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-jdk8:1.9.0")
}

kotlin {
    jvmToolchain(22)
}

application {
    mainClass.set("systems.zlink.samples.kotlin.bingo.server.registry.ProgramKt")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
