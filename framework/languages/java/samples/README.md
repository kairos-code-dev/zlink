# ZLink Java/Kotlin Samples

This directory contains executable Java and Kotlin sample checks for the sample
gate. Java samples live under `java/`, and Kotlin samples live under `kotlin/`.
Each language directory carries the executable scenario samples that are part of
the aggregate sample gate: `Bingo`, `TicTacToe`, `SupportChat`,
`DeliveryDispatch`, and `ShoppingMall`. `GameQuest` is also present as a
standalone scenario sample, but the aggregate gate does not run it yet. Each
sample exits with a non-zero status when its scenario invariant fails.

Open `framework/languages/java` in IntelliJ IDEA to work on the framework and
samples together. The framework root includes this directory as the
`zlink-framework-java-samples` Gradle build, so every Java and Kotlin sample role
is visible from one IDE import. You can also open this `samples/` directory
directly when you only want the sample modules.

Individual sample directories do not keep `settings.gradle.kts` files. IntelliJ
auto-detects every nested `settings.gradle.kts` as another Gradle root, which
makes the same sample projects appear more than once when `framework/languages/java`
is opened. Standalone command-line sample runs use each sample's
`standalone.settings.gradle.kts` through `run_sample.sh` or `run_sample.ps1`
instead.

```text
samples/
  java/
    TicTacToe/
    Bingo/
	    SupportChat/
	    DeliveryDispatch/
	    ShoppingMall/
	    GameQuest/
	  kotlin/
	    TicTacToe/
	    Bingo/
	    SupportChat/
	    DeliveryDispatch/
	    ShoppingMall/
	    GameQuest/
```

The framework parity samples mirror the .NET sample role layout. Java and
Kotlin keep one aggregate Gradle entry point per sample, and each sample also
exposes standalone role projects so users can run the client and server
processes separately. Source packages stay split by the same roles:

```text
TicTacToe/
  client/
  server/api/handlers/
	  server/configuration/
	  server/play/application/gamecreation/
	  server/play/actors/
	  server/play/domain/tictactoe/
	  server/play/infrastructure/zlink/spots/entryspot/handlers/
	  server/play/infrastructure/zlink/spots/tictactoegamespot/handlers/
	  server/play/sessions/
	  server/play/sessions/handlers/
	  shared/contracts/

Bingo/
  client/
	  server/api/handlers/
	  server/play/domain/bingo/
	  server/play/actors/
	  server/play/infrastructure/zlink/spots/bingoroomspot/handlers/
	  server/play/infrastructure/zlink/spots/entryspot/handlers/
	  server/registry/
	  server/session/sessions/handlers/
	  server/configuration/
  client/configuration/
  shared/contracts/
```

`shared/contracts` contains only message contracts. Server topology, endpoint
names, packet names, and timing settings live under `server/configuration`.
Client-only settings live under `client/configuration`.

Bingo uses Protobuf payloads for Java and Kotlin STREAM traffic. TicTacToe uses
MessagePack payloads for Java and Kotlin STREAM traffic. The other ported
samples use JSON payloads, Registry/Discovery automatic connection, and Spring
component scanning for handler registration.

Java TicTacToe keeps only the common direct API/Play sample. Session gateway and
reconnect variants are not maintained as separate Java TicTacToe samples.

Run all required samples:

```bash
./run_samples.sh
```

On Windows:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\run_samples.ps1
```

`run_samples.sh` and `run_samples.ps1` delegate to the aggregate gate samples:
`TicTacToe`, `Bingo`, `SupportChat`, `DeliveryDispatch`, and `ShoppingMall`.
Each sample runner starts the server roles as separate processes, waits for
readiness, runs the probe or client scenario, and cleans up the processes.
Application role code should not start the other sample roles for the test.

Client files that express the request, push, and final-state checks as a scenario
must be named `<Sample>ClientScenario` in both Java and Kotlin. `ClientApp`,
`TestScenario`, and sample-local `self-check` names are not used for the client
scenario flow.

## Execution And Configuration

Java and Kotlin samples use the Spring Boot configuration model for framework
roles. Endpoint and topology values should come from `application.yml`,
`application.properties`, command-line Spring properties, or environment values
that Spring maps into the application context. `ZLinkFrameworkConfigurer` beans
then read those configured values and apply them to the ZLink framework builder.

`run_sample.sh` and `run_sample.ps1` may prepare temporary settings for the
current run, but they should still start each role as a separate Spring process.
Server role code must not start the other sample roles in-process.

Check the IDE-importable Gradle project without running the samples:

```bash
./gradlew projects
./gradlew buildAllSamples
```

From `framework/languages/java`, the same sample build is available through the
included build name:

```bash
./gradlew :zlink-framework-java-samples:projects
./gradlew :zlink-framework-java-samples:buildAllSamples
```
