plugins {
    application
    id("org.jetbrains.kotlin.jvm") version "2.1.0"
}

fun sampleProject(name: String) = project("${sampleRootPath()}:$name")

fun sampleRootPath(): String {
    val serverIndex = path.indexOf(":Server")
    return if (serverIndex >= 0) {
        path.substring(0, serverIndex)
    } else {
        path.substringBeforeLast(":", "")
    }
}

dependencies {
    implementation(sampleProject("Shared"))
    implementation(files("../../../../../zlink-framework-core/build/libs/zlink-framework-core-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../../zlink-framework-spring-boot-starter/build/libs/zlink-framework-spring-boot-starter-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../../zlink-framework-kotlin/build/libs/zlink-framework-kotlin-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../../../../../bindings/java/build/libs/zlink-java-6.0.4.jar"))
    implementation("org.springframework.boot:spring-boot-starter:3.5.14")
    implementation("io.netty:netty-buffer:4.1.100.Final")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.9.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-jdk8:1.9.0")
}

kotlin {
    jvmToolchain(22)
}

application {
    mainClass.set("systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.registry.ProgramKt")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
