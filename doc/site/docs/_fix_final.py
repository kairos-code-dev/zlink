#!/usr/bin/env python3
"""Final comprehensive fix for all box diagrams."""

import unicodedata


def display_width(s):
    w = 0
    for ch in s:
        cat = unicodedata.east_asian_width(ch)
        w += 2 if cat in ('W', 'F') else 1
    return w


def find_all_box_chars(line):
    result = []
    w = 0
    for idx, ch in enumerate(line):
        if ch in '┌┐│└┘┤├┼┬┴─':
            result.append((w, idx, ch))
        cat = unicodedata.east_asian_width(ch)
        w += 2 if cat in ('W', 'F') else 1
    return result


def dcol_of(line, char_idx):
    w = 0
    for idx, ch in enumerate(line):
        if idx == char_idx:
            return w
        cat = unicodedata.east_asian_width(ch)
        w += 2 if cat in ('W', 'F') else 1
    return w


def adjust_right_border(line, char_idx, current_dcol, target_dcol):
    diff = target_dcol - current_dcol
    if diff == 0:
        return line
    if diff > 0:
        return line[:char_idx] + ' ' * diff + line[char_idx:]
    else:
        remove = -diff
        check = char_idx - 1
        avail = 0
        while check >= 0 and line[check] == ' ':
            avail += 1
            check -= 1
        remove = min(remove, avail)
        if remove > 0:
            return line[:char_idx - remove] + line[char_idx:]
    return line


def adjust_border_line(line, char_idx, current_dcol, target_dcol, fill_char='─'):
    diff = target_dcol - current_dcol
    if diff == 0:
        return line
    if diff > 0:
        return line[:char_idx] + fill_char * diff + line[char_idx:]
    else:
        remove = -diff
        check = char_idx - 1
        avail = 0
        while check >= 0 and line[check] == fill_char:
            avail += 1
            check -= 1
        remove = min(remove, avail)
        if remove > 0:
            return line[:char_idx - remove] + line[char_idx:]
    return line


def find_boxes(block_lines):
    boxes = []
    for i, line in enumerate(block_lines):
        chars = find_all_box_chars(line)
        tops = [(dc, ci) for dc, ci, ch in chars if ch == '┌']
        rights = [(dc, ci) for dc, ci, ch in chars if ch == '┐']

        for l_dc, l_ci in tops:
            matching = [(r_dc, r_ci) for r_dc, r_ci in rights if r_dc > l_dc]
            if not matching:
                continue
            r_dc, r_ci = max(matching, key=lambda x: x[0])

            bot_line = None
            for j in range(i + 1, len(block_lines)):
                bchars = find_all_box_chars(block_lines[j])
                for bdc, bci, bch in bchars:
                    if bch == '└' and bdc == l_dc:
                        bot_line = j
                        break
                if bot_line is not None:
                    break

            if bot_line is not None:
                boxes.append((i, l_dc, r_dc, bot_line))

    return boxes


def fix_block(block_lines):
    result = list(block_lines)

    changed = True
    iterations = 0
    while changed and iterations < 10:
        changed = False
        iterations += 1
        boxes = find_boxes(result)

        for top_idx, left_dcol, right_dcol, bot_idx in boxes:
            for j in range(top_idx + 1, bot_idx + 1):
                line = result[j]
                chars = find_all_box_chars(line)

                if j == bot_idx:
                    # Bottom border └─┘
                    for dc, ci, ch in chars:
                        if ch == '└' and dc == left_dcol:
                            brights = [(rdc, rci) for rdc, rci, rch in chars if rch == '┘']
                            if brights:
                                best = min(brights, key=lambda x: abs(x[0] - right_dcol))
                                if best[0] != right_dcol:
                                    result[j] = adjust_border_line(result[j], best[1], best[0], right_dcol)
                                    changed = True
                            break
                    continue

                # Separator ├─┤
                has_sep = False
                for dc, ci, ch in chars:
                    if ch == '├' and dc == left_dcol:
                        srights = [(rdc, rci) for rdc, rci, rch in chars if rch == '┤']
                        if srights:
                            best = min(srights, key=lambda x: abs(x[0] - right_dcol))
                            if best[0] != right_dcol:
                                result[j] = adjust_border_line(result[j], best[1], best[0], right_dcol)
                                changed = True
                        has_sep = True
                        break
                if has_sep:
                    continue

                # Content │...│
                for dc, ci, ch in chars:
                    if ch == '│' and dc == left_dcol:
                        right_candidates = [(rdc, rci) for rdc, rci, rch in chars
                                           if rch == '│' and rdc != left_dcol]
                        if not right_candidates:
                            break

                        # Find the │ closest to right_dcol
                        best = min(right_candidates, key=lambda x: abs(x[0] - right_dcol))
                        if abs(best[0] - right_dcol) <= 3 and best[0] != right_dcol:
                            result[j] = adjust_right_border(result[j], best[1], best[0], right_dcol)
                            changed = True
                        break

    return result


def process_file(filepath):
    with open(filepath) as f:
        lines = f.read().split('\n')

    modified = False
    in_block = False
    block_start = -1
    blocks = []

    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith('```') and not in_block:
            if stripped == '```' or stripped == '```text':
                in_block = True
                block_start = i
        elif stripped == '```' and in_block:
            in_block = False
            blocks.append((block_start + 1, i))

    for bstart, bend in blocks:
        block = lines[bstart:bend]
        has_box = any('┌' in l for l in block)
        if not has_box:
            continue

        fixed = fix_block(block)
        if fixed != block:
            lines[bstart:bend] = fixed
            modified = True

    if modified:
        with open(filepath, 'w') as f:
            f.write('\n'.join(lines))
    return modified


files = [
    'doc/site/docs/guide/07-1-discovery.md',
    'doc/site/docs/guide/07-1-discovery.ko.md',
    'doc/site/docs/guide/07-4-registry.ko.md',
    'doc/site/docs/internals/architecture.md',
    'doc/site/docs/internals/architecture.ko.md',
    'doc/site/docs/internals/threading-model.md',
    'doc/site/docs/internals/threading-model.ko.md',
]

for f in files:
    result = process_file(f)
    print(f"{'FIXED' if result else 'OK':5s}: {f}")
