## 要使用此專案之前，需先完成以下配置

---

ESP32-Lyrat 設置為 HFP 設備

可依照此影片配置步驟完成
[https://youtu.be/W-6s79OHgDI](https://youtu.be/W-6s79OHgDI)

### 步驟 1: 安裝 Espressif 開發環境

首先，安裝 Espressif 的 ESP-IDF（Espressif IoT Development Framework）。可至以下網站直接下載
[https://dl.espressif.com/dl/esp-idf/](https://dl.espressif.com/dl/esp-idf/)

### 步驟 2: 下載 BtStack

打開 ESP-IDF 命令提示符（ESP-IDF Command Prompt），運行以下命令來克隆 BtStack 庫：

```bash
git clone https://github.com/bluekitchen/btstack.git
```

### 步驟 3: 修改 Python 文件

找到 `integrate_btstack.py` 文件，文件位置如下：

```
\btstack\port\esp32\
```

### 步驟 4: 編譯 HFP 範例項目

進入以下範例目錄：

```
\btstack\port\esp32\example\hfp_hf_demo
```

然後，運行以下命令編譯範例項目：

```bash
idf.py build
```

### 步驟 5: 設定開發板

進入 ESP-IDF 的配置菜單，並將開發板設置為 **ESP32-LyRat**：

```bash
idf.py menuconfig
```

### 步驟 6: 準備燒錄

在開始燒錄之前，按住開發板上的 "BOOT" 和 "RST" 按鈕：

1. 先按住 "BOOT" 按鈕。
2. 再按下 "RST" 按鈕並釋放 "RST"。
3. 最後釋放 "BOOT" 按鈕。

這將使開發板進入燒錄模式。

### 步驟 7: 燒錄程式到 ESP32

使用以下命令來燒錄程式到開發板，請將 `COMx` 替換為您設備的正確串口號（如 `COM3` 或 `/dev/ttyUSB0`）：

```bash
idf.py -p COMx flash
```

### 步驟 8: 啟動和監控設備

燒錄完成後，按下 "RST" 按鈕重置開發板。開發板現在可以作為 HFP 設備工作了。

要監控設備的輸出，可以使用以下命令，並將 `COMx` 替換為正確的串口號：

```bash
idf.py -p COMx monitor
```
---
## 完成上述配置後，即可接續使用此專案

---
專案功能:於ESP32-LyRat運作免持聽筒並附帶字母'Q'鍵盤按鈕功能。

可依照此影片配置步驟完成
[https://youtu.be/W-6s79OHgDI](https://youtu.be/4ckwdMCLy2g)

### 步驟 1: 下載專案

打開 ESP-IDF 命令提示符（ESP-IDF Command Prompt），並運行以下命令來克隆專案代碼：

```bash
git clone https://github.com/YunrusCodes/AmazeDevice.git
```

### 步驟 2: 編譯專案

切換到以下目錄：

```
\AmazeDevice\hfp_hid_multi
```

然後，運行以下命令編譯專案：

```bash
idf.py build
```

### 步驟 3: 檢查代碼 `hfp_hid_multi.c`

在此步驟中，檢查專案中的代碼文件 `hfp_hid_multi.c`。代碼中已將 **GPIO1** 設置為鍵盤按鈕。

此外，將一個按鈕連接到 **TX 引腳**，以便觸發 HID 輸入。

### 步驟 4: 燒錄程式到 ESP32

使用以下命令將代碼燒錄到開發板中，請將 `COMx` 替換為您設備的正確串口號（如 `COM3` 或 `/dev/ttyUSB0`）：

```bash
idf.py -p COMx flash
```

### 步驟 5: 啟動設備

燒錄完成後，按下 "RST" 按鈕重置開發板。此時，開發板可以同時作為 HFP 設備和 HID 設備工作，並且按下鍵盤按鈕時可以輸出字母 "Q"。

---
