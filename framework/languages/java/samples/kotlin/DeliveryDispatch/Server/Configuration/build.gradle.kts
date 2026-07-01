plugins {
    id("org.jetbrains.kotlin.jvm")
}

dependencies {
    implementation(project(":Shared"))
}

kotlin {
    jvmToolchain(22)
}
