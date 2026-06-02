plugins {
    `java-library`
    `maven-publish`
}

description = "ZLink Java STREAM connector JSON codec"

dependencies {
    api(project(":zlink-stream-connector-codecs"))
}
