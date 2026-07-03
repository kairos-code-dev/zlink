plugins {
    id("org.jetbrains.kotlin.jvm")
}

dependencies {
    api("systems.zlink:zlink-framework-locations-redis:0.1.0-SNAPSHOT")
}

kotlin {
    jvmToolchain(22)
}
