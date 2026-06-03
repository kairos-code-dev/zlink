plugins {
    id("org.jetbrains.kotlin.jvm") version "2.1.0"
}

kotlin {
    jvmToolchain(22)
}

sourceSets {
    main {
        kotlin {
            srcDir("../src/main/kotlin")
            include("systems/zlink/samples/kotlin/tictactoe/shared/**")
        }
    }
}
