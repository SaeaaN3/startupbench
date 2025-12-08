# startupbench - GUIプログラムの起動時間の測定

## これは何？

`startupbench` は、GUIプログラムの起動時間を測定するためのCLI (command line interface) です。

## ビルド方法

CMakeを使ってください。

## 使い方

```txt
startupbench --- GUIプログラムの起動時間を計測する

使い方 1: startupbench --help
使い方 2: startupbench --version
使い方 3: startupbench CLASS_NAME program.exe ...
  CLASS_NAME       対象のウィンドウクラス名
  program.exe ...  測定対象のGUIプログラムとコマンドライン引数

例 1: startupbench Notepad notepad.exe             (メモ帳の起動時間を測定)
例 2: startupbench Notepad notepad.exe "file.txt"  (ファイルを指定してメモ帳の起動時間を測定)

NOTE: メモ帳(Notepad)は大きいファイル（32MB+ or 1GB+）を開けないので注意
```

## 対応環境

- MSYS2, Visual Studio

## ライセンス

- MIT

## 連絡先

- @katahiromz (katayama.hirofumi.mz@gmail.com)
