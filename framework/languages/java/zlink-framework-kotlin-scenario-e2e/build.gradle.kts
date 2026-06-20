plugins {
    application
    id("org.jetbrains.kotlin.jvm")
    id("org.jetbrains.kotlin.plugin.spring")
}

description = "ZLink Framework Kotlin scenario E2E runner"

kotlin {
    jvmToolchain(22)
}

dependencies {
    implementation(project(":zlink-framework-core"))
    implementation(project(":zlink-framework-kotlin"))
    implementation(project(":zlink-framework-spring-boot-starter"))
    implementation(project(":zlink-http-client"))
    implementation(project(":zlink-http-client-kotlin"))
    implementation("systems.zlink:zlink:6.0.4")
    implementation("com.fasterxml.jackson.core:jackson-databind:2.17.2")
    implementation("com.fasterxml.jackson.module:jackson-module-kotlin:2.17.2")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.9.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-jdk8:1.9.0")
    implementation("org.springframework.boot:spring-boot-starter:3.5.14")
    implementation("io.netty:netty-buffer:4.1.100.Final")
}

application {
    mainClass.set("systems.zlink.framework.kotlin.scenarioe2e.ScenarioE2E")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
