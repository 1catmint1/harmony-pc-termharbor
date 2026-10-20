import { TerminalSurface } from '../components/TerminalSurface';
import common from '@ohos.app.ability.common';
import type { ConnectProfile, ImportedProfile, MacroStepResult, ProtocolMode, ProxyOptions, ProxyType, SerialConnectionOptions, SerialFlowControl, SerialParity, SshConnectionResult, SshFileTransferOptions, SshFileTransferResult, SshPortForwardOptions, SshPortForwardStartResult, TerminalCell, TerminalColorTheme, TerminalDisplaySettings } from '../model/TerminalTypes';
import { ClipboardService } from '../services/ClipboardService';
import { PersistentProfileStore } from '../services/PersistentProfileStore';
import { ProfileRepository } from '../services/ProfileRepository';
import { SandboxLogStore } from '../services/SandboxLogStore';
import { SandboxTransferStore } from '../services/SandboxTransferStore';
import { SessionLogBuffer } from '../services/SessionLogBuffer';
import { TerminalNative } from '../services/TerminalNative';
import { UserFileExportStore } from '../services/UserFileExportStore';
import { UserFileImportStore } from '../services/UserFileImportStore';
import { RELEASE_NOTICES, PRIVACY_NOTICE } from '../model/ReleaseNotices';

interface TerminalTabRecord {
  sessionId: number;
  title: string;
  protocol: ProtocolMode;
  host: string;
  port: string;
  username: string;
  password: string;
  publicKeyPath: string;
  privateKeyPath: string;
  passphrase: string;
  serialDataBits: number;
  serialStopBits: number;
  serialParity: SerialParity;
  serialFlowControl: SerialFlowControl;
  proxyEnabled: boolean;
  proxyType: ProxyType;
  proxyHost: string;
  proxyPort: string;
  proxyUsername: string;
  proxyPassword: string;
  inputText: string;
  scrollbackRows: string[];
  rows: string[];
  cells: TerminalCell[][];
  cursorRow: number;
  cursorCol: number;
  terminalRows: number;
  terminalCols: number;
  status: string;
  logSize: number;
  searchText: string;
  searchStatus: string;
  selectionStartLine: number;
  selectionEndLine: number;
  selectionStartCol: number;
  selectionEndCol: number;
  transferLocalPath: string;
  transferRemotePath: string;
  transferStatus: string;
  forwardLocalHost: string;
  forwardLocalPort: string;
  forwardRemoteHost: string;
  forwardRemotePort: string;
  forwardId: number;
  forwardStatus: string;
  macroScript: string;
  macroId: number;
  macroStatus: string;
  macroStatusBoxVisible: boolean;
  macroStatusBoxTitle: string;
  macroStatusBoxText: string;
}

@Extend(Button) function winCommandButton() {
  .height(32)
  .fontSize(13)
  .fontColor('#111111')
  .borderRadius(0)
  .borderWidth(1)
  .borderColor('#8E8E8E')
  .backgroundColor('#F0F0F0')
}

@Extend(TextInput) function winTextInput() {
  .height(30)
  .fontSize(13)
  .fontColor('#111111')
  .borderRadius(0)
  .borderWidth(1)
  .borderColor('#8E8E8E')
  .backgroundColor('#FFFFFF')
}

@Extend(TextArea) function winTextArea() {
  .fontSize(13)
  .fontColor('#111111')
  .borderRadius(0)
  .borderWidth(1)
  .borderColor('#8E8E8E')
  .backgroundColor('#FFFFFF')
}

@Entry
@Component
struct Index {
  @State private helpNoticeText: string = PRIVACY_NOTICE;
  @State private localExperienceText: string = '选择体验后自动连接本机服务；无需注册或配置外部服务器。';
  @State private tabs: TerminalTabRecord[] = [];
  @State private activeTabIndex: number = -1;
  @State private protocol: ProtocolMode = 'telnet';
  @State private host: string = '';
  @State private port: string = '23';
  @State private username: string = '';
  @State private password: string = '';
  @State private publicKeyPath: string = '';
  @State private privateKeyPath: string = '';
  @State private passphrase: string = '';
  @State private serialDataBits: number = 8;
  @State private serialStopBits: number = 1;
  @State private serialParity: SerialParity = 'none';
  @State private serialFlowControl: SerialFlowControl = 'none';
  @State private serialDeviceCandidates: string[] = [];
  @State private selectedSerialDeviceIndex: number = 0;
  @State private serialScanStatus: string = 'Serial devices not scanned';
  @State private proxyEnabled: boolean = false;
  @State private proxyType: ProxyType = 'http';
  @State private proxyHost: string = '';
  @State private proxyPort: string = '8080';
  @State private proxyUsername: string = '';
  @State private proxyPassword: string = '';
  @State private knownHostsText: string = '';
  @State private knownHostPolicy: 'strict' | 'accept_new' | 'accept_changed' = 'accept_new';
  @State private inputText: string = '';
  @State private scrollbackRows: string[] = [];
  @State private rows: string[] = [];
  @State private cells: TerminalCell[][] = [];
  @State private cursorRow: number = 0;
  @State private cursorCol: number = 0;
  @State private terminalRows: number = 30;
  @State private terminalCols: number = 100;
  @State private status: string = 'Disconnected';
  @State private iniImportText: string = '';
  @State private profileJson: string = '';
  @State private importedProfiles: ImportedProfile[] = [];
  @State private selectedProfileIndex: number = -1;
  @State private logSize: number = 0;
  @State private searchText: string = '';
  @State private searchStatus: string = 'Search idle';
  @State private selectionStartLine: number = -1;
  @State private selectionEndLine: number = -1;
  @State private selectionStartCol: number = -1;
  @State private selectionEndCol: number = -1;
  @State private transferLocalPath: string = '';
  @State private transferRemotePath: string = '';
  @State private transferStatus: string = 'Transfer idle';
  @State private forwardLocalHost: string = '127.0.0.1';
  @State private forwardLocalPort: string = '0';
  @State private forwardRemoteHost: string = '';
  @State private forwardRemotePort: string = '';
  @State private forwardId: number = 0;
  @State private forwardStatus: string = 'Forward idle';
  @State private macroScript: string = '';
  @State private macroId: number = 0;
  @State private macroStatus: string = 'Macro idle';
  @State private macroDialogType: string = '';
  @State private macroDialogTitle: string = '';
  @State private macroDialogText: string = '';
  @State private macroDialogValue: string = '';
  @State private macroDialogItems: string[] = [];
  @State private macroDialogSelectedIndex: number = -1;
  @State private macroDialogPassword: boolean = false;
  @State private macroDialogSave: boolean = false;
  @State private macroStatusBoxVisible: boolean = false;
  @State private macroStatusBoxTitle: string = '';
  @State private macroStatusBoxText: string = '';
  @State private terminalFontFamily: string = 'HarmonyOS Sans Mono';
  @State private terminalFontSize: number = 14;
  @State private terminalLineHeight: number = 20;
  @State private terminalColorTheme: TerminalColorTheme = 'dark';
  @State private terminalKeyCaptureText: string = '';
  @State private toolsVisible: boolean = false;
  @State private activeToolsPanel: string = '';

  private inputControllers: Map<string, TextInputController> = new Map();
  private areaControllers: Map<string, TextAreaController> = new Map();
  private leftControlDown: boolean = false;
  private rightControlDown: boolean = false;

  private inputController(name: string): TextInputController {
    let controller = this.inputControllers.get(name);
    if (controller === undefined) {
      controller = new TextInputController();
      this.inputControllers.set(name, controller);
    }
    return controller;
  }

  private areaController(name: string): TextAreaController {
    let controller = this.areaControllers.get(name);
    if (controller === undefined) {
      controller = new TextAreaController();
      this.areaControllers.set(name, controller);
    }
    return controller;
  }

  private resetShortcutModifiers(): void {
    this.leftControlDown = false;
    this.rightControlDown = false;
  }

  private rememberShortcutModifiers(event: KeyEvent): void {
    if (event.keyCode === 2072) {
      this.leftControlDown = event.type !== KeyType.Up;
    } else if (event.keyCode === 2073) {
      this.rightControlDown = event.type !== KeyType.Up;
    }
  }

  private handleTextShortcut(event: KeyEvent, name: string, text: string, area: boolean): boolean {
    this.rememberShortcutModifiers(event);
    if (event.type !== KeyType.Down || event.keyCode !== 2017 || !this.modifierPressed(event, 'ctrl')) {
      return false;
    }
    if (area) {
      this.areaController(name).setTextSelection(0, text.length);
    } else {
      this.inputController(name).setTextSelection(0, text.length);
    }
    event.stopPropagation();
    return true;
  }

  private selectAllTerminalOutput(): void {
    const lines = this.terminalLines();
    let lastLine = lines.length - 1;
    while (lastLine >= 0 && lines[lastLine].trim().length === 0) {
      lastLine--;
    }
    if (lastLine < 0) {
      this.status = 'Terminal output is empty';
      return;
    }
    this.selectionStartLine = 0;
    this.selectionStartCol = 0;
    this.selectionEndLine = lastLine;
    this.selectionEndCol = Math.max(0, lines[lastLine].length - 1);
    this.status = `Selected all terminal output (${lastLine + 1} lines)`;
    this.saveActiveTab();
  }

  private sessionId: number = 0;
  private timerId: number = 0;
  private sessionLogs: Map<number, SessionLogBuffer> = new Map();

  aboutToAppear() {
    this.createTab();
    this.restoreAppState();
    this.timerId = setInterval(() => {
      this.pumpAllSessions();
    }, 50);
  }

  aboutToDisappear() {
    TerminalNative.stopLocalExperience();
    if (this.timerId !== 0) {
      clearInterval(this.timerId);
      this.timerId = 0;
    }
    this.tabs.forEach((tab: TerminalTabRecord) => {
      if (tab.macroId !== 0) {
        TerminalNative.stopMacro(tab.macroId);
      }
      if (tab.forwardId !== 0) {
        this.stopForwardById(tab.forwardId);
      }
      TerminalNative.close(tab.sessionId);
    });
    this.sessionLogs.clear();
    this.tabs = [];
    this.activeTabIndex = -1;
    this.sessionId = 0;
  }

  private newTabTitle(sessionId: number): string {
    return `Session ${sessionId}`;
  }

  private appContext(): common.UIAbilityContext {
    return getContext(this) as common.UIAbilityContext;
  }

  private async restoreAppState(): Promise<void> {
    try {
      const state = await PersistentProfileStore.load(this.appContext());
      this.importedProfiles = ProfileRepository.connectableProfiles(state.profiles);
      this.profileJson = ProfileRepository.serialize(this.importedProfiles);
      this.knownHostsText = state.knownHostsText;
      this.applyDisplaySettings(state.displaySettings);
      this.status = this.importedProfiles.length > 0 ? `Loaded ${this.importedProfiles.length} profiles` :
        this.status;
    } catch (error) {
      this.status = `${error}`;
    }
    this.saveActiveTab();
  }

  private async saveAppState(): Promise<void> {
    try {
      await PersistentProfileStore.save(
        this.appContext(), this.importedProfiles, this.knownHostsText, this.currentDisplaySettings());
    } catch (error) {
      this.status = `${error}`;
    }
  }

  private stopForwardById(forwardId: number): boolean {
    try {
      TerminalNative.stopSshLocalForward(forwardId);
      return true;
    } catch (error) {
      this.forwardStatus = `${error}`;
      return false;
    }
  }

  private currentDisplaySettings(): TerminalDisplaySettings {
    return {
      fontFamily: this.terminalFontFamily,
      fontSize: this.terminalFontSize,
      lineHeight: this.terminalLineHeight,
      colorTheme: this.terminalColorTheme,
    };
  }

  private applyDisplaySettings(settings: TerminalDisplaySettings) {
    this.terminalFontFamily = settings.fontFamily;
    this.terminalFontSize = settings.fontSize;
    this.terminalLineHeight = settings.lineHeight;
    this.terminalColorTheme = settings.colorTheme;
  }

  private setTerminalFontSize(fontSize: number) {
    const normalized = Math.max(10, Math.min(24, Math.round(fontSize)));
    this.terminalFontSize = normalized;
    this.terminalLineHeight = normalized + 6;
    this.saveAppState();
  }

  private setTerminalTheme(theme: TerminalColorTheme) {
    this.terminalColorTheme = theme;
    this.saveAppState();
  }

  private cycleTerminalFontFamily() {
    if (this.terminalFontFamily === 'HarmonyOS Sans Mono') {
      this.terminalFontFamily = 'Courier New';
    } else if (this.terminalFontFamily === 'Courier New') {
      this.terminalFontFamily = 'monospace';
    } else {
      this.terminalFontFamily = 'HarmonyOS Sans Mono';
    }
    this.saveAppState();
  }

  private openToolsPanel(panel: string) {
    if (this.toolsVisible && this.activeToolsPanel === panel) {
      this.toolsVisible = false;
      this.activeToolsPanel = '';
      return;
    }
    this.toolsVisible = true;
    this.activeToolsPanel = panel;
  }

  private toggleDefaultToolsPanel() {
    if (this.toolsVisible) {
      this.toolsVisible = false;
      this.activeToolsPanel = '';
      return;
    }
    this.toolsVisible = true;
    this.activeToolsPanel = this.protocol === 'ssh' ? 'connection' : 'setup';
  }

