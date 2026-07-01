plugins {
    application
}

dependencies {
    implementation(project(":Shared"))
    implementation("systems.zlink:zlink-http-client:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink:6.0.4")
    implementation("com.fasterxml.jackson.core:jackson-databind:2.17.2")
}

application {
    applicationName = "registration-codec-client"
    mainClass.set("systems.zlink.e2e.registrationcodec.client.Program")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
