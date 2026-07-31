#!/usr/bin/env python3
"""문서 상대 링크가 저장소에서 실제 파일을 가리키는지 검사한다.

mkdocs도 링크를 검증하지만 `docs_dir` 안만 본다. framework 사이트는 정본 트리를
그대로 docs root로 쓰므로, 정본 트리 밖(`bindings/doc/`, `doc/principal/`,
`framework/languages/`)을 가리키는 링크는 mkdocs가 전부 경고로 낸다. 그 링크들은
GitHub에서 문서를 읽을 때 맞는 링크이고 사이트에는 실을 대상이 아니다.

경고가 상수로 남으면 진짜로 깨진 링크가 그 안에 묻힌다. 이 스크립트가 저장소
기준으로 대조해서, mkdocs 경고 중 무엇이 정상이고 무엇이 회귀인지 가른다.

검사 대상:
  - `[text](path)` 형태의 상대 링크. `http(s):`·`mailto:`·앵커 전용(`#...`)은 뺀다.
  - 코드 펜스 안은 예제 경로라 검사하지 않는다.
  - `#anchor`는 떼고 파일 존재만 본다.

실행:
    python3 doc/site/scripts/check_doc_links.py            # 두 묶음 모두
    python3 doc/site/scripts/check_doc_links.py framework  # 하나만
위반이 있으면 비-0으로 종료한다.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

SITE_DIR = Path(__file__).resolve().parents[1]          # doc/site
REPO_ROOT = SITE_DIR.parents[1]                         # repo root

DOC_TREES = {
    "core": REPO_ROOT / "doc",
    "framework": REPO_ROOT / "framework" / "doc" / "framework",
}

LINK_RE = re.compile(r"\[[^\]]*\]\(([^()\s]+)\)")
FENCE_RE = re.compile(r"^\s*(```|~~~)")
SKIP_SCHEME_RE = re.compile(r"^(https?:|mailto:|ftp:|#|<)")
# 인라인 코드 안의 링크는 문법을 보여주는 예시다. 실제 대상이 아니다.
INLINE_CODE_RE = re.compile(r"`[^`]*`")


def links_in(text: str):
    """(줄 번호, 링크 대상) 목록. 코드 펜스 안은 건너뛴다."""
    in_fence = False
    for ln, line in enumerate(text.splitlines(), 1):
        if FENCE_RE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        for m in LINK_RE.finditer(INLINE_CODE_RE.sub("", line)):
            target = m.group(1)
            if SKIP_SCHEME_RE.match(target):
                continue
            yield ln, target


def check_tree(name: str, root: Path, errors: list[str]) -> tuple[int, int]:
    """문서 트리 하나를 검사하고 (문서 수, 링크 수)를 돌려준다."""
    md_files = sorted(root.rglob("*.md"))
    if not md_files:
        errors.append(f"[{name}] 검사할 markdown이 없다: {root}")
        return 0, 0

    total = 0
    for md in md_files:
        rel_md = md.relative_to(REPO_ROOT)
        for ln, target in links_in(md.read_text(encoding="utf-8")):
            total += 1
            path = target.split("#", 1)[0]
            if not path:                      # 같은 문서 안 앵커.
                continue
            resolved = (md.parent / path).resolve()
            if not resolved.exists():
                errors.append(f"[{name}] {rel_md}:{ln}: 링크 대상 없음: {target}")
    return len(md_files), total


def main() -> int:
    requested = sys.argv[1:] or list(DOC_TREES)
    unknown = [n for n in requested if n not in DOC_TREES]
    if unknown:
        print(f"알 수 없는 문서 트리: {unknown}. 가능한 값: {list(DOC_TREES)}",
              file=sys.stderr)
        return 2

    errors: list[str] = []
    for name in requested:
        docs, links = check_tree(name, DOC_TREES[name], errors)
        print(f"검사[{name}]: 문서 {docs}개, 상대 링크 {links}개")

    if errors:
        print(f"\n깨진 링크 {len(errors)}건:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK — 상대 링크가 모두 저장소의 실제 파일을 가리킨다")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
