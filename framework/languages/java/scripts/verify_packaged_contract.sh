#!/usr/bin/env bash
set -euo pipefail

language="${1:-}"
if [[ "$language" != "java" && "$language" != "kotlin" ]]; then
    echo "usage: $0 <java|kotlin>" >&2
    exit 2
fi

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_root="$(cd "$root_dir/../../.." && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/zlink-jvm-package-contract.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT

maven_dir="$work_dir/maven"
consumer_dir="$work_dir/consumer"
mkdir -p "$maven_dir" "$consumer_dir/src/main/java/contract"

artifacts=(
    zlink-framework-core
    zlink-framework-spring-boot-starter
    zlink-framework-locations-redis
    zlink-stream-connector
    zlink-framework-codec-protobuf
    zlink-framework-codec-msgpack
)
if [[ "$language" == "kotlin" ]]; then
    artifacts+=(zlink-framework-kotlin)
fi

publish_tasks=()
for artifact in "${artifacts[@]}"; do
    publish_tasks+=(":$artifact:publishAllPublicationsToReleaseRepoRepository")
done
MAVEN_REPOSITORY_URL="file://$maven_dir" \
    "$root_dir/gradlew" --no-daemon "${publish_tasks[@]}"

mapfile -t published < <(
    find "$maven_dir/systems/zlink" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort
)
mapfile -t expected < <(printf '%s\n' "${artifacts[@]}" | sort)
if [[ "${published[*]}" != "${expected[*]}" ]]; then
    echo "unexpected published artifact set" >&2
    printf 'expected: %s\nactual:   %s\n' "${expected[*]}" "${published[*]}" >&2
    exit 1
fi

cat > "$consumer_dir/settings.gradle.kts" <<EOF
pluginManagement { repositories { gradlePluginPortal(); mavenCentral() } }
dependencyResolutionManagement {
    repositories {
        maven {
            url = uri("$maven_dir")
            content { includeGroup("systems.zlink") }
        }
        maven {
            url = uri("$repo_root/.artifacts/wsl/maven")
            content { includeModule("systems.zlink", "zlink") }
        }
        mavenCentral()
    }
}
rootProject.name = "packaged-contract-consumer"
EOF

if [[ "$language" == "java" ]]; then
    cat > "$consumer_dir/build.gradle.kts" <<'EOF'
plugins { application }
dependencies {
    implementation("systems.zlink:zlink-framework-core:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-spring-boot-starter:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-locations-redis:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-stream-connector:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-codec-protobuf:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-codec-msgpack:0.1.0-SNAPSHOT")
}
application { mainClass.set("contract.PackagedContractConsumer") }
java { toolchain { languageVersion.set(JavaLanguageVersion.of(22)) } }
EOF
    cat > "$consumer_dir/src/main/java/contract/PackagedContractConsumer.java" <<'EOF'
package contract;

import systems.zlink.framework.actors.ZLinkActorJoinCall;
import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class PackagedContractConsumer {
    public static void main(String[] args) {
        Class<?>[] contract = {
            ZLinkFrameworkOptions.class,
            ZLinkActorJoinCall.class,
            ZLinkLocationStore.class,
            SpotHandle.class,
            ZLinkSession.class,
            ZLinkStreamConnector.class
        };
        if (contract.length != 6) throw new AssertionError("contract manifest");
        System.out.println("java packaged contract consumer passed");
    }
}
EOF
else
    mkdir -p "$consumer_dir/src/main/kotlin/contract"
    cat > "$consumer_dir/build.gradle.kts" <<'EOF'
plugins {
    application
    kotlin("jvm") version "2.2.21"
}
dependencies {
    implementation("systems.zlink:zlink-framework-kotlin:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-spring-boot-starter:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-locations-redis:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-codec-protobuf:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-codec-msgpack:0.1.0-SNAPSHOT")
}
application { mainClass.set("contract.PackagedContractConsumerKt") }
kotlin { jvmToolchain(22) }
EOF
    cat > "$consumer_dir/src/main/kotlin/contract/PackagedContractConsumer.kt" <<'EOF'
package contract

import systems.zlink.framework.kotlin.ZLinkSuspendingActorFactory
import systems.zlink.framework.kotlin.ZLinkSuspendingSession
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.kotlin
import java.util.concurrent.CompletableFuture

fun main() {
    check(CompletableFuture.completedFuture("ok").toCompletableFuture().join() == "ok")
    check(ZLinkSuspendingActorFactory::class.java.name.isNotBlank())
    check(ZLinkSuspendingSession::class.java.name.isNotBlank())
    println("kotlin packaged contract consumer passed")
}
EOF
fi

"$root_dir/gradlew" --no-daemon -p "$consumer_dir" clean run
echo "$language packaged contract verification passed"
