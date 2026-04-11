// ignore: unused_import
import 'package:intl/intl.dart' as intl;
import 'app_localizations.dart';

// ignore_for_file: type=lint

/// The translations for Japanese (`ja`).
class AppLocalizationsJa extends AppLocalizations {
  AppLocalizationsJa([String locale = 'ja']) : super(locale);

  @override
  String get appTitle => 'AetherKiri';

  @override
  String get settings => '設定';

  @override
  String get addGame => 'ゲームを追加';

  @override
  String get refresh => '更新';

  @override
  String get howToImport => 'インポート方法';

  @override
  String get noGamesYet => 'ゲームがまだ追加されていません';

  @override
  String get noGamesHintDesktop => '「ゲームを追加」をクリックしてゲームディレクトリを選択してください';

  @override
  String get noGamesHintIos =>
      '「ファイル」アプリでゲームフォルダをコピーしてください：\niPhone内 > Krkr2 > Games\nその後「更新」をタップ';

  @override
  String get importGames => 'ゲームをインポート';

  @override
  String get importGamesDesc =>
      '「ファイル」アプリを使用して、ゲームフォルダをこのアプリのディレクトリにコピーしてください：';

  @override
  String get importStep1 => '1. iPhoneの「ファイル」アプリを開く';

  @override
  String get importStep2 => '2. iPhone内 > Krkr2 > Games に移動';

  @override
  String get importStep3 => '3. ゲームフォルダをGamesディレクトリにコピー';

  @override
  String get importStep4 => '4. アプリに戻り「更新」をタップして新しいゲームを検出';

  @override
  String get gamesDirectory => 'ゲームディレクトリ：Games/';

  @override
  String get gotIt => '了解';

  @override
  String get removeGame => 'ゲームを削除';

  @override
  String removeGameConfirm(String title) {
    return 'リストから「$title」を削除しますか？\nゲームファイルは削除されません。';
  }

  @override
  String get cancel => 'キャンセル';

  @override
  String get remove => '削除';

  @override
  String get renameGame => 'ゲーム名を変更';

  @override
  String get displayTitle => '表示名';

  @override
  String get save => '保存';

  @override
  String gameAlreadyExists(String title) {
    return 'ゲームは既に存在します：$title';
  }

  @override
  String get builtInReady => '内蔵 ✓';

  @override
  String get builtInNotReady => '内蔵 ✗';

  @override
  String get customNotSet => 'カスタム（未設定）';

  @override
  String get engineNotFoundBuiltIn =>
      '内蔵エンジンが見つかりません。ビルドスクリプトでエンジンをバンドルするか、設定でカスタムモードに切り替えてください。';

  @override
  String get engineNotFoundCustom => 'エンジンdylibが設定されていません。先に設定で構成してください。';

  @override
  String lastPlayed(String time) {
    return '最終プレイ：$time';
  }

  @override
  String playDuration(String duration) {
    return 'プレイ時間 $duration';
  }

  @override
  String get rename => '名前変更';

  @override
  String get setCover => 'カバーを設定';

  @override
  String get coverFromGallery => 'ギャラリーから選択';

  @override
  String get coverFromCamera => '写真を撮る';

  @override
  String get coverRemove => 'カバーを削除';

  @override
  String get settingsEngine => 'エンジン';

  @override
  String get engineMode => 'エンジンモード';

  @override
  String get builtIn => '内蔵';

  @override
  String get custom => 'カスタム';

  @override
  String get builtInEngineAvailable => '内蔵エンジン利用可能';

  @override
  String get builtInEngineNotFound => '内蔵エンジンが見つかりません';

  @override
  String get builtInEngineHint => 'ビルドスクリプトを使用してエンジンをコンパイルし、アプリにバンドルしてください。';

  @override
  String get engineDylibPath => 'エンジンdylibパス';

  @override
  String get notSetRequired => '未設定（必須）';

  @override
  String get clearPath => 'パスをクリア';

  @override
  String get browse => '参照...';

  @override
  String get selectEngineDylib => 'エンジンdylibを選択';

  @override
  String get settingsRendering => 'レンダリング';

  @override
  String get settingsDevelopment => '開発';

  @override
  String get renderPipeline => 'レンダリングパイプライン';

  @override
  String get renderPipelineHint => 'ゲームの再起動後に有効になります';

  @override
  String get opengl => 'OpenGL';

  @override
  String get software => 'ソフトウェア';

  @override
  String get graphicsBackend => 'グラフィックスバックエンド';

  @override
  String get graphicsBackendHint => 'ANGLE翻訳レイヤーバックエンド（Androidのみ）。再起動が必要です。';

  @override
  String get opengles => 'OpenGL ES';

  @override
  String get vulkan => 'Vulkan';

  @override
  String get performanceOverlay => 'パフォーマンスオーバーレイ';

  @override
  String get performanceOverlayDesc => 'FPSとグラフィックAPI情報を表示';

  @override
  String get fpsLimitEnabled => 'フレームレート制限';

  @override
  String get fpsLimitEnabledDesc => 'エンジンの描画頻度を制限して省電力';

