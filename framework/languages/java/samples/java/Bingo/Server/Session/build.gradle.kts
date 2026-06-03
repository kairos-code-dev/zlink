plugins {
    application
}

dependencies {
    implementation(files("../../../../../zlink-framework-core/build/libs/zlink-framework-core-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../../zlink-stream-connector/build/libs/zlink-stream-connector-0.1.0-SNAPSHOT.jar"))
    implementation(files("../../../../../../../../bindings/java/build/libs/zlink-java-6.0.4.jar"))
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
            srcDir("../../src/main/java")
            srcDir("src/main/java")
            include("systems/zlink/samples/bingo/server/session/**")
            include("systems/zlink/samples/bingo/shared/**")
        }
    }
}

application {
    mainClass.set("systems.zlink.samples.bingo.server.session.Program")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
