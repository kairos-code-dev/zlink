plugins {
    `java-library`
    id("org.jetbrains.kotlin.jvm") version "2.1.0"
}

fun sampleProject(name: String) = project("${sampleRootPath()}:$name")

fun sampleRootPath(): String {
    val serverIndex = path.indexOf(":Server")
    return if (serverIndex >= 0) {
        path.substring(0, serverIndex)
    } else {
        path.substringBeforeLast(":", "")
    }
}

dependencies {
    api(sampleProject("Shared"))
    api("systems.zlink:zlink-framework-core:0.1.0-SNAPSHOT")
    implementation(kotlin("stdlib"))
}

kotlin {
    jvmToolchain(22)
}
