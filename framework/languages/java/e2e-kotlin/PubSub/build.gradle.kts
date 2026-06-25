plugins {
    application
    id("org.jetbrains.kotlin.jvm") version "2.1.0"
    id("org.jetbrains.kotlin.plugin.spring") version "2.1.0"
}

val e2eBuildDir = providers.environmentVariable("ZLINK_KOTLIN_E2E_BUILD_DIR").orNull
if (!e2eBuildDir.isNullOrBlank()) {
    layout.buildDirectory.set(file(e2eBuildDir))
}

dependencies {
    implementation("systems.zlink:zlink-framework-core:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-spring-boot-starter:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-kotlin:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink:6.0.4")
    implementation("com.fasterxml.jackson.core:jackson-databind:2.17.2")
    implementation("org.springframework.boot:spring-boot-starter:3.5.14")
}

kotlin {
    jvmToolchain(22)
}

java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(22))
    }
}

application {
    applicationName = "pub-sub-kotlin"
    mainClass.set("systems.zlink.e2e.kotlin.pubsub.ProgramKt")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
