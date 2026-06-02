#!/usr/bin/env python3
"""가이드 문서의 언어 탭/스니펫 회귀 체크.

다음 세 가지 회귀를 검출한다(P6):
  1. 탭 언어 누락 — 샘플 탭 블록이 9개 표준 언어를 전부 담지 않음(또는 중복/이질 라벨).
  2. API/샘플 부재 — `--8<--` 스니펫 경로가 실제 파일로 해석되지 않음.
  3. 언어↔확장자 불일치 — 예: "Rust" 탭이 .rs가 아닌 파일을 임베드.

mkdocs 빌드는 (1),(3)을 잡지 못하므로 별도 회귀 테스트로 강제한다.

실행:
    python3 doc/site/scripts/check_doc_tabs.py
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

TAB_RE = re.compile(r'^(\s*)=== "(.+?)"\s*$')
SNIPPET_RE = re.compile(r'--8<--\s*"([^"]+)"')

# pymdownx.snippets base_path: mkdocs cwd(doc/site)와 repo 루트.
SITE_DIR = Path(__file__).resolve().parents[1]          # doc/site
REPO_ROOT = SITE_DIR.parents[1]                         # repo root
BASE_PATHS = [SITE_DIR, REPO_ROOT]
GUIDE_DIR = SITE_DIR / "docs" / "guide"


def resolve_snippet(rel: str) -> Path | None:
    for base in BASE_PATHS:
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


def main() -> int:
    errors: list[str] = []
    md_files = sorted(GUIDE_DIR.rglob("*.md"))
    if not md_files:
        print(f"no guide markdown under {GUIDE_DIR}", file=sys.stderr)
        return 2

    total_snippets = 0
    sample_groups = 0
    for md in md_files:
        rel_md = md.relative_to(REPO_ROOT)
        lines = md.read_text(encoding="utf-8").splitlines()

        # (2) 모든 스니펫 경로 존재 확인(탭 안팎 무관).
        for ln, line in enumerate(lines, 1):
            for sm in SNIPPET_RE.finditer(line):
                total_snippets += 1
                if resolve_snippet(sm.group(1)) is None:
                    errors.append(f"{rel_md}:{ln}: 스니펫 경로 미해석: {sm.group(1)}")

        # (1)(3) 탭 그룹 검사: 스니펫을 담은 그룹은 9개 언어 전부 + 확장자 일치.
        for group in parse_tab_groups(lines):
            has_snippet = any(snips for _, snips in group)
            if not has_snippet:
                continue  # 샘플 임베드가 아닌 일반 탭(설명용)은 검사 제외
            sample_groups += 1
            labels = [lbl for lbl, _ in group]
            label_set = set(labels)
            first_label = labels[0]
            loc = f"{rel_md} (탭: '{first_label}'…)"

            missing = set(CANONICAL) - label_set
            extra = label_set - set(CANONICAL)
            dupes = [l for l in label_set if labels.count(l) > 1]
            if missing:
                errors.append(f"{loc}: 탭 언어 누락: {sorted(missing)}")
            if extra:
                errors.append(f"{loc}: 비표준 탭 라벨: {sorted(extra)}")
            if dupes:
                errors.append(f"{loc}: 중복 탭 라벨: {sorted(dupes)}")

            for lbl, snips in group:
                ext = CANONICAL.get(lbl)
                if ext is None:
                    continue
                for s in snips:
                    if not s.endswith(ext):
                        errors.append(
                            f"{loc}: '{lbl}' 탭이 {ext} 아닌 파일 임베드: {s}")

    print(f"검사: 가이드 {len(md_files)}개, 스니펫 {total_snippets}개, "
          f"샘플 탭 그룹 {sample_groups}개")
    if errors:
        print(f"\n탭/스니펫 회귀 {len(errors)}건:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK — 탭 언어 완전성·스니펫 존재·확장자 일치 모두 통과")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
