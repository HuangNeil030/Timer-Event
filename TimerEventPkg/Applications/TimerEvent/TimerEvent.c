#include <Uefi.h>

#include <Library/UefiLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

STATIC EFI_EVENT  mTimerEvent = NULL;

STATIC
VOID
EFIAPI
TimerNotify (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  EFI_STATUS  Status;
  EFI_TIME    Time;

  Status = gRT->GetTime(&Time, NULL);
  if (EFI_ERROR(Status)) {
    Print(L"\r                                        "); // 清行
    Print(L"\r--GetTime:%r--", Status);
    return;
  }
  Print(L"\r                                        ");
  // 同一行刷新（不一直洗屏）
  Print(L"--%02u:%02u:%02u--%04u/%02u/%02u\n",
      Time.Hour, Time.Minute, Time.Second,
      Time.Year, Time.Month, Time.Day);
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  Print(L"[Timer] Create periodic timer event...\n");

  // EVT_TIMER 可與 EVT_NOTIFY_SIGNAL OR 起來（你投影片那段）
  Status = gBS->CreateEvent(
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  TimerNotify,
                  NULL,
                  &mTimerEvent
                  );
  if (EFI_ERROR(Status)) {
    Print(L"[Timer] CreateEvent failed: %r\n", Status);
    return Status;
  }

  // 1 秒 = 10,000,000 * 100ns
  Status = gBS->SetTimer(
                  mTimerEvent,
                  TimerPeriodic,
                  10ULL * 1000 * 1000
                  );
  if (EFI_ERROR(Status)) {
    Print(L"[Timer] SetTimer failed: %r\n", Status);
    gBS->CloseEvent(mTimerEvent);
    mTimerEvent = NULL;
    return Status;
  }

  Print(L"[Timer] Running... Press any key to stop.\n");

  // 等按鍵，Timer callback 會持續被呼叫
  {
    EFI_EVENT  WaitList[1];
    UINTN      Index;
    WaitList[0] = gST->ConIn->WaitForKey;
    gBS->WaitForEvent(1, WaitList, &Index);

    // 把按鍵讀掉（避免殘留）
    {
      EFI_INPUT_KEY Key;
      gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
    }
  }

  // 停止 + 釋放 Event
  gBS->SetTimer(mTimerEvent, TimerCancel, 0);
  gBS->CloseEvent(mTimerEvent);
  mTimerEvent = NULL;

  Print(L"\n[Timer] Stopped.\n");
  return EFI_SUCCESS;
}