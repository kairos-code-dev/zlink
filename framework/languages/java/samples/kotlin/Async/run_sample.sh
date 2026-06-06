#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
../../gradlew --settings-file standalone.settings.gradle.kts run --quiet
