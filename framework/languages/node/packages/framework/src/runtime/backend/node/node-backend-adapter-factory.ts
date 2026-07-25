import type { Context, TopicMessage } from '@zlink-systems/zlink';
import type {
  ZLinkBackendAdapterFactory,
  ZLinkBackendContext,
  ZLinkBackendDealerSocket,
  ZLinkBackendPublisherSocket,
  ZLinkBackendReadablePoller,
  ZLinkBackendRouterSocket,
  ZLinkBackendSocket,
  ZLinkBackendSocketMonitor,
  ZLinkBackendStreamSocket,
  ZLinkBackendSubscriberSocket,
  ZLinkChannelBackendAdapter,
  ZLinkMonitoringBackendAdapter,
  ZLinkStreamBackendAdapter
} from '../contracts';
import {
  closeWithBusyRetry,
  isContextTerminatedError,
  zlink,
  type ZLinkBindingModule
} from './node-backend-adapter-support';
import { wrapMonitorSocket } from './node-monitor-backend-adapter';
import { wrapSocket } from './node-socket-backend-adapter';
import { ZLinkNodeMeshBackendAdapter } from './node-mesh-backend-adapter';

export { isDisconnectRouteNotFoundError } from './node-socket-backend-adapter';

export class ZLinkNodeBackendAdapterFactory implements ZLinkBackendAdapterFactory {
  createChannelAdapter(): ZLinkChannelBackendAdapter {
    return new ZLinkNodeChannelBackendAdapter();
  }

  createMeshAdapter(): ZLinkNodeMeshBackendAdapter {
    return new ZLinkNodeMeshBackendAdapter();
  }

  createStreamAdapter(): ZLinkStreamBackendAdapter {
    return new ZLinkNodeStreamBackendAdapter();
  }

  createMonitoringAdapter(): ZLinkMonitoringBackendAdapter {
    return new ZLinkNodeMonitoringBackendAdapter();
  }
}

class ZLinkNodeChannelBackendAdapter implements ZLinkChannelBackendAdapter {
  createContext(): ZLinkBackendContext {
    return new ZLinkNodeBackendContext(zlink.createContext());
  }

  createTopicMessage(): TopicMessage {
    return new zlink.TopicMessage();
  }

  createDealerSocket(context: ZLinkBackendContext): ZLinkBackendDealerSocket {
    return wrapSocket(zlink.createDealerSocket(asNodeContext(context))) as unknown as ZLinkBackendDealerSocket;
  }

  createRouterSocket(context: ZLinkBackendContext): ZLinkBackendRouterSocket {
    return wrapSocket(zlink.createRouterSocket(asNodeContext(context))) as unknown as ZLinkBackendRouterSocket;
  }

  createPublisherSocket(context: ZLinkBackendContext): ZLinkBackendPublisherSocket {
    return wrapSocket(zlink.createPubSocket(asNodeContext(context))) as unknown as ZLinkBackendPublisherSocket;
  }

  createSubscriberSocket(context: ZLinkBackendContext): ZLinkBackendSubscriberSocket {
    return wrapSocket(zlink.createSubSocket(asNodeContext(context))) as unknown as ZLinkBackendSubscriberSocket;
  }

  createReadablePoller(socket: ZLinkBackendSubscriberSocket): ZLinkBackendReadablePoller {
    const poller = zlink.createPoller();
    const events = zlink.createPollEvents(1);
    poller.add(socket.nativeInstance as never, [zlink.PollEventFlag.PollIn], 0);
    return {
      wait(timeoutMs: number): boolean {
        return poller.wait(events, timeoutMs) > 0 && events.hasEvent(0, zlink.PollEventFlag.PollIn);
      },
      dispose(): void {
        poller.remove(socket.nativeInstance as never);
        events.close();
        poller.close();
      }
    };
  }
}

class ZLinkNodeStreamBackendAdapter implements ZLinkStreamBackendAdapter {
  createStreamSocket(context: ZLinkBackendContext): ZLinkBackendStreamSocket {
    return wrapSocket(zlink.createStreamSocket(asNodeContext(context))) as unknown as ZLinkBackendStreamSocket;
  }
}

class ZLinkNodeMonitoringBackendAdapter implements ZLinkMonitoringBackendAdapter {
  openSocketMonitor(socket: ZLinkBackendSocket): ZLinkBackendSocketMonitor {
    const nativeSocket = socket.nativeInstance as {
      monitorOpen(): ReturnType<ZLinkBindingModule['createDealerSocket']>['monitorOpen'] extends (...args: never[]) => infer T
        ? T
        : never;
    };
    return wrapMonitorSocket(nativeSocket.monitorOpen());
  }
}

class ZLinkNodeBackendContext implements ZLinkBackendContext {
  constructor(readonly nativeInstance: Context) {}

  shutdown(): void {
    this.nativeInstance.shutdown();
  }

  async dispose(): Promise<void> {
    // Terminal cleanup shuts the context down before terminating it, the same
    // sequence the .NET reference binding runs inside `Context.Dispose()`
    // (`zlink_ctx_shutdown` then `zlink_ctx_term`). Without the shutdown signal
    // termination can block forever waiting on the reaper even after every
    // socket this runtime owns has been closed.
    try {
      this.nativeInstance.shutdown();
    } catch (error) {
      if (!isContextTerminatedError(error)) {
        throw error;
      }
    }
    await closeWithBusyRetry(this.nativeInstance);
  }

  close(): void {
    this.nativeInstance.close();
  }
}

function asNodeContext(context: ZLinkBackendContext): Context {
  return context.nativeInstance as Context;
}
