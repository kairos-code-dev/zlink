#!/usr/bin/env python3
"""Add Go language tabs after Rust tabs in markdown guide files."""
import re
import sys
import os

def rust_to_go(rust_code: str, context_hints: str = "") -> str:
    """Convert Rust code snippets to Go equivalents."""
    go = rust_code

    # --- Context and socket creation ---
    go = re.sub(r'let ctx = zlink::Context::new\(\)\?;', 'ctx := zlink.NewContext()', go)
    go = re.sub(r'let ctx = Context::new\(\);', 'ctx := zlink.NewContext()', go)
    go = re.sub(r'let ctx = Context::new\(\)\?;', 'ctx := zlink.NewContext()', go)

    # Socket constructors
    go = re.sub(r'let (\w+) = ctx\.pair_socket\(\)\??;', r'\1, _ := ctx.PairSocket()', go)
    go = re.sub(r'let (\w+) = ctx\.pub_socket\(\)\??;', r'\1, _ := ctx.PubSocket()', go)
    go = re.sub(r'let (\w+) = ctx\.sub_socket\(\)\??;', r'\1, _ := ctx.SubSocket()', go)
    go = re.sub(r'let (\w+) = ctx\.xpub_socket\(\)\??;', r'\1, _ := ctx.XPubSocket()', go)
    go = re.sub(r'let (\w+) = ctx\.xsub_socket\(\)\??;', r'\1, _ := ctx.XSubSocket()', go)
    go = re.sub(r'let (\w+) = ctx\.dealer_socket\(\)\??;', r'\1, _ := ctx.DealerSocket()', go)
    go = re.sub(r'let (\w+) = ctx\.router_socket\(\)\??;', r'\1, _ := ctx.RouterSocket()', go)
    go = re.sub(r'let (\w+) = ctx\.stream_socket\(\)\??;', r'\1, _ := ctx.StreamSocket()', go)

    # Socket creation with type suffix (zlink::xxx_socket_t style used in some blocks)
    go = re.sub(r'zlink::context_t ctx;', 'ctx := zlink.NewContext()', go)
    go = re.sub(r'zlink::pair_socket_t (\w+)\(ctx\);', r'\1, _ := ctx.PairSocket()', go)
    go = re.sub(r'zlink::pub_socket_t (\w+)\(ctx\);', r'\1, _ := ctx.PubSocket()', go)
    go = re.sub(r'zlink::sub_socket_t (\w+)\(ctx\);', r'\1, _ := ctx.SubSocket()', go)
    go = re.sub(r'zlink::xpub_socket_t (\w+)\(ctx\);', r'\1, _ := ctx.XPubSocket()', go)
    go = re.sub(r'zlink::xsub_socket_t (\w+)\(ctx\);', r'\1, _ := ctx.XSubSocket()', go)
    go = re.sub(r'zlink::dealer_socket_t (\w+)\(ctx\);', r'\1, _ := ctx.DealerSocket()', go)
    go = re.sub(r'zlink::router_socket_t (\w+)\(ctx\);', r'\1, _ := ctx.RouterSocket()', go)
    go = re.sub(r'zlink::stream_socket_t (\w+)\(ctx\);', r'\1, _ := ctx.StreamSocket()', go)

    # --- Bind / Connect ---
    go = re.sub(r'(\w+)\.bind\("([^"]+)"\)\??;', r'\1.Bind("\2")', go)
    go = re.sub(r'(\w+)\.connect\("([^"]+)"\)\??;', r'\1.Connect("\2")', go)
    go = re.sub(r'(\w+)\.connect\(&(\w+)\)\??;', r'\1.Connect(\2)', go)
    go = re.sub(r'(\w+)\.connect\((\w+)\)\??;', r'\1.Connect(\2)', go)
    go = re.sub(r'(\w+)\.connect\(&endpoint\)\??;', r'\1.Connect(endpoint)', go)

    # --- Send / Recv ---
    # send with byte literal
    go = re.sub(r'(\w+)\.send\(b"([^"]+)"\)\??;', r'\1.Send(zlink.NewMessage([]byte("\2")))', go)
    # send with string
    go = re.sub(r'(\w+)\.send\("([^"]+)"\)\??;', r'\1.Send(zlink.NewMessage([]byte("\2")))', go)
    # multipart send
    go = re.sub(r'(\w+)\.send\(&\[b"([^"]+)", b"([^"]+)"\]\)\??;', r'\1.Send(zlink.NewMultipartMessage([]byte("\2"), []byte("\3")))', go)

    # recv
    go = re.sub(r'let \((\w+), (\w+)\) = (\w+)\.recv\(\)\??;', r'\1, \2, _ := \3.Recv()', go)
    # recv with auto
    go = re.sub(r'auto \[(\w+), (\w+)\] = (\w+)\.recv\(\);', r'\1, \2, _ := \3.Recv()', go)

    # --- send_rid ---
    go = re.sub(r'(\w+)\.send_rid\(&(\w+), b"([^"]+)"\)\??;', r'\1.SendTo(\2, zlink.NewMessage([]byte("\3")))', go)
    go = re.sub(r'(\w+)\.send_rid\(&(\w+), &zlink::Message::from\("([^"]+)"\)\)\??;', r'\1.SendTo(\2, zlink.NewMessage([]byte("\3")))', go)
    go = re.sub(r'(\w+)\.send_rid\((\w+), b"([^"]+)"\)\??;', r'\1.SendTo(\2, zlink.NewMessage([]byte("\3")))', go)
    go = re.sub(r'(\w+)\.send_rid\(&(\w+), &(\w+)\)\??;', r'\1.SendTo(\2, \3)', go)
    go = re.sub(r'(\w+)\.send_rid\((\w+), (\w+)\)\??;', r'\1.SendTo(\2, \3)', go)

    # --- Publish ---
    go = re.sub(r'(\w+)\.publish\("([^"]*)", b"([^"]+)"\)\??;', r'\1.Publish("\2", zlink.NewMessage([]byte("\3")))', go)
    go = re.sub(r'(\w+)\.publish\("([^"]*)", &\[b"([^"]+)", b"([^"]+)"\]\)\??;', r'\1.Publish("\2", zlink.NewMultipartMessage([]byte("\3"), []byte("\4")))', go)

    # --- Subscribe ---
    go = re.sub(r'(\w+)\.set_subscription\("([^"]*)"\)\??;', r'\1.SetSubscription("\2")', go)
    go = re.sub(r'(\w+)\.unset_subscription\("([^"]*)"\)\??;', r'\1.UnsetSubscription("\2")', go)

    # --- Options ---
    go = re.sub(r'(\w+)\.set_option\(zlink::Option::(\w+), (\w+)\)\??;', r'\1.SetOption(zlink.Option\2, \3)', go)
    go = re.sub(r'(\w+)\.set_option\(ZLINK_OPT_(\w+), (\w+)\)\??;', r'\1.SetOption(zlink.Option\2, \3)', go)
    go = re.sub(r'(\w+)\.set_option\(zlink::Option::<OPTION>, value\)\??;', r'\1.SetOption(zlink.Option<OPTION>, value)', go)

    # set_linger
    go = re.sub(r'(\w+)\.set_linger\((\d+)\)\??;', r'\1.SetOption(zlink.OptionLinger, \2)', go)

    # get_option
    go = re.sub(r'let (\w+) = (\w+)\.get_option::<String>\(ZLINK_OPT_LAST_ENDPOINT\)\??;', r'\1, _ := \2.GetOption(zlink.OptionLastEndpoint)', go)
    go = re.sub(r'let (\w+) = (\w+)\.get_option::<String>\((\w+)\)\??;', r'\1, _ := \2.GetOption(\3)', go)

    # --- routing_id ---
    go = re.sub(r'(\w+)\.set_routing_id\("([^"]+)"\)\??;', r'\1.SetRoutingId(zlink.NewRoutingID([]byte("\2")))', go)
    go = re.sub(r'let (\w+) = zlink::RoutingId::from\("([^"]+)"\);', r'\1 := zlink.NewRoutingID([]byte("\2"))', go)
    go = re.sub(r'zlink::routing_id_t (\w+)\("([^"]+)", \d+\);', r'\1 := zlink.NewRoutingID([]byte("\2"))', go)

    # --- set_router_mandatory ---
    go = re.sub(r'(\w+)\.set_router_mandatory\(true\)\??;', r'\1.SetRouterMandatory(true)', go)

    # --- Close / Term ---
    go = re.sub(r'(\w+)\.close\(\)\??;', r'\1.Close()', go)
    go = re.sub(r'ctx\.term\(\)\??;', 'ctx.Close()', go)

    # --- println ---
    go = re.sub(r'println!\("([^"]*)", ([\w\[\]\.]+)\.as_str\(\)\?\);', r'fmt.Printf("\1\n", \2.String())', go)
    go = re.sub(r'println!\("Received: \{}", (\w+)\[0\]\.as_str\(\)\?\);', r'fmt.Printf("Received: %s\\n", \1[0].String())', go)
    go = re.sub(r'println!\("From \[\{}\]: \{}", (\w+), (\w+)\[0\]\.as_str\(\)\?\);', r'fmt.Printf("From [%s]: %s\\n", \1, \2[0].String())', go)

    # --- Error handling patterns ---
    # match/Err pattern for send_dontwait
    go = re.sub(
        r'match (\w+)\.send_dontwait\(b"(\w+)"\) \{\s*Err\(ZlinkError::Eagain\) => \{\s*// (.+)\s*\}\s*_ => \{\}\s*\}',
        r'err := \1.SendDontWait(zlink.NewMessage([]byte("\2")))\nif errors.Is(err, zlink.ErrAgain) {\n    // \3\n}',
        go
    )

    # match/Err pattern for send_rid
    go = re.sub(
        r'match (\w+)\.send_rid\(&(\w+), &(\w+)\) \{\s*Err\(e\) if e\.kind\(\) == zlink::ErrorKind::HostUnreachable => \{\s*// (.+)\s*\}\s*other => other\?,\s*\}',
        r'err := \1.SendTo(\2, \3)\nif errors.Is(err, zlink.ErrHostUnreachable) {\n    // \4\n}',
        go
    )

    # --- let message ---
    go = re.sub(r'let (\w+) = zlink::Message::from\("([^"]+)"\);', r'\1 := zlink.NewMessage([]byte("\2"))', go)
    go = re.sub(r'zlink::message_t (\w+)\("([^"]+)", \d+\);', r'\1 := zlink.NewMessage([]byte("\2"))', go)

    # --- proxy ---
    go = re.sub(r'zlink::proxy\(&(\w+), &(\w+), Some\(&(\w+)\)\)\??;', r'zlink.Proxy(\1, \2, \3)', go)
    go = re.sub(r'zlink::proxy\(&(\w+), &(\w+), None\)\??;', r'zlink.Proxy(\1, \2, nil)', go)
    go = re.sub(r'zlink::proxy\(&(\w+), &(\w+)\)\??;', r'zlink.Proxy(\1, \2, nil)', go)

    # --- on_message / recv_handler callbacks (simplify to comments) ---
    # Complex closures are replaced with Go-style comment
    go = re.sub(
        r'(\w+)\.on_message\(\|[^|]+\| \{[\s\S]*?\}\)\??;',
        lambda m: _convert_callback(m.group(0), m),
        go
    )
    go = re.sub(
        r'(\w+)\.recv_handler\(\|[^|]+\| \{[\s\S]*?\}\)\??;',
        lambda m: _convert_callback(m.group(0), m),
        go
    )

    # --- thread::sleep ---
    go = re.sub(r'thread::sleep\(Duration::from_millis\((\d+)\)\);', r'time.Sleep(\1 * time.Millisecond)', go)

    # --- Ok(()) ---
    go = re.sub(r'\s*Ok\(\(\)\)\s*$', '', go)

    # --- fn main ---
    go = re.sub(r'fn main\(\) -> zlink::Result<\(\)> \{', 'func main() {', go)

    # --- use statements to imports ---
    go = re.sub(r'use zlink::\{[^}]+\};', '', go)

    # Clean up trailing ? on function calls
    go = re.sub(r'\?;', '', go)
    go = re.sub(r'\?$', '', go, flags=re.MULTILINE)

    # Clean up let -> :=
    go = re.sub(r'let mut (\w+)', r'\1', go)
    go = re.sub(r'let (\w+)', r'\1', go)

    # Clean up &str to string
    go = re.sub(r'\.as_str\(\)', '.String()', go)

    # Clean up Rust-style // comments (keep them)

    return go


