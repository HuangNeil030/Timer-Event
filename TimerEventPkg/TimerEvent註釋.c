//
// TimerEvent.c  (UEFI Application)
// 功能：建立週期性 Timer Event，每秒觸發一次 Notify Callback，Callback 內呼叫 gRT->GetTime() 取得系統時間並顯示到「秒」。
// 操作：啟動後開始更新時間；按任意鍵停止並退出。
//
// ──────────────────────────────────────────────────────────────────────
// 【教科書式重點】
// 1) Boot Services (gBS)
//    - CreateEvent()：建立事件物件（這裡建立 Timer + Notify Signal）
//    - SetTimer()   ：把事件設定成週期性 timer
//    - WaitForEvent()：等待按鍵事件（用來讓主程式停在這裡，timer 在背景照樣跑）
//    - CloseEvent() ：釋放事件資源
//
// 2) Runtime Services (gRT)
//    - GetTime()：取得 RTC 時間（EFI_TIME：Year/Month/Day/Hour/Minute/Second）
//
// 3) 為什麼要用 EVT_TIMER | EVT_NOTIFY_SIGNAL？
//    - EVT_TIMER：這個 Event 會被 SetTimer() 以時間驅動 signal
//    - EVT_NOTIFY_SIGNAL：Event 被 signal 時，UEFI 會呼叫你提供的 NotifyFunction (callback)
//
// 4) 顯示秒數的關鍵：Print 格式字串要包含 Time.Second
//    - 正確：--HH:MM:SS--YYYY/MM/DD
//    - 若少了 :%02u（秒），Year(2026) 會直接接在 Minute 後面，看起來像「年覆蓋秒」
//
// 5) \r 同一行刷新 vs \n 換行：
//    - \r：回到行首，適合做「同一行刷新」；但要清行/補空白，避免殘影
//    - \n：每秒換行，最簡單、永不殘影，但會洗屏
//

#include <Uefi.h>                                // UEFI 核心型別/巨集：EFI_STATUS、EFI_EVENT、EFI_TIME、EFI_HANDLE 等

#include <Library/UefiLib.h>                      // Print()：UEFI console 輸出（寬字元）
#include <Library/UefiApplicationEntryPoint.h>    // UefiMain()：UEFI Application 的標準入口點宣告
#include <Library/UefiBootServicesTableLib.h>     // gBS：Boot Services 全域指標（CreateEvent/SetTimer/WaitForEvent/CloseEvent）
#include <Library/UefiRuntimeServicesTableLib.h>  // gRT：Runtime Services 全域指標（GetTime）

//
// ──────────────────────────────────────────────────────────────────────
// 全域變數：保存 Timer Event 的 Handle
// 目的：
//   - UefiMain() 建立 event 後保存起來
//   - 停止時（按鍵後）取消 timer 並 CloseEvent
//
STATIC EFI_EVENT  mTimerEvent = NULL;             // EFI_EVENT：UEFI 事件物件的抽象 handle；NULL 表示尚未建立