  @override
  String get fpsLimitOff => 'オフ（VSync）';

  @override
  String get forceLandscape => '横画面ロック';

  @override
  String get forceLandscapeDesc => 'ゲーム実行時に横向き表示を強制します（スマートフォン推奨）';

  @override
  String get pluginTrace => 'プラグインコールトレース';

  @override
  String get pluginTraceDesc => 'すべてのプラグインネイティブ呼び出しを plugin_trace.log に記録します';

  @override
  String get mockBypass => 'Mock バイパス';

  @override
  String get mockBypassDesc => '未実装プラグインのエラーを mock オブジェクトで回避します。無効化すると実際のエラーが表示されます。';

  @override
  String get consoleLogFile => 'コンソールログファイル';

  @override
  String get consoleLogFileDesc => 'エンジンコンソールログを krkr.console.log に出力します';

  @override
  String get targetFrameRate => '目標フレームレート';

  @override
  String get targetFrameRateDesc => '制限有効時の最大描画頻度';

  @override
  String fpsLabel(int fps) {
    return '$fps FPS';
  }

  @override
  String get settingsGeneral => '一般';

  @override
  String get language => '言語';

  @override
  String get languageSystem => 'システムに従う';

  @override
  String get languageEn => 'English';

  @override
  String get languageZh => '简体中文';

  @override
  String get languageJa => '日本語';

  @override
  String get themeMode => 'テーマ';

  @override
  String get themeDark => 'ダーク';

  @override
  String get themeLight => 'ライト';

  @override
  String get themeSystem => 'システムに従う';

  @override
  String get settingsAbout => 'バージョン情報';

  @override
  String get version => 'バージョン';

  @override
  String get aboutVersionDesc => '0.2.0-beta.1';

  @override
  String get aboutAuthor => '作者';

  @override
  String get aboutAuthorName => 'reAAAq (KYoiRyi により適合)';

  @override
  String get aboutGithubFork => 'AetherKiri (現在のフォーク)';

  @override
  String get aboutEmail => 'メール';

  @override
  String get aboutEmailCopied => 'メールアドレスをコピーしました';

  @override
  String get gameEngineError => 'エンジンエラー';

  @override
  String get unknownError => '不明なエラー';

  @override
  String get back => '戻る';

  @override
  String get retry => '再試行';

  @override
  String get hideDebug => 'デバッグを非表示';

  @override
  String get showDebug => 'デバッグを表示';

  @override
  String get pause => '一時停止';

  @override
  String get resume => '再開';

  @override
  String get exitGame => 'ゲームを終了';

  @override
  String get selectGameDirectory => 'ゲームディレクトリを選択';

  @override
  String get selectGameArchive => 'XP3 ファイルを選択';

  @override
  String get addArchive => 'XP3 追加';

  @override
  String get justNow => 'たった今';

  @override
  String minutesAgo(int count) {
    return '$count分前';
  }

  @override
  String hoursAgo(int count) {
    return '$count時間前';
  }

  @override
  String daysAgo(int count) {
    return '$count日前';
  }

  @override
  String get packXp3 => 'XP3にパック';

  @override
  String get unpackXp3 => 'XP3を展開';

  @override
  String get packingProgress => 'パック中...';

  @override
  String get unpackingProgress => '展開中...';

  @override
  String get packComplete => 'パック完了';

  @override
  String get unpackComplete => '展開完了';

  @override
  String xp3OperationFailed(String error) {
    return '操作失敗：$error';
  }

  @override
  String get launchGame => 'ゲームを開始';

  @override
  String get copiedToClipboard => 'クリップボードにコピーしました';

  @override
  String get gameFormat => 'フォーマット';

  @override
  String get gamePath => 'パス';

  @override
  String get scrapeMetadata => '情報を取得';

  @override
  String get scrapeMetadataDialogTitle => '情報を取得';

  @override
  String get scrapeMetadataSearchHint => 'ゲーム名を入力して検索';

  @override
  String get scrapeMetadataSearch => '検索';

  @override
  String get scrapeMetadataSelectTitle => '一致する作品を選択';

  @override
  String get scrapeMetadataNoResults => '一致する作品がありません。別のキーワードをお試しください。';

  @override
  String get scrapeMetadataConfirm => '確定';

  @override
  String get scrapeMetadataSuccess => '名前とカバーを更新しました。';

  @override
  String get scrapeMetadataCoverFailed => '名前を更新しました。カバーの取得に失敗しました。手動で設定できます。';

  @override
  String get scrapeMetadataEnterName => 'ゲーム名を入力してください。';

  @override
  String get scrapeMetadataSourceError => 'データソースが利用できません。しばらくしてから再試行してください。';

  @override
  String get scrapeMetadataSelectOne => '作品を選択してください。';

  @override
  String get scrapeAfterAddPrompt => 'このゲームの情報を取得しますか？「はい」で検索して名前とカバーを設定します。';

  @override
  String get scrapeAfterAddNo => 'いいえ';

  @override
  String get scrapeAfterAddYes => 'はい';
}
