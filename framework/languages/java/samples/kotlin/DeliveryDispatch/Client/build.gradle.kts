plugins {
    application
    id("org.jetbrains.kotlin.jvm")
}

dependencies {
    implementation(project("${path.substringBeforeLast(":Client")}:Shared"))
    implementation(project("${path.substringBeforeLast(":Client")}:Server:Configuration"))
    implementation("systems.zlink:zlink-stream-connector:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-kotlin:0.1.0-SNAPSHOT")
    implementation(zlinkLibs.zlink.bindings)
    implementation("com.fasterxml.jackson.module:jackson-module-kotlin:2.17.2")
    implementation("com.fasterxml.jackson.datatype:jackson-datatype-jsr310:2.17.2")
    implementation("io.netty:netty-buffer:4.1.100.Final")
}

kotlin {
    jvmToolchain(22)
}

application {
    mainClass.set("systems.zlink.samples.kotlin.deliverydispatch.client.ProgramKt")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
