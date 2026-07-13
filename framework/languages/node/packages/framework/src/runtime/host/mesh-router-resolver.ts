import type { RoutingId } from '../../contracts';
import type { ZLinkFrameworkRegistration } from '../configuration';
import type { ZLinkRemoteBoundSessionTarget } from '../actors';
import { normalizeRoutingId as normalizeRuntimeRoutingId, routingIdsEqual } from '../routing-id';

export class MeshRouterResolver {
  constructor(private readonly registration: ZLinkFrameworkRegistration) {}

  canUseRouterChannel(routerChannelId: string): boolean {
    return this.registration.routeChannels.has(routerChannelId)
      || this.registration.spotNodes.get(routerChannelId)?.router !== undefined;
  }

  defaultRouterChannelId(): string | undefined {
    const candidates = [
      ...this.registration.routeChannels,
      ...[...this.registration.spotNodes.entries()]
        .filter(([, spotNode]) => spotNode.router !== undefined)
        .map(([spotNodeName]) => spotNodeName)
    ];
    return candidates.length === 1 ? candidates[0] : undefined;
  }

  defaultSpotRouterChannelId(): string | undefined {
    const candidates = [...this.registration.spotNodes.entries()]
      .filter(([, spotNode]) => spotNode.router !== undefined)
      .map(([spotNodeName]) => spotNodeName);
    return candidates.length === 1 ? candidates[0] : undefined;
  }

  remoteBoundSessionTargetForSource(sourceNodeRid: RoutingId): ZLinkRemoteBoundSessionTarget | undefined {
    const routerChannelId = this.defaultSpotRouterChannelId()
      ?? this.defaultRouterChannelId();
    if (routerChannelId === undefined) {
      return undefined;
    }
    const targetNodeRid = normalizeRuntimeRoutingId(sourceNodeRid);
    return {
      routerChannelId,
      targetNodeRid,
      spotRid: targetNodeRid
    };
  }

  primarySpotMeshName(): string | undefined {
    const names = [...this.registration.spotNodes.keys()];
    return names.length === 1 ? names[0] : undefined;
  }

  actorSpotMeshName(actorType: string): string | undefined {
    const matches = [...this.registration.spotNodes.entries()]
      .filter(([, node]) => node.actorFactories instanceof Map
        ? node.actorFactories.has(actorType)
        : node.actorFactories !== undefined && Object.hasOwn(node.actorFactories, actorType))
      .map(([name]) => name);
    return matches.length === 1 ? matches[0] : undefined;
  }

  spotLocationMeshNames(): readonly string[] {
    return [...new Set([
      ...this.registration.spotNodes.keys(),
      ...this.registration.routeChannels.keys()
    ])];
  }

  spotRouterChannelIdByMesh(): (meshName: string) => string {
    const routeChannels = [...this.registration.routeChannelOptions.values()];
    const mapped = new Map<string, string>();
    for (const [spotMeshName, spotNode] of this.registration.spotNodes.entries()) {
      const conventionalRouteChannelId = `${spotMeshName}.route`;
      if (
        this.registration.routeChannels.has(conventionalRouteChannelId)
        || this.registration.routeChannelOptions.has(conventionalRouteChannelId)
      ) {
        mapped.set(spotMeshName, conventionalRouteChannelId);
        continue;
      }
      const spotNodeRid = spotNode.router?.routingId ?? spotNode.routingId;
      if (spotNodeRid === undefined) {
        continue;
      }
      const candidates = routeChannels
        .filter((routeChannel) =>
          routeChannel.routingId !== undefined &&
          routingIdsEqual(routeChannel.routingId, spotNodeRid)
        )
        .map((routeChannel) => routeChannel.routerChannelId);
      if (candidates.length === 1) {
        mapped.set(spotMeshName, candidates[0]);
      }
    }
    return (meshName) => mapped.get(meshName) ?? meshName;
  }
}
