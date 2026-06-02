plugins {
    `java-library`
    `maven-publish`
}

description = "ZLink Framework Java testkit with fake backend and contract fixtures"

dependencies {
    api(project(":zlink-framework-core"))
    api(project(":zlink-stream-connector"))
    testImplementation(project(":zlink-stream-connector-json"))
    testImplementation(project(":zlink-stream-connector-msgpack"))
    testImplementation(project(":zlink-stream-connector-protobuf"))
}
