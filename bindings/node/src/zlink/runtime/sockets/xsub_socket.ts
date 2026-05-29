// SPDX-License-Identifier: MPL-2.0

import { SubSocketOptions } from './socket_options';
import {
  SubscriberSocket,
} from './socket_operations';
import {
  NativeSocketType,
  type Context,
} from './socket_runtime_support';

export class XSubSocket extends SubscriberSocket {
  readonly options: SubSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.XSUB); this.options = SubSocketOptions.create(this); }
}
