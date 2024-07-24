# AviUtl 凸包拡張編集フィルタプラグイン

凸包を描画するフィルタ効果「凸包σ」を追加する拡張編集フィルタプラグインです．

![凸包σのデモ](https://github.com/user-attachments/assets/d1eaa1f3-b261-4c59-8e72-dd367e35301e)


## 動作要件

- AviUtl 1.10 + 拡張編集 0.92

  http://spring-fragrance.mints.ne.jp/aviutl
  - 拡張編集 0.93rc1 等の他バージョンでは動作しません．

- Visual C++ 再頒布可能パッケージ（\[2015/2017/2019/2022\] の x86 対応版が必要）

  https://learn.microsoft.com/ja-jp/cpp/windows/latest-supported-vc-redist

- patch.aul の `r43 謎さうなフォーク版58` (`r43_ss_58`) 以降

  https://github.com/nazonoSAUNA/patch.aul/releases/latest

## 導入方法

`aviutl.exe` と同階層にある `plugins` フォルダ内に `ConvexClosure_S.eef` ファイルをコピーしてください．

## 凸包とは

1.  図形上の任意の2点に対して，その2点を結ぶ線分がその図形に含まれるとき，その図形は**凸集合**であるという．

1.  図形に対して，その図形を含む最小の凸集合を**凸包**と呼ぶ．

1.  もっと一般に，「[測地線](https://ja.wikipedia.org/wiki/%E6%B8%AC%E5%9C%B0%E7%B7%9A)」を定義できれば「[凸集合](https://ja.wikipedia.org/wiki/%E5%87%B8%E9%9B%86%E5%90%88)」や「凸包」は定義でき，任意の集合に対して凸包は一意に存在する．

参考: https://ja.wikipedia.org/wiki/%E5%87%B8%E5%8C%85

### このフィルタで計算する凸包

ピクセル画像を `αしきい値` を境に「透明ピクセル」と「不透明ピクセル」に二分して，不透明ピクセルを含む凸包を計算します．計算結果は多角形になるので（必要ならその辺々を `余白` 分だけ外側に移動して），元画像の背景として配置します．

![計算手順](https://github.com/user-attachments/assets/a9351ae0-6e3a-4438-a1cb-63d75f740a47)


## 使い方

正しく導入できているとフィルタ効果に「凸包σ」が追加されています．オブジェクトの背景にそのオブジェクトの凸包を，指定した色またはパターン画像で描画します．

![凸包σのGUI](https://github.com/user-attachments/assets/643f9634-07b2-4fe4-836c-d6859c340fcb)


### 各種パラメタ

- 余白

  実際の凸包を表す多角形の辺々を，指定したピクセル数だけ外側に移動して余白を確保します．

  最小値は `0`, 最大値は `500`, 初期値は `0`.

- 透明度

  凸包を描画する際の透明度を % 単位で指定します．

  最小値は `0.0`, 最大値は `100.0`, 初期値は `0.0`.

- 内透明度

  凸包の元となった図形の透明度を % 単位で指定します．

  最小値は `0.0`, 最大値は `100.0`, 初期値は `0.0`.

- αしきい値

  凸包の元となる図形のピクセルを，透明/不透明に分類する基準になるα値を % 単位で指定します．

  最小値は `0.0`, 最大値は `100.0`, 初期値は `50.0`.

- 画像X / 画像Y

  `パターン画像ファイル` を設定している場合のみ有効，パターン画像ファイルの位置を調節します．

  画像ファイルの左上座標が，元のオブジェクトの左上座標と一致する配置が $(0,0)$ の基準点になります．

  最小値は `-4000`, 最大値は `4000`, 初期値は `0`.

- アンチエイリアス

  凸包を表す多角形の辺々を描画する際に，アンチエイリアスを適用するかどうかを指定します．`アンチエイリアス`が OFF の場合に描画されるピクセルは，ON だった場合α値が 100% で描画されるはずだったピクセル（完全に凸包に含まれるピクセル）に限られます．

  初期値は ON.

- 背景色の設定

  凸包の色を指定します．パターン画像ファイルが設定されている場合は無視されます．

  初期値は `RGB( 0 , 0 , 0 )` （黒）です．

- パターン画像ファイル

  凸包部分に適用するパターン画像ファイルを指定します．画像ファイルをドラッグ&ドロップでも設定できます．

  画像ファイルのパスは可能な限り相対パスで保存・管理されます．詳しくは[こちら](#パターン画像のファイルパスについて)．

## パターン画像のファイルパスについて

パターン画像のファイルパスは可能な限りプロジェクトファイルか AviUtl.exe のあるフォルダからの相対パスとして記録管理するようにしています．

これで動画編集ファイルを整理する際プロジェクトファイルと素材ファイルを同じフォルダにまとめておけば，フォルダごと移動するだけでそのまま移動先のフォルダで編集や閲覧ができるようになります．

スクリプトなどで使う素材ファイルを AviUtl.exe のあるフォルダに置いている場合でも，AviUtl.exe をフォルダごと移動 / コピーでそのまま使うことができます．

スクリプトで指定した場合や `.exo`, `.exa`, `.exc` ファイルなどでも相対パスの形で指定できます．仕様は以下の通り:

1.  `<exe>` から始まる文字列は AviUtl.exe のあるフォルダからの相対パスとみなされます．

    例:

    - AviUtl.exe が `C:\hoge\aviutl.exe` に配置されていて，ファイルパスが `<exe>fuga\image.png` だった場合，`C:\hoge\fuga\image.png` のファイルをパターン画像として読み込みます．

1.  `<aup>` から始まる文字列はプロジェクトファイルのあるフォルダからの相対パスとみなされます．ただしプロジェクトファイルが未保存でファイル名が未指定の場合，AviUtl.exe のあるフォルダからの相対パスとして取り扱われます．

    例:

    - プロジェクトファイルが `C:\foo\bar.aup` に保存されていて，ファイルパスが `<aup>xyz\image.png` だった場合，`C:\foo\xyz\image.png` のファイルをパターン画像として読み込みます．

    - プロジェクトファイルが未保存の場合，AviUtl.exe が `C:\hoge\aviutl.exe` に配置されていて，ファイルパスが `<aup>xyz\image.png` だった場合，`C:\hoge\xyz\image.png` のファイルをパターン画像として読み込みます．

1.  その他の場合，普通にフルパス（あるいは現在実行中の AviUtl.exe の作業フォルダからの相対パス）として扱います．


## TIPS

1.  オブジェクトの上下左右に「角」がある場合，`余白` 部分がオブジェクト境界を越えて切り取られたようになってしまうことがあります．この場合，あらかじめ `領域拡張` しておくことで自然な角になることがあります．

    ![角の描画と領域拡張](https://github.com/user-attachments/assets/1afe3f5b-2738-41b1-84a6-0a76bd73c2b3)


1.  スクリプト制御やアニメーション効果，カスタムオブジェクトなどでも利用できます．

    - 例:

      ```lua
      obj.effect("凸包σ","余白",10, "透明度",20, "アンチエイリアス",1, "color",0xff0000)
      ```

      `余白` が `10`, `透明度` が `20`, `アンチエイリアス` が ON, `背景色の設定` が赤色 (`#FF0000`) の指定で `凸包σ` を適用．

    ほとんどのパラメタ名はトラックバーのボタンにあるテキストの通りですが，一部違うものがあります．

    1.  `背景色の設定` は `"color"` で指定します．

        `number` 型で `0xRRGGBB` の形式です．

    1.  `パターン画像ファイル` は `"file"` で指定します．

        `<aup>` や `<exe>` を利用して相対パスでファイルを指定することもできます．[[詳細](#パターン画像のファイルパスについて)]


## 改版履歴

- **v1.00** (2024-07-24)

  - 初版．


## ライセンス

このプログラムの利用・改変・再頒布等に関しては MIT ライセンスに従うものとします．

---

The MIT License (MIT)

Copyright (C) 2024 sigma-axis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

https://mit-license.org/


#  Credits

##  aviutl_exedit_sdk

https://github.com/ePi5131/aviutl_exedit_sdk （利用したブランチは[こちら](https://github.com/sigma-axis/aviutl_exedit_sdk/tree/self-use)です．）

---

1条項BSD

Copyright (c) 2022
ePi All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
THIS SOFTWARE IS PROVIDED BY ePi “AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL ePi BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


#  連絡・バグ報告

- GitHub: https://github.com/sigma-axis
- Twitter: https://x.com/sigma_axis
- nicovideo: https://www.nicovideo.jp/user/51492481
- Misskey.io: https://misskey.io/@sigma_axis
- Bluesky: https://bsky.app/profile/sigma-axis.bsky.social

