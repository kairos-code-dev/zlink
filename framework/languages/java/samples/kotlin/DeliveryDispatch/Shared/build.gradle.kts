plugins {
    `java-library`
    id("org.jetbrains.kotlin.jvm") version "2.1.0"
}

dependencies {
    api("systems.zlink:zlink-framework-core:0.1.0-SNAPSHOT")
    implementation(kotlin("stdlib"))
}

kotlin {
    jvmToolchain(22)
}
