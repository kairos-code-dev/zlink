plugins {
    application
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
    applicationName = "zlink-java-perf-single"
    mainClass.set("dev.kairoscode.zlink.perf.single.PerfMain")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}

tasks.withType<JavaExec>().configureEach {
    jvmArgs("--enable-native-access=ALL-UNNAMED")
}

tasks.named<JavaExec>("run") {
    jvmArgs("--enable-native-access=ALL-UNNAMED")
}