def _convert_callback(code, match):
    """Convert Rust closure callbacks into Go comments."""
    # For complex callbacks, just keep the structure comment
    return "// Register message handler (Go uses goroutine with Recv loop)"


def extract_tab_blocks(content):
    """Find all tab blocks in the content. Returns list of (start, end, rust_code, indent_after_rust) tuples."""
    blocks = []
    lines = content.split('\n')
    i = 0
    while i < len(lines):
        line = lines[i]
        # Look for === "Rust"
        if line.strip() == '=== "Rust"':
            rust_start = i
            # Find the code block within this tab
            j = i + 1
            in_code = False
            code_start = -1
            code_end = -1
            code_lines = []
            lang = ""
            while j < len(lines):
                stripped = lines[j].strip()
                if not in_code and stripped.startswith('```'):
                    in_code = True
                    lang = stripped[3:]
                    code_start = j
                    j += 1
                    continue
                if in_code and stripped == '```':
                    code_end = j
                    in_code = False
                    j += 1
                    break
                if in_code:
                    # Remove 4-space indent for the tab
                    if lines[j].startswith('    '):
                        code_lines.append(lines[j][4:])
                    else:
                        code_lines.append(lines[j])
                j += 1

            # Find where the Rust tab ends (next === or non-blank non-indented)
            rust_end = j
            while rust_end < len(lines):
                l = lines[rust_end].strip()
                if l == '':
                    rust_end += 1
                    continue
                # If it's another tab or something else (not indented)
                if l.startswith('===') or (not lines[rust_end].startswith('    ') and l != ''):
                    break
                rust_end += 1

            rust_code = '\n'.join(code_lines)
            blocks.append({
                'rust_line_start': rust_start,
                'rust_block_end': rust_end,  # Insert Go tab before this line
                'code_end_line': code_end,   # Line with closing ```
                'rust_code': rust_code,
                'lang': lang,
            })
            i = rust_end
        else:
            i += 1
    return blocks


