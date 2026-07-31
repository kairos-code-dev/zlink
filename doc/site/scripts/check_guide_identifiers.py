#!/usr/bin/env python3
"""공통 가이드의 언어 탭 예제가 실제 표면만 쓰는지 검사한다.

가이드 예제는 샘플에서 끌어오는 스니펫과 직접 적는 교육용 예제 두 종류다
(런북 §5.1). 교육용 예제는 복붙이라 존재하지 않는 메서드를 지어내도 아무도
못 잡는다. 이 검사기가 그 자리를 막는다.

탭 코드에서 호출한 메서드 이름을 뽑아, 그 언어의 실제 소스(framework 구현 ·
샘플 · 언어별 공개 계약 spec)에 그 이름이 있는지 본다. 없으면 회귀다.

시그니처까지는 보지 않는다 — 인자 개수나 타입이 맞는지는 이 검사기의 몫이
아니다. 지어낸 이름을 잡는 것이 목적이다.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
GUIDE_DIR = REPO_ROOT / "framework" / "doc" / "framework" / "common" / "guide" / "server"
SPEC_DIR = REPO_ROOT / "framework" / "doc" / "framework" / "common" / "spec"

# 라벨 → (fence 언어, 표면을 찾을 경로, 소스 확장자)
LANGUAGES = {
    "C#/.NET": (
        "csharp",
        ["framework/languages/dotnet/src", "framework/languages/dotnet/samples"],
        [".cs"],
    ),
    "C++": (
        "cpp",
        ["framework/languages/cpp/framework", "framework/languages/cpp/samples"],
        [".hpp", ".cpp"],
    ),
    "Java": (
        "java",
        ["framework/languages/java", "framework/languages/java/samples/java"],
        [".java"],
    ),
    "Kotlin": (
        "kotlin",
        ["framework/languages/java", "framework/languages/java/samples/kotlin"],
        [".kt", ".java"],
    ),
    "Node/TypeScript": (
        "typescript",
        ["framework/languages/node/packages", "framework/languages/node/samples"],
        [".ts"],
    ),
}

# 언어별 공개 계약 spec 디렉터리. 샘플이 안 쓰는 표면은 여기에만 있다.
SPEC_DIRS = {
    "C#/.NET": "server/languages/dotnet",
    "C++": "server/languages/cpp",
    "Java": "server/languages/java",
    "Kotlin": "server/languages/kotlin",
    "Node/TypeScript": "server/languages/node",
}

TAB_RE = re.compile(r'^=== "(.+?)"\s*$')
FENCE_RE = re.compile(r"^\s{4}```(\w*)\s*$")

# `.foo(` 또는 `.foo (` — 체이닝 호출. C++은 여는 괄호 앞에 공백을 둔다.
CALL_RE = re.compile(r"\.([A-Za-z_]\w{2,})\s*[(<]")

# `Type::value` · `Type.VALUE` — enum 상수 참조. 언어마다 표기 관례가 달라
# (C++ snake_case, Java SCREAMING_SNAKE, .NET·Node PascalCase) 옮겨 적을 때
# 틀리기 쉬운 자리다. 호출과 같은 방식으로 실제 존재를 확인한다.
ENUM_RE = re.compile(r"\b([A-Z]\w+_t|[A-Z]\w+)\s*(?:::|\.)([a-z_][a-z0-9_]{2,}|[A-Z][A-Za-z0-9_]{2,})\b")

# 언어 표준 라이브러리와 예제용 도메인 이름은 검사 대상이 아니다.
# framework 표면인지만 본다.
IGNORE = {
    # 공통 언어 기본
    "toString", "to_string", "ToString", "length", "size", "count", "Count",
    "value", "Value", "get", "set", "add", "Add", "put", "push", "map", "Map",
    "filter", "Where", "Select", "join", "Join", "split", "Split", "forEach",
    "then", "catch", "throw", "log", "info", "warn", "error", "debug", "trace",
    "format", "Format", "parse", "Parse", "TryParse", "Contains", "contains",
    "equals", "Equals", "hashCode", "GetHashCode", "clone", "Clone", "copy",
    "toArray", "ToArray", "toList", "ToList", "keys", "values", "entries",
    "emplace", "emplace_back", "push_back", "insert", "erase", "find", "begin",
    "end", "empty", "clear", "reserve", "resize", "data", "str", "c_str",
    "has_value", "value_or", "lock", "unlock", "wait", "notify_one", "close",
    "FromSeconds", "FromMilliseconds", "FromMinutes", "TotalMilliseconds",
    "ofSeconds", "ofMillis", "toMillis", "now", "Now", "UtcNow", "elapsed",
    "printf", "println", "print", "WriteLine", "Write", "toFixed", "toISOString",
    "getTime", "setTimeout", "resolve", "reject", "all", "race", "from", "of",
    "slice", "concat", "indexOf", "includes", "startsWith", "endsWith", "trim",
    "replace", "toUpperCase", "toLowerCase", "sort", "reverse", "reduce",
    "some", "every", "find_if", "for_each", "max", "min", "abs", "round",
}

# framework 표면이 아니지만 예제에 필요한 이름이다. 항목마다 이유를 적는다.
# 여기에 이름을 추가하는 것은 "이건 framework API가 아니다"를 명시하는 행위다 —
# 지어낸 이름을 무심코 통과시키지 않도록 이유 없이 늘리지 않는다.
ALLOWED_NON_FRAMEWORK = {
    # 예제가 만들어 쓰는 도메인 타입의 메서드.
    "AppendChat": "예제 Spot(GameRoom)의 도메인 메서드",
    "ApplyScore": "예제 Spot의 도메인 메서드",
    "CopyBoard": "예제 Spot의 도메인 메서드",
    "TryFinishRound": "예제 Spot의 도메인 메서드",
    "ExportState": "예제 relocation adapter의 도메인 메서드",
    "ImportState": "예제 relocation adapter의 도메인 메서드",
    "ReportLag": "예제 메트릭 수집기의 도메인 메서드",
    "LoadAsync": "예제 repository의 도메인 메서드",
    "SaveChangesAsync": "예제 repository의 도메인 메서드",
    # 서드파티 라이브러리.
    "GenericWriter": "Apache Avro",
    "GenericReader": "Apache Avro",
    "BinaryEncoder": "Apache Avro",
    "BinaryDecoder": "Apache Avro",
    "AddOpenTelemetry": "OpenTelemetry",
    "WithMetrics": "OpenTelemetry",
    "AddMeter": "OpenTelemetry",
    "AddPrometheusExporter": "OpenTelemetry",
}


def language_sources(paths: list[str], exts: list[str]) -> str:
    """그 언어의 실제 소스를 한 덩어리 텍스트로 모은다."""
    chunks: list[str] = []
    for rel in paths:
        root = REPO_ROOT / rel
        if not root.is_dir():
            continue
        for ext in exts:
            for path in root.rglob(f"*{ext}"):
                parts = set(path.parts)
                if parts & {"node_modules", "build", "bin", "obj", "dist", ".gradle"}:
                    continue
                try:
                    chunks.append(path.read_text(encoding="utf-8", errors="ignore"))
                except OSError:
                    continue
    return "\n".join(chunks)


def spec_text(label: str) -> str:
    rel = SPEC_DIRS.get(label)
    if rel is None:
        return ""
    root = SPEC_DIR / rel
    if not root.is_dir():
        return ""
    return "\n".join(
        p.read_text(encoding="utf-8", errors="ignore") for p in root.rglob("*.ko.md")
    )


def tab_blocks(lines: list[str]):
    """(라벨, fence 언어, 코드 줄들, 시작 줄번호)를 차례로 낸다."""
    label = None
    index = 0
    while index < len(lines):
        match = TAB_RE.match(lines[index])
        if match:
            label = match.group(1)
            index += 1
            continue
        fence = FENCE_RE.match(lines[index])
        if fence and label:
            start = index + 1
            body: list[str] = []
            index += 1
            while index < len(lines) and lines[index].strip() != "```":
                body.append(lines[index])
                index += 1
            yield label, fence.group(1), body, start
        index += 1


def main() -> int:
    if not GUIDE_DIR.is_dir():
        print(f"가이드 디렉터리가 없다: {GUIDE_DIR}", file=sys.stderr)
        return 2

    surfaces: dict[str, str] = {}
    for label, (_, paths, exts) in LANGUAGES.items():
        surfaces[label] = language_sources(paths, exts) + "\n" + spec_text(label)

    unknown: list[str] = []
    checked = 0
    for doc in sorted(GUIDE_DIR.glob("*.ko.md")):
        lines = doc.read_text(encoding="utf-8").splitlines()
        for label, fence_lang, body, start in tab_blocks(lines):
            if label not in LANGUAGES:
                continue
            if fence_lang != LANGUAGES[label][0]:
                continue  # bash 등 소스가 아닌 블록.
            checked += 1
            haystack = surfaces[label]
            for offset, line in enumerate(body):
                code = line.split("//")[0].split("#")[0]
                for name in CALL_RE.findall(code):
                    if name in IGNORE or name in ALLOWED_NON_FRAMEWORK:
                        continue
                    if name in haystack:
                        continue
                    unknown.append(
                        f"  [{label}] {doc.name}:{start + offset}: "
                        f"실제 표면에 없는 이름: .{name}(")
                # enum 상수는 타입 이름이 framework 표면일 때만 본다. 예제가
                # 만들어 쓰는 도메인 타입까지 따라가면 잡음만 는다.
                for type_name, member in ENUM_RE.findall(code):
                    if not type_name.startswith(("ZLink", "zlink")) and not type_name.endswith("_t"):
                        continue
                    if type_name not in haystack:
                        continue
                    if member in IGNORE or member in ALLOWED_NON_FRAMEWORK:
                        continue
                    if member in haystack:
                        continue
                    unknown.append(
                        f"  [{label}] {doc.name}:{start + offset}: "
                        f"{type_name}에 없는 값: {member}")

    if unknown:
        print(f"\n가이드 예제 회귀 {len(unknown)}건:")
        seen = set()
        for item in unknown:
            if item in seen:
                continue
            seen.add(item)
            print(item)
        return 1

    print(f"검사: 탭 코드 블록 {checked}개")
    print("OK — 탭 예제가 실제 표면 이름만 쓴다")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