//
// ──────────────────────────────────────────────────────────────────────
// TimerNotify：Timer Event 的通知 callback
//
// 何時會進來？
//   - 當你用 CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, ...) 建立 event
//   - 然後用 SetTimer(event, TimerPeriodic, 1sec) 啟動週期 timer
//   - 每到 1 秒，UEFI 會 signal 這個 event，因為有 EVT_NOTIFY_SIGNAL，於是呼叫本函數
//
// 參數說明：
//   IN EFI_EVENT Event
//     - 觸發本 callback 的事件 handle（通常不一定要用，但可做辨識）
//
//   IN VOID *Context
//     - CreateEvent() 時傳入的 NotifyContext（這裡我們傳 NULL）
//     - 若你需要傳資料（例如設定、狀態、統計），可在這裡帶入 struct 指標
//
// 注意：
//   - callback 執行在 NotifyTpl 指定的 TPL（這裡用 TPL_CALLBACK）
//   - callback 不要做太重的工作（大量 IO/長迴圈）以免卡住系統事件派發
//
STATIC
VOID
EFIAPI
TimerNotify (
  IN EFI_EVENT  Event,                           // [IN] 觸發 callback 的 Event
  IN VOID      *Context                          // [IN] 使用者自訂 context（此範例未使用）
  )
{
  EFI_STATUS  Status;                            // EFI_STATUS：UEFI API 回傳狀態碼（EFI_SUCCESS 代表成功）
  EFI_TIME    Time;                              // EFI_TIME：UEFI 時間結構（Year/Month/Day/Hour/Minute/Second...）

  //
  // (第 1 層) 取得系統時間：gRT->GetTime()
  //
  // API：EFI_STATUS GetTime(OUT EFI_TIME *Time, OUT EFI_TIME_CAPABILITIES *Capabilities OPTIONAL)
  //
  // - Time：輸出參數，回傳目前時間
  // - Capabilities：可選，回傳 RTC 精度/解析度/是否支援 timezone 等（此範例不需要，傳 NULL）
  //
  // 常見回傳碼：
  // - EFI_SUCCESS：成功
  // - EFI_INVALID_PARAMETER：Time 指標為 NULL（此範例不會）
  // - EFI_DEVICE_ERROR：RTC/Time Service 錯誤（硬體或 driver 問題）
  //
  Status = gRT->GetTime (&Time, NULL);           // 取得時間；&Time 是輸出緩衝，NULL 表示不取 capabilities

  //
  // (第 2 層) 錯誤處理：如果 GetTime 失敗，就印出錯誤碼並返回
  //
  // EFI_ERROR(x)：判斷 EFI_STATUS 是否為錯誤（最高位元為 1 通常代表 error）
  //
  if (EFI_ERROR (Status)) {                      // 若 GetTime() 回傳不是 EFI_SUCCESS
    //
    // 這裡用 \r 同一行刷新：
    // - 先清行（印空白覆蓋舊字）再回行首印訊息
    // - 避免你之前遇到的「舊字殘留 / 覆蓋錯位」
    //
    Print (L"\r                                        "); // 清除上一行內容（空白要夠長）
    Print (L"\r--GetTime:%r--", Status);          // %r：UEFI Print 的特殊格式，可把 EFI_STATUS 轉成字串
    return;                                      // callback 結束，等待下一次 timer
  }

  //
  // (第 3 層) 正常顯示：顯示到「秒」
  //
  // 你之前看到 --10:112026/03/06 類型的輸出，
  // 就是因為少了 Time.Second 或少了分隔符，導致 Year(2026) 直接接在 Minute 後面。
  //
  // 正確格式：
  //   --HH:MM:SS--YYYY/MM/DD
  //
  // 額外處理：同一行刷新避免殘影
  //   1) \r + 空白：清行
  //   2) \r + 新內容：回到行首印新的時間
  //
  Print (L"\r                                        "); // (A) 清掉上一筆輸出，避免尾巴殘留
  Print (                                           // (B) 回行首後印出完整時間
    L"\r--%02u:%02u:%02u--%04u/%02u/%02u",
    Time.Hour,                                     // %02u：寬度 2、前補 0（例如 7 -> 07）
    Time.Minute,                                   // 分
    Time.Second,                                   // 秒（你要的重點）
    Time.Year,                                     // 年（4 位數）
    Time.Month,                                    // 月
    Time.Day                                       // 日
    );

  //
  // 注意：
  // - 這裡沒有加 \n，因此會「同一行刷新」
  // - 若你想每秒換行（像 log），把上面 L"\r..." 改成 L"--... \n" 即可
  //
}

