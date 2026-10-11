# OpenNR 2.20.5 MO2 rollback

1. Close SkyrimVR, SteamVR, and Mod Organizer.
2. In MO2, select the existing profile **<MO2_PROFILE>**. The current selection is **<MO2_PROFILE>**.
3. Verify `selected_profile=@ByteArray(<MO2_PROFILE>)` in `<MO2_INSTANCE>\ModOrganizer.ini`.
4. If MO2 cannot read the profile selection, restore the pre-install backup `<RESEARCH_DIR>\opennr_2_20_5_dungeon_fix_20261002\ModOrganizer.ini.before-install` over `<MO2_INSTANCE>\ModOrganizer.ini` while MO2 is closed. Its SHA-256 is in `install_manifest.json`.
5. Keep the 2.20.5 profile/mod for inspection until the 2.20.4 profile selection and enabled DLL provider have been read back and verified. The 2.20.4 profile, mod, overwrite settings, and installed files were left intact.