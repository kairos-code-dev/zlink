plugins {
    `java-library`
}

dependencies {
    api(files("../../../../zlink-framework-core/build/libs/zlink-framework-core-0.1.0-SNAPSHOT.jar"))
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
            include("systems/zlink/samples/bingo/shared/**")
        }
    }
}
