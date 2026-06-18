plugins {
    `java-library`
    `maven-publish`
    jacoco
    id("org.jetbrains.kotlin.jvm")
}

description = "ZLink Kotlin HTTP client coroutine (suspend) and DSL extensions"

kotlin {
    jvmToolchain(22)
}

dependencies {
    api(project(":zlink-http-client"))
    api("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.9.0")
    api("org.jetbrains.kotlinx:kotlinx-coroutines-jdk8:1.9.0")
    // Lets the shared Jackson mapper (findAndAddModules) deserialize Kotlin data classes.
    api("com.fasterxml.jackson.module:jackson-module-kotlin:2.17.2")
}

tasks.named<Test>("test") {
    finalizedBy(tasks.named("jacocoTestReport"))
}

tasks.named<JacocoReport>("jacocoTestReport") {
    dependsOn(tasks.named("test"))
    reports {
        xml.required.set(true)
        html.required.set(true)
    }
}

tasks.named<JacocoCoverageVerification>("jacocoTestCoverageVerification") {
    dependsOn(tasks.named("test"))
    violationRules {
        rule {
            limit {
                counter = "LINE"
                minimum = "0.80".toBigDecimal()
            }
        }
    }
}

tasks.named("check") {
    dependsOn(tasks.named("jacocoTestCoverageVerification"))
}