  private createTab() {
    this.saveActiveTab();
    const sessionId = TerminalNative.createSession(30, 100);
    const snapshot = TerminalNative.snapshot(sessionId);
    const tab: TerminalTabRecord = {
      sessionId,
      title: this.newTabTitle(sessionId),
      protocol: 'telnet',
      host: '',
      port: '23',
      username: '',
      password: '',
      publicKeyPath: '',
      privateKeyPath: '',
      passphrase: '',
      serialDataBits: 8,
      serialStopBits: 1,
      serialParity: 'none',
      serialFlowControl: 'none',
      proxyEnabled: false,
      proxyType: 'http',
      proxyHost: '',
      proxyPort: '8080',
      proxyUsername: '',
      proxyPassword: '',
      inputText: '',
      scrollbackRows: snapshot.scrollbackRows,
      rows: snapshot.rows,
      cells: snapshot.cells,
      cursorRow: snapshot.cursorRow,
      cursorCol: snapshot.cursorCol,
      terminalRows: 30,
      terminalCols: 100,
      status: 'Disconnected',
      logSize: 0,
      searchText: '',
      searchStatus: 'Search idle',
      selectionStartLine: -1,
      selectionEndLine: -1,
      selectionStartCol: -1,
      selectionEndCol: -1,
      transferLocalPath: '',
      transferRemotePath: '',
      transferStatus: 'Transfer idle',
      forwardLocalHost: '127.0.0.1',
      forwardLocalPort: '0',
      forwardRemoteHost: '',
      forwardRemotePort: '',
      forwardId: 0,
      forwardStatus: 'Forward idle',
      macroScript: '',
      macroId: 0,
      macroStatus: 'Macro idle',
      macroStatusBoxVisible: false,
      macroStatusBoxTitle: '',
      macroStatusBoxText: '',
    };
    this.sessionLogs.set(sessionId, new SessionLogBuffer());
    this.tabs = this.tabs.concat([tab]);
    this.activeTabIndex = this.tabs.length - 1;
    this.loadActiveTab();
  }

  private closeActiveTab() {
    if (this.activeTabIndex < 0 || this.activeTabIndex >= this.tabs.length) {
      return;
    }

    const tab = this.tabs[this.activeTabIndex];
    if (tab.macroId !== 0) {
      TerminalNative.stopMacro(tab.macroId);
    }
    if (tab.forwardId !== 0) {
      this.stopForwardById(tab.forwardId);
    }
    TerminalNative.close(tab.sessionId);
    this.sessionLogs.delete(tab.sessionId);
    this.tabs.splice(this.activeTabIndex, 1);

    if (this.tabs.length === 0) {
      this.activeTabIndex = -1;
      this.sessionId = 0;
      this.createTab();
      return;
    }

    this.activeTabIndex = Math.min(this.activeTabIndex, this.tabs.length - 1);
    this.tabs = this.tabs.slice();
    this.loadActiveTab();
  }

  private switchTab(index: number) {
    if (index < 0 || index >= this.tabs.length || index === this.activeTabIndex) {
      return;
    }
    this.saveActiveTab();
    this.activeTabIndex = index;
    this.loadActiveTab();
  }

  private saveActiveTab() {
    if (this.activeTabIndex < 0 || this.activeTabIndex >= this.tabs.length || this.sessionId === 0) {
      return;
    }
    const title = this.host.trim().length > 0 ? this.host.trim() : this.tabs[this.activeTabIndex].title;
    this.tabs[this.activeTabIndex] = {
      sessionId: this.sessionId,
      title,
      protocol: this.protocol,
      host: this.host,
      port: this.port,
      username: this.username,
      password: this.password,
      publicKeyPath: this.publicKeyPath,
      privateKeyPath: this.privateKeyPath,
      passphrase: this.passphrase,
      serialDataBits: this.serialDataBits,
      serialStopBits: this.serialStopBits,
      serialParity: this.serialParity,
      serialFlowControl: this.serialFlowControl,
      proxyEnabled: this.proxyEnabled,
      proxyType: this.proxyType,
      proxyHost: this.proxyHost,
      proxyPort: this.proxyPort,
      proxyUsername: this.proxyUsername,
      proxyPassword: this.proxyPassword,
      inputText: this.inputText,
      scrollbackRows: this.scrollbackRows,
      rows: this.rows,
      cells: this.cells,
      cursorRow: this.cursorRow,
      cursorCol: this.cursorCol,
      terminalRows: this.terminalRows,
      terminalCols: this.terminalCols,
      status: this.status,
      logSize: this.logSize,
      searchText: this.searchText,
      searchStatus: this.searchStatus,
      selectionStartLine: this.selectionStartLine,
      selectionEndLine: this.selectionEndLine,
      selectionStartCol: this.selectionStartCol,
      selectionEndCol: this.selectionEndCol,
      transferLocalPath: this.transferLocalPath,
      transferRemotePath: this.transferRemotePath,
      transferStatus: this.transferStatus,
      forwardLocalHost: this.forwardLocalHost,
      forwardLocalPort: this.forwardLocalPort,
      forwardRemoteHost: this.forwardRemoteHost,
      forwardRemotePort: this.forwardRemotePort,
      forwardId: this.forwardId,
      forwardStatus: this.forwardStatus,
      macroScript: this.macroScript,
      macroId: this.macroId,
      macroStatus: this.macroStatus,
      macroStatusBoxVisible: this.macroStatusBoxVisible,
      macroStatusBoxTitle: this.macroStatusBoxTitle,
      macroStatusBoxText: this.macroStatusBoxText,
    };
    this.tabs = this.tabs.slice();
  }

  private loadActiveTab() {
    if (this.activeTabIndex < 0 || this.activeTabIndex >= this.tabs.length) {
      return;
    }
    const tab = this.tabs[this.activeTabIndex];
    this.sessionId = tab.sessionId;
    this.protocol = tab.protocol;
    this.host = tab.host;
    this.port = tab.port;
    this.username = tab.username;
    this.password = tab.password;
    this.publicKeyPath = tab.publicKeyPath;
    this.privateKeyPath = tab.privateKeyPath;
    this.passphrase = tab.passphrase;
    this.serialDataBits = tab.serialDataBits;
    this.serialStopBits = tab.serialStopBits;
    this.serialParity = tab.serialParity;
    this.serialFlowControl = tab.serialFlowControl;
    this.proxyEnabled = tab.proxyEnabled;
    this.proxyType = tab.proxyType;
    this.proxyHost = tab.proxyHost;
    this.proxyPort = tab.proxyPort;
    this.proxyUsername = tab.proxyUsername;
    this.proxyPassword = tab.proxyPassword;
    this.inputText = tab.inputText;
    this.scrollbackRows = tab.scrollbackRows;
    this.rows = tab.rows;
    this.cells = tab.cells;
    this.cursorRow = tab.cursorRow;
    this.cursorCol = tab.cursorCol;
    this.terminalRows = tab.terminalRows;
    this.terminalCols = tab.terminalCols;
    this.status = tab.status;
    this.logSize = tab.logSize;
    this.searchText = tab.searchText;
    this.searchStatus = tab.searchStatus;
    this.selectionStartLine = tab.selectionStartLine;
    this.selectionEndLine = tab.selectionEndLine;
    this.selectionStartCol = tab.selectionStartCol;
    this.selectionEndCol = tab.selectionEndCol;
    this.transferLocalPath = tab.transferLocalPath;
    this.transferRemotePath = tab.transferRemotePath;
    this.transferStatus = tab.transferStatus;
    this.forwardLocalHost = tab.forwardLocalHost;
    this.forwardLocalPort = tab.forwardLocalPort;
    this.forwardRemoteHost = tab.forwardRemoteHost;
    this.forwardRemotePort = tab.forwardRemotePort;
    this.forwardId = tab.forwardId;
    this.forwardStatus = tab.forwardStatus;
    this.macroScript = tab.macroScript;
    this.macroId = tab.macroId;
    this.macroStatus = tab.macroStatus;
    this.macroStatusBoxVisible = tab.macroStatusBoxVisible;
    this.macroStatusBoxTitle = tab.macroStatusBoxTitle;
    this.macroStatusBoxText = tab.macroStatusBoxText;
    this.refreshSnapshot();
  }

  private logBufferFor(sessionId: number): SessionLogBuffer {
    let buffer = this.sessionLogs.get(sessionId);
    if (!buffer) {
      buffer = new SessionLogBuffer();
      this.sessionLogs.set(sessionId, buffer);
    }
    return buffer;
  }

  private selectProtocol(protocol: ProtocolMode) {
    this.protocol = protocol;
    if (protocol === 'telnet' && (this.port === '' || this.port === '22')) {
      this.port = '23';
    } else if (protocol === 'ssh' && (this.port === '' || this.port === '23')) {
      this.port = '22';
    } else if (protocol === 'serial') {
      if (this.host.trim().length === 0) {
        this.host = '/dev/ttyUSB0';
      }
      if (this.port === '' || this.port === '22' || this.port === '23') {
        this.port = '9600';
      }
      this.proxyEnabled = false;
    }
    this.saveActiveTab();
  }

  private hostPlaceholder(): string {
    return this.protocol === 'serial' ? 'Device path' : 'Host';
  }

  private portPlaceholder(): string {
    return this.protocol === 'serial' ? 'Baud' : 'Port';
  }

  private readProxyOptions(): ProxyOptions | undefined {
    if (!this.proxyEnabled) {
      return undefined;
    }
    const proxyPortNumber = Number.parseInt(this.proxyPort, 10);
    if (this.proxyHost.trim().length === 0 || Number.isNaN(proxyPortNumber) || proxyPortNumber <= 0) {
      this.status = 'Check proxy host and port';
      this.saveActiveTab();
      return undefined;
    }
    return {
      type: this.proxyType,
      host: this.proxyHost.trim(),
      port: proxyPortNumber,
      username: this.proxyUsername,
      password: this.proxyPassword,
    };
  }

  private readProfile(): ConnectProfile | undefined {
    const portNumber = Number.parseInt(this.port, 10);
    if (this.host.trim().length === 0 || Number.isNaN(portNumber) || portNumber <= 0) {
      this.status = this.protocol === 'serial' ? 'Check serial device and baud' : 'Check host and port';
      this.saveActiveTab();
      return undefined;
    }
    const proxy = this.protocol === 'serial' ? undefined : this.readProxyOptions();
    if (this.protocol !== 'serial' && this.proxyEnabled && !proxy) {
      return undefined;
    }
    if (this.protocol === 'serial') {
      return {
        protocol: this.protocol,
        host: this.host.trim(),
        port: portNumber,
        serial: this.readSerialOptions(this.host.trim(), portNumber),
      };
    }
    return {
      protocol: this.protocol,
      host: this.host.trim(),
      port: portNumber,
      proxy,
    };
  }

  private readSerialOptions(devicePath: string, baudRate: number): SerialConnectionOptions {
    return {
      devicePath,
      baudRate,
      dataBits: this.serialDataBits,
      stopBits: this.serialStopBits,
      parity: this.serialParity,
      flowControl: this.serialFlowControl,
    };
  }

  private serialDeviceSelectOptions(): SelectOption[] {
    if (this.serialDeviceCandidates.length === 0) {
      return [{ value: 'No serial device found' }];
    }
    return this.serialDeviceCandidates.map((devicePath: string): SelectOption => {
      return { value: devicePath };
    });
  }

  private scanSerialDevices(): void {
    try {
      const devices = TerminalNative.listSerialDevices();
      this.serialDeviceCandidates = devices;
      if (devices.length === 0) {
        this.serialScanStatus = 'No serial devices found';
      } else {
        const selectedIndex = devices.indexOf(this.host.trim());
        this.selectedSerialDeviceIndex = selectedIndex >= 0 ? selectedIndex : 0;
        this.host = devices[this.selectedSerialDeviceIndex];
        this.serialScanStatus = `${devices.length} serial device(s) found`;
      }
    } catch (error) {
      this.serialDeviceCandidates = [];
      this.selectedSerialDeviceIndex = 0;
      this.serialScanStatus = `${error}`;
    }
    this.saveActiveTab();
  }

  private selectSerialDevice(index: number): void {
    if (index < 0 || index >= this.serialDeviceCandidates.length) {
      return;
    }
    this.selectedSerialDeviceIndex = index;
    this.host = this.serialDeviceCandidates[index];
    this.serialScanStatus = `Selected ${this.host}`;
    this.saveActiveTab();
  }

  private startLocalExperience(protocol: ProtocolMode): void {
    try {
      const demo = TerminalNative.startLocalExperience();
      this.createTab();
      this.protocol = protocol;
      this.host = demo.host;
      this.port = `${protocol === 'ssh' ? demo.sshPort : demo.tcpPort}`;
      this.proxyEnabled = false;
      this.username = demo.username;
      this.password = demo.password;
      this.privateKeyPath = '';
      this.publicKeyPath = '';
      this.passphrase = '';
      this.transferRemotePath = demo.remotePath;
      this.transferLocalPath = SandboxTransferStore.localExperienceUpload(this.appContext());
      this.forwardLocalHost = '127.0.0.1';
      this.forwardLocalPort = '0';
      this.forwardRemoteHost = demo.host;
      this.forwardRemotePort = `${demo.tcpPort}`;
      this.inputText = protocol === 'ssh' ? 'echo 你好，泊舟连接台' : '你好，泊舟连接台';
      this.localExperienceText = '体验账号 local-demo / local-demo；文件 /hello.txt；发送 help 查看命令';
      this.saveActiveTab();
      this.connect();
      this.toolsVisible = false;
      this.activeToolsPanel = '';
      const localIssue = TerminalNative.localExperienceStatus().error;
      if (localIssue.length > 0) { this.localExperienceText = localIssue; }
    } catch (error) { this.status = `${error}`; }
  }

