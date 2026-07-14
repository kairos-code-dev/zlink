#!/usr/bin/env python3
"""Writes one role's configuration file for a DeliveryDispatch run.

The runner decides this run's ports, Redis endpoint and directories, and hands them to the
application in a file — never through the environment
(framework/doc/framework/common/sample-e2e-configuration-policy.ko.md 2.2, 6, 7). The file is
readable only by the user who ran it.
"""
import argparse
import json
import os
import stat


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--role", required=True)
    parser.add_argument("--node-rid", default="")
    parser.add_argument("--log-dir", required=True)
    parser.add_argument("--work-dir", required=True)
    parser.add_argument("--redis-endpoint", required=True)
    parser.add_argument("--redis-key-prefix", required=True)
    parser.add_argument("--dispatch-http", required=True)
    parser.add_argument("--dispatch-channel", required=True)
    parser.add_argument("--dispatch-spot-router", required=True)
    parser.add_argument("--tracking-channel", required=True)
    parser.add_argument("--tracking-spot-router", required=True)
    parser.add_argument("--tracking-spot", required=True)
    parser.add_argument("--customer-stream", required=True)
    parser.add_argument("--customer-spot-router", required=True)
    parser.add_argument("--customer-spot", required=True)
    parser.add_argument("--courier-stream", required=True)
    parser.add_argument("--courier-session-spot-router", required=True)
    parser.add_argument("--courier-session-spot", required=True)
    parser.add_argument("--courier-node1-router", required=True)
    parser.add_argument("--courier-node1", required=True)
    parser.add_argument("--courier-node2-router", required=True)
    parser.add_argument("--courier-node2", required=True)
    args = parser.parse_args()

    role = {
        "name": args.role,
        "logDir": args.log_dir,
        "workDir": args.work_dir,
    }
    if args.node_rid:
        role["nodeRid"] = args.node_rid

    document = {
        "sample": {
            "role": role,
            "topology": {
                "redisEndpoint": args.redis_endpoint,
                "redisKeyPrefix": args.redis_key_prefix,
                "dispatchHttpUrl": args.dispatch_http,
                "dispatchChannelEndpoint": args.dispatch_channel,
                "dispatchSpotRouterEndpoint": args.dispatch_spot_router,
                "trackingChannelEndpoint": args.tracking_channel,
                "trackingSpotRouterEndpoint": args.tracking_spot_router,
                "trackingSpotEndpoint": args.tracking_spot,
                "customerStreamEndpoint": args.customer_stream,
                "customerSpotRouterEndpoint": args.customer_spot_router,
                "customerSpotEndpoint": args.customer_spot,
                "courierStreamEndpoint": args.courier_stream,
                "courierSessionSpotRouterEndpoint": args.courier_session_spot_router,
                "courierSessionSpotEndpoint": args.courier_session_spot,
                "courierActorNode1RouterEndpoint": args.courier_node1_router,
                "courierActorNode1Endpoint": args.courier_node1,
                "courierActorNode2RouterEndpoint": args.courier_node2_router,
                "courierActorNode2Endpoint": args.courier_node2,
            },
        }
    }

    with open(args.output, "w", encoding="utf-8") as file:
        json.dump(document, file, indent=2)
    os.chmod(args.output, stat.S_IRUSR | stat.S_IWUSR)


if __name__ == "__main__":
    main()
