plugins {
    `java-library`
    `maven-publish`
}

description = "ZLink Framework Java Spring Boot starter"

dependencies {
    api(project(":zlink-framework-core"))
    api("org.springframework.boot:spring-boot-autoconfigure:3.5.14")
    api("org.springframework:spring-context")
    api("org.springframework.boot:spring-boot-actuator:3.5.14")
    compileOnly("io.micrometer:micrometer-core:1.15.8")
    testImplementation("io.micrometer:micrometer-core:1.15.8")
    testImplementation(project(":zlink-framework-testkit"))
}
