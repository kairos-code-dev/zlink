plugins {
    application
}

description = "ZLink Framework Java scenario E2E runner"

dependencies {
    implementation(project(":zlink-framework-core"))
    implementation(project(":zlink-framework-spring-boot-starter"))
    implementation(project(":zlink-http-client"))
    implementation("systems.zlink:zlink:6.0.4")
    implementation("com.fasterxml.jackson.core:jackson-databind:2.17.2")
    implementation("org.springframework.boot:spring-boot-starter:3.5.14")
    implementation("io.netty:netty-buffer:4.1.100.Final")
}

application {
    mainClass.set("systems.zlink.framework.scenarioe2e.ScenarioE2E")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
