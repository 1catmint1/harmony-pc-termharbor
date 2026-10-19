export interface TerminalSnapshot {
  scrollbackRows: string[];
  rows: string[];
  cells: TerminalCell[][];
  cursorRow: number;
  cursorCol: number;
}

export interface TerminalCell {
  text: string;
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
}

export function createSession(rows: number, cols: number): number;
export function writeSession(id: number, text: string): void;
export function connectTcpSession(id: number, host: string, port: number, timeoutMs?: number): void;
export function connectTcpSessionWithProxy(
  id: number,
  host: string,
  port: number,
  proxy: ProxyOptions,
  timeoutMs?: number,
): void;
export function connectTelnetSession(id: number, host: string, port: number, timeoutMs?: number): void;
export function connectTelnetSessionWithProxy(
  id: number,
  host: string,
  port: number,
  proxy: ProxyOptions,
  timeoutMs?: number,
): void;
export function connectSerialSession(id: number, options: SerialConnectionOptions): void;
export function listSerialDevices(): string[];
export function connectSshSession(id: number, options: SshConnectionOptions): SshConnectionResult;
export function scpDownload(options: SshFileTransferOptions): SshFileTransferResult;
export function scpUpload(options: SshFileTransferOptions): SshFileTransferResult;
export function sftpDownload(options: SshFileTransferOptions): SshFileTransferResult;
export function sftpUpload(options: SshFileTransferOptions): SshFileTransferResult;
export function startSshLocalForward(options: SshPortForwardOptions): SshPortForwardStartResult;
export function stopSshLocalForward(forwardId: number): void;
export function sendInput(id: number, text: string): void;
export function pumpSession(id: number, maxBytes?: number): SessionPumpResult;
export function disconnectSession(id: number): void;
export function drainTranscript(id: number): string;
export function readOutputHistory(id: number): string;
export function startMacro(id: number, script: string, options?: MacroOptions): MacroStepResult;
export function pumpMacro(macroId: number): MacroStepResult;
export function stopMacro(macroId: number): void;
export function resizeSession(id: number, rows: number, cols: number): void;
export function readSnapshot(id: number): TerminalSnapshot;
export function closeSession(id: number): void;
export function importIniProfiles(iniText: string): ImportedProfile[];
export function parseKnownHosts(knownHostsText: string): KnownHostEntry[];
export function checkKnownHost(
  knownHostsText: string,
  host: string,
  port: number,
  keyType: string,
  keyBase64: string,
): KnownHostMatch;
export function updateKnownHosts(
  knownHostsText: string,
  host: string,
  port: number,
  keyType: string,
  keyBase64: string,
  comment?: string,
): string;

export interface LocalExperienceInfo {
  scpTransfers: number;
  sftpRequests: number;
  host: string;
  username: string;
  password: string;
  remotePath: string;
  tcpPort: number;
  sshPort: number;
  tcpEchoes: number;
  sshLogins: number;
  fileReads: number;
  fileWrites: number;
}

export function startLocalExperience(): LocalExperienceInfo;
export function stopLocalExperience(): LocalExperienceInfo;
export function localExperienceStatus(): LocalExperienceInfo;
