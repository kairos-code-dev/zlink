plugins {
    application
}

dependencies {
    implementation(project(":Shared"))
    implementation(project(":Server:Play"))
    implementation("org.springframework.boot:spring-boot-starter:3.5.14")
}

application {
    applicationName = "spot-service-multi-node"
    mainClass.set("systems.zlink.e2e.spotservice.multinode.Program")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
