plugins {
    application
    id("org.jetbrains.kotlin.jvm") version "2.1.0"
}

dependencies {
    implementation(project(":Shared"))
    implementation(files("../../../../zlink-framework-core/build/libs/zlink-framework-core-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../zlink-framework-kotlin/build/libs/zlink-framework-kotlin-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../zlink-stream-connector/build/libs/zlink-stream-connector-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../zlink-stream-connector-json/build/libs/zlink-stream-connector-json-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../../../../bindings/java/build/libs/zlink-java-6.0.4.jar"))
    implementation("com.fasterxml.jackson.core:jackson-databind:2.17.2")
    implementation("com.fasterxml.jackson.module:jackson-module-kotlin:2.17.2")
    implementation("io.netty:netty-buffer:4.1.100.Final")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.9.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-jdk8:1.9.0")
}

kotlin {
    jvmToolchain(22)
}

sourceSets {
    main {
        kotlin {
            srcDir("../src/main/kotlin")
            srcDir("src/main/kotlin")
            include("systems/zlink/samples/kotlin/tictactoe/server/**")
        }
    }
}

application {
    mainClass.set("systems.zlink.samples.kotlin.tictactoe.server.ProgramKt")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
