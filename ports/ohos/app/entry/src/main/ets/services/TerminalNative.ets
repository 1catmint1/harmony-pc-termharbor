import type { ImportedProfile, KnownHostEntry, KnownHostMatch, MacroOptions, MacroStepResult, ProxyOptions, SerialConnectionOptions, SessionPumpResult, SshConnectionOptions, SshConnectionResult, SshFileTransferOptions, SshFileTransferResult, SshPortForwardOptions, SshPortForwardStartResult, TerminalSnapshot } from '../model/TerminalTypes';

import terminalNative from 'libteraterm_ohos.so';

interface NativeTerminalModule {
  createSession(rows: number, cols: number): number;
  writeSession(id: number, text: string): void;
  connectTcpSession(id: number, host: string, port: number, timeoutMs?: number): void;
  connectTcpSessionWithProxy(id: number, host: string, port: number, proxy: ProxyOptions, timeoutMs?: number): void;
  connectTelnetSession(id: number, host: string, port: number, timeoutMs?: number): void;
  connectTelnetSessionWithProxy(id: number, host: string, port: number, proxy: ProxyOptions, timeoutMs?: number): void;
  connectSerialSession(id: number, options: SerialConnectionOptions): void;
  listSerialDevices(): string[];
  connectSshSession(id: number, options: SshConnectionOptions): SshConnectionResult;
  scpDownload(options: SshFileTransferOptions): SshFileTransferResult;
  scpUpload(options: SshFileTransferOptions): SshFileTransferResult;
  sftpDownload(options: SshFileTransferOptions): SshFileTransferResult;
  sftpUpload(options: SshFileTransferOptions): SshFileTransferResult;
  startSshLocalForward(options: SshPortForwardOptions): SshPortForwardStartResult;
  stopSshLocalForward(forwardId: number): void;
  sendInput(id: number, text: string): void;
  pumpSession(id: number, maxBytes?: number): SessionPumpResult;
  disconnectSession(id: number): void;
  drainTranscript(id: number): string;
  readOutputHistory(id: number): string;
  startMacro(id: number, script: string, options?: MacroOptions): MacroStepResult;
  pumpMacro(macroId: number): MacroStepResult;
  respondMacroDialog(macroId: number, accepted: boolean, value: string): MacroStepResult;
  stopMacro(macroId: number): void;
  resizeSession(id: number, rows: number, cols: number): void;
  readSnapshot(id: number): TerminalSnapshot;
  closeSession(id: number): void;
  importIniProfiles(iniText: string): ImportedProfile[];
  parseKnownHosts(knownHostsText: string): KnownHostEntry[];
  checkKnownHost(knownHostsText: string, host: string, port: number, keyType: string, keyBase64: string): KnownHostMatch;
  updateKnownHosts(knownHostsText: string, host: string, port: number, keyType: string, keyBase64: string, comment?: string): string;
}

const nativeModule = terminalNative as NativeTerminalModule;

export class TerminalNative {
  static createSession(rows: number, cols: number): number {
    return nativeModule.createSession(rows, cols);
  }

  static connectTcp(id: number, host: string, port: number, timeoutMs: number = 5000): void {
    nativeModule.connectTcpSession(id, host, port, timeoutMs);
  }

  static connectTcpViaProxy(id: number,
                            host: string,
                            port: number,
                            proxy: ProxyOptions,
                            timeoutMs: number = 5000): void {
    nativeModule.connectTcpSessionWithProxy(id, host, port, proxy, timeoutMs);
  }

  static connectTelnet(id: number, host: string, port: number, timeoutMs: number = 5000): void {
    nativeModule.connectTelnetSession(id, host, port, timeoutMs);
  }

  static connectTelnetViaProxy(id: number,
                               host: string,
                               port: number,
                               proxy: ProxyOptions,
                               timeoutMs: number = 5000): void {
    nativeModule.connectTelnetSessionWithProxy(id, host, port, proxy, timeoutMs);
  }

  static connectSerial(id: number, options: SerialConnectionOptions): void {
    nativeModule.connectSerialSession(id, options);
  }

  static listSerialDevices(): string[] {
    return nativeModule.listSerialDevices();
  }

  static connectSsh(id: number, options: SshConnectionOptions): SshConnectionResult {
    return nativeModule.connectSshSession(id, options);
  }

  static scpDownload(options: SshFileTransferOptions): SshFileTransferResult {
    return nativeModule.scpDownload(options);
  }

  static scpUpload(options: SshFileTransferOptions): SshFileTransferResult {
    return nativeModule.scpUpload(options);
  }

  static sftpDownload(options: SshFileTransferOptions): SshFileTransferResult {
    return nativeModule.sftpDownload(options);
  }

  static sftpUpload(options: SshFileTransferOptions): SshFileTransferResult {
    return nativeModule.sftpUpload(options);
  }

  static startSshLocalForward(options: SshPortForwardOptions): SshPortForwardStartResult {
    return nativeModule.startSshLocalForward(options);
  }

  static stopSshLocalForward(forwardId: number): void {
    nativeModule.stopSshLocalForward(forwardId);
  }

  static sendInput(id: number, text: string): void {
    nativeModule.sendInput(id, text);
  }

  static pump(id: number, maxBytes: number = 65536): SessionPumpResult {
    return nativeModule.pumpSession(id, maxBytes);
  }

  static resize(id: number, rows: number, cols: number): void {
    nativeModule.resizeSession(id, rows, cols);
  }

  static snapshot(id: number): TerminalSnapshot {
    return nativeModule.readSnapshot(id);
  }

  static disconnect(id: number): void {
    nativeModule.disconnectSession(id);
  }

  static drainTranscript(id: number): string {
    return nativeModule.drainTranscript(id);
  }

  static outputHistory(id: number): string {
    return nativeModule.readOutputHistory(id);
  }

  static startMacro(id: number, script: string, options?: MacroOptions): MacroStepResult {
    return nativeModule.startMacro(id, script, options);
  }

  static pumpMacro(macroId: number): MacroStepResult {
    return nativeModule.pumpMacro(macroId);
  }

  static respondMacroDialog(macroId: number, accepted: boolean, value: string): MacroStepResult {
    return nativeModule.respondMacroDialog(macroId, accepted, value);
  }

  static stopMacro(macroId: number): void {
    nativeModule.stopMacro(macroId);
  }

  static close(id: number): void {
    nativeModule.closeSession(id);
  }

  static importIniProfiles(iniText: string): ImportedProfile[] {
    return nativeModule.importIniProfiles(iniText);
  }

  static parseKnownHosts(knownHostsText: string): KnownHostEntry[] {
    return nativeModule.parseKnownHosts(knownHostsText);
  }

  static checkKnownHost(knownHostsText: string,
                        host: string,
                        port: number,
                        keyType: string,
                        keyBase64: string): KnownHostMatch {
    return nativeModule.checkKnownHost(knownHostsText, host, port, keyType, keyBase64);
  }

  static updateKnownHosts(knownHostsText: string,
                          host: string,
                          port: number,
                          keyType: string,
                          keyBase64: string,
                          comment: string = ''): string {
    return nativeModule.updateKnownHosts(knownHostsText, host, port, keyType, keyBase64, comment);
  }
}