  private startLocalFileExperience(): void {
    this.startLocalExperience('ssh');
    if (this.status.startsWith('SSH connected')) {
      this.toolsVisible = true;
      this.activeToolsPanel = 'file';
      this.localExperienceText = '已连接本机 SSH；点击 SCP Get/Put 或 SFTP Get/Put 即可传输，账号与路径已填好。';
    }
  }

  private prepareOwnConnection(): void {
    this.createTab();
    this.toolsVisible = true;
    this.activeToolsPanel = 'connection';
    this.localExperienceText = '输入你自己的主机与端口；SSH 账号在下方工具区配置。本机体验仍可随时使用。';
  }

  private stopLocalExperience(): void {
    TerminalNative.stopLocalExperience();
    this.localExperienceText = '本机体验已停止，内存文件已释放';
  }

  private showLocalExperienceCounts(): void {
    const info = TerminalNative.localExperienceStatus();
    this.localExperienceText = `TCP 回声 ${info.tcpEchoes}；SSH 登录 ${info.sshLogins}；SCP 传输 ${info.scpTransfers}；SFTP 读写 ${info.sftpRequests}`;
  }

  private connect() {
    const profile = this.readProfile();
    if (!profile) {
      return;
    }

    try {
      if (profile.protocol === 'ssh') {
        const sshResult: SshConnectionResult = TerminalNative.connectSsh(this.sessionId, {
          host: profile.host,
          port: profile.port,
          username: this.username,
          password: this.password,
          publicKeyPath: this.publicKeyPath,
          privateKeyPath: this.privateKeyPath,
          passphrase: this.passphrase,
          knownHostsText: this.knownHostsText,
          knownHostPolicy: this.knownHostPolicy,
          terminalType: 'xterm-256color',
          proxy: profile.proxy,
        });
        if (!sshResult.connected) {
          this.status = sshResult.error;
          this.saveActiveTab();
          return;
        }
        this.knownHostsText = sshResult.updatedKnownHostsText;
        const hostKeyStatus = sshResult.hostKeyMatch.status === 'not_found' ? 'new host key trusted' :
          sshResult.hostKeyMatch.status;
        this.status = profile.proxy ? `SSH connected ${profile.host}:${profile.port} (${hostKeyStatus}) via ${profile.proxy.host}:${profile.proxy.port}` :
          `SSH connected ${profile.host}:${profile.port} (${hostKeyStatus})`;
        this.saveAppState();
      } else if (profile.protocol === 'telnet') {
        if (profile.proxy) {
          TerminalNative.connectTelnetViaProxy(this.sessionId, profile.host, profile.port, profile.proxy);
          this.status = `Connected ${profile.host}:${profile.port} via ${profile.proxy.host}:${profile.proxy.port}`;
        } else {
          TerminalNative.connectTelnet(this.sessionId, profile.host, profile.port);
          this.status = `Connected ${profile.host}:${profile.port}`;
        }
      } else if (profile.protocol === 'serial' && profile.serial) {
        TerminalNative.connectSerial(this.sessionId, profile.serial);
        this.status = `Connected ${profile.serial.devicePath} ${profile.serial.baudRate}bps`;
      } else {
        if (profile.proxy) {
          TerminalNative.connectTcpViaProxy(this.sessionId, profile.host, profile.port, profile.proxy);
          this.status = `Connected ${profile.host}:${profile.port} via ${profile.proxy.host}:${profile.proxy.port}`;
        } else {
          TerminalNative.connectTcp(this.sessionId, profile.host, profile.port);
          this.status = `Connected ${profile.host}:${profile.port}`;
        }
      }
      this.refreshSnapshot();
    } catch (error) {
      this.status = `${error}`;
    }
    this.saveActiveTab();
  }

  private disconnect() {
    if (this.macroId !== 0) {
      TerminalNative.stopMacro(this.macroId);
      this.macroId = 0;
      this.macroStatus = 'Macro stopped';
    }
    if (this.forwardId !== 0) {
      const stopped = this.stopForwardById(this.forwardId);
      this.forwardId = 0;
      if (stopped) {
        this.forwardStatus = 'Forward stopped';
      }
    }
    TerminalNative.disconnect(this.sessionId);
    this.status = 'Disconnected';
    this.refreshSnapshot();
    this.saveActiveTab();
  }

  private sendInput() {
    if (this.inputText.length === 0) {
      return;
    }
    try {
      TerminalNative.sendInput(this.sessionId, `${this.inputText}\r`);
      this.inputText = '';
      this.pumpAllSessions();
    } catch (error) {
      this.status = `${error}`;
    }
    this.saveActiveTab();
  }

  private modifierPressed(event: KeyEvent, name: string): boolean {
    if (name === 'ctrl' && (this.leftControlDown || this.rightControlDown)) {
      return true;
    }
    try {
      return event.getModifierKeyState !== undefined && event.getModifierKeyState([name]);
    } catch (_error) {
      return false;
    }
  }

  private printableKeyText(event: KeyEvent): string {
    if (event.keyText.length === 1) {
      return event.keyText;
    }
    return '';
  }

  private focusTerminalKeyboard(): void {
    focusControl.requestFocus('terminal-key-capture');
  }

  private terminalPayloadForKey(event: KeyEvent, includePrintable: boolean = true): string {
    const ctrlPressed = this.modifierPressed(event, 'ctrl');
    const altPressed = this.modifierPressed(event, 'alt');
    if (altPressed) {
      return '';
    }

    if (ctrlPressed && event.keyCode >= 2017 && event.keyCode <= 2042) {
      return String.fromCharCode(event.keyCode - 2016);
    }

    if (ctrlPressed) {
      return '';
    }

    switch (event.keyCode) {
      case 2049:
        return '\t';
      case 2054:
        return '\r';
      case 2055:
        return '\x7F';
      case 2070:
        return '\x1B';
      case 2012:
        return '\x1B[A';
      case 2013:
        return '\x1B[B';
      case 2014:
        return '\x1B[D';
      case 2015:
        return '\x1B[C';
      case 2081:
        return '\x1B[H';
      case 2082:
        return '\x1B[F';
      case 2068:
        return '\x1B[5~';
      case 2069:
        return '\x1B[6~';
      default:
        return includePrintable ? this.printableKeyText(event) : '';
    }
  }

  private captureTerminalText(value: string): void {
    if (value.length === this.terminalKeyCaptureText.length) {
      return;
    }
    const previous = this.terminalKeyCaptureText;
    if (value.length < previous.length) {
      this.terminalKeyCaptureText = value;
      return;
    }
    const payload = value.length > previous.length && value.startsWith(previous) ?
      value.substring(previous.length) : value;
    this.terminalKeyCaptureText = value;
    if (payload.length === 0) {
      return;
    }
    this.sendTerminalKeyboardPayload(payload);
  }

  private sendTerminalKeyboardPayload(payload: string): void {
    if (payload.length === 0) {
      return;
    }
    try {
      if (this.selectionStartLine >= 0) {
        this.selectionStartLine = -1;
        this.selectionEndLine = -1;
        this.selectionStartCol = -1;
        this.selectionEndCol = -1;
      }
      TerminalNative.sendInput(this.sessionId, payload);
      this.pumpAllSessions();
    } catch (error) {
      this.status = `${error}`;
    }
    this.saveActiveTab();
  }

  private handleTerminalKeyEvent(event: KeyEvent): boolean {
    this.rememberShortcutModifiers(event);
    if (event.type !== KeyType.Down) {
      return false;
    }

    if (this.modifierPressed(event, 'ctrl') && event.keyCode === 2017) {
      this.selectAllTerminalOutput();
      event.stopPropagation();
      return true;
    }
    if (this.modifierPressed(event, 'ctrl') && event.keyCode === 2019 && this.selectionStartLine >= 0) {
      this.copySelection();
      return true;
    }
    if (this.modifierPressed(event, 'ctrl') && event.keyCode === 2038) {
      this.pasteTerminal();
      return true;
    }

    const payload = this.terminalPayloadForKey(event);
    if (payload.length === 0) {
      return false;
    }
    this.sendTerminalKeyboardPayload(payload);
    return true;
  }

  private handleTerminalCaptureKeyEvent(event: KeyEvent): boolean {
    this.rememberShortcutModifiers(event);
    if (event.type !== KeyType.Down) {
      return false;
    }

    if (this.modifierPressed(event, 'ctrl') && event.keyCode === 2017) {
      this.selectAllTerminalOutput();
      event.stopPropagation();
      return true;
    }
    if (this.modifierPressed(event, 'ctrl') && event.keyCode === 2019 && this.selectionStartLine >= 0) {
      this.copySelection();
      return true;
    }
    if (this.modifierPressed(event, 'ctrl') && event.keyCode === 2038) {
      this.pasteTerminal();
      return true;
    }

    const payload = this.terminalPayloadForKey(event, false);
    if (payload.length === 0) {
      return false;
    }
    this.sendTerminalKeyboardPayload(payload);
    return true;
  }

  private pumpAllSessions() {
    this.saveActiveTab();
    this.tabs.forEach((tab: TerminalTabRecord, index: number) => {
      this.pumpTab(tab, index === this.activeTabIndex);
    });
    this.tabs = this.tabs.slice();
    this.loadActiveTab();
  }

  private pumpTab(tab: TerminalTabRecord, isActive: boolean) {
    try {
      const result = TerminalNative.pump(tab.sessionId);
      if (result.error.length > 0) {
        tab.status = result.error;
      } else if (!result.connected && tab.status.startsWith('Connected')) {
        tab.status = 'Disconnected';
      }
      if (result.bytesRead > 0) {
        tab.logSize += this.logBufferFor(tab.sessionId).appendFromNative(tab.sessionId);
        const snapshot = TerminalNative.snapshot(tab.sessionId);
        tab.scrollbackRows = snapshot.scrollbackRows;
        tab.rows = snapshot.rows;
        tab.cells = snapshot.cells;
        tab.cursorRow = snapshot.cursorRow;
        tab.cursorCol = snapshot.cursorCol;
      }
      if (tab.macroId !== 0) {
        const macroResult = TerminalNative.pumpMacro(tab.macroId);
        this.applyMacroResultToTab(tab, macroResult);
      }
    } catch (error) {
      tab.status = `${error}`;
      tab.macroId = 0;
    }

    if (isActive) {
      this.protocol = tab.protocol;
      this.host = tab.host;
      this.port = tab.port;
      this.username = tab.username;
      this.password = tab.password;
      this.publicKeyPath = tab.publicKeyPath;
      this.privateKeyPath = tab.privateKeyPath;
      this.passphrase = tab.passphrase;
      this.serialDataBits = tab.serialDataBits;
      this.serialStopBits = tab.serialStopBits;
      this.serialParity = tab.serialParity;
      this.serialFlowControl = tab.serialFlowControl;
      this.proxyEnabled = tab.proxyEnabled;
      this.proxyType = tab.proxyType;
      this.proxyHost = tab.proxyHost;
      this.proxyPort = tab.proxyPort;
      this.proxyUsername = tab.proxyUsername;
      this.proxyPassword = tab.proxyPassword;
      this.inputText = tab.inputText;
      this.scrollbackRows = tab.scrollbackRows;
      this.rows = tab.rows;
      this.cells = tab.cells;
      this.cursorRow = tab.cursorRow;
      this.cursorCol = tab.cursorCol;
      this.terminalRows = tab.terminalRows;
      this.terminalCols = tab.terminalCols;
      this.status = tab.status;
      this.logSize = tab.logSize;
      this.searchText = tab.searchText;
      this.searchStatus = tab.searchStatus;
      this.selectionStartLine = tab.selectionStartLine;
      this.selectionEndLine = tab.selectionEndLine;
      this.selectionStartCol = tab.selectionStartCol;
      this.selectionEndCol = tab.selectionEndCol;
      this.transferLocalPath = tab.transferLocalPath;
      this.transferRemotePath = tab.transferRemotePath;
      this.transferStatus = tab.transferStatus;
      this.forwardLocalHost = tab.forwardLocalHost;
      this.forwardLocalPort = tab.forwardLocalPort;
      this.forwardRemoteHost = tab.forwardRemoteHost;
      this.forwardRemotePort = tab.forwardRemotePort;
      this.forwardId = tab.forwardId;
      this.forwardStatus = tab.forwardStatus;
      this.macroScript = tab.macroScript;
      this.macroId = tab.macroId;
      this.macroStatus = tab.macroStatus;
    }
  }

  private importProfiles() {
    try {
      this.importedProfiles = ProfileRepository.connectableProfiles(ProfileRepository.importIniText(this.iniImportText));
      this.applyDisplaySettingsFromIni(this.iniImportText);
      this.profileJson = ProfileRepository.serialize(this.importedProfiles);
      this.status = `Imported ${this.importedProfiles.length} profiles`;
      this.saveAppState();
    } catch (error) {
      this.status = `${error}`;
    }
    this.saveActiveTab();
  }

  private async importProfilesFromFile(): Promise<void> {
    try {
      this.iniImportText = await UserFileImportStore.importIniText(this.appContext());
      this.importProfiles();
    } catch (error) {
      this.status = `${error}`;
      this.saveActiveTab();
    }
  }

