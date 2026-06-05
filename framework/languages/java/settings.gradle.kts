pluginManagement {
    repositories {
        gradlePluginPortal()
        mavenCentral()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        mavenCentral()
    }
}

rootProject.name = "zlink-framework-java"

includeBuild("samples") {
    name = "zlink-framework-java-samples"
}

include(
    "zlink-framework-core",
    "zlink-framework-spring-boot-starter",
    "zlink-stream-connector",
    "zlink-stream-connector-codecs",
    "zlink-stream-connector-json",
    "zlink-stream-connector-msgpack",
    "zlink-stream-connector-protobuf",
    "zlink-framework-kotlin",
    "zlink-framework-testkit",
)
