plugins {
    application
    id("org.jetbrains.kotlin.jvm")
}

dependencies {
    implementation(project(":Shared"))
    implementation("systems.zlink:zlink-stream-connector:0.1.0-SNAPSHOT")
}

kotlin {
    jvmToolchain(22)
}

application {
    applicationName = "yield-dispatch-kotlin-client"
    mainClass.set("systems.zlink.e2e.kotlin.yielddispatch.ProgramKt")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
