import { Inject, Injectable, type OnApplicationBootstrap, type OnApplicationShutdown } from '@nestjs/common';
import type { ZLinkRouteMeshRuntime } from '@zlink-systems/framework';
import { ZLINK_ROUTE_MESH_RUNTIME } from '@zlink-systems/nestjs';
import { SampleNames } from './sample-names';

@Injectable()
class RoomRouterReadinessHandler implements OnApplicationBootstrap, OnApplicationShutdown {
  private readonly stop = new AbortController();

  constructor(@Inject(ZLINK_ROUTE_MESH_RUNTIME) private readonly runtime: ZLinkRouteMeshRuntime) {}

  onApplicationBootstrap(): void {
    void this.observeReadiness();
  }

  onApplicationShutdown(): void {
    this.stop.abort();
  }

  private async observeReadiness(): Promise<void> {
    for await (const event of this.runtime.observe(SampleNames.roomSpotNode, 64, this.stop.signal)) {
      if (event.identifier !== 'zlink.runtime.mesh_node.peer_changed') continue;
      console.log(
        `bingo-room-peer-state rid=${event.peerRid ?? '-'} generation=${event.lifecycleGeneration ?? 0n} `
        + `reason=${event.reason ?? '-'}`
      );
    }
  }
}

export { RoomRouterReadinessHandler };