  private applyDisplaySettingsFromIni(iniText: string) {
    iniText.split('\n').forEach((rawLine: string) => {
      const line = rawLine.replace('\r', '').split(';')[0].trim();
      const lower = line.toLowerCase();
      if (lower.startsWith('vtfont=')) {
        const values = line.substring(7).split(',');
        if (values.length > 0 && values[0].trim().length > 0) {
          this.terminalFontFamily = values[0].trim();
        }
        if (values.length > 2) {
          const parsedSize = Math.abs(Number.parseInt(values[2], 10));
          if (!Number.isNaN(parsedSize) && parsedSize > 0) {
            const normalized = Math.max(10, Math.min(24, Math.round(parsedSize)));
            this.terminalFontSize = normalized;
            this.terminalLineHeight = normalized + 6;
          }
        }
      } else if (lower.startsWith('vtcolor=')) {
        const values = line.substring(8).split(',');
        if (values.length >= 6) {
          const red = Number.parseInt(values[0], 10);
          const green = Number.parseInt(values[1], 10);
          const blue = Number.parseInt(values[2], 10);
          const foregroundRed = Number.parseInt(values[3], 10);
          const foregroundGreen = Number.parseInt(values[4], 10);
          const foregroundBlue = Number.parseInt(values[5], 10);
          const backgroundIsLight = red + green + blue > 382;
          const foregroundIsGreen = foregroundGreen > foregroundRed + foregroundBlue;
          if (foregroundIsGreen && !backgroundIsLight) {
            this.terminalColorTheme = 'classic';
          } else {
            this.terminalColorTheme = backgroundIsLight ? 'light' : 'dark';
          }
        }
      }
    });
  }

  private loadSelectedProfile() {
    if (this.selectedProfileIndex < 0 || this.selectedProfileIndex >= this.importedProfiles.length) {
      return;
    }
    const profile = ProfileRepository.toConnectProfile(this.importedProfiles[this.selectedProfileIndex]);
    if (!profile) {
      return;
    }
    this.protocol = profile.protocol;
    this.host = profile.host;
    this.port = `${profile.port}`;
    this.username = this.importedProfiles[this.selectedProfileIndex].username;
    this.password = this.importedProfiles[this.selectedProfileIndex].password;
    this.publicKeyPath = this.importedProfiles[this.selectedProfileIndex].publicKeyPath;
    this.privateKeyPath = this.importedProfiles[this.selectedProfileIndex].privateKeyPath;
    this.passphrase = this.importedProfiles[this.selectedProfileIndex].passphrase;
    if (profile.serial) {
      this.serialDataBits = profile.serial.dataBits || 8;
      this.serialStopBits = profile.serial.stopBits || 1;
      this.serialParity = profile.serial.parity || 'none';
      this.serialFlowControl = profile.serial.flowControl || 'none';
    }
    this.proxyEnabled = profile.protocol !== 'serial' && profile.proxy !== undefined;
    this.proxyType = profile.proxy ? profile.proxy.type : 'http';
    this.proxyHost = profile.proxy ? profile.proxy.host : '';
    this.proxyPort = profile.proxy ? `${profile.proxy.port}` : '8080';
    this.proxyUsername = profile.proxy && profile.proxy.username ? profile.proxy.username : '';
    this.proxyPassword = profile.proxy && profile.proxy.password ? profile.proxy.password : '';
    this.saveActiveTab();
  }

  private saveCurrentProfile() {
    const portNumber = Number.parseInt(this.port, 10);
    if (this.host.trim().length === 0 || Number.isNaN(portNumber) || portNumber <= 0) {
      this.status = this.protocol === 'serial' ? 'Check serial device and baud' : 'Check host and port';
      this.saveActiveTab();
      return;
    }
    const profile: ImportedProfile = {
      name: this.host.trim(),
      protocol: this.protocol,
      host: this.host.trim(),
      port: this.protocol === 'serial' ? 0 : portNumber,
      username: this.username,
      password: this.password,
      publicKeyPath: this.publicKeyPath,
      privateKeyPath: this.privateKeyPath,
      passphrase: this.passphrase,
      proxyType: this.proxyEnabled ? this.proxyType : 'none',
      proxyHost: this.proxyEnabled ? this.proxyHost.trim() : '',
      proxyPort: this.proxyEnabled ? Number.parseInt(this.proxyPort, 10) || 0 : 0,
      proxyUsername: this.proxyEnabled ? this.proxyUsername : '',
      proxyPassword: this.proxyEnabled ? this.proxyPassword : '',
      serialDevicePath: this.protocol === 'serial' ? this.host.trim() : '',
      baudRate: this.protocol === 'serial' ? portNumber : 0,
      dataBits: this.serialDataBits,
      stopBits: this.serialStopBits,
      parity: this.serialParity,
      flowControl: this.serialFlowControl,
      setupFile: '',
      macroFile: '',
      source: 'saved from HarmonyOS PC',
      warnings: [],
    };
    const label = this.profileLabel(profile);
    const existingIndex = this.importedProfiles.findIndex((item: ImportedProfile) => this.profileLabel(item) === label);
    if (existingIndex >= 0) {
      this.importedProfiles[existingIndex] = profile;
      this.selectedProfileIndex = existingIndex;
    } else {
      this.importedProfiles.push(profile);
      this.selectedProfileIndex = this.importedProfiles.length - 1;
    }
    this.profileJson = ProfileRepository.serialize(this.importedProfiles);
    this.status = `Saved profile ${label}`;
    this.saveAppState();
    this.saveActiveTab();
  }

  private profileLabel(profile: ImportedProfile): string {
    if (profile.protocol === 'serial') {
      const devicePath = profile.serialDevicePath || profile.host;
      return `${profile.protocol}:${devicePath}:${profile.baudRate || 9600}`;
    }
    return `${profile.protocol}:${profile.host}:${profile.port}`;
  }

  private drainLog() {
    this.logSize += this.logBufferFor(this.sessionId).appendFromNative(this.sessionId);
    this.status = `Log buffer ${this.logSize} bytes`;
    this.saveActiveTab();
  }

  private async copyScreen(): Promise<void> {
    try {
      await ClipboardService.copyText(this.scrollbackRows.concat(this.rows).join('\n'));
      this.status = 'Copied screen';
    } catch (error) {
      this.status = `${error}`;
    }
    this.saveActiveTab();
  }

  private async copyLog(): Promise<void> {
    this.logSize += this.logBufferFor(this.sessionId).appendFromNative(this.sessionId);
    try {
      await ClipboardService.copyText(this.logBufferFor(this.sessionId).text());
      this.status = 'Copied log';
    } catch (error) {
      this.status = `${error}`;
    }
    this.saveActiveTab();
  }

  private async pasteInput(): Promise<void> {
    try {
      this.inputText += await ClipboardService.readText();
    } catch (error) {
      this.status = `${error}`;
    }
    this.saveActiveTab();
  }

  private async pasteTerminal(): Promise<void> {
    try {
      const text = await ClipboardService.readText();
      if (text.length === 0) {
        this.status = 'Clipboard is empty';
        this.saveActiveTab();
        return;
      }
      this.sendTerminalKeyboardPayload(text);
      this.status = 'Pasted to terminal';
    } catch (error) {
      this.status = `${error}`;
    }
    this.saveActiveTab();
  }

  private async pickPrivateKey(): Promise<void> {
    try {
      this.privateKeyPath = await UserFileImportStore.importPrivateKey(this.appContext());
      if (this.publicKeyPath.trim().length === 0 && this.privateKeyPath.trim().length > 0) {
        this.publicKeyPath = `${this.privateKeyPath.trim()}.pub`;
      }
      this.status = 'Private key selected';
    } catch (error) {
      this.status = `${error}`;
    }
    this.saveActiveTab();
  }

  private async loadMacroFromFile(): Promise<void> {
    try {
      this.macroScript = await UserFileImportStore.importMacroText(this.appContext());
      this.macroStatus = 'Macro loaded';
    } catch (error) {
      this.macroStatus = `${error}`;
    }
    this.saveActiveTab();
  }

  private async pickTransferFile(): Promise<void> {
    try {
      this.transferLocalPath = await UserFileImportStore.importTransferFile(this.appContext());
      this.transferStatus = 'Upload file selected';
    } catch (error) {
      this.transferStatus = `${error}`;
    }
    this.saveActiveTab();
  }

  private readSshFileTransferOptions(localPath: string, remotePath: string): SshFileTransferOptions | undefined {
    const portNumber = Number.parseInt(this.port, 10);
    if (this.host.trim().length === 0 || this.username.trim().length === 0 ||
        Number.isNaN(portNumber) || portNumber <= 0 ||
        localPath.trim().length === 0 || remotePath.trim().length === 0) {
      this.transferStatus = 'Check SSH host, user, local path, and remote path';
      this.saveActiveTab();
      return undefined;
    }

    const proxy = this.readProxyOptions();
    if (this.proxyEnabled && !proxy) {
      this.transferStatus = 'Check proxy host and port';
      return undefined;
    }

    return {
      host: this.host.trim(),
      port: portNumber,
      username: this.username.trim(),
      password: this.password,
      publicKeyPath: this.publicKeyPath,
      privateKeyPath: this.privateKeyPath,
      passphrase: this.passphrase,
      knownHostsText: this.knownHostsText,
      knownHostPolicy: this.knownHostPolicy,
      terminalType: 'xterm-256color',
      proxy,
      timeoutMs: 10000,
      localPath: localPath.trim(),
      remotePath: remotePath.trim(),
      mode: 420,
    };
  }

  private applyFileTransferResult(action: string, result: SshFileTransferResult, localPath: string) {
    if (result.updatedKnownHostsText.length > 0) {
      this.knownHostsText = result.updatedKnownHostsText;
      this.saveAppState();
    }
    if (result.success) {
      this.transferStatus = `${action} ${result.bytesTransferred} bytes ${localPath}`;
    } else {
      this.transferStatus = result.error.length > 0 ? result.error : `${action} failed`;
    }
    this.saveActiveTab();
  }

  private scpDownload() {
    const remotePath = this.transferRemotePath.trim();
    const localPath = this.transferLocalPath.trim().length > 0 ?
      this.transferLocalPath.trim() : SandboxTransferStore.downloadPath(this.appContext(), remotePath);
    const options = this.readSshFileTransferOptions(localPath, remotePath);
    if (!options) {
      return;
    }

    this.transferLocalPath = localPath;
    try {
      this.applyFileTransferResult('SCP downloaded', TerminalNative.scpDownload(options), localPath);
    } catch (error) {
      this.transferStatus = `${error}`;
      this.saveActiveTab();
    }
  }

  private scpUpload() {
    const options = this.readSshFileTransferOptions(this.transferLocalPath, this.transferRemotePath);
    if (!options) {
      return;
    }

    try {
      this.applyFileTransferResult('SCP uploaded', TerminalNative.scpUpload(options), this.transferLocalPath);
    } catch (error) {
      this.transferStatus = `${error}`;
      this.saveActiveTab();
    }
  }

  private sftpDownload() {
    const remotePath = this.transferRemotePath.trim();
    const localPath = this.transferLocalPath.trim().length > 0 ?
      this.transferLocalPath.trim() : SandboxTransferStore.downloadPath(this.appContext(), remotePath);
    const options = this.readSshFileTransferOptions(localPath, remotePath);
    if (!options) {
      return;
    }

    this.transferLocalPath = localPath;
    try {
      this.applyFileTransferResult('SFTP downloaded', TerminalNative.sftpDownload(options), localPath);
    } catch (error) {
      this.transferStatus = `${error}`;
      this.saveActiveTab();
    }
  }

  private sftpUpload() {
    const options = this.readSshFileTransferOptions(this.transferLocalPath, this.transferRemotePath);
    if (!options) {
      return;
    }

    try {
      this.applyFileTransferResult('SFTP uploaded', TerminalNative.sftpUpload(options), this.transferLocalPath);
    } catch (error) {
      this.transferStatus = `${error}`;
      this.saveActiveTab();
    }
  }

  private readSshPortForwardOptions(): SshPortForwardOptions | undefined {
    const sshPort = Number.parseInt(this.port, 10);
    const localPort = Number.parseInt(this.forwardLocalPort, 10);
    const remotePort = Number.parseInt(this.forwardRemotePort, 10);
    if (this.host.trim().length === 0 || this.username.trim().length === 0 ||
        Number.isNaN(sshPort) || sshPort <= 0 ||
        this.forwardRemoteHost.trim().length === 0 ||
        Number.isNaN(remotePort) || remotePort <= 0 ||
        Number.isNaN(localPort) || localPort < 0) {
      this.forwardStatus = 'Check SSH host, user, and forward ports';
      this.saveActiveTab();
      return undefined;
    }

    const proxy = this.readProxyOptions();
    if (this.proxyEnabled && !proxy) {
      this.forwardStatus = 'Check proxy host and port';
      return undefined;
    }

    return {
      host: this.host.trim(),
      port: sshPort,
      username: this.username.trim(),
      password: this.password,
      publicKeyPath: this.publicKeyPath,
      privateKeyPath: this.privateKeyPath,
      passphrase: this.passphrase,
      knownHostsText: this.knownHostsText,
      knownHostPolicy: this.knownHostPolicy,
      terminalType: 'xterm-256color',
      proxy,
      timeoutMs: 10000,
      localHost: this.forwardLocalHost.trim().length > 0 ? this.forwardLocalHost.trim() : '127.0.0.1',
      localPort,
      remoteHost: this.forwardRemoteHost.trim(),
      remotePort,
    };
  }

  private startSshForward() {
    if (this.forwardId !== 0) {
      this.forwardStatus = 'Forward already running';
      this.saveActiveTab();
      return;
    }
    const options = this.readSshPortForwardOptions();
    if (!options) {
      return;
    }

    try {
      const result: SshPortForwardStartResult = TerminalNative.startSshLocalForward(options);
      if (result.updatedKnownHostsText.length > 0) {
        this.knownHostsText = result.updatedKnownHostsText;
        this.saveAppState();
      }
      if (result.started) {
        this.forwardId = result.forwardId;
        this.forwardLocalPort = `${result.boundPort}`;
        this.forwardStatus = `Forward ${options.localHost}:${result.boundPort} -> ${options.remoteHost}:${options.remotePort}`;
      } else {
        this.forwardStatus = result.error.length > 0 ? result.error : 'Forward failed';
      }
    } catch (error) {
      this.forwardStatus = `${error}`;
    }
    this.saveActiveTab();
  }

