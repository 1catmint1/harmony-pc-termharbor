import dataPreferences from '@ohos.data.preferences';
import type common from '@ohos.app.ability.common';
import type { ImportedProfile, TerminalColorTheme, TerminalDisplaySettings } from '../model/TerminalTypes';
import { ProfileRepository } from './ProfileRepository';

export interface PersistedAppState {
  profiles: ImportedProfile[];
  knownHostsText: string;
  displaySettings: TerminalDisplaySettings;
}

export class PersistentProfileStore {
  private static readonly storeName: string = 'teraterm_user_state';
  private static readonly profilesKey: string = 'profiles_json';
  private static readonly knownHostsKey: string = 'known_hosts_text';
  private static readonly displaySettingsKey: string = 'display_settings_json';

  static defaultDisplaySettings(): TerminalDisplaySettings {
    return {
      fontFamily: 'HarmonyOS Sans Mono',
      fontSize: 14,
      lineHeight: 20,
      colorTheme: 'dark',
    };
  }

  static async load(context: common.Context): Promise<PersistedAppState> {
    const preferences = await dataPreferences.getPreferences(context, PersistentProfileStore.storeName);
    const profilesValue = await preferences.get(PersistentProfileStore.profilesKey, '');
    const knownHostsValue = await preferences.get(PersistentProfileStore.knownHostsKey, '');
    const displaySettingsValue = await preferences.get(PersistentProfileStore.displaySettingsKey, '');
    const profilesJson = typeof profilesValue === 'string' ? profilesValue : '';
    const knownHostsText = typeof knownHostsValue === 'string' ? knownHostsValue : '';
    const displaySettingsJson = typeof displaySettingsValue === 'string' ? displaySettingsValue : '';
    return {
      profiles: ProfileRepository.deserialize(profilesJson),
      knownHostsText,
      displaySettings: PersistentProfileStore.deserializeDisplaySettings(displaySettingsJson),
    };
  }

  static async save(context: common.Context,
                    profiles: ImportedProfile[],
                    knownHostsText: string,
                    displaySettings: TerminalDisplaySettings): Promise<void> {
    const preferences = await dataPreferences.getPreferences(context, PersistentProfileStore.storeName);
    await preferences.put(PersistentProfileStore.profilesKey, ProfileRepository.serialize(profiles));
    await preferences.put(PersistentProfileStore.knownHostsKey, knownHostsText);
    await preferences.put(PersistentProfileStore.displaySettingsKey, JSON.stringify(displaySettings));
    await preferences.flush();
  }

  private static deserializeDisplaySettings(jsonText: string): TerminalDisplaySettings {
    const defaults = PersistentProfileStore.defaultDisplaySettings();
    if (jsonText.trim().length === 0) {
      return defaults;
    }
    try {
      const parsed = JSON.parse(jsonText) as TerminalDisplaySettings;
      const fontSize = PersistentProfileStore.clampNumber(parsed.fontSize, 10, 24, defaults.fontSize);
      const lineHeight = PersistentProfileStore.clampNumber(parsed.lineHeight, fontSize + 2, 36, fontSize + 6);
      return {
        fontFamily: typeof parsed.fontFamily === 'string' && parsed.fontFamily.length > 0 ?
          parsed.fontFamily : defaults.fontFamily,
        fontSize,
        lineHeight,
        colorTheme: PersistentProfileStore.normalizeTheme(parsed.colorTheme),
      };
    } catch (error) {
      return defaults;
    }
  }

  private static clampNumber(value: number, minimum: number, maximum: number, fallback: number): number {
    if (typeof value !== 'number' || Number.isNaN(value)) {
      return fallback;
    }
    return Math.max(minimum, Math.min(maximum, Math.round(value)));
  }

  private static normalizeTheme(value: string): TerminalColorTheme {
    if (value === 'light' || value === 'classic') {
      return value;
    }
    return 'dark';
  }
}
