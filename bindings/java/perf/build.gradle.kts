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
        java.setSrcDirs(listOf("../src/main/java"))
        resources.setSrcDirs(listOf("../src/main/resources"))
    }
}

dependencies {
    compileOnly("io.netty:netty-buffer:4.1.100.Final")
    runtimeOnly("io.netty:netty-buffer:4.1.100.Final")
}
