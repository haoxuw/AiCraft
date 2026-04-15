// Browser-side WebSocket client for CellCraft MP. Wraps a WS with:
//   - Auto-reconnect with exponential backoff (disabled during normal close)
//   - Ping/pong for latency estimate
//   - Outbound action seq counter + pending buffer (for reconciliation)
//   - Queue of inbound ServerMsgs pulled from the main loop
//
// Reconciliation + prediction logic lives in match_scene.ts — this layer
// just ferries messages reliably.

import {
  ClientMsg,
  decodeServer,
  encode,
  ServerMsg,
  C_JOIN,
  C_ACTION
} from './protocol';

export type NetStatus = 'connecting' | 'open' | 'closed' | 'reconnecting';

export interface PendingAction {
  seq: number;
  msg: C_ACTION;
  sentAt: number;     // performance.now()
  clientPos: [number, number] | null;
}

export interface NetClientOpts {
  url: string;
  onStatus?: (s: NetStatus) => void;
  // If false, the client will not try to reconnect on close.
  autoReconnect?: boolean;
}

const PING_INTERVAL_MS = 2000;
const RECONNECT_BASE_MS = 500;
const RECONNECT_MAX_MS = 8000;

export class NetClient {
  private ws: WebSocket | null = null;
  private readonly opts: NetClientOpts;
  private status: NetStatus = 'connecting';
  private seq = 1;
  private inbox: ServerMsg[] = [];
  private pingTimer: ReturnType<typeof setInterval> | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private reconnectDelay = RECONNECT_BASE_MS;
  private stopped = false;

  readonly pending = new Map<number, PendingAction>();
  pingMs = 0;
  lastPongAt = 0;

  constructor(opts: NetClientOpts) {
    this.opts = opts;
    this.connect();
  }

  // --- Status + teardown ---------------------------------------------

  getStatus(): NetStatus {
    return this.status;
  }

  close(): void {
    this.stopped = true;
    if (this.pingTimer) clearInterval(this.pingTimer);
    this.pingTimer = null;
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
    this.reconnectTimer = null;
    if (this.ws) {
      try {
        this.ws.close();
      } catch {
        /* ignore */
      }
    }
    this.setStatus('closed');
  }

  private setStatus(s: NetStatus): void {
    if (this.status === s) return;
    this.status = s;
    if (this.opts.onStatus) this.opts.onStatus(s);
  }

  // --- Connect / reconnect -------------------------------------------

  private connect(): void {
    if (this.stopped) return;
    try {
      this.ws = new WebSocket(this.opts.url);
    } catch {
      this.scheduleReconnect();
      return;
    }
    this.setStatus('connecting');
    this.ws.onopen = () => {
      this.reconnectDelay = RECONNECT_BASE_MS;
      this.setStatus('open');
      if (this.pingTimer) clearInterval(this.pingTimer);
      this.pingTimer = setInterval(() => this.ping(), PING_INTERVAL_MS);
    };
    this.ws.onmessage = (ev) => {
      const m = decodeServer(String(ev.data));
      if (!m) return;
      if (m.type === 'S_PONG') {
        this.pingMs = Math.max(0, performance.now() - m.t);
        this.lastPongAt = performance.now();
        return;
      }
      this.inbox.push(m);
    };
    this.ws.onclose = () => {
      if (this.pingTimer) clearInterval(this.pingTimer);
      this.pingTimer = null;
      if (this.stopped || this.opts.autoReconnect === false) {
        this.setStatus('closed');
        return;
      }
      this.scheduleReconnect();
    };
    this.ws.onerror = () => {
      // Will fire onclose right after — handled there.
    };
  }

  private scheduleReconnect(): void {
    this.setStatus('reconnecting');
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
    this.reconnectTimer = setTimeout(() => {
      this.reconnectDelay = Math.min(RECONNECT_MAX_MS, this.reconnectDelay * 2);
      this.connect();
    }, this.reconnectDelay);
  }

  // --- Sending -------------------------------------------------------

  private sendRaw(msg: ClientMsg): boolean {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return false;
    try {
      this.ws.send(encode(msg));
      return true;
    } catch {
      return false;
    }
  }

  sendJoin(join: Omit<C_JOIN, 'type'>): boolean {
    return this.sendRaw({ type: 'C_JOIN', ...join });
  }

  // Returns the seq assigned to this action, or -1 if not sent.
  sendAction(
    action: C_ACTION['action'],
    clientPos: [number, number] | null
  ): number {
    const seq = this.seq++;
    const msg: C_ACTION = {
      type: 'C_ACTION',
      seq,
      action,
      clientPos: clientPos ? [clientPos[0], clientPos[1]] : undefined
    };
    this.pending.set(seq, {
      seq,
      msg,
      sentAt: performance.now(),
      clientPos: clientPos ? [clientPos[0], clientPos[1]] : null
    });
    this.sendRaw(msg);
    return seq;
  }

  private ping(): void {
    this.sendRaw({ type: 'C_PING', t: performance.now() });
  }

  // --- Receiving -----------------------------------------------------

  // Drain inbound messages (scene polls this each frame / tick).
  drain(): ServerMsg[] {
    if (this.inbox.length === 0) return [];
    const out = this.inbox;
    this.inbox = [];
    return out;
  }

  // Prune pending actions with seq <= ack.
  ackUpTo(ack: number): void {
    for (const seq of this.pending.keys()) {
      if (seq <= ack) this.pending.delete(seq);
    }
  }
}
