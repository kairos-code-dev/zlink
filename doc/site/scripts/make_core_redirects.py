#!/usr/bin/env python3
"""core가 `/core/` 아래로 내려간 뒤, 옛 최상위 경로를 새 자리로 보낸다.

`zlink.systems` 최상위는 core 사이트였다. framework 사이트가 그 자리를 차지하면
`/guide/...` · `/api/...` · `/internals/...` · `/ko/...`로 걸린 외부 링크와 검색
색인이 전부 죽는다. 옮기는 것과 같은 시점에 이 stub을 깔아야 한다
(`framework/doc/plan/language-guide-port-runbook.ko.md` §7.2).

GitHub Pages는 301을 낼 수 없다. 정적 호스팅이라 서버 규칙을 둘 자리가 없다.
그래서 `<meta http-equiv="refresh">` 문서를 옛 경로에 깔고 `<link rel="canonical">`로
새 주소를 알린다. 사람은 곧바로 넘어가고, 크롤러는 canonical을 따라간다.

framework 사이트가 이미 쓰는 경로는 건드리지 않는다. 지금은 겹치지 않지만
(framework 최상위는 `dotnet/` · `cpp/` · `java/` · `kotlin/` · `node/` · `common/`),
나중에 겹치는 이름이 생겨도 이 스크립트가 덮어써서 페이지를 잡아먹는 일은 없다.

실행:
    python3 doc/site/scripts/make_core_redirects.py <합쳐진-사이트-루트>
"""

from __future__ import annotations

import sys
from pathlib import Path

STUB = """<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="utf-8">
<title>이동함 — {target}</title>
<link rel="canonical" href="{target}">
<meta http-equiv="refresh" content="0; url={target}">
</head>
<body>
<p>core 문서가 <a href="{target}">{target}</a>으로 옮겨졌다.</p>
</body>
</html>
"""


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    root = Path(sys.argv[1]).resolve()
    core = root / "core"
    if not core.is_dir():
        print(f"core 빌드 결과가 없다: {core}", file=sys.stderr)
        return 1

    written = skipped = 0
    for page in sorted(core.rglob("index.html")):
        rel = page.parent.relative_to(core)
        if rel == Path("."):
            continue                      # core 첫 화면. 최상위는 framework 몫이다.
        old = root / rel / "index.html"
        if old.exists():
            skipped += 1                  # framework 페이지를 덮지 않는다
            continue
        old.parent.mkdir(parents=True, exist_ok=True)
        old.write_text(STUB.format(target=f"/core/{rel.as_posix()}/"),
                       encoding="utf-8")
        written += 1

    print(f"옛 core 경로 stub {written}개 생성"
          + (f", framework와 겹쳐 건너뜀 {skipped}개" if skipped else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
