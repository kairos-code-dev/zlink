plugins {
    `java-library`
    `maven-publish`
}

description = "ZLink Java STREAM connector Protobuf codec"

dependencies {
    api(project(":zlink-stream-connector-codecs"))
}
