// SPDX-License-Identifier: MPL-2.0

import { SubSocketOptions } from './socket_options';
import {
  SubscriberSocket,
} from './socket_operations';
import { configureSocketChannelName } from './socket_base';
import type { RuntimeContext as Context } from '../core/context';
import { configCall } from '../errors/native_errors';
import { getNativeHandle, NativeHandle } from '../handles/native_handle';
import { requireNative } from '../native/native';
import { SocketType as NativeSocketType } from '../../contracts/sockets/socket_constants';

export class SubSocket extends SubscriberSocket {
  readonly options: SubSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.SUB); this.options = SubSocketOptions.create(this); }
  setChannelName(channelName: string): void {
    configureSocketChannelName(this, channelName);
  }
  attachDiscovery(discovery: NativeHandle): void {
    configCall('socket discovery attachment failed', () => {
      requireNative().socketAttachDiscovery(getNativeHandle(this), getNativeHandle(discovery));
    });
  }
}