//
// ──────────────────────────────────────────────────────────────────────
// UefiMain：UEFI Application 入口點
//
// 參數說明：
//   IN EFI_HANDLE ImageHandle
//     - 本映像（TimerEvent.efi）在系統中的 handle
//   IN EFI_SYSTEM_TABLE *SystemTable
//     - 系統表，包含 gBS/gRT 等服務表（EDK2 幫你設好全域 gBS/gRT）
//
// 回傳：EFI_STATUS
//   - EFI_SUCCESS：成功完成
//   - 其他：表示某個 UEFI API 失敗
//
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,              // [IN] 本程式映像 handle
  IN EFI_SYSTEM_TABLE  *SystemTable              // [IN] 系統表（含 Console、Boot/Runtime Services 等）
  )
{
  EFI_STATUS  Status;                            // 儲存 API 回傳狀態

  //
  // (第 0 層) 提示訊息：開始建立 Timer Event
  //
  Print (L"[TimerEvent] Create periodic timer event...\n");

  //
  // (第 1 層) 建立 Event：CreateEvent()
  //
  // CreateEvent(Type, NotifyTpl, NotifyFunction, NotifyContext, OUT Event)
  //
  // Type：
  //   EVT_TIMER          ：這個 event 會被 SetTimer 驅動 signal
  //   EVT_NOTIFY_SIGNAL  ：event 被 signal 時呼叫 NotifyFunction
  //
  // NotifyTpl：
  //   TPL_CALLBACK：callback 執行時的 Task Priority Level
  //
  // NotifyFunction：
  //   TimerNotify：我們的 callback
  //
  // NotifyContext：
  //   NULL：此範例不傳 context（若要傳資料可放 struct*）
  //
  // Event：
  //   &mTimerEvent：輸出 event handle
  //
  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL, // [IN] 事件型態：Timer + Notify
                  TPL_CALLBACK,                   // [IN] callback 執行 TPL（常用）
                  TimerNotify,                    // [IN] 通知函式（Timer 到期會呼叫）
                  NULL,                           // [IN] 通知 context（此範例不用）
                  &mTimerEvent                    // [OUT] 事件 handle
                  );

  //
  // (第 2 層) 檢查 CreateEvent 是否成功
  //
  // 常見錯誤：
  //   EFI_OUT_OF_RESOURCES：資源不足（無法建立 event）
  //   EFI_INVALID_PARAMETER：參數錯誤
  //
  if (EFI_ERROR (Status)) {
    Print (L"[TimerEvent] CreateEvent failed: %r\n", Status);
    return Status;                                // 建立失敗就直接退出
  }

  //
  // (第 3 層) 設定 Timer：SetTimer()
  //
  // SetTimer(Event, Type, TriggerTime)
  //
  // Type：
  //   TimerPeriodic：週期性
  //
  // TriggerTime：
  //   單位為 100ns
  //   1 秒 = 10,000,000 * 100ns
  //
  Status = gBS->SetTimer (
                  mTimerEvent,                   // [IN] 要設定 timer 的 event
                  TimerPeriodic,                  // [IN] 週期性 timer
                  10ULL * 1000 * 1000             // [IN] 1 秒（100ns 單位）
                  );

  //
  // (第 4 層) 檢查 SetTimer 是否成功
  //
  if (EFI_ERROR (Status)) {
    Print (L"[TimerEvent] SetTimer failed: %r\n", Status);

    //
    // 失敗處理：event 已經建立了，必須釋放資源
    //
    gBS->CloseEvent (mTimerEvent);               // 關閉 event，避免資源泄漏
    mTimerEvent = NULL;                          // 清掉全域 handle，避免誤用
    return Status;
  }

  //
  // (第 5 層) 提示：Timer 已開始運作
  //
  Print (L"[TimerEvent] Running... Press any key to stop.\n");

  //
  // (第 6 層) 主程式等待「按鍵事件」
  //
  // 注意：我們沒有自己做 while(1) 迴圈去每秒 GetTime，
  //      因為 TimerNotify 已經會每秒被呼叫。
  //
  // 這裡只是讓主程式卡在 WaitForEvent，
  // 等使用者按任意鍵後才繼續往下做清理與結束。
  //
  {
    EFI_EVENT     WaitList[1];                   // 等待事件列表（只等 1 個：WaitForKey）
    UINTN         Index;                         // WaitForEvent 回傳：哪個 event 被觸發（0 表示第一個）
    EFI_INPUT_KEY Key;                           // 用來讀取按鍵（避免按鍵留在 buffer）

    WaitList[0] = gST->ConIn->WaitForKey;        // Console Input 的「按鍵事件」(event)

    //
    // WaitForEvent(NumberOfEvents, EventArray, OUT Index)
    // - NumberOfEvents：事件數量（這裡 1）
    // - EventArray：事件陣列（WaitList）
    // - Index：哪個事件被 signal
    //
    Status = gBS->WaitForEvent (1, WaitList, &Index);

    //
    // WaitForEvent 理論上不太會失敗，但仍可做保守處理
    //
    if (EFI_ERROR (Status)) {
      Print (L"\n[TimerEvent] WaitForEvent failed: %r\n", Status);
      // 即使失敗也要做後續清理（別直接 return）
    }

    //
    // 讀掉按鍵：避免 key 留在 input buffer（有些 shell 會因此出現怪行為）
    //
    gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  }

  //
  // (第 7 層) 停止 Timer
  //
  // 建議做法：先 Cancel 再 CloseEvent
  //
  gBS->SetTimer (mTimerEvent, TimerCancel, 0);   // 取消 timer（不再週期 signal）

  //
  // (第 8 層) 釋放 Event 資源
  //
  gBS->CloseEvent (mTimerEvent);                 // 關閉並釋放 event
  mTimerEvent = NULL;                            // 清空 handle（良好習慣）

  //
  // (第 9 層) 程式結束提示
  //
  Print (L"\n[TimerEvent] Stopped.\n");

  //
  // (第 10 層) 回傳成功
  //
  return EFI_SUCCESS;
}