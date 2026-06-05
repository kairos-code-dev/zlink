# ZLink Java/Kotlin Samples

This directory contains executable Java and Kotlin sample checks for the Phase
10 sample gate. Java samples live under `java/`, and Kotlin samples live under
`kotlin/`. Each language directory carries the same scenario set:
`TicTacToe`, `TicTacToe.SessionGateway`, `Bingo`, `StreamingClient`, and `Async`.
Each sample is a standalone Gradle application and exits with a non-zero status
when its scenario invariant fails.

Open `framework/languages/java` in IntelliJ IDEA to work on the framework and
samples together. The framework root includes this directory as the
`zlink-framework-java-samples` Gradle build, so every Java and Kotlin sample role
is visible from one IDE import. You can also open this `samples/` directory
directly when you only want the sample modules. The individual sample
`settings.gradle.kts` files are kept for command-line standalone sample builds.

```text
samples/
  java/
    TicTacToe/
    TicTacToe.SessionGateway/
    Bingo/
    StreamingClient/
    Async/
  kotlin/
    TicTacToe/
    TicTacToe.SessionGateway/
    Bingo/
    StreamingClient/
    Async/
```

The three framework parity samples mirror the .NET sample role layout. Java and
Kotlin keep one aggregate Gradle entry point per sample, and the larger samples
also expose standalone role projects so users can run the client and server
processes separately. Source packages stay split by the same roles:

```text
TicTacToe/
  client/
  server/api/handlers/
  server/configuration/
  server/play/actors/
  server/play/entryspot/handlers/
  server/play/gamespots/handlers/
  server/play/sessions/
  shared/contracts/

TicTacToe.SessionGateway/
  client/
  server/api/handlers/
  server/play/entryspot/handlers/
  server/play/gamespots/handlers/
  server/play/handlers/
  server/registry/
  server/session/sessions/handlers/
  shared/actors/
  shared/configuration/
  shared/contracts/

Bingo/
  client/
  server/api/handlers/
  server/play/actors/
  server/play/bingoroomspots/handlers/
  server/play/entryspot/handlers/
  server/play/handlers/
  server/registry/
  server/session/sessions/handlers/
  shared/configuration/
  shared/contracts/
```

Run all required samples:

```bash
./run_samples.sh
```

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
