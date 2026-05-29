// SPDX-License-Identifier: MPL-2.0

import { SubSocketOptions } from './socket_options';
import {
  SubscriberSocket,
} from './socket_operations';
import type { RuntimeContext as Context } from '../core/context';
import { configCall } from '../errors/native_errors';
import { requireNative } from '../native/native';
import { SocketType as NativeSocketType } from '../../contracts/sockets/socket_constants';

export class SubSocket extends SubscriberSocket {
  readonly options: SubSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.SUB); this.options = SubSocketOptions.create(this); }
  attachDiscovery(discovery: { nativeHandle(): unknown }): void {
    configCall('socket discovery attachment failed', () => {
      requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle());
    });
  }
}
