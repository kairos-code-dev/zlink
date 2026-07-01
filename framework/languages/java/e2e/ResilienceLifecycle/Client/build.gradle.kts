plugins {
    application
}

dependencies {
    implementation(project(":Shared"))
}

application {
    applicationName = "resilience-lifecycle-client"
    mainClass.set("systems.zlink.e2e.resiliencelifecycle.client.Program")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
