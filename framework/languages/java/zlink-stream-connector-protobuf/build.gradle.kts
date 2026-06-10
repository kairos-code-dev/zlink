plugins {
    `java-library`
    `maven-publish`
}

description = "ZLink Java STREAM connector Protobuf codec"

dependencies {
    api(project(":zlink-stream-connector"))
    api(project(":zlink-stream-connector-codecs"))
    implementation("com.fasterxml.jackson.core:jackson-databind:2.17.2")
}
