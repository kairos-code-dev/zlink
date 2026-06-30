plugins {
    application
}

dependencies {
    implementation(project(":Shared"))
    implementation("com.fasterxml.jackson.core:jackson-databind:2.17.2")
}

application {
    applicationName = "discovery-registry-ha-client"
    mainClass.set("systems.zlink.e2e.discoveryregistryha.client.Program")
}
