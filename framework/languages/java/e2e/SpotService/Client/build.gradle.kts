plugins {
    application
}

dependencies {
    implementation(project(":Shared"))
}

application {
    applicationName = "spot-service-client"
    mainClass.set("systems.zlink.e2e.spotservice.client.Program")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
