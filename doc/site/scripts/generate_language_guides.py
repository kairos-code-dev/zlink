#!/usr/bin/env python3
"""공통 정본에서 언어별 가이드 장을 생성한다.

공통 12장은 `=== "Java"` 같은 언어 탭으로 다섯 언어를 한 파일에 담는다. 그 파일은
**소스**다 — 사람이 읽는 자리가 아니다. 읽는 자리는 언어별 디렉터리이고, 이 스크립트가
소스에서 그 언어의 탭만 남겨 만든다.

탭을 읽는 자리로 두면 독자가 자기 언어와 무관한 코드를 60~67% 함께 본다. 장마다 탭 띠가
끼어들어 흐름도 끊긴다. 생성판은 그 언어의 코드만 담으므로 원본의 43% 분량이고 탭이 없다.

생성 규칙
  - `=== "라벨"` 블록 중 대상 언어만 남기고 4칸 들여쓰기를 푼다.
  - 머리에 생성 파일 표시를 붙인다. 손으로 고치면 다음 생성에서 지워진다.
  - 산문의 표면 이름을 그 언어 표기로 바꾼다(`surface_terms.py`). 공통 산문은 다섯
    언어가 공유하므로 `.NET` 모양으로 적혀 있는데, 생성판은 대상이 정해져 있다.
  - `` `11. Monitoring` 장 `` 표기를 **실제 링크**로 바꾼다. 공통 소스는 대상 언어가
    정해지지 않아 링크할 수 없지만, 생성판은 정해져 있다.
  - 그 언어의 읽는 순서대로 앞뒤 장 nav를 붙인다. 순서는 각 언어 README의 표가 소유한다.

상대 링크는 손대지 않는다. `common/guide/server/`와 `<lang>/guide/server/`는 깊이가 같아
`../../../common/spec/...` 같은 링크가 그대로 성립한다.

실행:
    python3 doc/site/scripts/generate_language_guides.py           # 생성
    python3 doc/site/scripts/generate_language_guides.py --check   # 최신인지만 확인
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import surface_terms  # noqa: E402

SITE_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SITE_DIR.parents[1]
FRAMEWORK = REPO_ROOT / "framework" / "doc" / "framework"
COMMON = FRAMEWORK / "common" / "guide" / "server"

#  탭 라벨 → 언어 디렉터리.
LANGUAGES = {
    "C#/.NET": "dotnet",
    "C++": "cpp",
    "Java": "java",
    "Kotlin": "kotlin",
    "Node/TypeScript": "node",
}

#  front matter의 title은 검색 결과에 쓰인다. 같은 장이 다섯 언어로 있으므로
#  언어를 붙이지 않으면 검색 결과에 같은 제목이 다섯 번 나온다. 사이드바 라벨은
#  mkdocs nav가 따로 정하므로 여기 언어를 붙여도 사이드바는 깔끔하게 남는다.
FRONT_MATTER = "---\ntitle: \"{title} · {label}\"\n---\n\n"

BANNER = (
    "<!-- generated:start -->\n"
    "<!-- 이 파일은 `common/guide/server/{source}`에서 생성한다."
    " 직접 고치지 않는다.\n"
    "     고칠 곳은 공통 소스이고, `python3 doc/site/scripts/"
    "generate_language_guides.py`로 다시 만든다. -->\n"
    "<!-- generated:end -->\n"
)

TAB_RE = re.compile(r'^=== +"(.+)"\s*$')
ORDER_RE = re.compile(r"^\| \d+ \| \[[^\]]+\]\(([^)]+)\)")
#  `11. Monitoring` 장 → 그 언어의 실제 장 링크. 공통 소스에서만 쓰는 표기다.
CHAPTER_REF_RE = re.compile(r"`(\d\d)\. ([^`]+)` 장")
NAV_RE = re.compile(
    r"<!-- framework-adapter-nav:start -->.*?<!-- framework-adapter-nav:end -->\n",
    re.S)


def language_switch(current: str, name: str) -> str:
    """같은 장의 다른 언어로 가는 줄.

    탭에서는 한 번 눌러 언어를 바꿀 수 있었다. 생성판은 언어별 파일이라 그 경로가
    사라진다. 장마다 이 줄을 붙여 되살린다. 저장소에서 읽을 때도 그대로 동작한다.
    """
    parts = []
    for label, lang in LANGUAGES.items():
        if lang == current:
            parts.append(f"**{label}**")
        else:
            parts.append(f"[{label}](../../../{lang}/guide/server/{name})")
    return ("<!-- language-switch:start -->\n"
            "다른 언어로 보기 — " + " · ".join(parts)
            + "\n<!-- language-switch:end -->\n\n")


def reading_order(lang_dir: str) -> list[str]:
    """그 언어 README의 읽는 순서 표에서 장 파일 목록을 읽는다."""
    readme = FRAMEWORK / lang_dir / "guide" / "server" / "README.ko.md"
    order = []
    for line in readme.read_text(encoding="utf-8").splitlines():
        m = ORDER_RE.match(line)
        if m:
            order.append(m.group(1))
    if not order:
        raise SystemExit(f"{readme}에 읽는 순서 표가 없다")
    return order


def chapter_title(path: Path) -> str:
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("# "):
            return line[2:].strip()
    return path.stem


def strip_tabs(text: str, label: str) -> str:
    """대상 언어 탭만 남기고 들여쓰기를 푼다."""
    lines = text.splitlines()
    out: list[str] = []
    i = 0
    while i < len(lines):
        if not TAB_RE.match(lines[i]):
            out.append(lines[i])
            i += 1
            continue
        keep: list[str] | None = None
        while i < len(lines):
            m = TAB_RE.match(lines[i])
            if not m:
                break
            current = m.group(1)
            i += 1
            body: list[str] = []
            while i < len(lines):
                line = lines[i]
                if TAB_RE.match(line):
                    break
                #  탭 본문은 4칸 들여쓴다. 들여쓰기가 끝나면 그룹도 끝난다.
                if line.strip() and not line.startswith("    "):
                    break
                body.append(line[4:] if line.startswith("    ") else line)
                i += 1
            if current == label:
                keep = body
        if keep is None:
            continue
        while keep and not keep[0].strip():
            keep.pop(0)
        while keep and not keep[-1].strip():
            keep.pop()
        out.extend(keep)
        out.append("")
    return "\n".join(out)


def link_chapter_refs(text: str, available: dict[str, str]) -> str:
    """`11. Monitoring` 장 → [11. Monitoring](11-monitoring.ko.md)."""
    def repl(m: re.Match) -> str:
        number, title = m.group(1), m.group(2)
        target = available.get(number)
        if target is None:
            return m.group(0)
        return f"[{number}. {title}]({target})"
    return CHAPTER_REF_RE.sub(repl, text)


def nav_block(order: list[str], name: str, titles: dict[str, str]) -> str:
    """그 언어의 읽는 순서 기준 앞뒤 장 nav."""
    try:
        index = order.index(name)
    except ValueError:
        return ""
    parts = ["[가이드 홈](README.ko.md)"]
    if index > 0:
        prev = order[index - 1]
        parts.append(f"[이전: {titles.get(prev, prev)}]({prev})")
    if index + 1 < len(order):
        nxt = order[index + 1]
        parts.append(f"[다음: {titles.get(nxt, nxt)}]({nxt})")
    return ("<!-- framework-adapter-nav:start -->\n"
            + " | ".join(parts)
            + "\n<!-- framework-adapter-nav:end -->\n\n")


def generate(check_only: bool) -> int:
    sources = sorted(COMMON.glob("*.ko.md"))
    if not sources:
        raise SystemExit(f"공통 소스가 없다: {COMMON}")

    stale: list[str] = []
    written = 0
    for label, lang_dir in LANGUAGES.items():
        target_dir = FRAMEWORK / lang_dir / "guide" / "server"
        raw_order = reading_order(lang_dir)
        #  순서 표는 공통 장을 `../../../common/guide/server/...`로 가리킨다. 그 장들은
        #  이 디렉터리에 생성되므로 형제가 된다. 다른 언어를 가리키는 항목(kotlin의
        #  Java 16장)만 외부 링크로 남는다.
        order: list[str] = []
        for entry in raw_order:
            name = Path(entry).name
            if (target_dir / name).exists() or (COMMON / name).exists():
                order.append(name)
            else:
                order.append(entry)
        siblings = [entry for entry in order if "/" not in entry]
        #  번호 → 형제 파일. 장 참조를 링크로 바꿀 때 쓴다.
        available = {name[:2]: name for name in siblings}

        titles: dict[str, str] = {}
        for entry in order:
            name = Path(entry).name
            local = target_dir / name
            src = COMMON / name
            path = local if local.exists() else src
            if path.exists():
                titles[entry] = chapter_title(path)

        generated = {src.name for src in sources} & set(siblings)
        for src in sources:
            if src.name not in generated:
                continue
            body = strip_tabs(src.read_text(encoding="utf-8"), label)
            body = NAV_RE.sub("", body)
            body, _ = surface_terms.translate(body, label)
            body = link_chapter_refs(body, available)
            content = (FRONT_MATTER.format(
                           title=chapter_title(src), label=label)
                       + BANNER.format(source=src.name) + "\n"
                       + nav_block(order, src.name, titles)
                       + language_switch(lang_dir, src.name) + body)
            content = re.sub(r"\n{3,}", "\n\n", content).rstrip() + "\n"

            out = target_dir / src.name
            previous = out.read_text(encoding="utf-8") if out.exists() else None
            if previous == content:
                continue
            if check_only:
                stale.append(str(out.relative_to(REPO_ROOT)))
                continue
            out.write_text(content, encoding="utf-8")
            written += 1

    if check_only:
        if stale:
            print(f"생성물이 소스와 다르다 {len(stale)}건:", file=sys.stderr)
            for s in stale:
                print(f"  - {s}", file=sys.stderr)
            print("\n`python3 doc/site/scripts/generate_language_guides.py`를 "
                  "실행하고 결과를 커밋한다.", file=sys.stderr)
            return 1
        print("OK — 언어별 가이드가 공통 소스와 일치한다")
        return 0

    print(f"생성: {written}개 갱신 (언어 {len(LANGUAGES)} × 공통 {len(sources)}장)")
    return 0


if __name__ == "__main__":
    raise SystemExit(generate("--check" in sys.argv[1:]))
