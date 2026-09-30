# DEVELOPMENT.md

## 必要なもの

- Visual Studio（または Build Tools）の「C++ によるデスクトップ開発」ワークロード（MSVC x64 + Windows SDK）
- Inno Setup 6（インストーラーを作るときだけ）

```ps
winget install --id JRSoftware.InnoSetup --scope machine
```

`C:\Program Files (x86)\Inno Setup 6\ISCC.exe` に入る。

## ビルド

```bat
build.bat
```

- `build\EdgThumb.dll`（シェル拡張）と `build\edgdump.exe`（テストツール）ができる
- `/W4`、`/MT`（静的リンク）。自前コードの警告はゼロ。miniz だけ個別に警告を抑制している
- `src\version.rc` を `rc.exe` でコンパイルして DLL に入れる
- ビルド後に `vctip.exe` が残って `build\` をロックすることがある。消せないときは止める

## テスト

`edgdump.exe` で PNG を出して見た目を確認する。

```bat
build\edgdump.exe samples\icons.edg out.png
```

終了コード：0 = 出力した / 1 = 失敗（壊れた入力、非対応、1 GiB 超など）/ 2 = 使い方の誤り。

大量のファイルで確かめたいときは、公式ローダー（[EDGE2 File Loader Library](https://takabosoft.com/download/win/edge2/edge2_loader_100.zip)、MIT）をリポジトリの外でビルドし、同じ規則で BGRA にした結果と画素単位で突き合わせる。公式のコードはこのリポジトリに入れない。

リポジトリに入れてよい `.edg` は `samples/icons.edg` だけ。それ以外の作品の `.edg` と出力 PNG は、リポジトリに入れたり外へ出したりしない。

## インストーラー

先に `build.bat` で DLL を作ってから：

```ps
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\EdgThumb.iss
```

`dist\EdgThumbSetup-1.0.0.exe` ができる（`dist\` は Git に入れない）。

- 管理者権限、x64 のみ、`HKLM` に登録（`.edg` の既定値には触らない）
- VC++ 再頒布パッケージは不要（DLL は `/MT`）

動作確認（インストール → サムネイル表示 → アンインストール）はエクスプローラーの再起動を伴うので、自分の環境で行う。
