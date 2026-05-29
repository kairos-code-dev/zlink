// SPDX-License-Identifier: MPL-2.0

import { SubSocketOptions } from './socket_options';
import {
  NativeSocketType,
  SubscriberSocket,
  requireNative,
  configCall,
  type Context,
} from './socket_operations';

export class SubSocket extends SubscriberSocket {
  readonly options: SubSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.SUB); this.options = SubSocketOptions.create(this); }
  attachDiscovery(discovery: { nativeHandle(): unknown }): void {
    configCall('socket discovery attachment failed', () => {
      requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle());
    });
  }
}
