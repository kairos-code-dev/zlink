"""mermaid 다이어그램을 컨테이너에 맞춰 줄이지 않게 한다.

mermaid는 기본값(`useMaxWidth: true`)으로 svg에 `width="100%"`와
`style="max-width: <자연폭>px"`을 건다. 자연폭보다 좁은 자리에 놓이면 viewBox가
전체를 축소하므로 노드가 많은 그림은 절반 이하로 줄어들고 안의 글씨를 읽을 수
없게 된다. 그림은 읽으라고 그린 것이니 축소 대신 자연폭을 지키고, 넘칠 때만
그 블록 안에서 가로로 움직이게 한다(폭 초과 처리는 `korean.css`).

CSS로는 고칠 수 없다. `width="100%"`를 CSS `width: auto`로 덮으면 viewBox만
있고 intrinsic width가 없는 svg가 기본값 300px로 떨어진다. mermaid 쪽 설정을
바꿔야 하고, 그 설정은 다이어그램 타입별로 나뉘어 있다.

Material은 mermaid를 자기 번들 안에서 초기화하므로 `extra_javascript`로 설정을
덮으려면 로딩 순서에 기대야 한다. 대신 markdown 단계에서 다이어그램마다
`%%{init}%%` 디렉티브를 심는다 — 빌드 시점에 끝나므로 경합이 없다.
"""

from __future__ import annotations

import re

#  다이어그램 타입 → mermaid 설정 키. 키가 타입마다 다르므로 한 번에 못 준다.
CONFIG_KEY = {
    "flowchart": "flowchart",
    "graph": "flowchart",
    "sequenceDiagram": "sequence",
    "stateDiagram-v2": "state",
    "stateDiagram": "state",
    "classDiagram": "class",
    "erDiagram": "er",
}

FENCE_RE = re.compile(r"(^(?P<indent>[ \t]*)```+mermaid[ \t]*\n)(?P<body>.*?)(?=^[ \t]*```)",
                      re.S | re.M)
#  이미 있는 디렉티브. 두 줄로 나누지 않고 이 안에 끼워 넣는다.
INIT_RE = re.compile(r"^([ \t]*%%\{\s*init\s*:\s*\{)")


def _diagram_type(body: str) -> str | None:
    for line in body.splitlines():
        s = line.strip()
        if not s or s.startswith("%%"):
            continue
        return s.split()[0].rstrip(";")
    return None


def _inject(body: str, key: str) -> str:
    fragment = f"'{key}': {{'useMaxWidth': false}}"
    lines = body.splitlines(keepends=True)
    for i, line in enumerate(lines):
        m = INIT_RE.match(line)
        if not m:
            continue
        if "useMaxWidth" in line:
            return body
        lines[i] = m.group(1) + fragment + ", " + line[m.end(1):]
        return "".join(lines)
    #  디렉티브가 없으면 맨 앞에 새로 놓는다. 들여쓰기는 첫 줄에서 가져온다.
    indent = re.match(r"[ \t]*", lines[0]).group(0) if lines else ""
    return f"{indent}%%{{init: {{{fragment}}}}}%%\n" + body


def on_page_markdown(markdown: str, **_) -> str:
    def repl(m: re.Match) -> str:
        body = m.group("body")
        key = CONFIG_KEY.get(_diagram_type(body) or "")
        return m.group(1) + (_inject(body, key) if key else body)

    return FENCE_RE.sub(repl, markdown)
