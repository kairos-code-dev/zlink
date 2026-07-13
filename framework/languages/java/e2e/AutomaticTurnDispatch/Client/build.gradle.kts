plugins {
    application
}

dependencies {
    implementation(project(":Shared"))
}

application {
    applicationName = "automatic-turn-dispatch-client"
mainClass.set("systems.zlink.e2e.automaticturn.client.Program")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
