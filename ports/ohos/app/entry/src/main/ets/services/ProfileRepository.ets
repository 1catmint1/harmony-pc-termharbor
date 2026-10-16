import type { ConnectProfile, ImportedProfile, ProtocolMode } from '../model/TerminalTypes';
import { TerminalNative } from './TerminalNative';

interface StoredProfileDocument {
  version: number;
  profiles: ImportedProfile[];
}

export class ProfileRepository {
  static importIniText(iniText: string): ImportedProfile[] {
    return TerminalNative.importIniProfiles(iniText);
  }

  static serialize(profiles: ImportedProfile[]): string {
    const document: StoredProfileDocument = {
      version: 1,
      profiles,
    };
    return JSON.stringify(document);
  }

  static deserialize(jsonText: string): ImportedProfile[] {
    if (jsonText.trim().length === 0) {
      return [];
    }
    const value = JSON.parse(jsonText) as StoredProfileDocument;
    if (!value || value.version !== 1 || !Array.isArray(value.profiles)) {
      return [];
    }
    return value.profiles;
  }

  static toConnectProfile(profile: ImportedProfile): ConnectProfile | undefined {
    if (profile.protocol !== 'tcp' && profile.protocol !== 'telnet' &&
        profile.protocol !== 'ssh' && profile.protocol !== 'serial') {
      return undefined;
    }
    if (profile.protocol === 'serial') {
      const devicePath = profile.serialDevicePath || profile.host;
      if (devicePath.length === 0) {
        return undefined;
      }
      return {
        protocol: 'serial',
        host: devicePath,
        port: profile.baudRate || 9600,
        serial: {
          devicePath,
          baudRate: profile.baudRate || 9600,
          dataBits: profile.dataBits || 8,
          stopBits: profile.stopBits || 1,
          parity: profile.parity || 'none',
          flowControl: profile.flowControl || 'none',
        },
      };
    }
    const proxyHost = profile.proxyHost || '';
    return {
      protocol: profile.protocol as ProtocolMode,
      host: profile.host,
      port: profile.port,
      proxy: (profile.proxyType === 'http' || profile.proxyType === 'telnet' || profile.proxyType === 'socks4' ||
        profile.proxyType === 'socks5') && proxyHost.length > 0 ? {
        type: profile.proxyType,
        host: proxyHost,
        port: profile.proxyPort || (profile.proxyType === 'http' ? 8080 : profile.proxyType === 'telnet' ? 23 : 1080),
        username: profile.proxyUsername || '',
        password: profile.proxyPassword || '',
      } : undefined,
    };
  }

  static connectableProfiles(profiles: ImportedProfile[]): ImportedProfile[] {
    return profiles.filter((profile: ImportedProfile) => {
      return profile.protocol === 'tcp' || profile.protocol === 'telnet' ||
        profile.protocol === 'ssh' || profile.protocol === 'serial';
    });
  }
}
