plugins {
    `java-library`
    `maven-publish`
}

description = "ZLink Framework Java Spring Boot starter"

dependencies {
    api(project(":zlink-framework-core"))
    api("org.springframework.boot:spring-boot-autoconfigure:3.5.14")
    api("org.springframework:spring-context")
    testImplementation(project(":zlink-framework-testkit"))
}
