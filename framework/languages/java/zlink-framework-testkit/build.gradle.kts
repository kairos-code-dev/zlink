plugins {
    `java-library`
    `maven-publish`
}

description = "ZLink Framework Java testkit with fake backend and contract fixtures"

dependencies {
    api(project(":zlink-framework-core"))
    api(project(":zlink-stream-connector"))
    testImplementation(project(":zlink-framework-codec-msgpack"))
    testImplementation(project(":zlink-framework-codec-protobuf"))
    testImplementation("com.google.protobuf:protobuf-java:4.30.2")
}
