plugins {
    application
}

dependencies {
    implementation(project(":Shared"))
}

application {
    applicationName = "runtime-monitoring-client"
    mainClass.set("systems.zlink.e2e.runtimemonitoring.client.Program")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
