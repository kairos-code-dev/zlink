plugins {
    application
}

dependencies {
    implementation(files("../../../../zlink-framework-core/build/libs/zlink-framework-core-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../../../../bindings/java/build/libs/zlink-java-6.0.4.jar"))
    implementation("com.fasterxml.jackson.core:jackson-databind:2.17.2")
    implementation("io.netty:netty-buffer:4.1.100.Final")
}

java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(22))
    }
}

sourceSets {
    main {
        java {
            srcDir("../src/main/java")
            srcDir("src/main/java")
            include("systems/zlink/samples/tictactoe/server/**")
            include("systems/zlink/samples/tictactoe/shared/**")
        }
    }
}

application {
    mainClass.set("systems.zlink.samples.tictactoe.server.Program")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
