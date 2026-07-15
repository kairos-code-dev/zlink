import { Inject, Injectable } from '@nestjs/common';
import type { ZLinkDrainControl } from '@zlink-systems/framework';
import { ZLINK_DRAIN_CONTROL } from './tokens';

@Injectable()
export class ZLinkDrainHealthIndicator {
  constructor(@Inject(ZLINK_DRAIN_CONTROL) private readonly drain: ZLinkDrainControl) {}

  async isHealthy(key = 'zlink'): Promise<Record<string, { readonly status: 'up' }>> {
    if (!this.drain.isReady()) {
      throw new Error(`${key} is draining.`);
    }
    return { [key]: { status: 'up' } };
  }
}
