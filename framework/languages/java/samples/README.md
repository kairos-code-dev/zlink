# ZLink Java/Kotlin Samples

This directory contains executable Java and Kotlin sample checks for the sample
gate. Java samples live under `java/`, and Kotlin samples live under `kotlin/`.
Each language directory carries the same scenario set: `Bingo` and
`TicTacToe`. Each sample exits with a non-zero status when its scenario
invariant fails.

Open `framework/languages/java` in IntelliJ IDEA to work on the framework and
samples together. The framework root includes this directory as the
`zlink-framework-java-samples` Gradle build, so every Java and Kotlin sample role
is visible from one IDE import. You can also open this `samples/` directory
directly when you only want the sample modules.

Individual sample directories do not keep `settings.gradle.kts` files. IntelliJ
auto-detects every nested `settings.gradle.kts` as another Gradle root, which
makes the same sample projects appear more than once when `framework/languages/java`
is opened. Standalone command-line sample runs use each sample's
`standalone.settings.gradle.kts` through `run_sample.sh` instead.

```text
samples/
  java/
    TicTacToe/
    Bingo/
  kotlin/
    TicTacToe/
    Bingo/
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
  server/play/entryspot/handlers/
  server/play/gamespots/handlers/
  server/play/sessions/
  server/play/sessions/handlers/
  shared/contracts/

Bingo/
  client/
  server/api/handlers/
  server/play/domain/bingo/
  server/play/actors/
  server/play/bingoroomspots/handlers/
  server/play/entryspot/handlers/
  server/play/handlers/
  server/registry/
  server/session/sessions/handlers/
  shared/configuration/
  shared/contracts/
```

Java TicTacToe keeps only the common direct API/Play sample. Session gateway and
reconnect variants are not maintained as separate Java TicTacToe samples.

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
