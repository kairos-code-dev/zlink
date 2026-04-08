plugins {
    application
}

val perfBuildDir = providers.gradleProperty("zlinkPerfBuildDir").orNull
if (!perfBuildDir.isNullOrBlank()) {
    layout.buildDirectory.set(file(perfBuildDir))
}

repositories {
    mavenCentral()
}

java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(22))
    }
}

sourceSets {
    named("main") {
        java.setSrcDirs(listOf("src/main/java", "../../common/src/main/java"))
    }
}

dependencies {
    implementation(project(":"))
    compileOnly("io.netty:netty-buffer:4.1.100.Final")
}

application {
    applicationName = "zlink-java-perf-multi"
    mainClass.set("dev.kairoscode.zlink.perf.multi.PerfMain")
    applicationDefaultJvmArgs = listOf(
        "--enable-native-access=ALL-UNNAMED",
        "-server",
        "-XX:TieredStopAtLevel=1",
    )
}

tasks.withType<JavaExec>().configureEach {
    jvmArgs("--enable-native-access=ALL-UNNAMED", "-server", "-XX:TieredStopAtLevel=1")
}

tasks.named<JavaExec>("run") {
    jvmArgs("--enable-native-access=ALL-UNNAMED", "-server", "-XX:TieredStopAtLevel=1")
}
