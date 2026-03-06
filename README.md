# Timer-Event

---

# Timer Event（UEFI）README 筆記

目標：**建立週期性 Timer Event**，每秒觸發 Notify Callback，在 Callback 裡呼叫 **gRT->GetTime()** 取得時間並顯示到「秒」。

---

## 0. 必要 Library / Header

### 常用 Header

* `<Uefi.h>`
* `<Library/UefiLib.h>`：`Print()`
* `<Library/UefiApplicationEntryPoint.h>`：`UefiMain()`
* `<Library/UefiBootServicesTableLib.h>`：`gBS`
* `<Library/UefiRuntimeServicesTableLib.h>`：`gRT`

### 常用 LibraryClasses（INF/DSC）

* `UefiApplicationEntryPoint`
* `UefiLib`
* `UefiBootServicesTableLib`
* `UefiRuntimeServicesTableLib`

> 你之前 build 一直缺 instance（DebugLib/PrintLib/PcdLib...）是因為你用「自製極簡 DSC」，所以要在 DSC 提供所有依賴的 library instance（平台 DSC 通常早就配好）。

---

## 1. gBS->CreateEvent()

### 功能

建立一個 UEFI Event 物件（可用於 Timer、Notify、Wait 等）。

### 原型（概念）

```c
EFI_STATUS
(EFIAPI *CreateEvent)(
  IN  UINT32              Type,
  IN  EFI_TPL             NotifyTpl,
  IN  EFI_EVENT_NOTIFY    NotifyFunction OPTIONAL,
  IN  VOID                *NotifyContext OPTIONAL,
  OUT EFI_EVENT           *Event
);
```

### 你這題 Timer Event 常用 Type

* `EVT_TIMER`：事件是 Timer
* `EVT_NOTIFY_SIGNAL`：事件被 Signal 時呼叫 NotifyFunction

✅ 常見寫法（Timer callback 版）

```c
gBS->CreateEvent(
  EVT_TIMER | EVT_NOTIFY_SIGNAL,
  TPL_CALLBACK,
  TimerNotify,
  NULL,
  &mTimerEvent
);
```

### NotifyTpl 怎麼選？

* `TPL_CALLBACK`：最常用（callback 等級）
* 太高的 TPL 可能影響系統/console 行為；一般不要亂用更高 TPL。

### 常見回傳

* `EFI_SUCCESS`
* `EFI_OUT_OF_RESOURCES`：資源不足
* `EFI_INVALID_PARAMETER`

### 踩雷

* 忘記 `EVT_NOTIFY_SIGNAL`：Timer 到了不會呼叫 callback（只會被 signal，但你沒 handler）
* callback 裡做太重的事：會影響系統 responsiveness（尤其 Print 可能慢）

---

## 2. gBS->SetTimer()

### 功能

把 Event 設成 **一次性 / 週期性** timer，或取消 timer。

### 原型（概念）

```c
EFI_STATUS
(EFIAPI *SetTimer)(
  IN EFI_EVENT      Event,
  IN EFI_TIMER_DELAY Type,
  IN UINT64         TriggerTime
);
```

### Type（EFI_TIMER_DELAY）

* `TimerCancel`：取消 timer
* `TimerPeriodic`：週期性
* `TimerRelative`：相對時間後觸發一次

### TriggerTime 單位（超重要）

* 單位是 **100ns**
* `1 秒 = 10,000,000 * 100ns`

✅ 每秒一次：

```c
gBS->SetTimer(mTimerEvent, TimerPeriodic, 10ULL * 1000 * 1000);
```

### 常見回傳

* `EFI_SUCCESS`
* `EFI_INVALID_PARAMETER`

### 踩雷

* TriggerTime 算錯（常見把 ms 當成 100ns）
* 忘記取消 timer 就 CloseEvent（有些平台會出怪行為，建議先 Cancel 再 Close）

---

## 3. gRT->GetTime()

### 功能

讀取目前 RTC 時間（年月日、時分秒、timezone、daylight 等）。

### 原型（概念）

```c
EFI_STATUS
(EFIAPI *GetTime)(
  OUT EFI_TIME               *Time,
  OUT EFI_TIME_CAPABILITIES  *Capabilities OPTIONAL
);
```

### EFI_TIME 常用欄位

