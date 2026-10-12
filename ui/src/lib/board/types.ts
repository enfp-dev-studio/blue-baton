/* Types for the plain-JS board modules (controller.js, store.js), as the React views use them */

export type Tone = 'ok' | 'busy' | 'bad' | 'idle';

export interface Host {
  host: number;
  addr: string;
  name: string;
  state: 'ready' | 'connected' | 'secured' | 'pairing' | 'offline';
  connected: boolean;
}

export interface Device {
  id: string;
  addr: string;
  name: string;
  kinds: string[];
  appearance?: number;
  state: string;
  paired: boolean;
  auto: boolean;
  lastSeq: number;
  bond?: string;
  error?: string;
  lastReason?: number;
}

export interface Found {
  addr: string;
  name: string;
  rssi: number;
  paired?: boolean;
  appearance?: number;
  hid?: boolean;
  state: string;
}

export interface Prompt {
  type: 'numcmp' | 'passkey_request' | 'passkey_display';
  addr: string;
  code: string;
}

export interface LogRow {
  at: Date;
  dir: string;
  text: string;
}

export interface BoardState {
  link: 'disconnected' | 'connecting' | 'connected';
  transport: string;
  fw: string | null;
  radio: string;
  scanning: boolean;
  scanPending?: boolean;
  scanUntil: number;
  bonds: number;
  bondsMax: number;
  inputs: number;
  inputsMax: number;
  activeHost: number;
  hosts: Map<number, Host>;
  hostWindow: { open: boolean; until: number };
  hostCandidate: { addr: string; state: string } | null;
  hostPrompt: { addr: string; len: number } | null;
  combos: string[];
  combosDefault: boolean;
  devices: Map<string, Device>;
  found: Map<string, Found>;
  showAll: boolean;
  addOpen: boolean;
  prompt: Prompt | null;
  notice: { kind: 'error' | 'info'; text: string; at: number } | null;
  log: LogRow[];
}

export interface Actions {
  connect(): Promise<void>;
  disconnectLink(): Promise<void>;
  monitor(on: boolean): void;
  dismiss(): void;
  openAdd(): void;
  closeAdd(): void;
  scan(on: boolean): Promise<void>;
  connectDevice(addr: string): Promise<void>;
  showAll(all: boolean): Promise<void>;
  selectHost(n: number): Promise<void>;
  openHostWindow(): Promise<void>;
  closeHostWindow(): Promise<void>;
  setCombos(list: string[]): Promise<boolean>;
  hostPasskey(code: string): Promise<void>;
  removeHost(n: number): Promise<void>;
  disconnectDevice(addr: string): Promise<void>;
  removeDevice(d: Device): Promise<void>;
  accept(yes: boolean): Promise<void>;
  passkey(code: string): Promise<void>;
}

export interface Controller {
  store: {
    state: BoardState;
    version: number;
    subscribe(fn: (s: BoardState) => void): () => void;
    changed(): void;
  };
  actions: Actions;
  monitorRoot: HTMLElement;
  inApp: boolean;
  needsSerial: boolean;
  useMock: boolean;
  role: 'main' | 'panel';
}
