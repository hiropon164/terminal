![terminal-logos](https://github.com/microsoft/terminal/assets/91625426/333ddc76-8ab2-4eb4-a8c0-4d7b953b1179)

---

## 📌 このフォークについて

> 🌐 **English version → [README.md](./README.md)**

これは [microsoft/terminal](https://github.com/microsoft/terminal) の **個人用フォーク**で、私的利用のために保守しています。Microsoft とは**無関係**で、公式ビルドでも**ありません**。元プロジェクトの [MIT ライセンス](./LICENSE) はそのまま継承しています。

> ベース：Windows Terminal **v1.24.2372.0**

> このページはフォーク独自の追加機能を日本語でまとめたものです。Windows Terminal 本体（アップストリーム）の説明は英語の [README.md](./README.md) を参照してください。

### このフォークの独自変更

ペイン操作の便利機能と、いくつかの新しいペイン種別（ファイルブラウザ・Webブラウザ・winget）、そしてローカル WebSocket 越しの読み取り専用ペイン共有を追加しています。

#### 1. ペインの均等化（Equalize panes）

現在のタブのすべての分割を一括でリサイズし、各ペインの面積を均等にします（タイル型ウィンドウマネージャの「equalize」相当）。各分割は両側のペイン数で重み付けされるため、半分ずつではなく**ペインごと**に同じ大きさになります。

- **アクション:** `equalizePanes`（id `Terminal.EqualizePanes`）
- ペインが1つだけのタブでは何もしません。

#### 2. マウスドラッグでのペインリサイズ

各分割線の上に、細い当たり判定付きの帯があります。マウスでドラッグすると隣接2ペインをライブでリサイズします（最小サイズでクランプ）。帯は通常は不可視で、ホバー時にうっすら強調されます。アップストリームの Windows Terminal はキーボード（`Alt+Shift+矢印`）でのリサイズのみ対応です。

#### 3. マウスドラッグでのペイン移動（`Alt`＋ドラッグ）

**`Alt`** を押しながらペインを別のペインへドラッグすると、そこへ移動します。ドラッグ中は掴んだペインが着色され、半透明のペインサイズのゴーストがカーソルに追従します。ドロップすると対象ペインが長辺方向に分割され、離した側（上下/左右）に移動ペインが挿入されます。ペイン領域外で離すとキャンセルされます。

> `Alt`＋左ドラッグをペイン移動に割り当てているため、このフォークでは端末の `Alt`＋ドラッグによる**矩形（ブロック）選択**はそのジェスチャでは使えません。

#### 4. ファイルブラウザペイン

キーボード操作のファイルブラウザ＆ビューアをペインとして表示します。矢印キーで移動、`Enter` でフォルダを開く、`Backspace` で上の階層へ、`D` でドライブ一覧。ファイルを選ぶとプレビューします — テキスト（UTF-8/UTF-16、BOM対応）はビューアで、画像（`png`/`jpg`/`gif`/`bmp`/`ico`/`tiff`/`webp`）はインライン表示。エクスプローラからファイル/フォルダをドラッグ＆ドロップもできます。

- **アクション:** `openFileBrowser`（id `Terminal.OpenFileBrowser`）
- **アクション:** `selectFileBrowserDrive`（id `Terminal.SelectFileBrowserDrive`）— ドライブ一覧を表示

#### 5. Webブラウザペイン

[WebView2](https://learn.microsoft.com/microsoft-edge/webview2/)（Chromium/Edge）ブラウザをペインとして表示します。アドレスバーに戻る/進む/再読み込みとクリップボード貼り付けボタン付き。URL を入力するか、検索語を入力すると設定した検索エンジンに送られます。起動時に表示するページはグローバル設定 `browserHomePage` です。

- **アクション:** `openBrowserPane`（id `Terminal.OpenBrowserPane`）
- **設定:** `browserHomePage`（グローバル、既定 `https://www.google.com`）
- WebView2 ランタイム（Microsoft Edge に同梱）が必要です。ポータブル配布物には必要な `WebView2Loader.dll` と `Microsoft.Web.WebView2.Core.dll` を自動同梱します。

#### 6. winget ペイン

[Windows パッケージマネージャ](https://github.com/microsoft/winget-cli)（`winget`）の小さな GUI フロントエンドです。パッケージ検索、インストール済み/アップグレード可能の一覧、一覧からの Install / Upgrade / Upgrade All / Uninstall ができます。内部で `winget.exe` を実行するため、Install/Upgrade で昇格（UAC）を求められることがあります。winget ペインは同時に1つだけで、再度開くと既存のものにフォーカスします。

- **アクション:** `openWingetPane`（id `Terminal.OpenWingetPane`）
- `winget`（Windows パッケージマネージャ）が必要です（現行の Windows 10/11 に同梱）。

#### 7. ペイン共有（読み取り専用）

稼働中の端末ペインを共有し、別のペイン・ウィンドウ・マシンから**読み取り専用**で閲覧できます。`sharePane` は **localhost** に bind した小さな WebSocket サーバを起動し、ランダムトークン付きの `ws://localhost:<port>/?token=…` URL をクリップボードへコピーします。`connectSharedSession` は観戦ペインを開き、その URL をクリップボードから自動入力して接続し、ホストペインをライブにミラーします。途中参加した観戦側もすぐに現在画面を見られます（ホストがバッファの VT スナップショットを送るため）。共有中ペインのタブには共有アイコンが表示され、`stopSharePane` で共有を終了してアイコンを消します。

共有エンジンは `src/cascadia/SharingEngine` にあり、Windows Terminal から独立しています（プロトコル / WebSocket / 認可コアは単体でビルド・テストできます）。ホストは `ConptyConnection` を一切改変せず、既存接続の出力を tee（分岐）するだけなので、共有がシェルの動作を変えることはありません。

セキュリティモデル（読み取り専用は「安全」ではなく「全情報開示」です — URL とトークンを知る人はそのペインの内容をすべて見られます）：

- **既定は localhost のみ。** サーバはループバックに bind します。平文 `ws://` をそのまま外に出すことは想定していません（出すなら TLS 終端のリバースプロキシ経由のみ）。
- **読み取り専用はサーバ側で強制**します（観戦側を信用せず、INPUT フレームを破棄）。
- **認証が成立するまで何も送りません**（不正/期限切れトークンには拒否のみ）。
- **自動失効。** トークンが新規観戦者を受け付けるのは30分間まで、観戦者ゼロの共有は10分で自動停止 — 共有の切り忘れでシェルが晒され続けないようにします。

##### 別マシンから接続する

ネットワークの信頼度に応じて2通りあります：

- **推奨 — SSHトンネル（暗号化＋認証）。** ホストは既定の localhost 共有（`sharePane`）のまま。観戦側マシンで `connectSharedSessionViaSsh` を実行し、**SSH先**（`user@host`）とホストの `ws://localhost:<port>/?token=…` URL を入力します。Windows Terminal がトンネルを自動で張ります — バックグラウンドで `ssh -N -L <local>:localhost:<port> user@host` を実行し、観戦側を `ws://localhost:<local>/?token=…` に接続、観戦ペインを閉じると `ssh` プロセスも終了します。ネットワークを流れるのは SSH だけです。前提：ホストで OpenSSH **サーバ**が稼働していること、観戦側が**鍵/エージェント認証**であること（トンネルは `BatchMode=yes` で動くため、パスワード認証だと失敗します。公開鍵を一度設定してください）。初回のホスト鍵は `StrictHostKeyChecking=accept-new` で自動受け入れします。Tailscale のようなメッシュ VPN でも同等に使えます（暗号化された tailnet 上を平文 `ws://` が流れるので、VPN アドレスへ直接共有できます）。
- **信頼できる LAN での平文（`sharePaneOnLan`）。** すべてのインターフェースに bind し、このマシンの LAN IP で URL を作るので、他の PC から直接つながります。これは**ネットワーク上を平文で流れる**（保護はトークンのみ）ため、信頼できるネットワークでのみ使ってください。ダイアログが警告を表示します。つながらない場合は Windows ファイアウォールでプライベートネットワークの WindowsTerminal を許可してください。チャネルが完全に信頼できない場合は SSH トンネルを優先してください。

- **アクション:** `sharePane`（id `Terminal.SharePane`）— フォーカス中ペインを共有（localhost）
- **アクション:** `sharePaneOnLan`（id `Terminal.SharePaneOnLan`）— LAN へ共有（平文・オプトイン）
- **アクション:** `stopSharePane`（id `Terminal.StopSharePane`）— 共有を停止
- **アクション:** `connectSharedSession`（id `Terminal.ConnectSharedSession`）— 観戦ペインを開く
- **アクション:** `connectSharedSessionViaSsh`（id `Terminal.ConnectSharedSessionViaSsh`）— SSH トンネルを自動起動して観戦

#### 8. 専用の `keybindings.json`

キーバインドはアプリの `LocalState` フォルダ内の別ファイル `keybindings.json` で管理できます。設定の*フラグメント*と異なり、このファイルはキーバインドの定義が許可され、`settings.json` の**上に**重ねられます（=優先されます）。これを直接編集すれば、キーバインドを他の設定と分けて管理できます。

#### キーバインド

ドラッグでのリサイズと `Alt`＋ドラッグでの移動はマウスジェスチャなのでキー割り当ては不要です。下のキーボードアクションは**既定では未割り当て**です — 必要なものを `settings.json`（または設定UI → 操作）で割り当ててください：

```jsonc
{
    "keybindings": [
        { "id": "Terminal.EqualizePanes", "keys": "ctrl+alt+e" },
        { "id": "Terminal.OpenFileBrowser", "keys": "alt+f" },
        { "id": "Terminal.OpenBrowserPane", "keys": "alt+b" },
        { "id": "Terminal.OpenWingetPane", "keys": "alt+w" },
        { "id": "Terminal.SharePane", "keys": "alt+s" },
        { "id": "Terminal.SharePaneOnLan", "keys": "alt+shift+l" },
        { "id": "Terminal.StopSharePane", "keys": "alt+shift+s" },
        { "id": "Terminal.ConnectSharedSession", "keys": "alt+shift+c" },
        { "id": "Terminal.ConnectSharedSessionViaSsh", "keys": "alt+shift+t" }
    ]
}
```

| アクションID | 推奨キー | 内容 |
|-----------|----------------|--------------|
| `Terminal.EqualizePanes` | `ctrl+alt+e` | 現在のタブの全ペインを均等サイズにする |
| `Terminal.OpenFileBrowser` | `alt+f` | キーボード操作のファイルブラウザペインを開く |
| `Terminal.SelectFileBrowserDrive` | *(未割当)* | フォーカス中のファイルブラウザでドライブ一覧を表示 |
| `Terminal.OpenBrowserPane` | `alt+b` | WebView2 の Web ブラウザペインを開く |
| `Terminal.OpenWingetPane` | `alt+w` | winget パッケージマネージャペインを開く |
| `Terminal.SharePane` | `alt+s` | フォーカス中ペインを読み取り専用共有（localhost） |
| `Terminal.SharePaneOnLan` | `alt+shift+l` | フォーカス中ペインを LAN へ共有（平文・オプトイン） |
| `Terminal.StopSharePane` | `alt+shift+s` | フォーカス中ペインの共有を停止 |
| `Terminal.ConnectSharedSession` | `alt+shift+c` | 共有セッションの観戦ペインを開く |
| `Terminal.ConnectSharedSessionViaSsh` | `alt+shift+t` | SSH トンネルを自動起動して観戦 |

---

この下（英語の [README.md](./README.md)）はオリジナルの Microsoft README で、参考用にそのまま残しています。