  private stopSshForward() {
    if (this.forwardId === 0) {
      return;
    }
    const stopped = this.stopForwardById(this.forwardId);
    if (stopped) {
      this.forwardStatus = 'Forward stopped';
    }
    this.forwardId = 0;
    this.saveActiveTab();
  }

  private async saveLog(): Promise<void> {
    this.logSize += this.logBufferFor(this.sessionId).appendFromNative(this.sessionId);
    const text = this.logBufferFor(this.sessionId).text();
    if (text.length === 0) {
      this.status = 'Log is empty';
      this.saveActiveTab();
      return;
    }
    try {
      const uri = await UserFileExportStore.saveText(this.appContext(), this.host || `session-${this.sessionId}`, text);
      this.status = `Saved log ${uri}`;
    } catch (error) {
      const path = SandboxLogStore.save(this.appContext(), this.host || `session-${this.sessionId}`, text);
      this.status = `Saved sandbox log ${path}`;
    }
    this.saveActiveTab();
  }

  private saveSandboxLog() {
    this.logSize += this.logBufferFor(this.sessionId).appendFromNative(this.sessionId);
    const text = this.logBufferFor(this.sessionId).text();
    if (text.length === 0) {
      this.status = 'Log is empty';
      this.saveActiveTab();
      return;
    }
    try {
      const path = SandboxLogStore.save(this.appContext(), this.host || `session-${this.sessionId}`, text);
      this.status = `Saved sandbox log ${path}`;
    } catch (error) {
      this.status = `${error}`;
    }
    this.saveActiveTab();
  }

  private searchOutput() {
    const needle = this.searchText.trim().toLowerCase();
    if (needle.length === 0) {
      this.searchStatus = 'Search is empty';
      this.saveActiveTab();
      return;
    }

    const history = TerminalNative.outputHistory(this.sessionId).toLowerCase();
    let count = 0;
    let offset = 0;
    while (offset < history.length) {
      const found = history.indexOf(needle, offset);
      if (found < 0) {
        break;
      }
      ++count;
      offset = found + needle.length;
    }
    this.searchStatus = count === 0 ? 'No matches' : `${count} matches`;
    this.saveActiveTab();
  }

  private terminalLines(): string[] {
    return this.scrollbackRows.concat(this.rows);
  }

  private selectionBounds(): number[] {
    const lines = this.terminalLines();
    if (lines.length === 0 || this.selectionStartLine < 0) {
      return [];
    }
    const rawEnd = this.selectionEndLine < 0 ? this.selectionStartLine : this.selectionEndLine;
    const start = Math.max(0, Math.min(this.selectionStartLine, rawEnd));
    const end = Math.min(lines.length - 1, Math.max(this.selectionStartLine, rawEnd));
    return [start, end];
  }

  private selectionCellBounds(): number[] {
    const lines = this.terminalLines();
    if (lines.length === 0 || this.selectionStartLine < 0 || this.selectionEndLine < 0 ||
        this.selectionStartCol < 0 || this.selectionEndCol < 0) {
      return [];
    }
    const startBeforeEnd = this.selectionStartLine < this.selectionEndLine ||
      (this.selectionStartLine === this.selectionEndLine && this.selectionStartCol <= this.selectionEndCol);
    const startLine = startBeforeEnd ? this.selectionStartLine : this.selectionEndLine;
    const startCol = startBeforeEnd ? this.selectionStartCol : this.selectionEndCol;
    const endLine = startBeforeEnd ? this.selectionEndLine : this.selectionStartLine;
    const endCol = startBeforeEnd ? this.selectionEndCol : this.selectionStartCol;
    return [
      Math.max(0, Math.min(lines.length - 1, startLine)),
      Math.max(0, startCol),
      Math.max(0, Math.min(lines.length - 1, endLine)),
      Math.max(0, endCol),
    ];
  }

  private selectedTerminalText(): string {
    const bounds = this.selectionCellBounds();
    if (bounds.length < 4) {
      return '';
    }

    const lines = this.terminalLines();
    const selected: string[] = [];
    for (let index = bounds[0]; index <= bounds[2]; ++index) {
      const line = lines[index];
      const lineLength = line.length;
      let startCol = index === bounds[0] ? Math.min(bounds[1], lineLength) : 0;
      let endCol = index === bounds[2] ? Math.min(bounds[3], Math.max(0, lineLength - 1)) : lineLength - 1;
      if (lineLength === 0 || endCol < startCol) {
        selected.push('');
      } else {
        selected.push(line.substring(startCol, endCol + 1).replace(/\s+$/g, ''));
      }
    }
    return selected.join('\n');
  }

  private selectTerminalLine(lineIndex: number) {
    const lines = this.terminalLines();
    if (lineIndex < 0 || lineIndex >= lines.length) {
      return;
    }

    this.focusTerminalKeyboard();
    if (this.selectionStartLine < 0 ||
        (this.selectionEndLine >= 0 && this.selectionStartLine !== this.selectionEndLine)) {
      this.selectionStartLine = lineIndex;
      this.selectionEndLine = lineIndex;
      this.selectionStartCol = 0;
      this.selectionEndCol = Math.max(0, lines[lineIndex].length - 1);
    } else {
      this.selectionEndLine = lineIndex;
      this.selectionEndCol = Math.max(0, lines[lineIndex].length - 1);
    }
    const bounds = this.selectionBounds();
    this.status = bounds.length < 2 ? 'Selection started' : `Selected lines ${bounds[0] + 1}-${bounds[1] + 1}`;
    this.saveActiveTab();
  }

  private beginTerminalLineSelection(lineIndex: number, colIndex: number = 0) {
    const lines = this.terminalLines();
    if (lineIndex < 0 || lineIndex >= lines.length) {
      return;
    }
    this.focusTerminalKeyboard();
    this.selectionStartLine = lineIndex;
    this.selectionEndLine = lineIndex;
    this.selectionStartCol = Math.max(0, Math.min(colIndex, Math.max(0, lines[lineIndex].length - 1)));
    this.selectionEndCol = this.selectionStartCol;
    this.status = `Selection started at ${lineIndex + 1}:${this.selectionStartCol + 1}`;
    this.saveActiveTab();
  }

  private updateTerminalLineSelection(lineIndex: number, colIndex: number = 0) {
    const lines = this.terminalLines();
    if (lineIndex < 0 || lineIndex >= lines.length) {
      return;
    }
    this.focusTerminalKeyboard();
    if (this.selectionStartLine < 0) {
      this.selectionStartLine = lineIndex;
      this.selectionStartCol = Math.max(0, Math.min(colIndex, Math.max(0, lines[lineIndex].length - 1)));
    }
    this.selectionEndLine = lineIndex;
    this.selectionEndCol = Math.max(0, Math.min(colIndex, Math.max(0, lines[lineIndex].length - 1)));
    const bounds = this.selectionBounds();
    this.status = bounds.length < 2 ? 'Selection started' : `Selected lines ${bounds[0] + 1}-${bounds[1] + 1}`;
    this.saveActiveTab();
  }

  private clearSelection() {
    this.selectionStartLine = -1;
    this.selectionEndLine = -1;
    this.selectionStartCol = -1;
    this.selectionEndCol = -1;
    this.status = 'Selection cleared';
    this.saveActiveTab();
  }

  private async copySelection(): Promise<void> {
    const text = this.selectedTerminalText();
    if (text.length === 0) {
      this.status = 'Selection is empty';
      this.saveActiveTab();
      return;
    }
    try {
      await ClipboardService.copyText(text);
      const bounds = this.selectionCellBounds();
      this.status = `Copied selected cells ${bounds[0] + 1}:${bounds[1] + 1}-${bounds[2] + 1}:${bounds[3] + 1}`;
    } catch (error) {
      this.status = `${error}`;
    }
    this.saveActiveTab();
  }

  private startMacro() {
    if (this.macroScript.trim().length === 0) {
      this.macroStatus = 'Macro is empty';
      this.saveActiveTab();
      return;
    }
    if (this.macroId !== 0) {
      TerminalNative.stopMacro(this.macroId);
      this.macroId = 0;
    }
    this.clearMacroDialog();
    this.clearMacroStatusBox();
    try {
      const result = TerminalNative.startMacro(this.sessionId, this.macroScript, {
        knownHostsText: this.knownHostsText,
        knownHostPolicy: this.knownHostPolicy,
        connectTimeoutMs: 5000,
      });
      this.applyMacroResult(result);
      this.refreshSnapshot();
    } catch (error) {
      this.macroId = 0;
      this.macroStatus = `${error}`;
    }
    this.saveActiveTab();
  }

  private pumpMacroOnce() {
    if (this.macroId === 0) {
      return;
    }
    try {
      const result = TerminalNative.pumpMacro(this.macroId);
      this.applyMacroResult(result);
      this.refreshSnapshot();
    } catch (error) {
      this.macroId = 0;
      this.macroStatus = `${error}`;
    }
    this.saveActiveTab();
  }

  private stopMacro() {
    if (this.macroId !== 0) {
      TerminalNative.stopMacro(this.macroId);
    }
    this.macroId = 0;
    this.macroStatus = 'Macro stopped';
    this.clearMacroDialog();
    this.clearMacroStatusBox();
    this.saveActiveTab();
  }

  private clearMacroDialog() {
    this.macroDialogType = '';
    this.macroDialogTitle = '';
    this.macroDialogText = '';
    this.macroDialogValue = '';
    this.macroDialogItems = [];
    this.macroDialogSelectedIndex = -1;
    this.macroDialogPassword = false;
    this.macroDialogSave = false;
  }

  private clearMacroStatusBox() {
    this.macroStatusBoxVisible = false;
    this.macroStatusBoxTitle = '';
    this.macroStatusBoxText = '';
  }

  private showMacroDialog(result: MacroStepResult) {
    const sameDialog = this.macroDialogType === result.dialogType &&
      this.macroDialogTitle === result.dialogTitle &&
      this.macroDialogText === result.dialogText;
    this.macroDialogType = result.dialogType;
    this.macroDialogTitle = result.dialogTitle;
    this.macroDialogText = result.dialogText;
    this.macroDialogItems = result.dialogItems;
    this.macroDialogSelectedIndex = result.dialogSelectedIndex;
    if (!sameDialog) {
      if (result.dialogType === 'listbox') {
        const selected = result.dialogSelectedIndex >= 0 ? result.dialogSelectedIndex :
          (result.dialogItems.length > 0 ? 0 : -1);
        this.macroDialogSelectedIndex = selected;
        this.macroDialogValue = selected >= 0 ? `${selected}` : '';
      } else {
        this.macroDialogValue = result.dialogDefaultText;
      }
    }
    this.macroDialogPassword = result.dialogPassword;
    this.macroDialogSave = result.dialogSave;
  }

  private answerMacroDialog(accepted: boolean) {
    if (this.macroId === 0 || this.macroDialogType.length === 0) {
      return;
    }
    try {
      if (this.macroDialogType === 'listbox') {
        this.macroDialogValue = this.macroDialogSelectedIndex >= 0 ? `${this.macroDialogSelectedIndex}` : '-1';
      }
      const result = TerminalNative.respondMacroDialog(this.macroId, accepted, this.macroDialogValue);
      this.clearMacroDialog();
      this.applyMacroResult(result);
      this.refreshSnapshot();
    } catch (error) {
      this.macroId = 0;
      this.clearMacroDialog();
      this.macroStatus = `${error}`;
    }
    this.saveActiveTab();
  }

  private applyMacroStatusBox(result: MacroStepResult) {
    if (result.statusBoxAction === 'show') {
      this.macroStatusBoxVisible = true;
      this.macroStatusBoxTitle = result.statusBoxTitle;
      this.macroStatusBoxText = result.statusBoxText;
    } else if (result.statusBoxAction === 'close') {
      this.clearMacroStatusBox();
    }
  }

  private applyMacroStatusBoxToTab(tab: TerminalTabRecord, result: MacroStepResult) {
    if (result.statusBoxAction === 'show') {
      tab.macroStatusBoxVisible = true;
      tab.macroStatusBoxTitle = result.statusBoxTitle;
      tab.macroStatusBoxText = result.statusBoxText;
      if (tab.sessionId === this.sessionId) {
        this.applyMacroStatusBox(result);
      }
    } else if (result.statusBoxAction === 'close') {
      tab.macroStatusBoxVisible = false;
      tab.macroStatusBoxTitle = '';
      tab.macroStatusBoxText = '';
      if (tab.sessionId === this.sessionId) {
        this.clearMacroStatusBox();
      }
    }
  }

  private applyMacroResult(result: MacroStepResult) {
    if (result.updatedKnownHostsText.length > 0) {
      this.knownHostsText = result.updatedKnownHostsText;
      this.saveAppState();
    }
    this.applyMacroStatusBox(result);
    this.logSize += this.logBufferFor(this.sessionId).appendFromNative(this.sessionId);
    if (result.status === 'running' || result.status === 'waiting') {
      this.macroId = result.macroId;
      if (result.dialogType.length > 0) {
        this.showMacroDialog(result);
      }
      this.macroStatus = result.message.length > 0 ? `Macro ${result.status}: ${result.message}` :
        `Macro ${result.status} line ${result.nextLine}`;
      return;
    }
    this.macroId = 0;
    this.clearMacroDialog();
    this.macroStatus = result.message.length > 0 ? `Macro ${result.status}: ${result.message}` :
      `Macro ${result.status}`;
  }

