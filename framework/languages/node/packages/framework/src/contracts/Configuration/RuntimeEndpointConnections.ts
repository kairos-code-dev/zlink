import type { ZLinkEndpointConnections } from './Connections';

interface ConnectableEndpointSocket {
  connect(endpoint: string): void;
  disconnect(endpoint: string): void;
}

const controllers = new WeakMap<object, RuntimeEndpointConnections>();

class RuntimeEndpointConnections implements ZLinkEndpointConnections {
  private socket?: ConnectableEndpointSocket;

  constructor(private readonly endpoints: string[]) {}

  connect(endpoint: string): void {
    if (!this.endpoints.includes(endpoint)) {
      this.endpoints.push(endpoint);
      this.socket?.connect(endpoint);
    }
  }

  disconnect(endpoint: string): void {
    const index = this.endpoints.indexOf(endpoint);
    if (index >= 0) {
      this.endpoints.splice(index, 1);
      this.socket?.disconnect(endpoint);
    }
  }

  listConnections(): readonly string[] {
    return Object.freeze([...this.endpoints]);
  }

  attach(socket: ConnectableEndpointSocket): void {
    this.socket = socket;
  }
}

export function endpointConnections(owner: object, endpoints: string[]): ZLinkEndpointConnections {
  let controller = controllers.get(owner);
  if (controller === undefined) {
    controller = new RuntimeEndpointConnections(endpoints);
    controllers.set(owner, controller);
  }
  return controller;
}

export function attachEndpointConnections(owner: object, socket: ConnectableEndpointSocket): void {
  const controller = controllers.get(owner);
  controller?.attach(socket);
}
