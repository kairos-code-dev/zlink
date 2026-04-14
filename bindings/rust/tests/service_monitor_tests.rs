//! Discovery service monitor tests.

use std::sync::mpsc;
use std::time::Duration;

use zlink::*;

#[test]
fn discovery_open_monitor_exposes_public_service_monitor_surface() {
    let ctx = Context::new().unwrap();
    let discovery = Discovery::new(&ctx, ServiceType::Spot, "svc-mon-open").unwrap();
    let _monitor = discovery.monitor_open().unwrap();
    let _recv: fn(&ServiceMonitor) -> Result<ServiceEvent, RecvError> = ServiceMonitor::recv;
}

#[test]
fn discovery_service_monitor_callback_surface_exists() {
    let ctx = Context::new().unwrap();
    let discovery = Discovery::new(&ctx, ServiceType::Spot, "svc-mon-callback").unwrap();
    let mut monitor = discovery.monitor_open().unwrap();
    let (tx, rx) = mpsc::channel();
    monitor
        .on_event(move |event| {
            let _ = tx.send(event.event_type.bits());
        })
        .unwrap();

    assert!(rx.recv_timeout(Duration::from_millis(100)).is_err());
}