  private applyMacroResultToTab(tab: TerminalTabRecord, result: MacroStepResult) {
    if (result.updatedKnownHostsText.length > 0) {
      this.knownHostsText = result.updatedKnownHostsText;
      this.saveAppState();
    }
    this.applyMacroStatusBoxToTab(tab, result);
    tab.logSize += this.logBufferFor(tab.sessionId).appendFromNative(tab.sessionId);
    if (result.status === 'running' || result.status === 'waiting') {
      tab.macroId = result.macroId;
      if (tab.sessionId === this.sessionId && result.dialogType.length > 0) {
        this.showMacroDialog(result);
      }
      tab.macroStatus = result.message.length > 0 ? `Macro ${result.status}: ${result.message}` :
        `Macro ${result.status} line ${result.nextLine}`;
      return;
    }
    tab.macroId = 0;
    if (tab.sessionId === this.sessionId) {
      this.clearMacroDialog();
    }
    tab.macroStatus = result.message.length > 0 ? `Macro ${result.status}: ${result.message}` :
      `Macro ${result.status}`;
  }

  private refreshSnapshot() {
    if (this.sessionId === 0) {
      return;
    }
    const snapshot = TerminalNative.snapshot(this.sessionId);
    this.scrollbackRows = snapshot.scrollbackRows;
    this.rows = snapshot.rows;
    this.cells = snapshot.cells;
    this.cursorRow = snapshot.cursorRow;
    this.cursorCol = snapshot.cursorCol;
  }

  private resizeTerminalViewport(rows: number, cols: number): void {
    const normalizedRows = Math.max(8, Math.min(80, Math.round(rows)));
    const normalizedCols = Math.max(40, Math.min(220, Math.round(cols)));
    if (this.sessionId === 0 ||
        (normalizedRows === this.terminalRows && normalizedCols === this.terminalCols)) {
      return;
    }
    try {
      TerminalNative.resize(this.sessionId, normalizedRows, normalizedCols);
      this.terminalRows = normalizedRows;
      this.terminalCols = normalizedCols;
      this.refreshSnapshot();
    } catch (error) {
      this.status = `${error}`;
    }
    this.saveActiveTab();
  }

