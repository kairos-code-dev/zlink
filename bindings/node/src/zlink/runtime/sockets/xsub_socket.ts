// SPDX-License-Identifier: MPL-2.0

import { SubSocketOptions } from './socket_options';
import {
  NativeSocketType,
  SubscriberSocket,
  type Context,
} from './socket_operations';

export class XSubSocket extends SubscriberSocket {
  readonly options: SubSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.XSUB); this.options = SubSocketOptions.create(this); }
}
