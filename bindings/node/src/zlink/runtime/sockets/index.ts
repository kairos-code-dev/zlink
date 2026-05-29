// SPDX-License-Identifier: MPL-2.0

import type { PairSocket } from './pair_socket';
import type { PubSocket } from './pub_socket';
import type { XPubSocket } from './xpub_socket';
import type { SubSocket } from './sub_socket';
import type { XSubSocket } from './xsub_socket';
import type { DealerSocket } from './dealer_socket';
import type { RouterSocket } from './router_socket';
import type { StreamSocket } from './stream_socket';

export {
  CommonSocketOptions as DefaultCommonSocketOptions,
  DealerSocketOptions as DefaultDealerSocketOptions,
  PubSocketOptions as DefaultPubSocketOptions,
  RouterSocketOptions as DefaultRouterSocketOptions,
  StreamSocketOptions as DefaultStreamSocketOptions,
  SubSocketOptions as DefaultSubSocketOptions,
} from './socket_options';
export { MonitorSocket as DefaultMonitorSocket } from '../eventing/monitor_socket';
export { PairSocket as DefaultPairSocket } from './pair_socket';
export { PubSocket as DefaultPubSocket } from './pub_socket';
export { XPubSocket as DefaultXPubSocket } from './xpub_socket';
export { SubSocket as DefaultSubSocket } from './sub_socket';
export { XSubSocket as DefaultXSubSocket } from './xsub_socket';
export { DealerSocket as DefaultDealerSocket } from './dealer_socket';
export { RouterSocket as DefaultRouterSocket } from './router_socket';
export { StreamSocket as DefaultStreamSocket } from './stream_socket';

export type BaseSocket =
  | PairSocket
  | PubSocket
  | XPubSocket
  | SubSocket
  | XSubSocket
  | DealerSocket
  | RouterSocket
  | StreamSocket;
export type { BaseSocket as DefaultBaseSocket };
