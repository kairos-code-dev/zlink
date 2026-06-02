plugins {
    `java-library`
    `maven-publish`
}

description = "ZLink Java STREAM connector MessagePack codec"

dependencies {
    api(project(":zlink-stream-connector-codecs"))
}