  build() {
    Column() {
      Column({ space: 6 }) {
        Row({ space: 12 }) {
          Text('泊舟连接台').fontSize(20).fontWeight(FontWeight.Bold).fontColor('#16464B')
          Text('本机连接体验').fontSize(14).fontColor('#235E63')
          Blank()
          Button('停止体验').winCommandButton().onClick(() => this.stopLocalExperience())
          Button('体验统计').winCommandButton().onClick(() => this.showLocalExperienceCounts())
        }.width('100%')
        Text('无需注册、外部服务器或配置账号。选择 TCP、SSH 或文件传输，应用会自动填写参数并连接本机服务。')
          .fontSize(13).fontColor('#235E63').width('100%')
        Row({ space: 10 }) {
          Button('TCP 回声体验').winCommandButton().height(36).id('experience-tcp')
            .onClick(() => this.startLocalExperience('tcp'))
          Button('SSH 终端体验').winCommandButton().height(36).id('experience-ssh')
            .onClick(() => this.startLocalExperience('ssh'))
          Button('文件传输体验').winCommandButton().height(36).backgroundColor('#106E7A').fontColor('#FFFFFF')
            .id('experience-files').onClick(() => this.startLocalFileExperience())
          Button('连接自己的服务器').winCommandButton().height(36)
            .onClick(() => this.prepareOwnConnection())
        }.width('100%')
        Text(this.localExperienceText).fontSize(12).fontColor('#16464B').width('100%')
      }.width('100%').padding({ left: 14, right: 14, top: 8, bottom: 8 }).backgroundColor('#E5F4F1')
      Row() {
        Text('File')
          .fontSize(13)
          .fontColor('#1F1F1F')
          .width(56)
          .height(26)
          .textAlign(TextAlign.Center)
          .backgroundColor(this.toolsVisible && this.activeToolsPanel === 'file' ? '#DADCE0' : '#F4F4F4')
          .onClick(() => this.openToolsPanel('file'))
        Text('Edit')
          .fontSize(13)
          .fontColor('#1F1F1F')
          .width(56)
          .height(26)
          .textAlign(TextAlign.Center)
          .backgroundColor(this.toolsVisible && this.activeToolsPanel === 'edit' ? '#DADCE0' : '#F4F4F4')
          .onClick(() => this.openToolsPanel('edit'))
        Text('Setup')
          .fontSize(13)
          .fontColor('#1F1F1F')
          .width(72)
          .height(26)
          .textAlign(TextAlign.Center)
          .backgroundColor(this.toolsVisible && this.activeToolsPanel === 'setup' ? '#DADCE0' : '#F4F4F4')
          .onClick(() => this.openToolsPanel('setup'))
        Text('Control')
          .fontSize(13)
          .fontColor('#1F1F1F')
          .width(84)
          .height(26)
          .textAlign(TextAlign.Center)
          .backgroundColor(this.toolsVisible && this.activeToolsPanel === 'control' ? '#DADCE0' : '#F4F4F4')
          .onClick(() => this.openToolsPanel('control'))
        Text('Window')
          .fontSize(13)
          .fontColor('#1F1F1F')
          .width(84)
          .height(26)
          .textAlign(TextAlign.Center)
          .backgroundColor(this.toolsVisible && this.activeToolsPanel === 'window' ? '#DADCE0' : '#F4F4F4')
          .onClick(() => this.openToolsPanel('window'))
        Text('Help')
          .fontSize(13)
          .fontColor('#1F1F1F')
          .width(56)
          .height(26)
          .textAlign(TextAlign.Center)
          .backgroundColor(this.toolsVisible && this.activeToolsPanel === 'help' ? '#DADCE0' : '#F4F4F4')
          .onClick(() => this.openToolsPanel('help'))
      }
      .height(28)
      .padding({ left: 14, right: 14 })
      .backgroundColor('#F4F4F4')
      .alignItems(VerticalAlign.Center)
      .width('100%')

      Row() {
        ForEach(this.tabs, (tab: TerminalTabRecord, index: number) => {
          Button(tab.title, { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .type(ButtonType.Normal)
            .height(32)
            .fontSize(13)
            .fontColor(index === this.activeTabIndex ? '#111111' : '#333333')
            .backgroundColor(index === this.activeTabIndex ? '#FFFFFF' : '#E9EAED')
            .borderRadius(0)
            .onClick(() => this.switchTab(index))
        }, (tab: TerminalTabRecord) => `${tab.sessionId}:${tab.title}`)
        Button('+', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .height(32)
          .fontSize(15)
          .fontColor('#111111')
          .backgroundColor('#E9EAED')
          .borderRadius(0)
          .onClick(() => this.createTab())
        Button('Close', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .enabled(this.tabs.length > 1)
          .height(32)
          .fontSize(13)
          .fontColor('#333333')
          .backgroundColor('#E9EAED')
          .borderRadius(0)
          .onClick(() => this.closeActiveTab())
      }
      .height(40)
      .padding({ left: 12, right: 12 })
      .backgroundColor('#ECEFF3')
      .alignItems(VerticalAlign.Center)
      .width('100%')

      Row() {
        Button('TCP', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .type(ButtonType.Normal)
          .height(34)
          .fontSize(13)
          .fontColor('#111111')
          .backgroundColor(this.protocol === 'tcp' ? '#DCEBFF' : '#F6F6F6')
          .borderRadius(0)
          .onClick(() => this.selectProtocol('tcp'))
        Button('Telnet', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .type(ButtonType.Normal)
          .height(34)
          .fontSize(13)
          .fontColor('#111111')
          .backgroundColor(this.protocol === 'telnet' ? '#DCEBFF' : '#F6F6F6')
          .borderRadius(0)
          .onClick(() => this.selectProtocol('telnet'))
        Button('SSH', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .type(ButtonType.Normal)
          .height(34)
          .fontSize(13)
          .fontColor('#111111')
          .backgroundColor(this.protocol === 'ssh' ? '#DCEBFF' : '#F6F6F6')
          .borderRadius(0)
          .onClick(() => this.selectProtocol('ssh'))
        Button('Serial', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .type(ButtonType.Normal)
          .height(34)
          .fontSize(13)
          .fontColor('#111111')
          .backgroundColor(this.protocol === 'serial' ? '#DCEBFF' : '#F6F6F6')
          .borderRadius(0)
          .onClick(() => this.selectProtocol('serial'))
        TextInput({ placeholder: this.hostPlaceholder(), text: this.host, controller: this.inputController('host') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'host', this.host, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
          .onChange((value: string) => {
            this.host = value;
            this.saveActiveTab();
          })
          .enterKeyType(EnterKeyType.Go)
          .onSubmit((_enterKey: EnterKeyType, _event: SubmitEvent) => {
            this.connect();
          })
          .layoutWeight(1)
        TextInput({ placeholder: this.portPlaceholder(), text: this.port, controller: this.inputController('port') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'port', this.port, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
          .type(InputType.Number)
          .onChange((value: string) => {
            this.port = value;
            this.saveActiveTab();
          })
          .enterKeyType(EnterKeyType.Go)
          .onSubmit((_enterKey: EnterKeyType, _event: SubmitEvent) => {
            this.connect();
          })
          .width(96)
        Button('Connect', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .height(34)
          .fontSize(13)
          .fontColor('#111111')
          .backgroundColor('#F6F6F6')
          .borderRadius(0)
          .onClick(() => this.connect())
        Button('Disconnect', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .height(34)
          .fontSize(13)
          .fontColor('#111111')
          .backgroundColor('#F6F6F6')
          .borderRadius(0)
          .onClick(() => this.disconnect())
        Button(this.toolsVisible ? '收起工具 / Tools' : '工具 / Tools', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .height(34)
          .fontSize(13)
          .fontColor('#111111')
          .backgroundColor(this.toolsVisible ? '#DCEBFF' : '#F6F6F6')
          .borderRadius(0)
          .onClick(() => this.toggleDefaultToolsPanel())
      }
      .height(48)
      .padding({ left: 12, right: 12 })
      .backgroundColor('#F7F7F2')
      .alignItems(VerticalAlign.Center)
      .width('100%')

      if (this.toolsVisible && (this.activeToolsPanel === 'setup' || this.activeToolsPanel === 'connection') &&
        this.protocol !== 'serial') {
        Row() {
        Button(this.proxyEnabled ? 'Proxy On' : 'Proxy Off', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .type(ButtonType.Normal)
          .backgroundColor(this.proxyEnabled ? '#DCEBFF' : '#F0F0F0')
          .onClick(() => {
            this.proxyEnabled = !this.proxyEnabled;
            this.saveActiveTab();
          })
        Button('HTTP', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .type(ButtonType.Normal)
          .backgroundColor(this.proxyType === 'http' ? '#DCEBFF' : '#F0F0F0')
          .enabled(this.proxyEnabled)
          .onClick(() => {
            this.proxyType = 'http';
            if (this.proxyPort === '' || this.proxyPort === '23' || this.proxyPort === '1080') {
              this.proxyPort = '8080';
            }
            this.saveActiveTab();
          })
        Button('TELNET', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .type(ButtonType.Normal)
          .backgroundColor(this.proxyType === 'telnet' ? '#DCEBFF' : '#F0F0F0')
          .enabled(this.proxyEnabled)
          .onClick(() => {
            this.proxyType = 'telnet';
            if (this.proxyPort === '' || this.proxyPort === '8080' || this.proxyPort === '1080') {
              this.proxyPort = '23';
            }
            this.saveActiveTab();
          })
        Button('SOCKS4', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .type(ButtonType.Normal)
          .backgroundColor(this.proxyType === 'socks4' ? '#DCEBFF' : '#F0F0F0')
          .enabled(this.proxyEnabled)
          .onClick(() => {
            this.proxyType = 'socks4';
            if (this.proxyPort === '' || this.proxyPort === '23' || this.proxyPort === '8080') {
              this.proxyPort = '1080';
            }
            this.saveActiveTab();
          })
        Button('SOCKS5', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .type(ButtonType.Normal)
          .backgroundColor(this.proxyType === 'socks5' ? '#DCEBFF' : '#F0F0F0')
          .enabled(this.proxyEnabled)
          .onClick(() => {
            this.proxyType = 'socks5';
            if (this.proxyPort === '' || this.proxyPort === '23' || this.proxyPort === '8080') {
              this.proxyPort = '1080';
            }
            this.saveActiveTab();
          })
        TextInput({ placeholder: 'Proxy host', text: this.proxyHost, controller: this.inputController('proxyHost') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'proxyHost', this.proxyHost, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
          .enabled(this.proxyEnabled)
          .onChange((value: string) => {
            this.proxyHost = value;
            this.saveActiveTab();
          })
          .layoutWeight(1)
        TextInput({ placeholder: 'Proxy port', text: this.proxyPort, controller: this.inputController('proxyPort') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'proxyPort', this.proxyPort, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
          .enabled(this.proxyEnabled)
          .type(InputType.Number)
          .onChange((value: string) => {
            this.proxyPort = value;
            this.saveActiveTab();
          })
          .width(110)
        TextInput({ placeholder: 'Proxy user', text: this.proxyUsername, controller: this.inputController('proxyUsername') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'proxyUsername', this.proxyUsername, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
          .enabled(this.proxyEnabled)
          .onChange((value: string) => {
            this.proxyUsername = value;
            this.saveActiveTab();
          })
          .width(160)
        TextInput({ placeholder: 'Proxy password', text: this.proxyPassword, controller: this.inputController('proxyPassword') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'proxyPassword', this.proxyPassword, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
          .enabled(this.proxyEnabled)
          .type(InputType.Password)
          .onChange((value: string) => {
            this.proxyPassword = value;
            this.saveActiveTab();
          })
          .width(180)
      }
      .height(52)
      .padding({ left: 12, right: 12 })
      .backgroundColor('#F3F5F6')
      .alignItems(VerticalAlign.Center)
      .width('100%')
      }

      if (this.toolsVisible && this.activeToolsPanel === 'setup' && this.protocol === 'serial') {
        Row() {
          Button('7-bit', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .type(ButtonType.Normal)
            .backgroundColor(this.serialDataBits === 7 ? '#DCEBFF' : '#F0F0F0')
            .onClick(() => {
              this.serialDataBits = 7;
              this.saveActiveTab();
            })
          Button('8-bit', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .type(ButtonType.Normal)
            .backgroundColor(this.serialDataBits === 8 ? '#DCEBFF' : '#F0F0F0')
            .onClick(() => {
              this.serialDataBits = 8;
              this.saveActiveTab();
            })
          Button('Stop 1', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .type(ButtonType.Normal)
            .backgroundColor(this.serialStopBits === 1 ? '#DCEBFF' : '#F0F0F0')
            .onClick(() => {
              this.serialStopBits = 1;
              this.saveActiveTab();
            })
          Button('Stop 2', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .type(ButtonType.Normal)
            .backgroundColor(this.serialStopBits === 2 ? '#DCEBFF' : '#F0F0F0')
            .onClick(() => {
              this.serialStopBits = 2;
              this.saveActiveTab();
            })
          Button('None', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .type(ButtonType.Normal)
            .backgroundColor(this.serialParity === 'none' ? '#DCEBFF' : '#F0F0F0')
            .onClick(() => {
              this.serialParity = 'none';
              this.saveActiveTab();
            })
          Button('Odd', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .type(ButtonType.Normal)
            .backgroundColor(this.serialParity === 'odd' ? '#DCEBFF' : '#F0F0F0')
            .onClick(() => {
              this.serialParity = 'odd';
              this.saveActiveTab();
            })
          Button('Even', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .type(ButtonType.Normal)
            .backgroundColor(this.serialParity === 'even' ? '#DCEBFF' : '#F0F0F0')
            .onClick(() => {
              this.serialParity = 'even';
              this.saveActiveTab();
            })
          Button('No Flow', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .type(ButtonType.Normal)
            .backgroundColor(this.serialFlowControl === 'none' ? '#DCEBFF' : '#F0F0F0')
            .onClick(() => {
              this.serialFlowControl = 'none';
              this.saveActiveTab();
            })
          Button('XON', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .type(ButtonType.Normal)
            .backgroundColor(this.serialFlowControl === 'software' ? '#DCEBFF' : '#F0F0F0')
            .onClick(() => {
              this.serialFlowControl = 'software';
              this.saveActiveTab();
            })
          Button('RTS', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .type(ButtonType.Normal)
            .backgroundColor(this.serialFlowControl === 'hardware' ? '#DCEBFF' : '#F0F0F0')
            .onClick(() => {
              this.serialFlowControl = 'hardware';
              this.saveActiveTab();
            })
        }
        .height(52)
        .padding({ left: 12, right: 12 })
        .backgroundColor('#FBFBF8')
        .alignItems(VerticalAlign.Center)
        .width('100%')
      }

      if (this.toolsVisible && this.activeToolsPanel === 'setup' && this.protocol === 'serial') {
        Row() {
          Text(this.serialScanStatus)
            .fontSize(12)
            .fontColor('#4A4A4A')
            .maxLines(1)
            .textOverflow({ overflow: TextOverflow.Ellipsis })
            .width(220)
          Button('Scan Serial', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .onClick(() => this.scanSerialDevices())
          Select(this.serialDeviceSelectOptions())
            .selected(this.serialDeviceCandidates.length === 0 ? 0 : this.selectedSerialDeviceIndex)
            .onSelect((index: number) => this.selectSerialDevice(index))
            .layoutWeight(1)
          Button('Use Path', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .onClick(() => {
              this.serialScanStatus = this.host.trim().length === 0 ? 'Enter a serial device path' :
                `Using ${this.host.trim()}`;
              this.saveActiveTab();
            })
        }
        .height(48)
        .padding({ left: 12, right: 12 })
        .backgroundColor('#F6F7F8')
        .alignItems(VerticalAlign.Center)
        .width('100%')
      }

      if (this.toolsVisible && (this.activeToolsPanel === 'connection' || this.activeToolsPanel === 'setup') && this.protocol === 'ssh') {
        Row() {
          TextInput({ placeholder: 'User', text: this.username, controller: this.inputController('username') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'username', this.username, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
            .onChange((value: string) => {
              this.username = value;
              this.saveActiveTab();
            })
            .width(180)
          TextInput({ placeholder: 'Password', text: this.password, controller: this.inputController('password') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'password', this.password, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
            .type(InputType.Password)
            .onChange((value: string) => {
              this.password = value;
              this.saveActiveTab();
            })
            .width(220)
          TextInput({ placeholder: 'Private key path', text: this.privateKeyPath, controller: this.inputController('privateKeyPath') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'privateKeyPath', this.privateKeyPath, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
            .onChange((value: string) => {
              this.privateKeyPath = value;
              if (this.publicKeyPath.trim().length === 0 && value.trim().length > 0) {
                this.publicKeyPath = `${value.trim()}.pub`;
              }
              this.saveActiveTab();
            })
            .width(220)
          Button('Key', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .onClick(() => {
              this.pickPrivateKey();
            })
          TextInput({ placeholder: 'Public key path', text: this.publicKeyPath, controller: this.inputController('publicKeyPath') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'publicKeyPath', this.publicKeyPath, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
            .onChange((value: string) => {
              this.publicKeyPath = value;
              this.saveActiveTab();
            })
            .width(220)
          TextInput({ placeholder: 'Passphrase', text: this.passphrase, controller: this.inputController('passphrase') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'passphrase', this.passphrase, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
            .type(InputType.Password)
            .onChange((value: string) => {
              this.passphrase = value;
              this.saveActiveTab();
            })
            .width(180)
        }
        .height(64)
        .padding({ left: 12, right: 12, top: 8, bottom: 8 })
        .backgroundColor('#FBFBF8')
        .alignItems(VerticalAlign.Center)
        .width('100%')
      }

      if (this.toolsVisible && (this.activeToolsPanel === 'connection' || this.activeToolsPanel === 'setup') && this.protocol === 'ssh') {
        Row() {
          Button(this.knownHostPolicy === 'strict' ? 'Strict' : 'Trust New', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .onClick(() => {
              this.knownHostPolicy = this.knownHostPolicy === 'strict' ? 'accept_new' : 'strict';
            })
          TextArea({ placeholder: 'known_hosts', text: this.knownHostsText, controller: this.areaController('knownHostsText') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'knownHostsText', this.knownHostsText, true))
          .onBlur(() => this.resetShortcutModifiers())
                .winTextArea()
            .onChange((value: string) => {
              this.knownHostsText = value;
              this.saveAppState();
            })
            .layoutWeight(1)
            .height(48)
        }
        .height(64)
        .padding({ left: 12, right: 12, top: 8, bottom: 8 })
        .backgroundColor('#FBFBF8')
        .alignItems(VerticalAlign.Center)
        .width('100%')
      }

      if (this.toolsVisible && this.activeToolsPanel === 'file' && this.protocol === 'ssh') {
        Row() {
          Text(this.transferStatus)
            .fontSize(12)
            .fontColor('#4A4A4A')
            .maxLines(1)
            .textOverflow({ overflow: TextOverflow.Ellipsis })
            .width(180)
          TextInput({ placeholder: 'Local path', text: this.transferLocalPath, controller: this.inputController('transferLocalPath') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'transferLocalPath', this.transferLocalPath, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
            .onChange((value: string) => {
              this.transferLocalPath = value;
              this.saveActiveTab();
            })
            .layoutWeight(1)
          Button('Pick', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .onClick(() => {
              this.pickTransferFile();
            })
          TextInput({ placeholder: 'Remote path', text: this.transferRemotePath, controller: this.inputController('transferRemotePath') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'transferRemotePath', this.transferRemotePath, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
            .onChange((value: string) => {
              this.transferRemotePath = value;
              this.saveActiveTab();
            })
            .layoutWeight(1)
          Button('SCP Get', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .onClick(() => this.scpDownload())
          Button('SCP Put', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .onClick(() => this.scpUpload())
          Button('SFTP Get', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .onClick(() => this.sftpDownload())
          Button('SFTP Put', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .onClick(() => this.sftpUpload())
        }
        .height(56)
        .padding({ left: 12, right: 12 })
        .backgroundColor('#F7F8FA')
        .alignItems(VerticalAlign.Center)
        .width('100%')
      }

      if (this.toolsVisible && this.activeToolsPanel === 'setup' && this.protocol === 'ssh') {
        Row() {
          Text(this.forwardStatus)
            .fontSize(12)
            .fontColor('#4A4A4A')
            .maxLines(1)
            .textOverflow({ overflow: TextOverflow.Ellipsis })
            .width(180)
          TextInput({ placeholder: 'Local host', text: this.forwardLocalHost, controller: this.inputController('forwardLocalHost') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'forwardLocalHost', this.forwardLocalHost, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
            .onChange((value: string) => {
              this.forwardLocalHost = value;
              this.saveActiveTab();
            })
            .width(160)
          TextInput({ placeholder: 'Local port', text: this.forwardLocalPort, controller: this.inputController('forwardLocalPort') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'forwardLocalPort', this.forwardLocalPort, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
            .type(InputType.Number)
            .onChange((value: string) => {
              this.forwardLocalPort = value;
              this.saveActiveTab();
            })
            .width(120)
          TextInput({ placeholder: 'Remote host', text: this.forwardRemoteHost, controller: this.inputController('forwardRemoteHost') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'forwardRemoteHost', this.forwardRemoteHost, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
            .onChange((value: string) => {
              this.forwardRemoteHost = value;
              this.saveActiveTab();
            })
            .layoutWeight(1)
          TextInput({ placeholder: 'Remote port', text: this.forwardRemotePort, controller: this.inputController('forwardRemotePort') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'forwardRemotePort', this.forwardRemotePort, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
            .type(InputType.Number)
            .onChange((value: string) => {
              this.forwardRemotePort = value;
              this.saveActiveTab();
            })
            .width(130)
          Button('Forward', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .enabled(this.forwardId === 0)
            .onClick(() => this.startSshForward())
          Button('Stop Fwd', { type: ButtonType.Normal, stateEffect: false })
            .winCommandButton()
            .enabled(this.forwardId !== 0)
            .onClick(() => this.stopSshForward())
        }
        .height(56)
        .padding({ left: 12, right: 12 })
        .backgroundColor('#FAFAFA')
        .alignItems(VerticalAlign.Center)
        .width('100%')
      }

      if (this.toolsVisible && this.activeToolsPanel === 'setup') {
        Row() {
        Button('Dark', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .type(ButtonType.Normal)
          .backgroundColor(this.terminalColorTheme === 'dark' ? '#DCEBFF' : '#F0F0F0')
          .onClick(() => this.setTerminalTheme('dark'))
        Button('Light', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .type(ButtonType.Normal)
          .backgroundColor(this.terminalColorTheme === 'light' ? '#DCEBFF' : '#F0F0F0')
          .onClick(() => this.setTerminalTheme('light'))
        Button('Classic', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .type(ButtonType.Normal)
          .backgroundColor(this.terminalColorTheme === 'classic' ? '#DCEBFF' : '#F0F0F0')
          .onClick(() => this.setTerminalTheme('classic'))
        Button(this.terminalFontFamily, { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .onClick(() => this.cycleTerminalFontFamily())
          .width(190)
        Button('-', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .onClick(() => this.setTerminalFontSize(this.terminalFontSize - 1))
        Text(`${this.terminalFontSize}px`)
          .fontSize(12)
          .fontColor('#4A4A4A')
          .width(54)
          .textAlign(TextAlign.Center)
        Button('+', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .onClick(() => this.setTerminalFontSize(this.terminalFontSize + 1))
      }
      .height(48)
      .padding({ left: 12, right: 12 })
      .backgroundColor('#F8F8F5')
      .alignItems(VerticalAlign.Center)
      .width('100%')
      }

      if (this.toolsVisible && (this.activeToolsPanel === 'file' || this.activeToolsPanel === 'edit' ||
        this.activeToolsPanel === 'control' || this.activeToolsPanel === 'window' ||
        this.activeToolsPanel === 'help')) {
        Scroll() {
          Column() {
          if (this.activeToolsPanel === 'file') {
            Row() {
              TextArea({ placeholder: 'TERATERM.INI [Hosts]', text: this.iniImportText, controller: this.areaController('iniImportText') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'iniImportText', this.iniImportText, true))
          .onBlur(() => this.resetShortcutModifiers())
                .winTextArea()
                .onChange((value: string) => {
                  this.iniImportText = value;
                })
                .layoutWeight(1)
                .height(68)
              Button('Open INI', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .onClick(() => {
                  this.importProfilesFromFile();
                })
              Button('Import', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .onClick(() => this.importProfiles())
              Select(this.importedProfiles.map((profile: ImportedProfile): SelectOption => {
                const option: SelectOption = {
                  value: this.profileLabel(profile),
                };
                return option;
              }))
                .selected(this.selectedProfileIndex < 0 ? 0 : this.selectedProfileIndex)
                .onSelect((index: number) => {
                  this.selectedProfileIndex = index;
                  this.loadSelectedProfile();
                })
                .width(240)
              Button('Save Profile', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .onClick(() => this.saveCurrentProfile())
              Button('Save Log', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .onClick(() => {
                  this.saveLog();
                })
              Button('Sandbox Log', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .onClick(() => this.saveSandboxLog())
            }
            .height(84)
            .padding({ left: 12, right: 12, top: 8, bottom: 8 })
            .backgroundColor('#FAFAFA')
            .alignItems(VerticalAlign.Center)
            .width('100%')
          }

          if (this.activeToolsPanel === 'control') {
            Row() {
              Text(this.macroStatus)
                .fontSize(12)
                .fontColor('#4A4A4A')
                .maxLines(1)
                .textOverflow({ overflow: TextOverflow.Ellipsis })
                .width(220)
              TextArea({ placeholder: 'TTL macro', text: this.macroScript, controller: this.areaController('macroScript') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'macroScript', this.macroScript, true))
          .onBlur(() => this.resetShortcutModifiers())
                .winTextArea()
                .onChange((value: string) => {
                  this.macroScript = value;
                  this.saveActiveTab();
                })
                .layoutWeight(1)
                .height(64)
              Button('Open TTL', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .onClick(() => {
                  this.loadMacroFromFile();
                })
              Button('Run', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .onClick(() => this.startMacro())
              Button('Step', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .enabled(this.macroId !== 0)
                .onClick(() => this.pumpMacroOnce())
              Button('Stop', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .enabled(this.macroId !== 0)
                .onClick(() => this.stopMacro())
            }
            .height(80)
            .padding({ left: 12, right: 12, top: 8, bottom: 8 })
            .backgroundColor('#F7F8FA')
            .alignItems(VerticalAlign.Center)
            .width('100%')
          }

          if (this.activeToolsPanel === 'control' && this.macroDialogType.length > 0) {
            Column() {
              Text(this.macroDialogTitle.length > 0 ? this.macroDialogTitle : 'Macro dialog')
                .fontSize(13)
                .fontWeight(FontWeight.Medium)
                .fontColor('#242424')
                .maxLines(1)
                .textOverflow({ overflow: TextOverflow.Ellipsis })
                .width('100%')
              Text(this.macroDialogText)
                .fontSize(12)
                .fontColor('#3A3A3A')
                .maxLines(4)
                .width('100%')
              if (this.macroDialogType === 'inputbox') {
                TextInput({ placeholder: 'Input', text: this.macroDialogValue, controller: this.inputController('macroDialogValue') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'macroDialogValue', this.macroDialogValue, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
                  .onChange((value: string) => {
                    this.macroDialogValue = value;
                  })
                  .width('100%')
              }
              if (this.macroDialogType === 'passwordbox') {
                TextInput({ placeholder: 'Password', text: this.macroDialogValue, controller: this.inputController('macroDialogValue') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'macroDialogValue', this.macroDialogValue, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
                  .type(InputType.Password)
                  .onChange((value: string) => {
                    this.macroDialogValue = value;
                  })
                  .width('100%')
              }
              if (this.macroDialogType === 'filenamebox' || this.macroDialogType === 'dirnamebox') {
                TextInput({
                  placeholder: this.macroDialogType === 'dirnamebox' ? 'Folder path' :
                    (this.macroDialogSave ? 'Save file path' : 'Open file path'),
                  text: this.macroDialogValue, controller: this.inputController('macroDialogValue') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'macroDialogValue', this.macroDialogValue, false))
          .onBlur(() => this.resetShortcutModifiers())
                  .winTextInput()
                  .onChange((value: string) => {
                    this.macroDialogValue = value;
                  })
                  .width('100%')
              }
              if (this.macroDialogType === 'listbox') {
                Column() {
                  ForEach(this.macroDialogItems, (item: string, index: number) => {
                    Button(`${this.macroDialogSelectedIndex === index ? '* ' : ''}${item.length > 0 ? item : '(empty)'}`, { type: ButtonType.Normal, stateEffect: false })
                      .winCommandButton()
                      .onClick(() => {
                        this.macroDialogSelectedIndex = index;
                        this.macroDialogValue = `${index}`;
                      })
                      .width('100%')
                  }, (item: string, index: number) => `${index}:${item}`)
                }
                .width('100%')
              }
              Row() {
                if (this.macroDialogType === 'yesnobox') {
                  Button('Yes', { type: ButtonType.Normal, stateEffect: false })
                    .winCommandButton()
                    .onClick(() => this.answerMacroDialog(true))
                  Button('No', { type: ButtonType.Normal, stateEffect: false })
                    .winCommandButton()
                    .onClick(() => this.answerMacroDialog(false))
                } else if (this.macroDialogType === 'inputbox' || this.macroDialogType === 'passwordbox' ||
                  this.macroDialogType === 'filenamebox' || this.macroDialogType === 'dirnamebox' ||
                  this.macroDialogType === 'listbox') {
                  Button('OK', { type: ButtonType.Normal, stateEffect: false })
                    .winCommandButton()
                    .onClick(() => this.answerMacroDialog(true))
                  Button('Cancel', { type: ButtonType.Normal, stateEffect: false })
                    .winCommandButton()
                    .onClick(() => this.answerMacroDialog(false))
                } else {
                  Button('OK', { type: ButtonType.Normal, stateEffect: false })
                    .winCommandButton()
                    .onClick(() => this.answerMacroDialog(true))
                }
              }
              .width('100%')
              .justifyContent(FlexAlign.End)
            }
            .padding({ left: 12, right: 12, top: 8, bottom: 8 })
            .backgroundColor('#FFFBEA')
            .width('100%')
          }

          if (this.activeToolsPanel === 'control' && this.macroStatusBoxVisible) {
            Column() {
              Text(this.macroStatusBoxTitle.length > 0 ? this.macroStatusBoxTitle : 'Macro status')
                .fontSize(13)
                .fontWeight(FontWeight.Medium)
                .fontColor('#1F2A44')
                .maxLines(1)
                .textOverflow({ overflow: TextOverflow.Ellipsis })
                .width('100%')
              Text(this.macroStatusBoxText)
                .fontSize(12)
                .fontColor('#253858')
                .maxLines(4)
                .width('100%')
            }
            .padding({ left: 12, right: 12, top: 8, bottom: 8 })
            .backgroundColor('#EEF6FF')
            .width('100%')
          }

          if (this.activeToolsPanel === 'edit') {
            Row() {
              Text(this.searchStatus)
                .fontSize(12)
                .fontColor('#4A4A4A')
                .maxLines(1)
                .textOverflow({ overflow: TextOverflow.Ellipsis })
                .width(160)
              TextInput({ placeholder: 'Search output', text: this.searchText, controller: this.inputController('searchText') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'searchText', this.searchText, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
                .onChange((value: string) => {
                  this.searchText = value;
                  this.saveActiveTab();
                })
                .layoutWeight(1)
              Button('Find', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .onClick(() => this.searchOutput())
              Button('Copy', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .onClick(() => {
                  this.copyScreen();
                })
              PasteButton({ icon: PasteIconStyle.LINES, text: PasteDescription.PASTE, buttonType: ButtonType.Normal })
                .height(32)
                .onClick((_event: ClickEvent, result: PasteButtonOnClickResult): void => {
                  if (result === PasteButtonOnClickResult.SUCCESS) {
                    this.pasteTerminal();
                    return;
                  }
                  this.status = 'Paste authorization failed';
                  this.saveActiveTab();
                })
              Button('Copy Sel', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .enabled(this.selectionStartLine >= 0)
                .onClick(() => {
                  this.copySelection();
                })
              Button('Clear Sel', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .enabled(this.selectionStartLine >= 0)
                .onClick(() => this.clearSelection())
              Button('Copy Log', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .onClick(() => {
                  this.copyLog();
                })
              Button('Log', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .onClick(() => this.drainLog())
            }
            .height(52)
            .padding({ left: 12, right: 12 })
            .backgroundColor('#FAFBFC')
            .alignItems(VerticalAlign.Center)
            .width('100%')
          }

          if (this.activeToolsPanel === 'window') {
            Row() {
              Text(`Tabs: ${this.tabs.length}`)
                .fontSize(12)
                .fontColor('#4A4A4A')
                .width(160)
              Button('New Window', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .onClick(() => this.createTab())
              Button('Close Window', { type: ButtonType.Normal, stateEffect: false })
                .winCommandButton()
                .enabled(this.tabs.length > 1)
                .onClick(() => this.closeActiveTab())
            }
            .height(52)
            .padding({ left: 12, right: 12 })
            .backgroundColor('#FAFBFC')
            .alignItems(VerticalAlign.Center)
            .width('100%')
          }

          if (this.activeToolsPanel === 'help') {
            Row() {
              Text(`泊舟连接台 1.0.3  |  ${this.status}`)
                .fontSize(12)
                .fontColor('#333333')
                .maxLines(1)
                .textOverflow({ overflow: TextOverflow.Ellipsis })
                .layoutWeight(1)
              Button('隐私说明').onClick(() => { this.helpNoticeText = PRIVACY_NOTICE; })
              Button('开源许可').onClick(() => { this.helpNoticeText = RELEASE_NOTICES; })
            }
            .height(52)
            .padding({ left: 12, right: 12 })
            .backgroundColor('#FAFBFC')
            .alignItems(VerticalAlign.Center)
            .width('100%')
            Scroll() {
              Text(this.helpNoticeText)
                .fontSize(13)
                .fontColor('#273546')
                .width('100%')
                .padding(12)
            }
            .height(170)
            .width('100%')
          }
          }
          .width('100%')
        }
        .height(this.activeToolsPanel === 'help' ? 240 : (this.activeToolsPanel === 'control' ? 180 : (this.activeToolsPanel === 'file' ? 96 : 64)))
        .backgroundColor('#FAFBFC')
        .width('100%')
      }

      TerminalSurface({
        scrollbackRows: this.scrollbackRows,
        rows: this.rows,
        cells: this.cells,
        cursorRow: this.cursorRow,
        cursorCol: this.cursorCol,
        fontFamily: this.terminalFontFamily,
        fontSize: this.terminalFontSize,
        lineHeight: this.terminalLineHeight,
        colorTheme: this.terminalColorTheme,
        selectedStartLine: this.selectionStartLine,
        selectedEndLine: this.selectionEndLine,
        selectedStartCol: this.selectionStartCol,
        selectedEndCol: this.selectionEndCol,
        onLineClick: (_lineIndex: number): void => {
          this.focusTerminalKeyboard();
        },
        onLineSelectStart: (lineIndex: number, colIndex: number): void => {
          this.beginTerminalLineSelection(lineIndex, colIndex);
        },
        onLineSelectUpdate: (lineIndex: number, colIndex: number): void => {
          this.updateTerminalLineSelection(lineIndex, colIndex);
        },
        onKeyboardInput: (event: KeyEvent): boolean => this.handleTerminalKeyEvent(event),
        onViewportResize: (rows: number, cols: number): void => {
          this.resizeTerminalViewport(rows, cols);
        },
      })

      TextInput({ text: this.terminalKeyCaptureText })
          .winTextInput()
        .id('terminal-key-capture')
        .type(InputType.Normal)
        .caretColor('#00000000')
        .fontColor('#00000000')
        .backgroundColor('#00000000')
        .borderWidth(0)
        .opacity(0)
        .width(1)
        .height(1)
        .focusable(true)
        .enterKeyType(EnterKeyType.Send)
        .onChange((value: string) => this.captureTerminalText(value))
        .onSubmit((_enterKey: EnterKeyType, _event: SubmitEvent) => {
          this.terminalKeyCaptureText = '';
          this.sendTerminalKeyboardPayload('\r');
        })
        .onKeyPreIme((event: KeyEvent): boolean => this.handleTerminalCaptureKeyEvent(event))
        .onBlur(() => this.resetShortcutModifiers())

      Row() {
        Text(this.status)
          .fontSize(12)
          .fontColor('#4A4A4A')
          .maxLines(1)
          .textOverflow({ overflow: TextOverflow.Ellipsis })
          .width(220)
        TextInput({ placeholder: 'Input', text: this.inputText, controller: this.inputController('inputText') })
          .onKeyPreIme((event: KeyEvent): boolean => this.handleTextShortcut(event, 'inputText', this.inputText, false))
          .onBlur(() => this.resetShortcutModifiers())
          .winTextInput()
          .onChange((value: string) => {
            this.inputText = value;
            this.saveActiveTab();
          })
          .enterKeyType(EnterKeyType.Send)
          .onSubmit((_enterKey: EnterKeyType, _event: SubmitEvent) => {
            this.sendInput();
          })
          .layoutWeight(1)
        PasteButton({ icon: PasteIconStyle.LINES, text: PasteDescription.PASTE, buttonType: ButtonType.Normal })
          .height(34)
          .onClick((_event: ClickEvent, result: PasteButtonOnClickResult): void => {
            if (result === PasteButtonOnClickResult.SUCCESS) {
              this.pasteInput();
              return;
            }
            this.status = 'Paste authorization failed';
            this.saveActiveTab();
          })
        Button('Send', { type: ButtonType.Normal, stateEffect: false })
          .winCommandButton()
          .height(34)
          .fontSize(13)
          .fontColor('#111111')
          .backgroundColor('#F6F6F6')
          .borderRadius(0)
          .onClick(() => this.sendInput())
      }
      .height(48)
      .padding({ left: 12, right: 12 })
      .backgroundColor('#FFFFFF')
      .alignItems(VerticalAlign.Center)
      .width('100%')
    }
    .height('100%')
    .width('100%')
    .backgroundColor('#FFFFFF')
  }
}
