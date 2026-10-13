// SPDX-License-Identifier: GPL-3.0-only
import { Label } from '@/components/ui/label';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { STRINGS, chooseLang, chosenLang, t } from '@/lib/board/i18n.js';

/* At the bottom of the settings window: follow the system language, or pick one (each file names itself) */
export function LanguagePicker() {
  return (
    <div className="flex items-center justify-center gap-2">
      <Label htmlFor="bb-lang" className="text-xs font-normal text-muted-foreground">
        {t('lang.label')}
      </Label>
      <Select value={chosenLang() || 'system'} onValueChange={(v) => chooseLang(v === 'system' ? '' : v)}>
        <SelectTrigger id="bb-lang" size="sm" className="text-xs">
          <SelectValue />
        </SelectTrigger>
        <SelectContent>
          <SelectItem value="system">{t('lang.system')}</SelectItem>
          {Object.entries(STRINGS).map(([code, table]) => (
            <SelectItem key={code} value={code}>
              {(table as Record<string, string>)['lang.name']}
            </SelectItem>
          ))}
        </SelectContent>
      </Select>
    </div>
  );
}
