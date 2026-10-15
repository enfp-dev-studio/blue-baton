// SPDX-License-Identifier: GPL-3.0-only
import { useEffect, useState } from 'react';
import { toast } from 'sonner';
import { Toaster } from '@/components/ui/sonner';
import { TooltipProvider } from '@/components/ui/tooltip';
import { AddDeviceDialog, ConfirmDialog, HostPairingDialog, type ConfirmRequest } from '@/components/Dialogs';
import { DevicesPanel } from '@/components/DevicesPanel';
import { Diagnostics } from '@/components/Diagnostics';
import { Header } from '@/components/Header';
import { HostsPanel } from '@/components/HostsPanel';
import { GeneralGroup } from '@/components/General';
import { ConnectCard } from '@/components/ConnectCard';
import { FirmwareGroup, FirmwareInstall, FirmwareProgressDialog } from '@/components/Firmware';
import type { Controller } from '@/lib/board/types';
import { useBoard } from '@/lib/useBoard';

export function App({ ctl }: { ctl: Controller }) {
  const s = useBoard(ctl);
  const a = ctl.actions;
  const [confirm, setConfirm] = useState<ConfirmRequest | null>(null);

  /* Board notices become toasts. The same message again (a failure that repeats) renews its toast instead of stacking another */
  useEffect(() => {
    const n = s.notice;
    if (!n) return;
    if (n.kind === 'error') toast.error(n.text, { id: n.text });
    else toast(n.text, { id: n.text });
  }, [s.notice?.at]);

  const ready = s.link === 'connected';
  return (
    <TooltipProvider>
      <div className="min-h-screen">
        <Header s={s} a={a} ctl={ctl} />
        <main className="mx-auto grid max-w-[680px] gap-6 px-4 pb-8 pt-5">
          {!ready && (s.appBoard === 'other' && s.fwBundled ? <FirmwareInstall s={s} a={a} confirm={setConfirm} /> : <ConnectCard s={s} a={a} ctl={ctl} />)}
          {ready && (
            <>
              <HostsPanel s={s} a={a} confirm={setConfirm} />
              <DevicesPanel s={s} a={a} confirm={setConfirm} />
              <FirmwareGroup s={s} a={a} confirm={setConfirm} />
            </>
          )}
          <GeneralGroup ctl={ctl} />
          <Diagnostics s={s} a={a} ctl={ctl} />
        </main>
        {ready && <HostPairingDialog s={s} a={a} />}
        {ready && <AddDeviceDialog s={s} a={a} />}
        <ConfirmDialog req={confirm} onClose={() => setConfirm(null)} />
        <FirmwareProgressDialog s={s} a={a} />
        <Toaster position="bottom-center" richColors />
      </div>
    </TooltipProvider>
  );
}
