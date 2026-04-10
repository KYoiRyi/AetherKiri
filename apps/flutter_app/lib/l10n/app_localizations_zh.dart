// ignore: unused_import
import 'package:intl/intl.dart' as intl;
import 'app_localizations.dart';

// ignore_for_file: type=lint

/// The translations for Chinese (`zh`).
class AppLocalizationsZh extends AppLocalizations {
  AppLocalizationsZh([String locale = 'zh']) : super(locale);

  @override
  String get appTitle => 'KrKr2 Next';

  @override
  String get settings => '设置';

  @override
  String get addGame => '添加游戏';

  @override
  String get refresh => '刷新';

  @override
  String get howToImport => '导入指南';

  @override
  String get noGamesYet => '尚未添加任何游戏';

  @override
  String get noGamesHintDesktop => '点击「添加游戏」选择游戏目录';

  @override
  String get noGamesHintIos =>
      '使用「文件」App 将游戏文件夹复制到：\n我的 iPhone > Krkr2 > Games\n然后点击「刷新」';

  @override
  String get importGames => '导入游戏';

  @override
  String get importGamesDesc => '请使用「文件」App 将游戏文件夹复制到本应用的目录：';

  @override
  String get importStep1 => '1. 打开 iPhone 上的「文件」App';

  @override
  String get importStep2 => '2. 前往：我的 iPhone > Krkr2 > Games';

  @override
  String get importStep3 => '3. 将游戏文件夹复制到 Games 目录';

  @override
  String get importStep4 => '4. 返回本应用，点击「刷新」检测新游戏';

  @override
  String get gamesDirectory => '游戏目录：Games/';

  @override
  String get gotIt => '知道了';

  @override
  String get removeGame => '移除游戏';

  @override
  String removeGameConfirm(String title) {
    return '从列表中移除「$title」？\n这不会删除游戏文件。';
  }

  @override
  String get cancel => '取消';

  @override
  String get remove => '移除';

  @override
  String get renameGame => '重命名游戏';

  @override
  String get displayTitle => '显示名称';

  @override
  String get save => '保存';

  @override
  String gameAlreadyExists(String title) {
    return '游戏已存在：$title';
  }

  @override
  String get builtInReady => '内置 ✓';

  @override
  String get builtInNotReady => '内置 ✗';

  @override
  String get customNotSet => '自定义（未设置）';

  @override
  String get engineNotFoundBuiltIn => '未找到内置引擎。请使用构建脚本将引擎打包到应用中，或在设置中切换到自定义模式。';

  @override
  String get engineNotFoundCustom => '引擎 dylib 未设置。请先在设置中进行配置。';

  @override
  String lastPlayed(String time) {
    return '上次游玩：$time';
  }

  @override
  String playDuration(String duration) {
    return '已玩 $duration';
  }

  @override
  String get rename => '重命名';

  @override
  String get setCover => '设置封面';

  @override
  String get coverFromGallery => '从相册选择';

  @override
  String get coverFromCamera => '拍照';

  @override
  String get coverRemove => '移除封面';

  @override
  String get settingsEngine => '引擎';

  @override
  String get engineMode => '引擎模式';

  @override
  String get builtIn => '内置';

  @override
  String get custom => '自定义';

  @override
  String get builtInEngineAvailable => '内置引擎可用';

  @override
  String get builtInEngineNotFound => '未找到内置引擎';

  @override
  String get builtInEngineHint => '请使用构建脚本编译并将引擎打包到应用中。';

  @override
  String get engineDylibPath => '引擎 dylib 路径';

  @override
  String get notSetRequired => '未设置（必填）';

  @override
  String get clearPath => '清除路径';

  @override
  String get browse => '浏览...';

  @override
  String get selectEngineDylib => '选择引擎 dylib';

  @override
  String get settingsRendering => '渲染';

  @override
  String get settingsDevelopment => '开发者';

  @override
  String get renderPipeline => '渲染管线';

  @override
  String get renderPipelineHint => '需要重启游戏后生效';

  @override
  String get opengl => 'OpenGL';

  @override
  String get software => '软件渲染';

  @override
  String get graphicsBackend => '图形后端';

  @override
  String get graphicsBackendHint => 'ANGLE 翻译层后端（仅 Android）。需重启生效。';

  @override
  String get opengles => 'OpenGL ES';

  @override
  String get vulkan => 'Vulkan';

  @override
  String get performanceOverlay => '性能监控';

  @override
  String get performanceOverlayDesc => '显示帧率和图形 API 信息';

  @override
  String get fpsLimitEnabled => '帧率限制';

  @override
  String get fpsLimitEnabledDesc => '限制引擎渲染频率以节省功耗';

