export type ProtocolMode = 'tcp' | 'telnet' | 'ssh' | 'serial';
export type TerminalColorTheme = 'dark' | 'light' | 'classic';

export interface TerminalSnapshot {
  scrollbackRows: string[];
  rows: string[];
  cells: TerminalCell[][];
  cursorRow: number;
  cursorCol: number;
}

export interface TerminalCell {
  text: string;
  columns?: number;
  foreground: number;
  background: number;
  bold: boolean;
  underline: boolean;
  inverse: boolean;
}

export interface SessionPumpResult {
  bytesRead: number;
  connected: boolean;
  error: string;
}

export type ProxyType = 'none' | 'http' | 'telnet' | 'socks4' | 'socks5';
export type SerialParity = 'none' | 'odd' | 'even';
export type SerialFlowControl = 'none' | 'software' | 'hardware';

export interface ProxyOptions {
  type: ProxyType;
  host: string;
  port: number;
  username?: string;
  password?: string;
  telnetHostnamePrompt?: string;
  telnetUsernamePrompt?: string;
  telnetPasswordPrompt?: string;
  telnetConnectedMessage?: string;
  telnetErrorMessage?: string;
}

export interface SerialConnectionOptions {
  devicePath: string;
  baudRate?: number;
  dataBits?: number;
  stopBits?: number;
  parity?: SerialParity;
  flowControl?: SerialFlowControl;
  timeoutMs?: number;
}

export interface ConnectProfile {
  protocol: ProtocolMode;
  host: string;
  port: number;
  proxy?: ProxyOptions;
  serial?: SerialConnectionOptions;
}

export interface ImportedProfile {
  name: string;
  protocol: 'tcp' | 'telnet' | 'ssh' | 'serial' | 'unsupported';
  host: string;
  port: number;
  username: string;
  password: string;
  publicKeyPath: string;
  privateKeyPath: string;
  passphrase: string;
  proxyType: ProxyType;
  proxyHost: string;
  proxyPort: number;
  proxyUsername: string;
  proxyPassword: string;
  serialDevicePath: string;
  baudRate: number;
  dataBits: number;
  stopBits: number;
  parity: SerialParity;
  flowControl: SerialFlowControl;
  setupFile: string;
  macroFile: string;
  source: string;
  warnings: string[];
}

export interface KnownHostEntry {
  marker: string;
  hosts: string[];
  keyType: string;
  keyBase64: string;
  comment: string;
  rawLine: string;
  warnings: string[];
}

export interface KnownHostMatch {
  status: 'trusted' | 'changed' | 'not_found' | 'unsupported';
  message: string;
  entry: KnownHostEntry;
}

export interface SshConnectionOptions {
  host: string;
  port?: number;
  username: string;
  password?: string;
  publicKeyPath?: string;
  privateKeyPath?: string;
  passphrase?: string;
  knownHostsText?: string;
  knownHostPolicy?: 'strict' | 'accept_new' | 'accept_changed';
  terminalType?: string;
  proxy?: ProxyOptions;
  timeoutMs?: number;
}

export interface SshConnectionResult {
  connected: boolean;
  error: string;
  keyType: string;
  keyBase64: string;
  updatedKnownHostsText: string;
  hostKeyMatch: KnownHostMatch;
}

export interface SshFileTransferOptions extends SshConnectionOptions {
  localPath: string;
  remotePath: string;
  mode?: number;
}

export interface SshFileTransferResult {
  success: boolean;
  error: string;
  bytesTransferred: number;
  updatedKnownHostsText: string;
  hostKeyMatch: KnownHostMatch;
}

export interface SshPortForwardOptions extends SshConnectionOptions {
  localHost?: string;
  localPort?: number;
  remoteHost: string;
  remotePort: number;
}

export interface SshPortForwardStartResult {
  started: boolean;
  error: string;
  forwardId: number;
  boundPort: number;
  updatedKnownHostsText: string;
  hostKeyMatch: KnownHostMatch;
}

export interface MacroOptions {
  knownHostsText?: string;
  knownHostPolicy?: 'strict' | 'accept_new' | 'accept_changed';
  connectTimeoutMs?: number;
}

export interface MacroStepResult {
  macroId: number;
  status: 'running' | 'waiting' | 'completed' | 'error';
  nextLine: number;
  waitMs: number;
  message: string;
  updatedKnownHostsText: string;
  dialogType: '' | 'messagebox' | 'yesnobox' | 'inputbox' | 'passwordbox' | 'listbox' | 'filenamebox' | 'dirnamebox';
  dialogTitle: string;
  dialogText: string;
  dialogDefaultText: string;
  dialogItems: string[];
  dialogSelectedIndex: number;
  dialogPassword: boolean;
  dialogSave: boolean;
  statusBoxAction: '' | 'show' | 'close';
  statusBoxTitle: string;
  statusBoxText: string;
}

export interface TerminalDisplaySettings {
  fontFamily: string;
  fontSize: number;
  lineHeight: number;
  colorTheme: TerminalColorTheme;
}
