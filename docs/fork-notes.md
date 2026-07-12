<!--
SPDX-FileCopyrightText: 2026 pocotaru <pocotaru@gmail.com>
SPDX-License-Identifier: BSL-1.0
-->

# Fork notes (pocotaru/stackchan-idf)

このフォークで加えた変更と、実機デバッグで得た知見の記録。upstream の
[JOURNAL.md](../JOURNAL.md) と衝突させないため独立ファイルにしている。
対象ボードは **CoreS3**（`BOARD=cores3`）。

---

## 加えた変更（main の系譜）

| コミット | 変更 | 目的 |
|---|---|---|
| grounding | `conversation`: Gemini Google 検索グラウンディング | 天気・ニュース等の**時事**に音声で回答（`setup.tools` に `{"googleSearch":{}}` を足すだけ。ハンドラ不要、検索はサーバ側） |
| Bluetooth OFF | `config_service`: `CONFIG_BT_ENABLED=n`（cores3） | NimBLE コントローラ/ホストが**内蔵 DRAM を ~54 KiB**占有し、Gemini wss/TLS の書き込みバッファを枯渇させて「接続エラー」再接続ループを起こしていた。BT を切ると内蔵ヒープ空きが ~5 KiB→**70〜97 KiB**（最大連続ブロック ~31 KiB）に回復し、**カメラを有効のまま**接続が安定。BLE GATT 設定サービスはビルドから外し、公開 API を `config_service_stub.cpp` の no-op で置換（初回 Wi-Fi 投入は SoftAP、以降は Wi-Fi HTTP 設定へ） |
| Wi-Fi PS OFF | `wifi_sta`: `esp_wifi_set_ps(WIFI_PS_NONE)` | 既定の modem sleep が**リアルタイム音声上りに遅延**（mic push_interval が最大 ~9.5 s スパイク）を出し、応答音声が途切れた。給電機なので無線を起こしっぱなしに → push_interval が安定 40 ms |
| tolerant tools | `conversation`: `set_expression`/`set_head_pose` を寛容化 | モデルがスキーマ外の引数（`"excitement"`/`"surprise"`、`yaw_deg`でなく`yaw`）を送ってきてハンドラがエラーを返し、モデルが毎ターン「表情と首の向きが設定できない」と喋っていた。`set_expression` は `parse_emotion()`（語彙拡張）へ、`set_head_pose` は `yaw`/`pitch` も受理し、**どちらも決してエラーを返さない** |
| Gemini 3.1 | `conversation`: モデルを `gemini-3.1-flash-live-preview` に | `gemini-2.5-flash-native-audio-preview-12-2025` が Google 側でローテされ、setup は通るのに音声を **code 1007 "audio content type not supported for this model configuration"** で切断→無反応に。3.1 は安定（廃止か混雑かは未確定だが 3.1 が安定なのは実測済み）。注: 3.1 は同じ voice/prompt でも**声色/言語追従が変わる**（大人っぽい声・英語に流れやすい）→ voice 選定 or プロンプト強化で調整可 |
| conv stack 20 KiB | `conversation`: タスク stack 8→20 KiB | 下記「無反応の真犯人」参照 |

---

## デバッグ知見：「認識はするのに無反応・通信エラーも出ない」の正体

### 症状
- スピナー/文字起こしは出て、**音声認識はできている**のに、応答が返らない
- Gemini の `ws close` / `通信エラー` は**出ない**
- 間欠的（毎回ではない）

### 真犯人＝**サイレントなパニックリブート**（conversation タスクの stack overflow）
シリアルログに：
```
***ERROR*** A stack overflow in task conversation has been detected.
Backtrace: ... |<-CORRUPTED
rst:0xc (RTC_SW_CPU_RST)   /  reset reason: 4  (= PANIC)
```
`speak_katakoto` ツールが **hts_engine の HMM 合成**を conversation タスク上で走らせ、
そのローカル（ラベルパース / `HTS_dp_match` の DP）が **8 KiB stack を溢れて panic → reboot**。
リブートでセッションが飛ぶので「無反応」に見える。`通信エラー`が出ないのは
WebSocket が落ちたのではなく**チップごと落ちている**から。

**間欠だった理由**：`set_expression`/`set_head_pose` のターンでは起きず、モデルが
実際に `speak_katakoto`（ものまね・効果音）を呼んだ時だけ発火するため。

main タスクは同じ hts_engine 理由で既に 16 KiB へ増量済み
（`CONFIG_ESP_MAIN_TASK_STACK_SIZE`）だったが、conversation タスクは 8 KiB のまま
取りこぼされていた。→ 20 KiB に増量で解消（BT OFF で内蔵 RAM に余裕があるので無害）。

### 効いた診断手順（再現性のある型）
1. `uv run --with pyserial python tools/monitor_log.py --port /dev/ttyACM0 --seconds 60` で無反応時のシリアルを採取
   （※ `monitor_log.py` は接続時に DTR/RTS でチップをリセットする＝接続自体が 1 回リブートを起こすので、
   `reset reason: 11`(ESP_RST_USB) は自作自演。**それ以外**の rst を見る）
2. `board initialized` が複数回 = 自発リブート、`reset reason: 4`/`rst:0xc` = panic
3. backtrace を addr2line でデコード：
   ```
   xtensa-esp32s3-elf-addr2line -pfiC -e build-cores3/stackchan_idf.elf 0x... 0x...
   ```
   → 落ちた関数（今回は `HTS_dp_match`）が判明
4. `UserTranscript`(`conv-task: user: …`) の有無で「音声が Gemini に届いて処理されたか」を判定
   （クリーンに出ていれば音声は破壊されていない＝原因は下流）

---

## 音声パイプラインの構造（把握メモ）

- マイク PCM(16 kHz/16-bit/mono, 40 ms チャンク)は Listening 中 **Gemini へ連続送信**。
  **Speaking(応答再生)中だけマイクを止める**（半二重＝自分の声の回り込み防止）
- Gemini は**自動(サーバ)VAD**。クライアントは `automaticActivityDetection` を送っておらず、
  `ConversationConfig` の `vad_*` は Gemini 経路ではデッド
- エッジが受け取れるのは**ターンの"終わり"**（`SpeechStopped`/`turnComplete`）＋ `UserTranscript` ＋
  応答音声。**"始まり"（SpeechStarted）は来ない**（＝端末は「いつユーザーのターンが始まったか」を知らない）
- 「話し始めでスピナー」を出すには端末側 VAD か手動アクティビティ検出が要る。今回は実装したが
  **無反応の原因がクラッシュだったため撤去**（クラッシュ診断にスピナーは無力、シリアルログが唯一の手段）

---

## 既知の非致命

- **task_wdt（CPU 飽和の一瞬ヒッチ）**：重い瞬間（katakoto 合成＋grounding＋音声）に CPU0/CPU1 が
  ~5 s 飽和して警告が出ることがある。`CONFIG_ESP_TASK_WDT_PANIC` は未設定＝**警告のみ・リブートしない**
  （最悪タッチ取りこぼし程度）。気になるなら hts_engine 合成をワーカータスクに逃がす
- **GHA**：`gh workflow run release.yml --ref main` で cores3 は成功。atoms3/atoms3r/stopwatch は失敗（要調査）
- **キャラ**：Gemini 3.1 で声が大人っぽく・英語に流れやすい（voice/プロンプト調整の余地）
