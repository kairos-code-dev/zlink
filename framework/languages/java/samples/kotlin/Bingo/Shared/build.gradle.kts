plugins {
    id("org.jetbrains.kotlin.jvm") version "2.1.0"
}

dependencies {
    api(files("../../../../zlink-framework-core/build/libs/zlink-framework-core-0.1.0-SNAPSHOT.jar"))
    api(files("../../../../zlink-framework-kotlin/build/libs/zlink-framework-kotlin-0.1.0-SNAPSHOT.jar"))
}

kotlin {
    jvmToolchain(22)
}
