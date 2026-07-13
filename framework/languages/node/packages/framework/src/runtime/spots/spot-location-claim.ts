import type { RoutingId } from '../../contracts';
import {
  ZLinkLocationWriteStatus,
  ZLinkSpotKind,
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import type { ZLinkLocationLifecycle } from '../locations';

interface ZLinkSpotLocationClaimOptions {
  readonly lifecycle?: ZLinkLocationLifecycle;
  readonly meshName?: string;
  readonly nodeRid?: RoutingId;
  readonly nodeRidProvider?: () => RoutingId | undefined;
}

export type ZLinkSpotLocationClaimResult =
  | { readonly claimed: true; readonly meshName: string }
  | { readonly claimed: false; readonly state: 'existing'; readonly meshName: string };

export class ZLinkSpotLocationClaim {
  constructor(private readonly options: ZLinkSpotLocationClaimOptions) {}

  async claimUserSpot(
    spotRid: RoutingId,
    spotTypeName: string
  ): Promise<ZLinkSpotLocationClaimResult> {
    if (this.options.lifecycle === undefined) {
      return { claimed: true, meshName: '' };
    }
    const meshName = this.requireMeshName();
    const status = await this.options.lifecycle.claimSpot(
      meshName,
      spotRid,
      spotTypeName,
      this.requireNodeRid(),
      ZLinkSpotKind.User
    );
    if (status === ZLinkLocationWriteStatus.RejectedConflict) {
      return { claimed: false, state: 'existing', meshName };
    }
    if (status !== ZLinkLocationWriteStatus.Stored) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.SpotCreateFailed,
        `Spot '${spotRid}' location claim failed with '${status}'.`
      );
    }
    return { claimed: true, meshName };
  }

  async release(meshName: string, spotRid: RoutingId): Promise<void> {
    if (this.options.lifecycle === undefined) {
      return;
    }
    await this.options.lifecycle.releaseSpot(meshName, spotRid);
  }

  meshNameForRelease(): string {
    return this.options.lifecycle === undefined ? '' : this.requireMeshName();
  }

  private requireMeshName(): string {
    const meshName = this.options.meshName;
    if (meshName === undefined || meshName.length === 0) {
      throw new ZLinkConfigurationException('Location spot claim requires a mesh name.');
    }
    return meshName;
  }

  private requireNodeRid(): RoutingId {
    const nodeRid = this.options.nodeRidProvider?.() ?? this.options.nodeRid;
    if (nodeRid === undefined) {
      throw new ZLinkConfigurationException('Location spot claim requires a node routing id.');
    }
    return nodeRid;
  }
}
