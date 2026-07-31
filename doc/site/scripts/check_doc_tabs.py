#!/usr/bin/env python3
"""가이드 문서의 언어 탭/스니펫 회귀 체크.

다음 세 가지 회귀를 검출한다(P6):
  1. 탭 언어 누락 — 샘플 탭 블록이 그 문서 묶음의 표준 언어를 전부 담지 않음
     (또는 중복/이질 라벨).
  2. API/샘플 부재 — `--8<--` 스니펫 경로가 실제 파일로 해석되지 않음.
  3. 언어↔확장자 불일치 — 예: "Rust" 탭이 .rs가 아닌 파일을 임베드.

두 문서 묶음을 검사한다.
  core       — doc/site/docs/guide, 9개 바인딩 언어
  framework  — framework/doc/site/docs, 5개 framework 언어

구현이 아직 없는 언어의 탭은 스니펫 없이 안내 문구만 둘 수 있다. 라벨이 있으면
누락으로 보지 않으므로 (1)을 통과한다.

mkdocs 빌드는 (1),(3)을 잡지 못하므로 별도 회귀 테스트로 강제한다.

실행:
    python3 doc/site/scripts/check_doc_tabs.py            # 두 묶음 모두
    python3 doc/site/scripts/check_doc_tabs.py core       # 하나만
위반이 있으면 비-0으로 종료하고 보고서를 출력한다.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# 표준 언어 탭 라벨(코어 가이드 전역 동일). 순서는 검사에 영향 없음.
CANONICAL = {
    "C++": ".cpp",
    "C#/.NET": ".cs",
    "Java": ".java",
    "Kotlin": ".kt",
    "Python": ".py",
    "Node/TypeScript": ".ts",
    "JavaScript": ".js",
    "Go": ".go",
    "Rust": ".rs",
}

# framework 가이드 탭 라벨. framework가 지원하는 다섯 언어다.
FRAMEWORK_CANONICAL = {
    "C#/.NET": ".cs",
    "C++": [".cpp", ".hpp"],
    "Java": ".java",
    "Kotlin": ".kt",
    "Node/TypeScript": ".ts",
}

TAB_RE = re.compile(r'^(\s*)=== "(.+?)"\s*$')
SNIPPET_RE = re.compile(r'--8<--\s*"([^"]+)"')

SITE_DIR = Path(__file__).resolve().parents[1]          # doc/site
REPO_ROOT = SITE_DIR.parents[1]                         # repo root
FRAMEWORK_SITE_DIR = REPO_ROOT / "framework" / "doc" / "site"
FRAMEWORK_DOC_DIR = REPO_ROOT / "framework" / "doc" / "framework"


class DocSet:
    """검사 대상 문서 묶음 하나."""

    def __init__(self, name, docs_dir, canonical, base_paths):
        self.name = name
        self.docs_dir = docs_dir
        self.canonical = canonical
        self.base_paths = base_paths

    def extensions(self, label):
        """라벨이 허용하는 확장자 목록. 없으면 None."""
        ext = self.canonical.get(label)
        if ext is None:
            return None
        return [ext] if isinstance(ext, str) else list(ext)


DOC_SETS = {
    "core": DocSet(
        "core",
        SITE_DIR / "docs" / "guide",
        CANONICAL,
        [SITE_DIR, REPO_ROOT],
    ),
    # framework 사이트는 정본 트리를 그대로 docs root로 쓴다. 사본이 없으므로
    # 검사 대상도 정본 트리 하나다.
    "framework": DocSet(
        "framework",
        FRAMEWORK_DOC_DIR,
        FRAMEWORK_CANONICAL,
        [FRAMEWORK_SITE_DIR, REPO_ROOT],
    ),
}


def strip_section(rel: str) -> str:
    """`path/to/file.ext:section` → `path/to/file.ext` (pymdownx snippet section)."""
    return re.sub(r":[A-Za-z0-9_-]+$", "", rel)


def resolve_snippet(rel: str, base_paths: list[Path]) -> Path | None:
    rel = strip_section(rel)
    for base in base_paths:
        p = (base / rel)
        if p.is_file():
            return p
    return None


def parse_tab_groups(lines: list[str]):
    """연속된 `=== "Label"` 항목을 하나의 탭 그룹으로 묶는다.

    각 그룹은 [(label, [snippet_paths])] 리스트. 탭 본문(들여쓰기/공백) 사이의
    탭 헤더는 같은 그룹, 들여쓰기 없는 본문 라인이 나오면 그룹 종료.
    """
    groups = []
    current = None  # list of (label, [snippets])
    i = 0
    n = len(lines)
    while i < n:
        m = TAB_RE.match(lines[i])
        if m:
            label = m.group(2)
            body_snippets = []
            i += 1
            # 탭 본문: 다음 탭 헤더 전까지의 들여쓰기/공백 라인
            while i < n and not TAB_RE.match(lines[i]):
                if lines[i].strip() == "":
                    i += 1
                    continue
                if lines[i].startswith((" ", "\t")):
                    sm = SNIPPET_RE.search(lines[i])
                    if sm:
                        body_snippets.append(sm.group(1))
                    i += 1
                else:
                    break  # 들여쓰기 없는 본문 → 그룹 종료
            if current is None:
                current = []
            current.append((label, body_snippets))
        else:
            if current is not None:
                groups.append(current)
                current = None
            i += 1
    if current is not None:
        groups.append(current)
    return groups


def check_doc_set(ds: "DocSet", errors: list[str]) -> tuple[int, int, int]:
    """문서 묶음 하나를 검사하고 (문서 수, 스니펫 수, 샘플 탭 그룹 수)를 돌려준다."""
    md_files = sorted(ds.docs_dir.rglob("*.md"))
    if not md_files:
        errors.append(f"[{ds.name}] 검사할 markdown이 없다: {ds.docs_dir}")
        return 0, 0, 0

    total_snippets = 0
    sample_groups = 0
    for md in md_files:
        rel_md = md.relative_to(REPO_ROOT)
        lines = md.read_text(encoding="utf-8").splitlines()

        # (2) 모든 스니펫 경로 존재 확인(탭 안팎 무관).
        for ln, line in enumerate(lines, 1):
            for sm in SNIPPET_RE.finditer(line):
                total_snippets += 1
                if resolve_snippet(sm.group(1), ds.base_paths) is None:
                    errors.append(
                        f"[{ds.name}] {rel_md}:{ln}: 스니펫 경로 미해석: {sm.group(1)}")

        # (1)(3) 탭 그룹 검사: 스니펫을 담은 그룹은 표준 언어 전부 + 확장자 일치.
        for group in parse_tab_groups(lines):
            has_snippet = any(snips for _, snips in group)
            # 언어 탭은 샘플을 임베드하든 예제를 인라인으로 쓰든 언어 전부를 갖춰야 한다.
            # 라벨이 하나도 표준 언어가 아니면 설명용 탭이므로 검사에서 뺀다.
            is_language_group = bool({lbl for lbl, _ in group} & set(ds.canonical))
            if not has_snippet and not is_language_group:
                continue
            sample_groups += 1
            labels = [lbl for lbl, _ in group]
            label_set = set(labels)
            loc = f"[{ds.name}] {rel_md} (탭: '{labels[0]}'…)"

            missing = set(ds.canonical) - label_set
            extra = label_set - set(ds.canonical)
            dupes = [l for l in label_set if labels.count(l) > 1]
            if missing:
                errors.append(f"{loc}: 탭 언어 누락: {sorted(missing)}")
            if extra:
                errors.append(f"{loc}: 비표준 탭 라벨: {sorted(extra)}")
            if dupes:
                errors.append(f"{loc}: 중복 탭 라벨: {sorted(dupes)}")

            for lbl, snips in group:
                exts = ds.extensions(lbl)
                if exts is None:
                    continue
                for s in snips:
                    if not any(strip_section(s).endswith(e) for e in exts):
                        errors.append(
                            f"{loc}: '{lbl}' 탭이 {'/'.join(exts)} 아닌 파일 임베드: {s}")

    return len(md_files), total_snippets, sample_groups


def main() -> int:
    requested = sys.argv[1:] or list(DOC_SETS)
    unknown = [n for n in requested if n not in DOC_SETS]
    if unknown:
        print(f"알 수 없는 문서 묶음: {unknown}. 가능한 값: {list(DOC_SETS)}",
              file=sys.stderr)
        return 2

    errors: list[str] = []
    for name in requested:
        ds = DOC_SETS[name]
        docs, snippets, groups = check_doc_set(ds, errors)
        print(f"검사[{ds.name}]: 문서 {docs}개, 스니펫 {snippets}개, "
              f"샘플 탭 그룹 {groups}개")

    if errors:
        print(f"\n탭/스니펫 회귀 {len(errors)}건:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK — 탭 언어 완전성·스니펫 존재·확장자 일치 모두 통과")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
