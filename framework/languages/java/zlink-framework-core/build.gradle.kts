plugins {
    `java-library`
    `maven-publish`
}

description = "ZLink Framework Java core contracts, runtime, handler scanner, and backend adapter"

sourceSets {
    main {
        java.srcDir("../../../runtime/protocol/generated/jvm")
    }

    val m5Foundation by creating {
        java.setSrcDirs(listOf(
            "src/main/java",
            "../../../runtime/protocol/generated/jvm"
        ))
        java.include(
            "systems/zlink/framework/runtime/binding/ZLinkJavaRawServicePort.java",
            "systems/zlink/framework/runtime/internal/service/**",
            "ServiceWireConstants.java"
        )
        compileClasspath += configurations.compileClasspath.get()
        runtimeClasspath += output + compileClasspath + configurations.runtimeClasspath.get()
    }

    val m5FoundationTest by creating {
        java.setSrcDirs(listOf("src/test/java"))
        java.include(
            "systems/zlink/framework/runtime/binding/ZLinkJavaRawServicePortContractTest.java",
            "systems/zlink/framework/runtime/internal/service/**"
        )
        compileClasspath += m5Foundation.output + configurations.testCompileClasspath.get()
        runtimeClasspath += output + compileClasspath + configurations.testRuntimeClasspath.get()
    }

    configurations.named(m5FoundationTest.implementationConfigurationName) {
        extendsFrom(configurations.testImplementation.get())
    }
    configurations.named(m5FoundationTest.runtimeOnlyConfigurationName) {
        extendsFrom(configurations.testRuntimeOnly.get())
    }

    tasks.register<Test>("m5FoundationTest") {
        description = "Runs the isolated RouteMesh v11 JVM M5 foundation contract."
        group = LifecycleBasePlugin.VERIFICATION_GROUP
        testClassesDirs = m5FoundationTest.output.classesDirs
        classpath = m5FoundationTest.runtimeClasspath
        useJUnitPlatform()
    }
}

dependencies {
    api(zlinkLibs.zlink.bindings)
    compileOnlyApi("org.jspecify:jspecify:1.0.0")
    api("io.netty:netty-buffer:4.1.100.Final")
    api("com.fasterxml.jackson.core:jackson-databind:2.17.2")
    implementation("org.lz4:lz4-java:1.8.0")
}
