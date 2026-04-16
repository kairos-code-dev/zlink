//! Discovery + registry sample.

#[path = "sample_support.rs"]
mod sample_support;

use std::time::Duration;

use zlink::{Context, Discovery, Registry, ServiceType};

const SERVICE_NAME: &str = "sample";

fn main() {
    let ctx = Context::new().expect("context creation failed");
    let registry = Registry::new(&ctx).expect("registry creation failed");
    let discovery =
        Discovery::new(&ctx, ServiceType::Socket, SERVICE_NAME).expect("discovery failed");
    let provider = ctx.pub_socket().expect("pub socket failed");
    let registry_pub = sample_support::tcp_endpoint();
    let registry_router = sample_support::tcp_endpoint();
    let service_endpoint = sample_support::tcp_endpoint();

    registry
        .bind(&registry_pub, &registry_router)
        .expect("registry bind failed");
    discovery
        .connect_registry(&registry_router)
        .expect("discovery connect failed");
    provider
        .attach_discovery(&discovery)
        .expect("attach discovery failed");
    provider.bind(&service_endpoint).expect("provider bind failed");

    sample_support::wait_until(
        || {
            registry
                .topology_snapshot()
                .map(|entries| entries.iter().any(|entry| entry.service_name == SERVICE_NAME))
                .unwrap_or(false)
        },
        Duration::from_secs(5),
        "discovery registry sample",
    );

    println!("[discovery-registry] service: \"sample\" -> discovered");
}
