plugins {
    application
}

dependencies {
    implementation(files("../../../zlink-framework-core/build/libs/zlink-framework-core-0.1.0-SNAPSHOT.jar"))
}

java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(22))
    }
}

application {
    mainClass.set("systems.zlink.samples.asyncjava.AsyncJavaSample")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
