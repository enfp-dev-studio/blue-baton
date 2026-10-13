// SPDX-License-Identifier: GPL-3.0-only
import { Cpu } from 'lucide-react';
import type { ConfirmRequest } from '@/components/Dialogs';
import { Button } from '@/components/ui/button';
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from '@/components/ui/dialog';
import { Group } from '@/components/parts';
import { Progress } from '@/components/ui/progress';
import { t } from '@/lib/board/i18n.js';
import { compareVersions } from '@/lib/board/version.js';
import type { Actions, BoardState } from '@/lib/board/types';

type Confirm = (req: ConfirmRequest) => void;

/*
 * The board's firmware version, against the one that comes with the app (app/src-tauri/src/firmware.rs) when there
 * is one: an older board gets an update that keeps its pairings; a newer one (a development board, or an old app)
 * only a note.
 */
export function FirmwareGroup({ s, a, confirm }: { s: BoardState; a: Actions; confirm: Confirm }) {
  if (!s.fw) return null;
  const order = s.fwBundled ? compareVersions(s.fw, s.fwBundled) : NaN;
  const update = () =>
    confirm({
      title: t('fw.updateConfirmTitle', { version: s.fwBundled }),
      body: t('fw.updateConfirmBody'),
      confirmText: t('fw.update'),
      destructive: false,
      action: () => a.writeFirmware(true),
    });
  return (
    <Group title={t('fw.title')}>
      <div className="flex min-h-[44px] items-center justify-between gap-3 px-3 py-2">
        <div className="min-w-0">
          <div className="text-ui">{t('fw.current', { version: s.fw })}</div>
          {order > 0 && <div className="text-xs text-muted-foreground">{t('fw.newer', { version: s.fwBundled })}</div>}
        </div>
        {order < 0 && (
          <div className="flex shrink-0 items-center gap-2">
            <span className="text-xs text-muted-foreground">{t('fw.updateAvailable', { version: s.fwBundled })}</span>
            <Button size="sm" variant="outline" disabled={!!s.fwJob} onClick={update}>
              {t('fw.update')}
            </Button>
          </div>
        )}
        {order === 0 && <span className="shrink-0 text-xs text-muted-foreground">{t('fw.upToDate')}</span>}
      </div>
    </Group>
  );
}

/* An ESP32-S3 board that does not answer as Blue Baton (a new board, or one with other firmware): offer to install */
export function FirmwareInstall({ s, a, confirm }: { s: BoardState; a: Actions; confirm: Confirm }) {
  const install = () =>
    confirm({
      title: t('fw.installConfirmTitle', { version: s.fwBundled }),
      body: t('fw.installConfirmBody'),
      confirmText: t('fw.install'),
      destructive: true,
      action: () => a.writeFirmware(false),
    });
  return (
    <div className="mx-auto mt-[10vh] max-w-sm text-center">
      <div className="mx-auto mb-3 grid size-11 place-items-center rounded-xl bg-muted text-muted-foreground">
        <Cpu className="size-5" />
      </div>
      <h2 className="text-[15px] font-semibold">{t('fw.otherTitle')}</h2>
      <p className="mb-4 mt-1 text-xs text-muted-foreground">{t('fw.otherHint', { version: s.fwBundled })}</p>
      <Button size="sm" disabled={!!s.fwJob} onClick={install}>
        {t('fw.install')}
      </Button>
    </div>
  );
}

/* Progress while the firmware is written. It cannot be closed until it fails; then it says what to try */
export function FirmwareProgressDialog({ s, a }: { s: BoardState; a: Actions }) {
  const job = s.fwJob;
  const failed = job?.stage === 'failed';
  const text = failed
    ? t('fw.failed', { error: job?.error || '' })
    : job?.stage === 'writing'
      ? t('fw.writing', { percent: job.percent })
      : t('fw.connecting');
  return (
    <Dialog open={!!job} onOpenChange={(open) => !open && failed && a.dismissFirmware()}>
      <DialogContent showCloseButton={failed} className="sm:max-w-sm">
        <DialogHeader>
          <DialogTitle>{t('fw.writingTitle')}</DialogTitle>
          <DialogDescription>{text}</DialogDescription>
        </DialogHeader>
        {!failed && <Progress value={job?.percent ?? 0} className="h-1.5" />}
        <p className="text-xs text-muted-foreground">{failed ? t('fw.failedHint') : t('fw.keepPlugged')}</p>
        {failed && (
          <DialogFooter>
            <Button onClick={a.dismissFirmware}>{t('common.close')}</Button>
          </DialogFooter>
        )}
      </DialogContent>
    </Dialog>
  );
}