* `Time.Year`
* `Time.Month` (1~12)
* `Time.Day` (1~31)
* `Time.Hour` (0~23)
* `Time.Minute` (0~59)
* `Time.Second` (0~59)

✅ 顯示到秒（你要的）

```c
Print(L"--%02u:%02u:%02u--%04u/%02u/%02u\n",
      Time.Hour, Time.Minute, Time.Second,
      Time.Year, Time.Month, Time.Day);
```

### 常見回傳

* `EFI_SUCCESS`
* `EFI_INVALID_PARAMETER`：Time 指標為 NULL
* `EFI_DEVICE_ERROR`：RTC/Time service 出問題

### 踩雷：你照片遇到的「秒被年蓋掉」

根因幾乎都是 **Print 格式字串寫錯**，例如少了 `:%02u` 秒欄位：

```c
// 錯：沒有 Time.Second，Year 會緊接在 Minute 後面
Print(L"--%02u:%02u%04u/%02u/%02u\n", ...);
```

改成正確格式（包含秒 & 分隔符）就好。

---

## 4. gBS->WaitForEvent()（用來等按鍵結束）

### 功能

阻塞等待某個 event 被 signal（常用等鍵盤、等 timer、等某 device event）。

### 原型（概念）

```c
EFI_STATUS
(EFIAPI *WaitForEvent)(
  IN  UINTN     NumberOfEvents,
  IN  EFI_EVENT *Event,
  OUT UINTN     *Index
);
```

### 常見用法：等待按鍵

```c
EFI_EVENT WaitList[1];
UINTN Index;
WaitList[0] = gST->ConIn->WaitForKey;
gBS->WaitForEvent(1, WaitList, &Index);
```

---

## 5. gBS->CloseEvent()

### 功能

釋放 Event 資源。

✅ 建議順序

1. `SetTimer(Event, TimerCancel, 0);`
2. `CloseEvent(Event);`

---

## 6. Console 輸出技巧：避免殘影 / 覆蓋問題

### A) 每秒換行（最簡單，不會殘影）

```c
Print(L"--%02u:%02u:%02u--%04u/%02u/%02u\n", ...);
```

### B) 同一行刷新（\r），要「清尾巴」

若你用 `\r` 回行首刷新，**新字串比舊字串短時會殘留舊字**。
可用「先清行再印」：

```c
Print(L"\r                                        "); // 清行(空白要夠長)
Print(L"\r--%02u:%02u:%02u--%04u/%02u/%02u", ...);
```

---

## 7. 最小流程（你作業要求對應）

1. `CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, NotifyFunc, ...)`
2. `SetTimer(Event, TimerPeriodic, 1 sec)`
3. NotifyFunc：

   * `gRT->GetTime(&Time, NULL)`
   * `Print()` 顯示到秒
4. 按鍵停止：

   * `SetTimer(Event, TimerCancel, 0)`
   * `CloseEvent(Event)`

---

## 系統架構與主選單流程 (System Architecture & Menu Flow)

```
TimerEvent.efi  (UEFI Application)
│
├─ Entry Point (UefiMain)
│   ├─ 建立 Timer Event：gBS->CreateEvent()
│   ├─ 設定週期觸發：gBS->SetTimer(TimerPeriodic, 1s)
│   ├─ 主迴圈 / 等待使用者輸入：gBS->WaitForEvent(WaitForKey)
│   └─ 結束清理：SetTimer(TimerCancel) + CloseEvent()
│
├─ Timer Callback (Notify Function: TimerNotify)
│   ├─ 讀取系統時間：gRT->GetTime()
│   └─ 顯示時間到秒：Print("--HH:MM:SS--YYYY/MM/DD")
│
└─ UEFI Services
    ├─ Boot Services (gBS)
    │   ├─ CreateEvent / SetTimer / WaitForEvent / CloseEvent
    │   └─ 事件分派與 TPL 管控（callback 執行環境）
    │
    └─ Runtime Services (gRT)
        └─ GetTime（RTC/Time Service）
```

---

cd /d D:\BIOS\MyWorkSpace\edk2

edksetup.bat Rebuild

chcp 65001

set PYTHONUTF8=1

set PYTHONIOENCODING=utf-8

rmdir /s /q Build\CmosRwAppPkg

build -p TimerEventPkg\TimerEventPkg.dsc -a X64 -t VS2019 -b DEBUG
