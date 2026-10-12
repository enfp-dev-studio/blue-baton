// SPDX-License-Identifier: GPL-3.0-only
import { STRINGS, chooseLang, chosenLang, t } from '@/lib/board/i18n.js';

/* At the bottom of the settings window: follow the system language, or pick one (each table names itself) */
export function LanguagePicker() {
  return (
    <div className="flex items-center justify-center gap-2 text-xs text-muted-foreground">
      <label htmlFor="bb-lang">{t('lang.label')}</label>
      <select
        id="bb-lang"
        value={chosenLang()}
        onChange={(e) => chooseLang(e.target.value)}
        className="rounded-md border bg-card px-2 py-1 text-xs text-foreground"
      >
        <option value="">{t('lang.system')}</option>
        {Object.entries(STRINGS).map(([code, table]) => (
          <option key={code} value={code}>
            {(table as Record<string, string>)['lang.name']}
          </option>
        ))}
      </select>
    </div>
  );
}
