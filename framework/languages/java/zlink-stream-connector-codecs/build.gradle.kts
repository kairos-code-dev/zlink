plugins {
    `java-library`
    `maven-publish`
}

description = "ZLink Java STREAM connector codec contracts and auto codec selection"

dependencies {
    api(project(":zlink-stream-connector"))
}
