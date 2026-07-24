import type { ZLinkFrameworkRegistration, ZLinkSpotNodeOptions } from '../configuration';
import type {
  ZLinkChannelBackendAdapter,
  ZLinkBackendContext,
  ZLinkBackendSpot,
  ZLinkBackendSpotNode,
  ZLinkBackendSpotNodeMode
} from '../backend/contracts';
import {
  ZLINK_BACKEND_SPOT_NODE_MODE_ALL,
  ZLINK_BACKEND_SPOT_NODE_MODE_PUBSUB,
  ZLINK_BACKEND_SPOT_NODE_MODE_ROUTED
} from '../backend/contracts';
import { ZLinkAsyncSubmitter } from '../messaging';

export interface ZLinkOwnedBackendObject {
  dispose(): Promise<void>;
}

export interface ZLinkSpotPublisherBundle {
  readonly node: ZLinkBackendSpotNode;
  readonly spot: ZLinkBackendSpot;
  readonly submitter: ZLinkAsyncSubmitter;
}

interface ZLinkSpotNodeConnectorOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly context: ZLinkBackendContext;
  readonly channelAdapter: ZLinkChannelBackendAdapter;
  readonly ownedObjects: ZLinkOwnedBackendObject[];
  readonly publisherBundles: Map<string, ZLinkSpotPublisherBundle>;
}

export class ZLinkSpotNodeConnector {
  constructor(private readonly options: ZLinkSpotNodeConnectorOptions) {}

  configure(node: ZLinkBackendSpotNode, spotNodeName: string, spotNode: ZLinkSpotNodeOptions): void {
    this.applySpotNodeOptions(node, spotNode);
    this.initializeSpotPublisherClient(node, spotNodeName, spotNode);
  }

  private applySpotNodeOptions(node: ZLinkBackendSpotNode, spotNode: ZLinkSpotNodeOptions): void {
    const routingId = spotNode.router?.routingId ?? spotNode.pubSub?.routingId;
    if (routingId !== undefined) {
      node.setRoutingId(routingId);
    }
    if (spotNode.pubSub?.routingId !== undefined) {
      node.setPublisherRoutingId(spotNode.pubSub.routingId);
      node.setSubscriberRoutingId(spotNode.pubSub.routingId);
    }
    if (spotNode.entrySpot?.routingId !== undefined) {
      node.entrySpot().setRoutingId(spotNode.entrySpot.routingId);
    } else if (spotNode.router?.routingId !== undefined) {
      node.entrySpot().setRoutingId(spotNode.router.routingId);
    }
    if (spotNode.router?.bind !== undefined) {
      node.setRouterBind(spotNode.router.bind);
    }
    for (const endpoint of spotNode.router?.manualConnections ?? []) {
      node.connectPeer(endpoint);
    }
    for (const connection of spotNode.router?.manualPeerConnections ?? []) {
      setImmediate(() => node.connectPeerRid(connection.peerRid, connection.endpoint));
    }
    if (spotNode.pubSub?.bind !== undefined) {
      node.setPubBind(spotNode.pubSub.bind);
    }
    for (const endpoint of spotNode.pubSub?.manualConnections ?? []) {
      node.connectPeer(endpoint);
    }
  }

  private initializeSpotPublisherClient(
    node: ZLinkBackendSpotNode,
    spotNodeName: string,
    spotNode: ZLinkSpotNodeOptions
  ): void {
    if (spotNode.pubSub === undefined) {
      return;
    }
    const publisher = node.createSpot();
    const submitter = new ZLinkAsyncSubmitter(
      (handler) => publisher.onSendReady(handler),
      {
        capacity: Math.max(1, spotNode.publisherConfig?.sendHighWaterMark ?? 1)
      }
    );
    this.options.ownedObjects.push(publisher);
    this.options.publisherBundles.set(spotNodeName, { node, spot: publisher, submitter });
  }
}

export function spotNodeMode(spotNode: ZLinkSpotNodeOptions): ZLinkBackendSpotNodeMode {
  if (spotNode.router !== undefined && spotNode.pubSub !== undefined) {
    return ZLINK_BACKEND_SPOT_NODE_MODE_ALL;
  }
  if (spotNode.router !== undefined) {
    return ZLINK_BACKEND_SPOT_NODE_MODE_ROUTED;
  }
  if (spotNode.pubSub !== undefined) {
    return ZLINK_BACKEND_SPOT_NODE_MODE_PUBSUB;
  }
  return ZLINK_BACKEND_SPOT_NODE_MODE_ALL;
}
