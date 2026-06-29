plugins {
    application
}

dependencies {
    implementation(project(":Shared"))
}

application {
    applicationName = "yield-dispatch-client"
    mainClass.set("systems.zlink.e2e.yielddispatch.client.Program")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
