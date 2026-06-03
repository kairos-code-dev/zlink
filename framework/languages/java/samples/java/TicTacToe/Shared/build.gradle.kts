plugins {
    `java-library`
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
            include("systems/zlink/samples/tictactoe/shared/**")
        }
    }
}
