#!/usr/bin/env python3
"""Check framework public contract symbols against the shared inventory.

Runs without third-party dependencies:
    python3 doc/site/scripts/check_framework_contract_inventory.py
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

SITE_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SITE_DIR.parents[1]
INVENTORY_PATH = REPO_ROOT / "framework/doc/contract-inventory/framework-public-contract-inventory.json"

LANGUAGES = ("dotnet", "node", "java", "cpp")

PATHS = {
    "dotnet_locations": REPO_ROOT / "framework/languages/dotnet/src/Zlink.Framework/Contracts/Locations",
    "dotnet_errors": REPO_ROOT / "framework/languages/dotnet/src/Zlink.Framework/Contracts/Errors/ZLinkFrameworkException.cs",
    "dotnet_config": REPO_ROOT / "framework/languages/dotnet/src/Zlink.Framework/Contracts/Configuration/Builders.cs",
    "node_locations": REPO_ROOT / "framework/languages/node/packages/framework/src/contracts/Locations",
    "node_errors": REPO_ROOT / "framework/languages/node/packages/framework/src/contracts/Errors/ZLinkFrameworkException.ts",
    "node_config": REPO_ROOT / "framework/languages/node/packages/framework/src/contracts/Configuration/Builders.ts",
    "java_locations": REPO_ROOT / "framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/locations",
    "java_errors": REPO_ROOT / "framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/errors/ZLinkFrameworkErrorKind.java",
    "java_config": REPO_ROOT / "framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/configuration/ZLinkFrameworkOptions.java",
    "cpp_locations": REPO_ROOT / "framework/languages/cpp/framework/include/zlink/framework/contracts/locations",
    "cpp_errors": REPO_ROOT / "framework/languages/cpp/framework/include/zlink/framework/contracts/errors/error.hpp",
    "cpp_config": REPO_ROOT / "framework/languages/cpp/framework/include/zlink/framework/contracts/configuration/framework_options.hpp",
}


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def iter_files(path: Path, suffixes: tuple[str, ...]) -> list[Path]:
    return sorted(p for p in path.rglob("*") if p.is_file() and p.suffix in suffixes)


def pascal(identifier: str) -> str:
    return "".join(part.capitalize() for part in identifier.split("_"))


def camel(identifier: str) -> str:
    value = pascal(identifier)
    return value[:1].lower() + value[1:]


def upper_snake(identifier: str) -> str:
    return identifier.upper()


def cpp_type(identifier: str) -> str:
    return f"{identifier}_t"


def default_type_name(symbol: dict, lang: str) -> str:
    identifier = symbol["id"]
    if lang == "cpp":
        return cpp_type(identifier)
    prefix = "I" if symbol["kind"] == "interface" and lang in {"dotnet", "node"} else ""
    return f"{prefix}ZLink{pascal(identifier)}"


def expected_type_name(symbol: dict, lang: str) -> str:
    return symbol.get("names", {}).get(lang) or default_type_name(symbol, lang)


def enum_member_name(member: str, lang: str) -> str:
    snake = re.sub(r"(?<!^)([A-Z])", r"_\1", member).lower()
    if lang == "java":
        return snake.upper()
    if lang == "cpp":
        return snake
    return member


def extract_dotnet_types() -> set[str]:
    found: set[str] = set()
    pattern = re.compile(
        r"\bpublic\s+(?:abstract\s+|sealed\s+|readonly\s+)?"
        r"(?:record\s+struct|record|interface|enum|class)\s+([A-Za-z_][A-Za-z0-9_]*)"
    )
    for path in iter_files(PATHS["dotnet_locations"], (".cs",)):
        text = read_text(path)
        if "record ZLinkLocationKey" in text:
            found.add("ZLinkLocationKey")
        for match in pattern.finditer(text):
            name = match.group(1)
            if name in {"Peer", "Spot", "Actor", "Route"}:
                continue
            found.add(name)
    found.update(re.findall(r"\bpublic\s+enum\s+([A-Za-z_][A-Za-z0-9_]*)", read_text(PATHS["dotnet_errors"])))
    return found


def extract_node_types() -> set[str]:
    found: set[str] = set()
    pattern = re.compile(r"\bexport\s+(?:interface|type|class|enum)\s+([A-Za-z_][A-Za-z0-9_]*)")
    for path in iter_files(PATHS["node_locations"], (".ts",)):
        found.update(pattern.findall(read_text(path)))
    found.update(re.findall(r"\bexport\s+enum\s+([A-Za-z_][A-Za-z0-9_]*)", read_text(PATHS["node_errors"])))
    return found


def extract_java_types() -> set[str]:
    found: set[str] = set()
    pattern = re.compile(r"\bpublic\s+(?:sealed\s+|final\s+)?(?:record|interface|enum|class)\s+([A-Za-z_][A-Za-z0-9_]*)")
    for path in iter_files(PATHS["java_locations"], (".java",)):
        found.update(pattern.findall(read_text(path)))
    found.update(re.findall(r"\bpublic\s+enum\s+([A-Za-z_][A-Za-z0-9_]*)", read_text(PATHS["java_errors"])))
    return found


def extract_cpp_types() -> set[str]:
    found: set[str] = set()
    pattern = re.compile(r"\b(?:struct|class|enum\s+class)\s+([a-z_][a-z0-9_]*_t)\b")
    alias_pattern = re.compile(r"\busing\s+([a-z_][a-z0-9_]*_t)\s*=")
    for path in iter_files(PATHS["cpp_locations"], (".hpp",)):
        text = read_text(path)
        found.update(pattern.findall(text))
        found.update(alias_pattern.findall(text))
    found.update(re.findall(r"\benum\s+class\s+([a-z_][a-z0-9_]*_t)\b", read_text(PATHS["cpp_errors"])))
    return found


def extract_methods(lang: str) -> set[str]:
    if lang == "dotnet":
        texts = [read_text(p) for p in iter_files(PATHS["dotnet_locations"], (".cs",))]
        texts.append(read_text(PATHS["dotnet_config"]))
        pattern = re.compile(r"\b([A-Z][A-Za-z0-9_]*)\s*\(")
        return {m.group(1) for text in texts for m in pattern.finditer(text)}
    if lang == "node":
        texts = [read_text(p) for p in iter_files(PATHS["node_locations"], (".ts",))]
        texts.append(read_text(PATHS["node_config"]))
        pattern = re.compile(r"\b([a-z][A-Za-z0-9_]*)\s*\(")
        return {m.group(1) for text in texts for m in pattern.finditer(text)}
    if lang == "java":
        texts = [read_text(p) for p in iter_files(PATHS["java_locations"], (".java",))]
        texts.append(read_text(PATHS["java_config"]))
        pattern = re.compile(r"\b(?:public\s+)?[A-Za-z0-9_<>, ?]+\s+([a-z][A-Za-z0-9_]*)\s*\(")
        return {m.group(1) for text in texts for m in pattern.finditer(text)}
    texts = [read_text(p) for p in iter_files(PATHS["cpp_locations"], (".hpp",))]
    texts.append(read_text(PATHS["cpp_config"]))
    pattern = re.compile(r"\b([a-z][a-z0-9_]*)\s*\(")
    return {m.group(1) for text in texts for m in pattern.finditer(text)}


def extract_dotnet_enum(name: str) -> dict[str, int]:
    candidates = list(iter_files(PATHS["dotnet_locations"], (".cs",))) + [PATHS["dotnet_errors"]]
    return extract_braced_enum(candidates, rf"public\s+enum\s+{re.escape(name)}(?:\s*:\s*\w+)?", "dotnet")


def extract_java_enum(name: str) -> dict[str, int]:
    candidates = list(iter_files(PATHS["java_locations"], (".java",))) + [PATHS["java_errors"]]
    return extract_braced_enum(candidates, rf"public\s+enum\s+{re.escape(name)}", "java")


def extract_node_enum(name: str) -> dict[str, int]:
    if name == "ZLinkFrameworkErrorKind":
        text = read_text(PATHS["node_errors"])
        body = re.search(r"ZLINK_FRAMEWORK_ERROR_KIND_VALUES:[\s\S]*?Object\.freeze\(\{([\s\S]*?)\}\);", text)
        if not body:
            return {}
        return {
            match.group(1): int(match.group(2))
            for match in re.finditer(r"ZLinkFrameworkErrorKind\.([A-Za-z0-9_]+)\]:\s*([0-9]+)", body.group(1))
        }
    candidates = iter_files(PATHS["node_locations"], (".ts",))
    return extract_braced_enum(candidates, rf"export\s+enum\s+{re.escape(name)}", "node")


def extract_cpp_enum(name: str) -> dict[str, int]:
    candidates = list(iter_files(PATHS["cpp_locations"], (".hpp",))) + [PATHS["cpp_errors"]]
    return extract_braced_enum(candidates, rf"enum\s+class\s+{re.escape(name)}(?:\s*:\s*[\w:]+)?", "cpp")


def extract_braced_enum(paths: list[Path], header_pattern: str, lang: str) -> dict[str, int]:
    header_re = re.compile(header_pattern)
    for path in paths:
        text = read_text(path)
        header = header_re.search(text)
        if not header:
            continue
        start = text.find("{", header.end())
        if start < 0:
            continue
        depth = 0
        end = start
        for idx in range(start, len(text)):
            if text[idx] == "{":
                depth += 1
            elif text[idx] == "}":
                depth -= 1
                if depth == 0:
                    end = idx
                    break
        body = text[start + 1:end]
        found: dict[str, int] = {}
        if lang == "java":
            for match in re.finditer(r"\b([A-Z][A-Z0-9_]*)\s*\(\s*([0-9]+)\s*\)", body):
                found[match.group(1)] = int(match.group(2))
            return found
        for match in re.finditer(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([0-9]+)", body):
            found[match.group(1)] = int(match.group(2))
        return found
    return {}


def allowed_set(inventory: dict, section: str) -> set[str]:
    allowed: set[str] = set()
    for lang, values in inventory.get("allowed_language_only_symbols", {}).items():
        for item in values.get(section, []):
            allowed.add(f"{lang}:{item}")
    return allowed


def main() -> int:
    inventory = json.loads(read_text(INVENTORY_PATH))
    errors: list[str] = []

    type_sets = {
        "dotnet": extract_dotnet_types(),
        "node": extract_node_types(),
        "java": extract_java_types(),
        "cpp": extract_cpp_types(),
    }
    method_sets = {lang: extract_methods(lang) for lang in LANGUAGES}
    enum_extractors = {
        "dotnet": extract_dotnet_enum,
        "node": extract_node_enum,
        "java": extract_java_enum,
        "cpp": extract_cpp_enum,
    }

    expected_type_names: dict[str, dict[str, str]] = {}
    expected_types_by_lang: dict[str, set[str]] = {lang: set() for lang in LANGUAGES}
    for symbol in inventory["types"]:
        expected_type_names[symbol["id"]] = {}
        for lang in LANGUAGES:
            name = expected_type_name(symbol, lang)
            expected_type_names[symbol["id"]][lang] = name
            expected_types_by_lang[lang].add(name)
            if name not in type_sets[lang]:
                errors.append(f"missing type: {lang} {symbol['id']} expected {name}")

    for method in inventory["methods"]:
        for lang in LANGUAGES:
            name = method["names"][lang]
            if name not in method_sets[lang]:
                errors.append(f"missing method: {lang} {method['id']} expected {name}")

    allowed_type_extras = allowed_set(inventory, "types")
    for lang in LANGUAGES:
        for name in sorted(type_sets[lang] - expected_types_by_lang[lang]):
            concept = re.sub(r"^I?ZLink", "", name) if lang != "cpp" else re.sub(r"_t$", "", name)
            concept = re.sub(r"(?<!^)([A-Z])", r"_\1", concept).lower()
            if f"{lang}:{concept}" in allowed_type_extras:
                continue
            errors.append(f"unexpected public type: {lang} {name}")

    allowed_enum_extras = allowed_set(inventory, "enum_members")
    for enum_id, enum_spec in inventory["enums"].items():
        type_symbol = next(s for s in inventory["types"] if s["id"] == enum_id)
        for lang in LANGUAGES:
            enum_name = expected_type_names[enum_id][lang]
            actual = enum_extractors[lang](enum_name)
            expected = {
                enum_member_name(member, lang): value
                for member, value in enum_spec["members"].items()
            }
            for member, value in expected.items():
                if member not in actual:
                    errors.append(f"missing enum member: {lang} {enum_id}.{member}={value}")
                elif actual[member] != value:
                    errors.append(
                        f"enum value mismatch: {lang} {enum_id}.{member} expected {value} actual {actual[member]}"
                    )
            for member, value in sorted(actual.items()):
                if member in expected:
                    continue
                canonical = member
                if lang == "java":
                    canonical = pascal(member.lower())
                elif lang == "cpp":
                    canonical = member
                key = f"{lang}:{enum_id}.{canonical}"
                if key in allowed_enum_extras:
                    continue
                errors.append(f"unexpected enum member: {lang} {enum_id}.{member}={value}")

    print(
        "검사: framework 계약 인벤토리 "
        f"types={len(inventory['types'])}, methods={len(inventory['methods'])}, enums={len(inventory['enums'])}"
    )
    if errors:
        print(f"\n계약 인벤토리 불일치 {len(errors)}건:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("OK — framework public contract inventory parity passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
