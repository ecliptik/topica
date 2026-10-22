/*
 * settings.h: the preferences in use (core/prefs.h), the file they live in
 * (Topica Preferences, in the Preferences folder or the System Folder), and
 * the Preferences dialog.
 */
#ifndef TOPICA_SETTINGS_H
#define TOPICA_SETTINGS_H

#include "../core/prefs.h"

extern TopicaPrefs gPrefs;

void SettingsLoad(void);
void SettingsSave(void);
/* The dialog: 1 when something changed (and was saved) */
Boolean SettingsDialog(void);
/* The editor's font number for gPrefs.font (Monaco if it isn't installed) */
short SettingsFontNumber(void);

#endif
