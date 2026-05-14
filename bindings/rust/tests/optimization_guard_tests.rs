use std::fs;
use std::path::{Path, PathBuf};

const AGGREGATE_SYMBOLS: &[&str] = &[
    "zlink_send",
    "zlink_recv",
    "zlink_publish",
    "zlink_subscribe",
    "zlink_router_recv",
    "zlink_dealer_request",
    "zlink_router_request",
    "zlink_router_reply",
    "zlink_spot_send_channel",
    "zlink_spot_request_channel",
    "zlink_spot_request_spot",
    "zlink_spot_request_router",
    "zlink_spot_publish",
    "zlink_spot_subscribe",
    "zlink_spot_send_spot",
    "zlink_spot_reply_spot",
    "zlink_spot_reply_router",
    "zlink_spot_recv",
];

const REQUIRED_PART_SYMBOLS: &[&str] = &[
    "zlink_send_part",
    "zlink_recv_part",
    "zlink_publish_part",
    "zlink_subscribe_part",
    "zlink_router_recv_part",
    "zlink_dealer_request_part",
    "zlink_router_request_part",
    "zlink_router_reply_part",
    "zlink_spot_publish_part",
    "zlink_spot_subscribe_part",
    "zlink_spot_request_channel_part",
    "zlink_spot_request_spot_part",
    "zlink_spot_reply_router_part",
];

fn source_files(root: &Path) -> Vec<PathBuf> {
    fn visit(dir: &Path, out: &mut Vec<PathBuf>) {
        for entry in fs::read_dir(dir).unwrap() {
            let entry = entry.unwrap();
            let path = entry.path();
            if path.is_dir() {
                visit(&path, out);
            } else if path.extension().and_then(|ext| ext.to_str()) == Some("rs") {
                out.push(path);
            }
        }
    }

    let mut files = Vec::new();
    visit(root, &mut files);
    files
}

#[test]
fn hot_paths_use_part_substrate_not_aggregate_calls() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("src");
    let files = source_files(&root);
    let all = files
        .iter()
        .map(|path| fs::read_to_string(path).unwrap())
        .collect::<Vec<_>>()
        .join("\n");

    for symbol in REQUIRED_PART_SYMBOLS {
        assert!(
            all.contains(symbol),
            "missing required helper substrate {symbol}"
        );
    }

    let mut violations = Vec::new();
    for path in files {
        let body = fs::read_to_string(&path).unwrap();
        for symbol in AGGREGATE_SYMBOLS {
            let needle = format!("ffi::{symbol}(");
            let mut start = 0;
            while let Some(offset) = body[start..].find(&needle) {
                let idx = start + offset;
                if !body[idx..].starts_with(&format!("ffi::{symbol}_part")) {
                    violations.push(format!("{}:{symbol}", path.display()));
                }
                start = idx + needle.len();
            }
        }
    }

    assert!(violations.is_empty(), "{violations:#?}");
}

#[test]
fn binding_runtime_does_not_hide_sleep_or_join_in_hot_paths() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("src");
    for path in source_files(&root) {
        let body = fs::read_to_string(&path).unwrap();
        if path.file_name().and_then(|name| name.to_str()) == Some("runtime.rs") {
            continue;
        }
        assert!(
            !body.contains("thread::sleep(") && !body.contains("std::thread::sleep("),
            "{} contains hidden sleep",
            path.display()
        );
    }
}