  @override
  String get fpsLimitOff => '关闭（垂直同步）';

  @override
  String get forceLandscape => '锁定横屏';

  @override
  String get forceLandscapeDesc => '游戏运行时强制横屏显示（手机推荐开启）';

  @override
  String get pluginTrace => '插件调用追踪';

  @override
  String get pluginTraceDesc => '将所有插件原生调用记录到 plugin_trace.log 用于调试';

  @override
  String get mockBypass => 'Mock 绕过';

  @override
  String get mockBypassDesc => '为缺失插件返回 mock 对象以抑制错误。关闭可暴露真实错误用于调试。';

  @override
  String get consoleLogFile => '控制台日志文件';

  @override
  String get consoleLogFileDesc => '将引擎控制台日志写入 krkr.console.log 文件';

  @override
  String get targetFrameRate => '目标帧率';

  @override
  String get targetFrameRateDesc => '启用限制时的最大渲染频率';

  @override
  String fpsLabel(int fps) {
    return '$fps FPS';
  }

  @override
  String get settingsGeneral => '通用';

  @override
  String get language => '语言';

  @override
  String get languageSystem => '跟随系统';

  @override
  String get languageEn => 'English';

  @override
  String get languageZh => '简体中文';

  @override
  String get languageJa => '日本語';

  @override
  String get themeMode => '主题';

  @override
  String get themeDark => '深色';

  @override
  String get themeLight => '浅色';

  @override
  String get settingsAbout => '关于';

  @override
  String get version => '版本';

  @override
  String get aboutVersionDesc => '0.2.0-beta.1';

  @override
  String get aboutAuthor => '作者';

  @override
  String get aboutAuthorName => 'reAAAq (由 KYoiRyi 适配)';

  @override
  String get aboutGithubFork => 'AetherKiri (当前分支项目)';

  @override
  String get aboutEmail => '邮箱';

  @override
  String get aboutEmailCopied => '邮箱已复制到剪贴板';

  @override
  String get gameEngineError => '引擎错误';

  @override
  String get unknownError => '未知错误';

  @override
  String get back => '返回';

  @override
  String get retry => '重试';

  @override
  String get hideDebug => '隐藏调试';

  @override
  String get showDebug => '显示调试';

  @override
  String get pause => '暂停';

  @override
  String get resume => '继续';

  @override
  String get exitGame => '退出游戏';

  @override
  String get selectGameDirectory => '选择游戏目录';

  @override
  String get selectGameArchive => '选择 XP3 文件';

  @override
  String get addArchive => '添加 XP3';

  @override
  String get justNow => '刚刚';

  @override
  String minutesAgo(int count) {
    return '$count 分钟前';
  }

  @override
  String hoursAgo(int count) {
    return '$count 小时前';
  }

  @override
  String daysAgo(int count) {
    return '$count 天前';
  }

  @override
  String get packXp3 => '打包为 XP3';

  @override
  String get unpackXp3 => '解包 XP3';

  @override
  String get packingProgress => '正在打包...';

  @override
  String get unpackingProgress => '正在解包...';

  @override
  String get packComplete => '打包完成';

  @override
  String get unpackComplete => '解包完成';

  @override
  String xp3OperationFailed(String error) {
    return '操作失败：$error';
  }

  @override
  String get launchGame => '启动游戏';

  @override
  String get copiedToClipboard => '已复制到剪贴板';

  @override
  String get gameFormat => '格式';

  @override
  String get gamePath => '路径';

  @override
  String get scrapeMetadata => '刮削信息';

  @override
  String get scrapeMetadataDialogTitle => '刮削信息';

  @override
  String get scrapeMetadataSearchHint => '输入游戏名称搜索';

  @override
  String get scrapeMetadataSearch => '搜索';

  @override
  String get scrapeMetadataSelectTitle => '选择匹配作品';

  @override
  String get scrapeMetadataNoResults => '未找到匹配作品，请尝试其他关键词。';

  @override
  String get scrapeMetadataConfirm => '确认';

  @override
  String get scrapeMetadataSuccess => '已更新名称与封面。';

  @override
  String get scrapeMetadataCoverFailed => '名称已更新，封面获取失败可手动设置。';

  @override
  String get scrapeMetadataEnterName => '请输入游戏名称。';

  @override
  String get scrapeMetadataSourceError => '数据源暂时不可用，请稍后重试。';

  @override
  String get scrapeMetadataSelectOne => '请选择一项作品。';

  @override
  String get scrapeAfterAddPrompt => '是否刮削该游戏？选择「是」将搜索并填写名称与封面。';

  @override
  String get scrapeAfterAddNo => '否';

  @override
  String get scrapeAfterAddYes => '是';
}
