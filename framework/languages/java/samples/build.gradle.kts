plugins {
    base
}

val sampleBuildNames = listOf(
    "zlink-java-sample-async",
    "zlink-java-sample-bingo",
    "zlink-java-sample-streaming-client",
    "zlink-java-sample-tictactoe",
    "zlink-java-sample-tictactoe-session-gateway",
    "zlink-kotlin-sample-async",
    "zlink-kotlin-sample-bingo",
    "zlink-kotlin-sample-streaming-client",
    "zlink-kotlin-sample-tictactoe",
    "zlink-kotlin-sample-tictactoe-session-gateway",
)

tasks.register("buildAllSamples") {
    group = LifecycleBasePlugin.BUILD_GROUP
    description = "Builds every Java and Kotlin ZLink sample included in this IDE project."
    dependsOn(sampleBuildNames.map { gradle.includedBuild(it).task(":build") })
}

tasks.register("cleanAllSamples") {
    group = LifecycleBasePlugin.BUILD_GROUP
    description = "Cleans every Java and Kotlin ZLink sample included in this IDE project."
    dependsOn(sampleBuildNames.map { gradle.includedBuild(it).task(":clean") })
}
