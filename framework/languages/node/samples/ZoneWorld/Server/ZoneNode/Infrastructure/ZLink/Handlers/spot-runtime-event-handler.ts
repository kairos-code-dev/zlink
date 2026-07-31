import { Inject, Injectable, type OnApplicationBootstrap, type OnApplicationShutdown } from '@nestjs/common';
import { ZLINK_ROUTE_MESH_RUNTIME } from '@zlink-systems/nestjs';
import type { ZLinkRouteMeshRuntime } from '@zlink-systems/framework';
import { ZONEWORLD_CONFIG } from '../../../../Configuration/configuration';
import type { ZoneWorldConfiguration } from '../../../../Configuration/configuration';
import { ZoneWorldNames } from '../../../../../Shared/spec';

@Injectable()
class SpotRuntimeStatusObserver implements OnApplicationBootstrap, OnApplicationShutdown {
  private readonly stop = new AbortController();

  constructor(
    @Inject(ZONEWORLD_CONFIG) private readonly config: ZoneWorldConfiguration,
    @Inject(ZLINK_ROUTE_MESH_RUNTIME) private readonly routeMeshRuntime: ZLinkRouteMeshRuntime
  ) {}

  onApplicationBootstrap(): void {
    void this.observeReadiness();
  }

  onApplicationShutdown(): void {
    this.stop.abort();
  }

  private async observeReadiness(): Promise<void> {
    const nodeId = this.config.zoneNode?.nodeId;
    if (nodeId === undefined) return;
    for await (const status of this.routeMeshRuntime.observe(ZoneWorldNames.zoneMesh, 64, this.stop.signal)) {
      console.log(
        `mesh status node=${nodeId} state=${status.state}`
        + ` readyPeers=${status.readyPeerCount}`
      );
    }
  }

}

export { SpotRuntimeStatusObserver };
