plugins {
    `java-library`
    `maven-publish`
    id("org.jetbrains.kotlin.jvm")
}

description = "ZLink Framework Kotlin coroutine and DSL extensions"

kotlin {
    jvmToolchain(22)
}

dependencies {
    api(project(":zlink-framework-core"))
    api(project(":zlink-stream-connector"))
}
