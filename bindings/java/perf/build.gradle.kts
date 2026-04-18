plugins {
    java
}

repositories {
    mavenCentral()
}

java {
    toolchain {
        languageVersion = JavaLanguageVersion.of(22)
    }
}

sourceSets {
    named("main") {
        java.setSrcDirs(listOf("src/main/java", "../src/main/java"))
        resources.setSrcDirs(listOf("../src/main/resources"))
    }
}

dependencies {
    compileOnly("io.netty:netty-buffer:4.1.100.Final")
    runtimeOnly("io.netty:netty-buffer:4.1.100.Final")
}

tasks.register<JavaExec>("runMemoryMicrobench") {
    group = "benchmark"
    description = "Run Java memory-path microbenchmarks (FFM vs pre-FFM style)"
    classpath = sourceSets["main"].runtimeClasspath
    mainClass.set("dev.kairoscode.zlink.perf.MemoryInteropMicrobench")
    jvmArgs("--enable-native-access=ALL-UNNAMED")
}

tasks.register<JavaExec>("runMessagePathMicrobench") {
    group = "benchmark"
    description = "Run Java message-path microbenchmarks"
    classpath = sourceSets["main"].runtimeClasspath
    mainClass.set("dev.kairoscode.zlink.MessagePathMicrobench")
    jvmArgs("--enable-native-access=ALL-UNNAMED")
}
