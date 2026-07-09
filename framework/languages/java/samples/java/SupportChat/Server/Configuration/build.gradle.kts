plugins {
    `java-library`
}

dependencies {
    implementation(project(":Shared"))
    implementation("systems.zlink:zlink-framework-locations-redis:0.1.0-SNAPSHOT")
}

java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(22))
    }
}