def generate_go_code_for_block(rust_code, lang):
    """Generate Go equivalent for a Rust code block."""
    go_code = rust_to_go(rust_code)

    # Clean up empty lines at start/end
    go_lines = go_code.split('\n')
    while go_lines and go_lines[0].strip() == '':
        go_lines.pop(0)
    while go_lines and go_lines[-1].strip() == '':
        go_lines.pop()

    return '\n'.join(go_lines)


def add_go_tabs_to_file(filepath):
    """Add Go tabs after Rust tabs in a markdown file."""
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    blocks = extract_tab_blocks(content)
    if not blocks:
        print(f"  No Rust tabs found in {filepath}")
        return False

    lines = content.split('\n')

    # Process blocks in reverse order to preserve line numbers
    for block in reversed(blocks):
        rust_code = block['rust_code']
        go_code = generate_go_code_for_block(rust_code, block['lang'])

        # Build the Go tab block
        go_tab_lines = ['', '=== "Go"', '']
        go_tab_lines.append('    ```go')
        for gl in go_code.split('\n'):
            if gl:
                go_tab_lines.append('    ' + gl)
            else:
                go_tab_lines.append('')
        go_tab_lines.append('    ```')

        # Insert after the Rust tab's closing ``` line
        insert_at = block['code_end_line'] + 1

        # Check if there are blank lines after the closing ```
        # We want to insert Go tab right after rust closing + any trailing blank
        while insert_at < len(lines) and lines[insert_at].strip() == '':
            # Check if next non-blank line is another === or non-tab content
            peek = insert_at + 1
            while peek < len(lines) and lines[peek].strip() == '':
                peek += 1
            if peek < len(lines) and lines[peek].strip().startswith('==='):
                # Another tab follows, don't skip blank lines
                break
            if peek < len(lines) and not lines[peek].startswith('    '):
                # Non-indented content follows (section end)
                break
            insert_at += 1

        lines = lines[:insert_at] + go_tab_lines + lines[insert_at:]

    with open(filepath, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))

    print(f"  Added {len(blocks)} Go tabs to {filepath}")
    return True


def main():
    base = "/home/hep7/project/kairos/zlink/doc/site/docs/guide"
    files = [
        "01-overview.md",
        "03-0-socket-patterns.md",
        "12-socket-options.md",
        "03-1-pair.md",
        "03-2-pubsub.md",
        "03-3-dealer.md",
        "03-4-router.md",
        "03-5-stream.md",
        "03-6-proxy.md",
    ]

    for f in files:
        en_path = os.path.join(base, f)
        ko_path = os.path.join(base, f.replace('.md', '.ko.md'))

        print(f"Processing {f}...")
        if os.path.exists(en_path):
            add_go_tabs_to_file(en_path)
        else:
            print(f"  WARNING: {en_path} not found")

        print(f"Processing {f.replace('.md', '.ko.md')}...")
        if os.path.exists(ko_path):
            add_go_tabs_to_file(ko_path)
        else:
            print(f"  WARNING: {ko_path} not found")


if __name__ == '__main__':
    main()
