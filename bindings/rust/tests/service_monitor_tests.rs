//! Discovery service monitor tests.

use std::sync::mpsc;
use std::time::Duration;

use zlink::*;

#[test]
fn discovery_open_monitor_exposes_public_service_monitor_surface() {
    let ctx = Context::new().unwrap();
    let discovery = Discovery::new(&ctx, ServiceType::Spot, "svc-mon-open").unwrap();
    let monitor = discovery
        .monitor_open(SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP)
        .unwrap();

    assert!(monitor.try_recv().unwrap().is_none());
}

#[test]
fn discovery_service_monitor_callback_surface_exists() {
    let ctx = Context::new().unwrap();
    let discovery = Discovery::new(&ctx, ServiceType::Spot, "svc-mon-callback").unwrap();
    let mut monitor = discovery
        .monitor_open(SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP)
        .unwrap();
    let (tx, rx) = mpsc::channel();
    monitor
        .on_event(move |event| {
            let _ = tx.send(event.event_type.bits());
        })
        .unwrap();

    assert!(rx.recv_timeout(Duration::from_millis(100)).is_err());
}
