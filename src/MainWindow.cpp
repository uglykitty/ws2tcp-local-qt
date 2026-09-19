#include "MainWindow.h"

#include "UpdateDownloadDialog.h"

#include <QByteArray>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QScrollBar>
#include <QStyle>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#ifdef Q_OS_WIN
#include <QProcess>

#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

#include <cmath>

#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
#include "SystemProxy.h"
#endif
#ifdef Q_OS_WIN
#include "WslConfig.h"
#endif

namespace {

constexpr auto kDefaultListenAddress = "127.0.0.1:3128";
constexpr auto kUpdateManifestUrl =
    "https://wangguofang.net/ws2tcp-local/releases/latest.json";

bool isVersionNewer(const QString &remote, const QString &local) {
  const QStringList remoteParts = remote.split(QLatin1Char('.'));
  const QStringList localParts = local.split(QLatin1Char('.'));
  const int partCount = std::max(remoteParts.size(), localParts.size());
  for (int i = 0; i < partCount; ++i) {
    const int remoteValue =
        i < remoteParts.size() ? remoteParts.at(i).toInt() : 0;
    const int localValue =
        i < localParts.size() ? localParts.at(i).toInt() : 0;
    if (remoteValue != localValue) {
      return remoteValue > localValue;
    }
  }
  return false;
}

QIcon applicationIcon() {
  QIcon icon(":/icons/app-icon.png");
  if (icon.isNull()) {
    icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
  }
  return icon;
}

QPixmap gearPixmap(int size, const QColor &color) {
  QPixmap pixmap(size, size);
  pixmap.fill(Qt::transparent);

  constexpr int toothCount = 8;
  constexpr double pi = 3.14159265358979323846;
  const QPointF center(size / 2.0, size / 2.0);
  const double outerRadius = size * 0.46;
  const double rootRadius = size * 0.34;

  QPainterPath gear;
  for (int point = 0; point < toothCount * 4; ++point) {
    const double radius = point % 4 < 2 ? outerRadius : rootRadius;
    const double angle = -pi / 2.0 + point * pi / (toothCount * 2.0);
    const QPointF vertex(center.x() + std::cos(angle) * radius,
                         center.y() + std::sin(angle) * radius);
    if (point == 0) {
      gear.moveTo(vertex);
    } else {
      gear.lineTo(vertex);
    }
  }
  gear.closeSubpath();
  gear.addEllipse(center, size * 0.14, size * 0.14);
  gear.setFillRule(Qt::OddEvenFill);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillPath(gear, color);
  return pixmap;
}

QIcon settingsIcon(const QPalette &palette) {
  QIcon icon;
  for (const int size : {16, 24, 32}) {
    icon.addPixmap(gearPixmap(size, palette.color(QPalette::ButtonText)),
                   QIcon::Normal);
    icon.addPixmap(gearPixmap(size, palette.color(QPalette::Disabled,
                                                  QPalette::ButtonText)),
                   QIcon::Disabled);
  }
  return icon;
}

// The rest of the menu/toolbar icon set follows the same hand-drawn,
// palette-tinted approach as gearPixmap/settingsIcon above so every icon
// automatically matches the active theme (including dark mode) and renders
// crisply at any DPI, instead of shipping raster assets.
using IconPathBuilder = std::function<void(QPainterPath &, int)>;

QPixmap fillPathPixmap(int size, const QColor &color,
                       const IconPathBuilder &build) {
  QPixmap pixmap(size, size);
  pixmap.fill(Qt::transparent);
  QPainterPath path;
  build(path, size);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillPath(path, color);
  return pixmap;
}

QIcon fillPathIcon(const QPalette &palette, const IconPathBuilder &build) {
  QIcon icon;
  for (const int size : {16, 24, 32}) {
    icon.addPixmap(fillPathPixmap(size, palette.color(QPalette::ButtonText),
                                  build),
                   QIcon::Normal);
    icon.addPixmap(fillPathPixmap(size,
                                  palette.color(QPalette::Disabled,
                                                QPalette::ButtonText),
                                  build),
                   QIcon::Disabled);
  }
  return icon;
}

QPixmap strokePathPixmap(int size, const QColor &color,
                         const IconPathBuilder &build) {
  QPixmap pixmap(size, size);
  pixmap.fill(Qt::transparent);
  QPainterPath path;
  build(path, size);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen(color, std::max(1.0, size * 0.09));
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(path);
  return pixmap;
}

QIcon strokePathIcon(const QPalette &palette, const IconPathBuilder &build) {
  QIcon icon;
  for (const int size : {16, 24, 32}) {
    icon.addPixmap(strokePathPixmap(size, palette.color(QPalette::ButtonText),
                                    build),
                   QIcon::Normal);
    icon.addPixmap(strokePathPixmap(size,
                                    palette.color(QPalette::Disabled,
                                                  QPalette::ButtonText),
                                    build),
                   QIcon::Disabled);
  }
  return icon;
}

QPixmap glyphPixmap(int size, const QColor &color, QChar glyph) {
  QPixmap pixmap(size, size);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  const qreal margin = size * 0.12;
  QPen pen(color, std::max(1.0, size * 0.08));
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawEllipse(QRectF(margin, margin, size - 2 * margin,
                             size - 2 * margin));
  QFont font = painter.font();
  font.setBold(true);
  font.setPixelSize(static_cast<int>(size * 0.5));
  painter.setFont(font);
  painter.setPen(color);
  painter.drawText(pixmap.rect(), Qt::AlignCenter, glyph);
  return pixmap;
}

QIcon glyphIcon(const QPalette &palette, QChar glyph) {
  QIcon icon;
  for (const int size : {16, 24, 32}) {
    icon.addPixmap(glyphPixmap(size, palette.color(QPalette::ButtonText),
                               glyph),
                   QIcon::Normal);
    icon.addPixmap(glyphPixmap(size,
                               palette.color(QPalette::Disabled,
                                             QPalette::ButtonText),
                               glyph),
                   QIcon::Disabled);
  }
  return icon;
}

void playPath(QPainterPath &path, int size) {
  path.moveTo(size * 0.32, size * 0.20);
  path.lineTo(size * 0.32, size * 0.80);
  path.lineTo(size * 0.80, size * 0.50);
  path.closeSubpath();
}

void stopPath(QPainterPath &path, int size) {
  const qreal margin = size * 0.26;
  path.addRoundedRect(margin, margin, size - 2 * margin, size - 2 * margin,
                      size * 0.08, size * 0.08);
}

void exitPath(QPainterPath &path, int size) {
  const QRectF circle(size * 0.22, size * 0.24, size * 0.56, size * 0.56);
  path.arcMoveTo(circle, 125);
  path.arcTo(circle, 125, 290);
  path.moveTo(size * 0.5, size * 0.12);
  path.lineTo(size * 0.5, size * 0.42);
}

void terminalPath(QPainterPath &path, int size) {
  const QRectF frame(size * 0.14, size * 0.22, size * 0.72, size * 0.56);
  path.addRoundedRect(frame, size * 0.06, size * 0.06);
  const qreal chevronX = frame.left() + size * 0.14;
  path.moveTo(chevronX, frame.top() + size * 0.14);
  path.lineTo(chevronX + size * 0.12, frame.center().y());
  path.lineTo(chevronX, frame.bottom() - size * 0.14);
  path.moveTo(chevronX + size * 0.16, frame.bottom() - size * 0.14);
  path.lineTo(chevronX + size * 0.34, frame.bottom() - size * 0.14);
}

void downloadPath(QPainterPath &path, int size) {
  const qreal cx = size * 0.5;
  path.moveTo(cx, size * 0.14);
  path.lineTo(cx, size * 0.54);
  path.moveTo(cx - size * 0.16, size * 0.40);
  path.lineTo(cx, size * 0.58);
  path.lineTo(cx + size * 0.16, size * 0.40);
  path.moveTo(size * 0.20, size * 0.70);
  path.lineTo(size * 0.20, size * 0.84);
  path.lineTo(size * 0.80, size * 0.84);
  path.lineTo(size * 0.80, size * 0.70);
}

void wifiPath(QPainterPath &path, int size) {
  const QPointF base(size * 0.5, size * 0.76);
  path.addEllipse(base, size * 0.045, size * 0.045);
  for (const double radius : {0.18, 0.32, 0.46}) {
    const QRectF rect(base.x() - size * radius, base.y() - size * radius,
                      size * radius * 2, size * radius * 2);
    path.arcMoveTo(rect, 35);
    path.arcTo(rect, 35, 110);
  }
}

void refreshPath(QPainterPath &path, int size) {
  const QRectF circle(size * 0.2, size * 0.2, size * 0.6, size * 0.6);
  path.arcMoveTo(circle, 40);
  path.arcTo(circle, 40, 260);

  constexpr double pi = 3.14159265358979323846;
  const double angle = 40.0 * pi / 180.0;
  const QPointF center = circle.center();
  const double radius = circle.width() / 2.0;
  const QPointF tip(center.x() + radius * std::cos(angle),
                    center.y() - radius * std::sin(angle));
  const double tangent = angle + pi / 2.0;
  const QPointF direction(std::cos(tangent), -std::sin(tangent));
  const QPointF normal(-direction.y(), direction.x());
  const double headLength = size * 0.16;
  const double headWidth = size * 0.12;
  const QPointF back = tip - direction * headLength;
  path.moveTo(tip);
  path.lineTo(back + normal * headWidth);
  path.moveTo(tip);
  path.lineTo(back - normal * headWidth);
}

#ifdef Q_OS_WIN
// wsl.exe mis-reconstructs the Linux command line when an argv element it
// receives contains embedded double quotes (verified empirically: even
// `bash -lc "echo \"A $HOME\""` loses the $HOME expansion) -- so install
// logic lives in real .sh files under src/resources/wsl, bundled as Qt
// resources, and gets shipped into WSL as a script file instead of an
// inline -c string. See MainWindow::runWslScript.
QByteArray readBundledScript(const QString &resourcePath) {
  QFile file(resourcePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return QByteArray();
  }
  // On a Windows checkout these .sh resources can end up with CRLF line
  // endings (core.autocrlf). That stray '\r' rides along into WSL's bash,
  // e.g. turning "set -eo pipefail" into "set -eo pipefail\r" -- bash then
  // rejects "pipefail\r" as an invalid option name. Normalize to LF since
  // this only ever runs as a Unix shell script.
  QByteArray script = file.readAll();
  script.replace("\r\n", "\n");
  return script;
}

// wsl.exe's own launcher messages (install progress, "not installed" /
// "run wsl --update" style diagnostics, ...) come out as UTF-16LE with no
// BOM whenever stdout isn't a real console, which ours never is -- while
// actual output relayed from inside the distro (curl, npm, nvm, ...) is
// plain UTF-8. Detect the former by its telltale alternating-NUL bytes.
QString decodeWslOutput(const QByteArray &data) {
  const bool looksUtf16Le =
      data.size() >= 4 && data.at(1) == '\0' && data.at(3) == '\0';
  if (looksUtf16Le) {
    return QString::fromUtf16(
        reinterpret_cast<const char16_t *>(data.constData()),
        data.size() / 2);
  }
  return QString::fromUtf8(data);
}
#endif

}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  SystemProxy::recoverStaleSettings();
#endif
  handle_ = ws2tcp_handle_new();

  auto *central = new QWidget(this);
  auto *root = new QVBoxLayout(central);
  auto *form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  listenEdit_ = new QLineEdit(kDefaultListenAddress, this);
  socksListenEdit_ = new QLineEdit(this);
  socksListenEdit_->setPlaceholderText(
      tr("127.0.0.1:1080 (optional, blank disables SOCKS5)"));
  gatewayEdit_ =
      new QLineEdit("wss://wangguofang.net/tunnel", this);
  usernameEdit_ = new QLineEdit(this);
  passwordEdit_ = new QLineEdit(this);
  passwordEdit_->setEchoMode(QLineEdit::Password);
  passwordVisibilityButton_ = new QToolButton(this);
  passwordVisibilityButton_->setText(tr("Show"));
  passwordVisibilityButton_->setCheckable(true);
  passwordVisibilityButton_->setToolTip(tr("Show password"));

  auto *passwordRow = new QWidget(this);
  auto *passwordLayout = new QHBoxLayout(passwordRow);
  passwordLayout->setContentsMargins(0, 0, 0, 0);
  passwordLayout->addWidget(passwordEdit_);
  passwordLayout->addWidget(passwordVisibilityButton_);
  customRulesEdit_ = new QLineEdit(this);
  customRulesBrowseButton_ = new QToolButton(this);
  customRulesBrowseButton_->setText(tr("Browse..."));
  customRulesBrowseButton_->setToolTip(tr("Select custom rules file"));

  auto *customRulesRow = new QWidget(this);
  auto *customRulesLayout = new QHBoxLayout(customRulesRow);
  customRulesLayout->setContentsMargins(0, 0, 0, 0);
  customRulesLayout->addWidget(customRulesEdit_);
  customRulesLayout->addWidget(customRulesBrowseButton_);

  proxyModeCombo_ = new QComboBox(this);
  proxyModeCombo_->addItem("global");
  proxyModeCombo_->addItem("auto");
  proxyModeCombo_->setCurrentText("auto");

#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  systemProxyCheck_ = new QCheckBox(this);
#endif

  form->addRow(tr("Listen"), listenEdit_);
  form->addRow(tr("SOCKS5 listen"), socksListenEdit_);
  form->addRow(tr("Gateway"), gatewayEdit_);
  form->addRow(tr("Username"), usernameEdit_);
  form->addRow(tr("Password"), passwordRow);
  form->addRow(tr("Custom rules"), customRulesRow);
  form->addRow(tr("Proxy mode"), proxyModeCombo_);
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  form->addRow(tr("Set system proxy"), systemProxyCheck_);
#endif

  logView_ = new QPlainTextEdit(this);
  logView_->setReadOnly(true);
  connect(logView_, &QPlainTextEdit::textChanged, this, [this]() {
    QScrollBar *scrollBar = logView_->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
  });

  root->addLayout(form);
  root->addWidget(logView_);

  setCentralWidget(central);
  setWindowTitle("ws2tcp-local");
  setWindowIcon(applicationIcon());
  resize(720, 520);

  startAction_ = new QAction(fillPathIcon(palette(), playPath), tr("&Start"),
                             this);
  startAction_->setToolTip(tr("Start the proxy"));
  stopAction_ = new QAction(fillPathIcon(palette(), stopPath), tr("S&top"),
                            this);
  stopAction_->setToolTip(tr("Stop the proxy"));
  settingsAction_ = new QAction(settingsIcon(palette()), tr("&Settings"), this);
  settingsAction_->setToolTip(tr("Open settings"));

  auto *proxyToolBar = addToolBar(tr("Proxy"));
  proxyToolBar->setObjectName("proxyToolBar");
  proxyToolBar->setIconSize(QSize(22, 22));
  proxyToolBar->addAction(startAction_);
  proxyToolBar->addAction(stopAction_);
  proxyToolBar->addSeparator();
  proxyToolBar->addAction(settingsAction_);

  auto *proxyMenu = menuBar()->addMenu(tr("&Proxy"));
  proxyMenu->addAction(startAction_);
  proxyMenu->addAction(stopAction_);
  proxyMenu->addSeparator();
  auto *exitAction = proxyMenu->addAction(tr("E&xit"));
  exitAction->setIcon(strokePathIcon(palette(), exitPath));
  exitAction->setShortcut(QKeySequence::Quit);
  connect(exitAction, &QAction::triggered, this, &MainWindow::quitFromTray);

  // Each language's name is shown in its own script, not translated, so a
  // user can find their language even if the current UI text is unreadable
  // to them.
  auto *languageMenu = menuBar()->addMenu(tr("&Language"));
  auto *languageGroup = new QActionGroup(this);
  languageGroup->setExclusive(true);
  auto *englishLanguageAction =
      languageMenu->addAction(QStringLiteral("English"));
  englishLanguageAction->setCheckable(true);
  englishLanguageAction->setData("en_US");
  languageGroup->addAction(englishLanguageAction);
  auto *chineseLanguageAction =
      languageMenu->addAction(QStringLiteral(u"简体中文"));
  chineseLanguageAction->setCheckable(true);
  chineseLanguageAction->setData("zh_CN");
  languageGroup->addAction(chineseLanguageAction);
  connect(languageGroup, &QActionGroup::triggered, this,
          [this](QAction *action) {
            const QString newLanguage = action->data().toString();
            if (newLanguage == language_) {
              return;
            }
            language_ = newLanguage;
            saveUserSettings();
            QMessageBox::information(
                this, tr("Settings"),
                tr("The language change will take effect after you restart "
                   "ws2tcp-local."));
          });

#ifdef Q_OS_WIN
  wslMenu_ = menuBar()->addMenu(tr("&WSL"));

  auto *installWslAction = wslMenu_->addAction(tr("Install WSL"));
  installWslAction->setIcon(strokePathIcon(palette(), downloadPath));
  connect(installWslAction, &QAction::triggered, this,
          &MainWindow::installWsl);

  auto *wslMirroredAction =
      wslMenu_->addAction(tr("Set WSL Networking to Mirrored"));
  wslMirroredAction->setIcon(strokePathIcon(palette(), wifiPath));
  connect(wslMirroredAction, &QAction::triggered, this,
          &MainWindow::enableWslMirroredNetworking);

  wslMenu_->addSeparator();

  auto *installNodeAction =
      wslMenu_->addAction(tr("Install Node.js (via nvm) in WSL"));
  installNodeAction->setIcon(strokePathIcon(palette(), terminalPath));
  connect(installNodeAction, &QAction::triggered, this,
          &MainWindow::installNodeViaNvm);

  auto *installOpenCodeAction =
      wslMenu_->addAction(tr("Install opencode CLI in WSL"));
  installOpenCodeAction->setIcon(strokePathIcon(palette(), terminalPath));
  connect(installOpenCodeAction, &QAction::triggered, this,
          &MainWindow::installOpenCodeCli);

  auto *installCodexAction =
      wslMenu_->addAction(tr("Install Codex CLI in WSL"));
  installCodexAction->setIcon(strokePathIcon(palette(), terminalPath));
  connect(installCodexAction, &QAction::triggered, this,
          &MainWindow::installCodexCli);

  auto *installClaudeAction =
      wslMenu_->addAction(tr("Install Claude Code CLI in WSL"));
  installClaudeAction->setIcon(strokePathIcon(palette(), terminalPath));
  connect(installClaudeAction, &QAction::triggered, this,
          &MainWindow::installClaudeCodeCli);
#endif

  auto *helpMenu = menuBar()->addMenu(tr("&Help"));
  auto *checkForUpdatesAction = helpMenu->addAction(tr("Check for &Updates..."));
  checkForUpdatesAction->setIcon(strokePathIcon(palette(), refreshPath));
  connect(checkForUpdatesAction, &QAction::triggered, this,
          &MainWindow::checkForUpdates);
  auto *aboutAction = helpMenu->addAction(tr("&About ws2tcp-local"));
  aboutAction->setIcon(glyphIcon(palette(), QChar('i')));
  connect(aboutAction, &QAction::triggered, this,
          &MainWindow::showAboutDialog);

  connect(startAction_, &QAction::triggered, this, &MainWindow::startProxy);
  connect(stopAction_, &QAction::triggered, this, &MainWindow::stopProxy);
  connect(settingsAction_, &QAction::triggered, this,
          &MainWindow::showSettingsDialog);
  connect(passwordVisibilityButton_, &QToolButton::toggled, this,
          [this](bool visible) {
            passwordEdit_->setEchoMode(visible ? QLineEdit::Normal
                                               : QLineEdit::Password);
            passwordVisibilityButton_->setText(visible ? tr("Hide")
                                                       : tr("Show"));
            passwordVisibilityButton_->setToolTip(
                visible ? tr("Hide password") : tr("Show password"));
          });
  connect(customRulesBrowseButton_, &QToolButton::clicked, this, [this]() {
    QString initialPath = customRulesEdit_->text().trimmed();
    if (!initialPath.isEmpty() && QFileInfo(initialPath).isFile()) {
      initialPath = QFileInfo(initialPath).absolutePath();
    }
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("Select custom rules file"), initialPath,
        tr("All files (*)"));
    if (!filePath.isEmpty()) {
      customRulesEdit_->setText(filePath);
    }
  });
  const int logRc =
      ws2tcp_set_log_callback(&MainWindow::handleRustLog, this,
                              "ws2tcp_local=info,ws2tcp_local_ffi=info");
  if (logRc != WS2TCP_OK) {
    logMessage(tr("Failed to initialize Rust log callback"));
  }

  statusTimer_ = new QTimer(this);
  statusTimer_->setInterval(1000);
  connect(statusTimer_, &QTimer::timeout, this, &MainWindow::refreshStatus);
  statusTimer_->start();

  if (handle_ == nullptr) {
    showError(tr("Failed to create ws2tcp handle"));
    startAction_->setEnabled(false);
    stopAction_->setEnabled(false);
  }

  loadUserSettings();
#ifdef Q_OS_WIN
  maybePromptWslMirroredNetworking();
#endif
  (language_ == "zh_CN" ? chineseLanguageAction : englishLanguageAction)
      ->setChecked(true);
  connect(proxyModeCombo_, &QComboBox::currentTextChanged, this,
          &MainWindow::updateProxyMode);
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  connect(systemProxyCheck_, &QCheckBox::toggled, this,
          &MainWindow::setSystemProxyEnabled);
#endif
  setupTrayIcon();
  refreshStatus();
}

MainWindow::~MainWindow() {
  if (!userSettingsCleared_) {
    saveUserSettings();
  }

#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  SystemProxy::disable(nullptr);
#endif

  if (handle_ != nullptr) {
    ws2tcp_stop(handle_);
    ws2tcp_handle_free(handle_);
  }
}

bool MainWindow::clearUserSettingsAndQuit() {
  if (handle_ != nullptr &&
      ws2tcp_status(handle_) == WS2TCP_STATUS_RUNNING) {
    stopProxy();
    if (ws2tcp_status(handle_) == WS2TCP_STATUS_RUNNING) {
      return false;
    }
  }

#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  QString error;
  if (!SystemProxy::disable(&error)) {
    showError(tr("Unable to restore the system proxy: %1").arg(error));
    return false;
  }
  systemProxyActive_ = false;
#endif

  QSettings settings;
  settings.clear();
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    showError(tr("Unable to clear user settings"));
    return false;
  }

  userSettingsCleared_ = true;
  allowClose_ = true;
  QApplication::quit();
  return true;
}

void MainWindow::startProxy() {
  if (handle_ == nullptr) {
    showError(tr("Failed to create ws2tcp handle"));
    return;
  }
  if (gatewayEdit_->text().trimmed().isEmpty()) {
    showError(tr("Gateway is required"));
    return;
  }

  const QByteArray config = buildConfigJson();
  const int rc = ws2tcp_start(handle_, config.constData());
  if (rc == WS2TCP_OK) {
    // The proxy starts asynchronously and may already have stopped again (for
    // example when the gateway check fails) before the first status refresh;
    // treat it as running so that refreshStatus() reports the failure.
    wasRunning_ = true;
    saveUserSettings();
    updateConfigurationInputs(true);
    updateRuntimeStatus(tr("Starting %1").arg(listenEdit_->text().trimmed()));
    logMessage(tr("Started"));
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
    if (systemProxyCheck_->isChecked()) {
      setSystemProxyEnabled(true);
    }
#endif
  } else {
    appendError(tr("Start failed"));
  }
  refreshStatus();
  updateTrayActions();
}

void MainWindow::stopProxy() {
  if (handle_ == nullptr) {
    return;
  }

#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  setSystemProxyEnabled(false);
#endif

  const int rc = ws2tcp_stop(handle_);
  if (rc == WS2TCP_OK) {
    saveUserSettings();
    updateConfigurationInputs(false);
    updateRuntimeStatus(tr("Stopped"));
    logMessage(tr("Stopped"));
  } else {
    appendError(tr("Stop failed"));
  }
  refreshStatus();
  updateTrayActions();
}

void MainWindow::updateProxyMode(const QString &mode) {
  saveUserSettings();
  if (handle_ == nullptr ||
      ws2tcp_status(handle_) != WS2TCP_STATUS_RUNNING) {
    return;
  }

  const QByteArray modeUtf8 = mode.toUtf8();
  const int rc = ws2tcp_set_proxy_mode(handle_, modeUtf8.constData());
  if (rc == WS2TCP_OK) {
    logMessage(tr("Proxy mode changed to %1").arg(mode));
  } else {
    appendError(tr("Failed to change proxy mode"));
  }
}

void MainWindow::refreshStatus() {
  if (handle_ == nullptr) {
    updateRuntimeStatus(tr("Unavailable"));
    startAction_->setEnabled(false);
    stopAction_->setEnabled(false);
    return;
  }

  const bool running =
      ws2tcp_status(handle_) == WS2TCP_STATUS_RUNNING;
  // Update before anything below can open a modal dialog: the status timer
  // keeps firing while it is shown and must not report the same stop again.
  const bool wasRunning = wasRunning_;
  wasRunning_ = running;
  startAction_->setEnabled(!running);
  stopAction_->setEnabled(running);
  updateConfigurationInputs(running);
  updateTrayActions();

  if (wasRunning && !running) {
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
    setSystemProxyEnabled(false);
#endif
    const QString error =
        handle_ != nullptr ? QString::fromUtf8(ws2tcp_last_error(handle_))
                           : QString();
    if (!error.isEmpty()) {
      logMessage(tr("Proxy stopped with error: %1").arg(error));
      updateRuntimeStatus(tr("Stopped with error"));

      // The gateway is checked when the proxy starts. Tell the user what to
      // fix instead of leaving the failure in the log only.
      const Ws2TcpErrorKind kind = ws2tcp_last_error_kind(handle_);
      if (kind == WS2TCP_ERROR_KIND_AUTH_FAILED) {
        usernameEdit_->setFocus();
        showGatewayCheckFailure(
            tr("The gateway rejected the username or password. Check the "
               "credentials and start again."));
      } else if (kind == WS2TCP_ERROR_KIND_GATEWAY_CHECK_FAILED) {
        showGatewayCheckFailure(
            tr("The gateway is not available.\n\n%1").arg(error));
      }
    } else {
      updateRuntimeStatus(tr("Stopped"));
    }
  }
}

void MainWindow::showGatewayCheckFailure(const QString &message) {
  showAndActivate();
  QMessageBox::warning(this, tr("ws2tcp-local"), message);
}

void MainWindow::appendLog(QString message) {
  logMessage(message);
  updateRuntimeStatusFromLog(message);
}

void MainWindow::logMessage(const QString &message) {
  const QString timestamp =
      QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
  logView_->appendPlainText(QString("[%1] %2").arg(timestamp, message));
}

void MainWindow::showSettingsDialog() {
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Settings"));

  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout();
  auto *bufferSizeSpin = new QSpinBox(&dialog);
  bufferSizeSpin->setRange(1, 1024 * 1024);
  bufferSizeSpin->setValue(bufferSize_);
  const bool running =
      handle_ != nullptr &&
      ws2tcp_status(handle_) == WS2TCP_STATUS_RUNNING;
  bufferSizeSpin->setEnabled(!running);
  form->addRow(tr("Buffer size"), bufferSizeSpin);

  auto *refreshIntervalSpin = new QSpinBox(&dialog);
  refreshIntervalSpin->setRange(1, 24 * 60 * 60);
  refreshIntervalSpin->setValue(refreshIntervalSeconds_);
  refreshIntervalSpin->setEnabled(!running);
  form->addRow(tr("Rule refresh seconds"), refreshIntervalSpin);

  auto *insecureCheck = new QCheckBox(&dialog);
  insecureCheck->setChecked(insecure_);
  insecureCheck->setEnabled(!running);
  form->addRow(tr("Skip TLS certificate verification (insecure)"),
              insecureCheck);

  auto *closeBehaviorCombo = new QComboBox(&dialog);
  closeBehaviorCombo->addItem(tr("Ask every time"), "ask");
  closeBehaviorCombo->addItem(tr("Minimize to tray"), "tray");
  closeBehaviorCombo->addItem(tr("Exit application"), "exit");
  const int currentIndex = closeBehaviorCombo->findData(closeBehavior_);
  if (currentIndex >= 0) {
    closeBehaviorCombo->setCurrentIndex(currentIndex);
  }
  form->addRow(tr("When closing window"), closeBehaviorCombo);

#ifdef Q_OS_WIN
  auto *envProxyNoticeCheck = new QCheckBox(&dialog);
  envProxyNoticeCheck->setChecked(!suppressEnvProxyNotice_);
  form->addRow(tr("Show system proxy restart reminder"), envProxyNoticeCheck);

  auto *wslRestartNoticeCheck = new QCheckBox(&dialog);
  wslRestartNoticeCheck->setChecked(!suppressWslRestartNotice_);
  form->addRow(tr("Show WSL restart reminder"), wslRestartNoticeCheck);

  auto *wslMirroredPromptCheck = new QCheckBox(&dialog);
  wslMirroredPromptCheck->setChecked(!suppressWslMirroredPrompt_);
  form->addRow(tr("Prompt to enable WSL mirrored networking on startup"),
              wslMirroredPromptCheck);
#endif

  layout->addLayout(form);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() == QDialog::Accepted) {
    bufferSize_ = bufferSizeSpin->value();
    refreshIntervalSeconds_ = refreshIntervalSpin->value();
    insecure_ = insecureCheck->isChecked();
    closeBehavior_ = closeBehaviorCombo->currentData().toString();
    sessionCloseBehavior_.clear();
#ifdef Q_OS_WIN
    suppressEnvProxyNotice_ = !envProxyNoticeCheck->isChecked();
    suppressWslRestartNotice_ = !wslRestartNoticeCheck->isChecked();
    suppressWslMirroredPrompt_ = !wslMirroredPromptCheck->isChecked();
#endif
    saveUserSettings();
  }
}

void MainWindow::updateConfigurationInputs(bool running) {
  const bool editable = !running;
  listenEdit_->setEnabled(editable);
  socksListenEdit_->setEnabled(editable);
  gatewayEdit_->setEnabled(editable);
  usernameEdit_->setEnabled(editable);
  passwordEdit_->setEnabled(editable);
  passwordVisibilityButton_->setEnabled(editable);
  customRulesEdit_->setEnabled(editable);
  customRulesBrowseButton_->setEnabled(editable);
}

void MainWindow::showAboutDialog() {
  QMessageBox::about(
      this, tr("About ws2tcp-local"),
      tr("<h3>ws2tcp-local</h3>"
         "<p><b>Version:</b> %1</p>"
         "<p><b>Build time:</b> %2</p>"
         "<p>A Qt GUI for the ws2tcp-local WebSocket-to-TCP proxy.</p>"
         "<p><b>Repository:</b> "
         "<a href=\"https://github.com/uglykitty/ws2tcp-local-qt\">"
         "github.com/uglykitty/ws2tcp-local-qt</a><br>"
         "<b>Author:</b> Guofang Wang<br>"
         "<b>Email:</b> "
         "<a href=\"mailto:lazysoez@gmail.com\">"
         "lazysoez@gmail.com</a></p>")
          .arg(QCoreApplication::applicationVersion().toHtmlEscaped(),
               QStringLiteral(__DATE__ " " __TIME__).toHtmlEscaped()));
}

void MainWindow::checkForUpdates() {
  auto *manager = new QNetworkAccessManager(this);
  QNetworkRequest request{QUrl(kUpdateManifestUrl)};
  request.setHeader(QNetworkRequest::UserAgentHeader,
                     QStringLiteral("ws2tcp-local-qt/%1")
                         .arg(QCoreApplication::applicationVersion()));
  QNetworkReply *reply = manager->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
    reply->deleteLater();
    manager->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
      QMessageBox::warning(
          this, tr("Check for Updates"),
          tr("Failed to check for updates: %1").arg(reply->errorString()));
      return;
    }

    const QJsonObject manifest =
        QJsonDocument::fromJson(reply->readAll()).object();
    const QString remoteVersion =
        manifest.value(QStringLiteral("version")).toString();
    if (remoteVersion.isEmpty()) {
      QMessageBox::warning(this, tr("Check for Updates"),
                            tr("Update server returned an unexpected response."));
      return;
    }

    const QString currentVersion = QCoreApplication::applicationVersion();
    if (!isVersionNewer(remoteVersion, currentVersion)) {
      QMessageBox::information(
          this, tr("Check for Updates"),
          tr("You are using the latest version (%1).").arg(currentVersion));
      return;
    }

    QString downloadUrl;
#if defined(Q_OS_WIN)
    downloadUrl = manifest.value(QStringLiteral("windows_url")).toString();
#elif defined(Q_OS_MACOS)
    downloadUrl = manifest.value(QStringLiteral("macos_url")).toString();
#endif
    const QString notesUrl =
        manifest.value(QStringLiteral("notes_url")).toString();

    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(tr("Update Available"));
    box.setText(tr("A new version %1 is available (you have %2).")
                    .arg(remoteVersion, currentVersion));
    QPushButton *downloadButton =
        downloadUrl.isEmpty()
            ? nullptr
            : box.addButton(tr("Download"), QMessageBox::AcceptRole);
    QPushButton *notesButton =
        notesUrl.isEmpty()
            ? nullptr
            : box.addButton(tr("Release Notes"), QMessageBox::HelpRole);
    box.addButton(QMessageBox::Close);
    box.exec();

    if (downloadButton && box.clickedButton() == downloadButton) {
      UpdateDownloadDialog downloadDialog(downloadUrl, remoteVersion, this);
      downloadDialog.exec();
      if (downloadDialog.installerLaunched()) {
        quitGracefully(false);
      }
    } else if (notesButton && box.clickedButton() == notesButton) {
      QDesktopServices::openUrl(QUrl(notesUrl));
    }
  });
}

void MainWindow::toggleWindowVisibility() {
  if (isVisible()) {
    hide();
  } else {
    showAndActivate();
  }
  updateTrayActions();
}

void MainWindow::showAndActivate() {
  showNormal();
  raise();
  activateWindow();
  updateTrayActions();
}

void MainWindow::quitFromTray() {
  quitGracefully();
}

void MainWindow::quitGracefully(bool confirmIfWslBusy) {
#ifdef Q_OS_WIN
  if (confirmIfWslBusy && !confirmQuitDuringWslOperation()) {
    return;
  }
#endif
  allowClose_ = true;
  QApplication::quit();
}

void MainWindow::handleTrayActivation(
    QSystemTrayIcon::ActivationReason reason) {
  if (reason == QSystemTrayIcon::Trigger ||
      reason == QSystemTrayIcon::DoubleClick) {
    toggleWindowVisibility();
  }
}

#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
void MainWindow::setSystemProxyEnabled(bool enabled) {
  if (enabled &&
      (handle_ == nullptr ||
       ws2tcp_status(handle_) != WS2TCP_STATUS_RUNNING)) {
    systemProxyActive_ = false;
    updateTrayActions();
    return;
  }

  QString error;
  const bool ok = enabled ? SystemProxy::enable(listenEdit_->text(), &error)
                          : SystemProxy::disable(&error);
  if (!ok) {
    if (enabled) {
      systemProxyActive_ = false;
    }
    const QSignalBlocker blocker(systemProxyCheck_);
    systemProxyCheck_->setChecked(systemProxyActive_);
    logMessage(tr("System proxy error: %1").arg(error));
    showError(tr("Failed to update system proxy: %1").arg(error));
  } else {
    systemProxyActive_ = enabled;
    logMessage(enabled ? tr("System proxy enabled")
                       : tr("System proxy disabled"));
#ifdef Q_OS_WIN
    if (enabled) {
      showEnvProxyRestartNotice();
    }
#endif
  }
  updateTrayActions();
}
#endif

void MainWindow::closeEvent(QCloseEvent *event) {
  if (allowClose_) {
    event->accept();
    return;
  }

  QString behavior = closeBehavior_;
  if (behavior == "ask" && !sessionCloseBehavior_.isEmpty()) {
    behavior = sessionCloseBehavior_;
  }
  if (behavior == "ask") {
    QMessageBox messageBox(this);
    messageBox.setWindowTitle(tr("Close ws2tcp-local"));
    messageBox.setText(tr("What should happen when the window is closed?"));
    auto *minimizeButton = messageBox.addButton(
        tr("Minimize to Tray"), QMessageBox::AcceptRole);
    auto *exitButton = messageBox.addButton(
        tr("Exit"), QMessageBox::DestructiveRole);
    messageBox.addButton(QMessageBox::Cancel);
    messageBox.setDefaultButton(exitButton);
    auto *rememberCheck = new QCheckBox(tr("Remember my choice"), &messageBox);
    messageBox.setCheckBox(rememberCheck);
    messageBox.exec();
    const bool rememberChoice = rememberCheck->isChecked();

    if (messageBox.clickedButton() == minimizeButton) {
      behavior = "tray";
    } else if (messageBox.clickedButton() == exitButton) {
      behavior = "exit";
    } else {
      event->ignore();
      return;
    }

    sessionCloseBehavior_ = behavior;
    if (rememberChoice) {
      closeBehavior_ = behavior;
      QSettings settings;
      settings.setValue("ui/close_behavior", behavior);
      settings.sync();
    }
  }

  if (behavior == "exit" || trayIcon_ == nullptr || !trayIcon_->isVisible()) {
#ifdef Q_OS_WIN
    if (!confirmQuitDuringWslOperation()) {
      // Declining leaves the window open with nothing actually decided --
      // don't let this count as the session's remembered "exit" choice, or
      // the next close attempt would skip straight back to this WSL prompt
      // instead of asking again.
      sessionCloseBehavior_.clear();
      event->ignore();
      return;
    }
#endif
    allowClose_ = true;
    event->accept();
    QApplication::quit();
    return;
  }

  hide();
  updateTrayActions();
  event->ignore();
}

QByteArray MainWindow::buildConfigJson() const {
  QJsonObject config;
  config["listen"] = listenEdit_->text().trimmed();
  const QString socksListen = socksListenEdit_->text().trimmed();
  if (!socksListen.isEmpty()) {
    config["socks_listen"] = socksListen;
  }
  config["gateway"] = gatewayEdit_->text().trimmed();
  config["buffer_size"] = bufferSize_;
  config["rule_refresh_interval_secs"] = refreshIntervalSeconds_;
  config["proxy_mode"] = proxyModeCombo_->currentText();
  config["insecure"] = insecure_;
  QJsonObject headers;
  headers["User-Agent"] = QStringLiteral("ws2tcp-local-qt/%1")
                              .arg(QCoreApplication::applicationVersion());
  config["headers"] = headers;

  const QString username = usernameEdit_->text().trimmed();
  if (!username.isEmpty()) {
    config["basic_auth"] = username + ":" + passwordEdit_->text();
  }
  if (!customRulesEdit_->text().isEmpty()) {
    config["custom_domain_rules"] = customRulesEdit_->text().trimmed();
  }

  return QJsonDocument(config).toJson(QJsonDocument::Compact);
}

void MainWindow::setupTrayIcon() {
  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    logMessage(tr("System tray is not available"));
    return;
  }

  trayMenu_ = new QMenu(this);
  showHideAction_ = trayMenu_->addAction(tr("Hide window"));
  trayMenu_->addAction(startAction_);
  trayMenu_->addAction(stopAction_);
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  traySystemProxyAction_ = trayMenu_->addAction(tr("Set system proxy"));
  traySystemProxyAction_->setCheckable(true);
#endif
  trayMenu_->addSeparator();
  quitAction_ = trayMenu_->addAction(tr("Quit"));

  trayIcon_ = new QSystemTrayIcon(applicationIcon(), this);
  trayIcon_->setToolTip("ws2tcp-local");
  trayIcon_->setContextMenu(trayMenu_);

  connect(showHideAction_, &QAction::triggered, this,
          &MainWindow::toggleWindowVisibility);
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  connect(traySystemProxyAction_, &QAction::toggled, systemProxyCheck_,
          &QCheckBox::setChecked);
#endif
  connect(quitAction_, &QAction::triggered, this, &MainWindow::quitFromTray);
  connect(trayIcon_, &QSystemTrayIcon::activated, this,
          &MainWindow::handleTrayActivation);

  updateTrayActions();
  trayIcon_->show();
}

void MainWindow::updateTrayActions() {
  if (showHideAction_ != nullptr) {
    showHideAction_->setText(isVisible() ? tr("Hide window")
                                         : tr("Show window"));
  }

  if (handle_ == nullptr) {
    startAction_->setEnabled(false);
    stopAction_->setEnabled(false);
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
    if (traySystemProxyAction_ != nullptr) {
      const QSignalBlocker blocker(traySystemProxyAction_);
      traySystemProxyAction_->setChecked(false);
      traySystemProxyAction_->setEnabled(false);
    }
#endif
    return;
  }

  const bool running =
      ws2tcp_status(handle_) == WS2TCP_STATUS_RUNNING;
  startAction_->setEnabled(!running);
  stopAction_->setEnabled(running);
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  if (traySystemProxyAction_ != nullptr) {
    const QSignalBlocker blocker(traySystemProxyAction_);
    traySystemProxyAction_->setChecked(systemProxyActive_);
    traySystemProxyAction_->setEnabled(running);
  }
#endif
}

void MainWindow::loadUserSettings() {
  QSettings settings;

  listenEdit_->setText(
      settings.value("proxy/listen", listenEdit_->text()).toString());
  socksListenEdit_->setText(
      settings.value("proxy/socks_listen", socksListenEdit_->text())
          .toString());
  gatewayEdit_->setText(
      settings.value("proxy/gateway", gatewayEdit_->text()).toString());
  if (settings.contains("proxy/username") ||
      settings.contains("proxy/password")) {
    usernameEdit_->setText(settings.value("proxy/username").toString());
    passwordEdit_->setText(settings.value("proxy/password").toString());
  } else {
    // Migrate the former combined `username:password` setting. Split only on
    // the first colon because colons are valid password characters.
    const QString basicAuth = settings.value("proxy/basic_auth").toString();
    const qsizetype separator = basicAuth.indexOf(':');
    if (separator >= 0) {
      usernameEdit_->setText(basicAuth.left(separator));
      passwordEdit_->setText(basicAuth.mid(separator + 1));
    } else {
      usernameEdit_->setText(basicAuth);
    }
  }
  customRulesEdit_->setText(settings.value("proxy/custom_rules").toString());
  const int bufferSize =
      settings.value("proxy/buffer_size", bufferSize_).toInt();
  if (bufferSize >= 1 && bufferSize <= 1024 * 1024) {
    bufferSize_ = bufferSize;
  }
  const int refreshInterval =
      settings
          .value("proxy/rule_refresh_interval_secs", refreshIntervalSeconds_)
          .toInt();
  if (refreshInterval >= 1 && refreshInterval <= 24 * 60 * 60) {
    refreshIntervalSeconds_ = refreshInterval;
  }

  const QString proxyMode =
      settings.value("proxy/proxy_mode", proxyModeCombo_->currentText())
          .toString();
  const int proxyModeIndex = proxyModeCombo_->findText(proxyMode);
  if (proxyModeIndex >= 0) {
    proxyModeCombo_->setCurrentIndex(proxyModeIndex);
  }

  const QString closeBehavior = settings.value("ui/close_behavior", "ask")
                                    .toString();
  if (closeBehavior == "ask" || closeBehavior == "tray" ||
      closeBehavior == "exit") {
    closeBehavior_ = closeBehavior;
  }

  const QString language = settings.value("ui/language", "en_US").toString();
  if (language == "en_US" || language == "zh_CN") {
    language_ = language;
  }

  insecure_ = settings.value("proxy/insecure", insecure_).toBool();
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  systemProxyCheck_->setChecked(
      settings.value("proxy/set_system_proxy", false).toBool());
#endif
#ifdef Q_OS_WIN
  suppressEnvProxyNotice_ =
      settings.value("ui/suppress_env_proxy_notice", false).toBool();
  suppressWslRestartNotice_ =
      settings.value("ui/suppress_wsl_restart_notice", false).toBool();
  suppressWslMirroredPrompt_ =
      settings.value("ui/suppress_wsl_mirrored_prompt", false).toBool();
#endif
}

void MainWindow::saveUserSettings() const {
  QSettings settings;

  settings.setValue("proxy/listen", listenEdit_->text());
  settings.setValue("proxy/socks_listen", socksListenEdit_->text());
  settings.setValue("proxy/gateway", gatewayEdit_->text());
  settings.setValue("proxy/username", usernameEdit_->text());
  settings.setValue("proxy/password", passwordEdit_->text());
  settings.remove("proxy/basic_auth");
  settings.setValue("proxy/custom_rules", customRulesEdit_->text());
  settings.setValue("proxy/buffer_size", bufferSize_);
  settings.setValue("proxy/rule_refresh_interval_secs",
                    refreshIntervalSeconds_);
  settings.setValue("proxy/proxy_mode", proxyModeCombo_->currentText());
  settings.setValue("ui/close_behavior", closeBehavior_);
  settings.setValue("ui/language", language_);
  settings.setValue("proxy/insecure", insecure_);
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  settings.setValue("proxy/set_system_proxy",
                    systemProxyCheck_->isChecked());
#endif
#ifdef Q_OS_WIN
  settings.setValue("ui/suppress_env_proxy_notice", suppressEnvProxyNotice_);
  settings.setValue("ui/suppress_wsl_restart_notice",
                    suppressWslRestartNotice_);
  settings.setValue("ui/suppress_wsl_mirrored_prompt",
                    suppressWslMirroredPrompt_);
#endif
}

void MainWindow::appendError(const QString &prefix) {
  const char *error = ws2tcp_last_error(handle_);
  const QString message = prefix + ": " + QString::fromUtf8(error);
  logMessage(message);
  showError(message);
}

void MainWindow::showError(const QString &message) {
  updateRuntimeStatus(message);
  QMessageBox::warning(this, tr("ws2tcp-local"), message);
}

void MainWindow::showInfo(const QString &message) {
  QMessageBox::information(this, tr("ws2tcp-local"), message);
}

void MainWindow::updateRuntimeStatus(const QString &message) {
  runtimeStatus_ = message;
  statusBar()->showMessage(runtimeStatus_);
}

#ifdef Q_OS_WIN
void MainWindow::showRestartNotice(const QString &message,
                                   const QString &settingsKey,
                                   bool *suppressed) {
  if (*suppressed) {
    return;
  }

  QMessageBox messageBox(this);
  messageBox.setIcon(QMessageBox::Information);
  messageBox.setWindowTitle(tr("ws2tcp-local"));
  messageBox.setText(message);
  messageBox.addButton(QMessageBox::Ok);
  auto *dontShowAgainCheck =
      new QCheckBox(tr("Don't show this again"), &messageBox);
  messageBox.setCheckBox(dontShowAgainCheck);
  messageBox.exec();

  if (dontShowAgainCheck->isChecked()) {
    *suppressed = true;
    QSettings settings;
    settings.setValue(settingsKey, true);
    settings.sync();
  }
}

void MainWindow::showEnvProxyRestartNotice() {
  showRestartNotice(
      tr("System proxy enabled. The HTTP_PROXY, HTTPS_PROXY and ALL_PROXY "
         "user environment variables have also been set.\n\n"
         "Already-open terminals and applications won't see them until you "
         "restart the terminal (or the app)."),
      "ui/suppress_env_proxy_notice", &suppressEnvProxyNotice_);
}

void MainWindow::enableWslMirroredNetworking() {
  if (!isWslUsable()) {
    showWslNotReadyMessage();
    return;
  }
  applyMirroredNetworking();
}

bool MainWindow::isWslUsable() {
  // wsl.exe's own text output is unreliable to parse (UTF-16LE with no
  // console attached, and inconsistent across Windows versions/states --
  // see the WSL menu's commit history), but --status is a fast, cheap
  // query (unlike actually trying to launch a distro) whose exit code is
  // a trustworthy signal on its own: 0 once WSL is installed and has a
  // usable default distro, non-zero otherwise.
  QProcess process;
  process.start(QStringLiteral("wsl.exe"), {QStringLiteral("--status")});
  if (!process.waitForFinished(5000)) {
    process.kill();
    process.waitForFinished();
    return false;
  }
  return process.exitStatus() == QProcess::NormalExit &&
         process.exitCode() == 0;
}

bool MainWindow::confirmQuitDuringWslOperation() {
  // The WSL menu action is disabled for the duration of every WSL
  // operation (see runWslCommand/installWsl), so that's a reliable single
  // signal for "something is still running" without tracking it twice.
  if (wslMenu_ == nullptr || wslMenu_->menuAction()->isEnabled()) {
    return true;
  }

  QMessageBox messageBox(this);
  messageBox.setIcon(QMessageBox::Warning);
  messageBox.setWindowTitle(tr("ws2tcp-local"));
  messageBox.setText(
      tr("A WSL operation is still running. Quitting now will interrupt "
         "it and may leave it partially installed.\n\n"
         "Quit anyway?"));
  auto *yesButton = messageBox.addButton(QMessageBox::Yes);
  auto *noButton = messageBox.addButton(QMessageBox::No);
  messageBox.setDefaultButton(noButton);
  messageBox.exec();
  return messageBox.clickedButton() == yesButton;
}

void MainWindow::showWslNotReadyMessage() {
  showError(tr("WSL is not installed or not ready. Use \"Install WSL\" in "
               "the WSL menu first."));
}

void MainWindow::applyMirroredNetworking() {
  QString error;
  if (!WslConfig::enableMirroredNetworking(&error)) {
    showError(tr("Failed to update .wslconfig: %1").arg(error));
    return;
  }

  showRestartNotice(
      tr("WSL networking mode set to mirrored in .wslconfig.\n\n"
         "Restart WSL (run \"wsl --shutdown\" in a terminal) for the "
         "change to take effect."),
      "ui/suppress_wsl_restart_notice", &suppressWslRestartNotice_);
}

void MainWindow::maybePromptWslMirroredNetworking() {
  if (suppressWslMirroredPrompt_) {
    return;
  }
  if (!isWslUsable()) {
    return;
  }
  if (WslConfig::isMirroredNetworkingEnabled()) {
    return;
  }

  QMessageBox messageBox(this);
  messageBox.setIcon(QMessageBox::Question);
  messageBox.setWindowTitle(tr("ws2tcp-local"));
  messageBox.setText(
      tr("WSL is not using mirrored networking mode. Switch to mirrored "
         "mode now?\n\n"
         "This updates .wslconfig; WSL will need a restart (\"wsl "
         "--shutdown\") to apply it."));
  auto *yesButton = messageBox.addButton(QMessageBox::Yes);
  messageBox.addButton(QMessageBox::No);
  messageBox.setDefaultButton(QMessageBox::No);
  auto *dontAskAgainCheck = new QCheckBox(tr("Don't ask again"), &messageBox);
  messageBox.setCheckBox(dontAskAgainCheck);
  messageBox.exec();

  if (dontAskAgainCheck->isChecked()) {
    suppressWslMirroredPrompt_ = true;
    QSettings settings;
    settings.setValue("ui/suppress_wsl_mirrored_prompt", true);
    settings.sync();
  }

  if (messageBox.clickedButton() != yesButton) {
    return;
  }

  applyMirroredNetworking();
}

void MainWindow::runWslCommand(const QString &label,
                               const QStringList &arguments,
                               const QByteArray &stdinData,
                               std::function<void(bool)> onFinished) {
  if (wslProcess_ != nullptr) {
    const QString message =
        tr("%1: another WSL command is already running.").arg(label);
    logMessage(message);
    showError(message);
    return;
  }

  logMessage(tr("%1: starting...").arg(label));
  wslMenu_->menuAction()->setEnabled(false);

  auto *process = new QProcess(this);
  wslProcess_ = process;
  process->setProgram(QStringLiteral("wsl.exe"));
  process->setArguments(arguments);
  process->setProcessChannelMode(QProcess::MergedChannels);

  connect(process, &QProcess::readyReadStandardOutput, this,
          [this, process]() {
            const QString output =
                decodeWslOutput(process->readAllStandardOutput());
            for (const QString &line :
                 output.split('\n', Qt::SkipEmptyParts)) {
              logMessage(line.trimmed());
            }
          });
  connect(process,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, process, label,
           onFinished](int exitCode, QProcess::ExitStatus status) {
            const bool success =
                status == QProcess::NormalExit && exitCode == 0;
            if (success) {
              logMessage(tr("%1: done.").arg(label));
              showInfo(tr("%1: done.").arg(label));
            } else {
              const QString message =
                  tr("%1: failed (exit code %2).").arg(label).arg(exitCode);
              logMessage(message);
              showError(message);
            }
            wslMenu_->menuAction()->setEnabled(true);
            wslProcess_ = nullptr;
            process->deleteLater();
            if (onFinished) {
              onFinished(success);
            }
          });
  connect(process, &QProcess::errorOccurred, this,
          [this, process, label, onFinished](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart) {
              // 'finished' above reports a crash or non-zero exit.
              return;
            }
            const QString message =
                tr("%1: failed to start (%2).")
                    .arg(label, process->errorString());
            logMessage(message);
            showError(message);
            wslMenu_->menuAction()->setEnabled(true);
            wslProcess_ = nullptr;
            process->deleteLater();
            if (onFinished) {
              onFinished(false);
            }
          });

  process->start();
  if (!stdinData.isEmpty()) {
    process->write(stdinData);
  }
  process->closeWriteChannel();
}

void MainWindow::runWslScript(const QString &label,
                              const QString &resourcePath,
                              const QStringList &scriptArgs,
                              std::function<void(bool)> onFinished) {
  const QByteArray script = readBundledScript(resourcePath);
  if (script.isEmpty()) {
    const QString message = tr("%1: bundled script %2 is missing or empty.")
                                 .arg(label, resourcePath);
    logMessage(message);
    showError(message);
    return;
  }

  const QString remotePath = QStringLiteral("/tmp/ws2tcp-local-%1")
                                 .arg(QFileInfo(resourcePath).fileName());
  // No double quotes anywhere in this wrapper -- wsl.exe mis-reconstructs
  // argv elements that contain embedded double quotes (see
  // readBundledScript's comment), so the actual script, which does need
  // them, travels over stdin instead of as part of this command line. The
  // brace group captures the script's real exit code so cleanup (which
  // always "succeeds") doesn't mask a failure.
  const QString wrapper =
      QStringLiteral("cat > %1 && { bash %1 %2; ec=$?; rm -f %1; exit $ec; "
                    "} || exit 1")
          .arg(remotePath, scriptArgs.join(QLatin1Char(' ')));
  runWslCommand(label,
               {QStringLiteral("bash"), QStringLiteral("-c"), wrapper},
               script, onFinished);
}

void MainWindow::installWsl() {
  // wsl --install needs to enable Windows features and can install the WSL
  // platform package, which requires elevation; run unelevated (as this
  // app normally is), it was observed to bail out immediately with a
  // generic "not installed" message instead of actually attempting that.
  // ShellExecuteEx's "runas" verb elevates it, at the cost of not being
  // able to stream its output back into our own (non-elevated) log panel
  // the way runWslCommand does -- so it gets its own visible console
  // instead, and we just poll for its exit code.
  if (wslProcess_ != nullptr) {
    const QString message =
        tr("Install WSL: another WSL command is already running.");
    logMessage(message);
    showError(message);
    return;
  }

  if (isWslUsable()) {
    const QString message = tr("Install WSL: already installed and ready.");
    logMessage(message);
    showInfo(message);
    return;
  }

  logMessage(tr("Install WSL: requesting administrator privileges..."));
  wslMenu_->menuAction()->setEnabled(false);

  SHELLEXECUTEINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS;
  info.lpVerb = L"runas";
  info.lpFile = L"wsl.exe";
  info.lpParameters = L"--install";
  info.nShow = SW_SHOWNORMAL;

  if (!ShellExecuteExW(&info)) {
    const DWORD error = GetLastError();
    wslMenu_->menuAction()->setEnabled(true);
    QString message;
    if (error == ERROR_CANCELLED) {
      message = tr("Install WSL: cancelled (UAC prompt was declined).");
    } else {
      message = tr("Install WSL: failed to start elevated (Windows error "
                   "%1).")
                    .arg(error);
    }
    logMessage(message);
    showError(message);
    return;
  }

  const HANDLE processHandle = info.hProcess;
  auto *watcher = new QTimer(this);
  watcher->setInterval(500);
  connect(watcher, &QTimer::timeout, this, [this, processHandle, watcher]() {
    DWORD exitCode = STILL_ACTIVE;
    if (!GetExitCodeProcess(processHandle, &exitCode) ||
        exitCode == STILL_ACTIVE) {
      return;
    }
    watcher->stop();
    watcher->deleteLater();
    CloseHandle(processHandle);
    wslMenu_->menuAction()->setEnabled(true);
    if (exitCode == 0) {
      logMessage(tr("Install WSL: done."));
      showInfo(tr("Install WSL: done."));
      applyMirroredNetworking();
    } else {
      const QString message =
          tr("Install WSL: failed (exit code %1). Check the console "
             "window it opened for details.")
              .arg(exitCode);
      logMessage(message);
      showError(message);
    }
  });
  watcher->start();
}

void MainWindow::installNodeViaNvm() {
  if (!isWslUsable()) {
    showWslNotReadyMessage();
    return;
  }
  runWslScript(tr("Install Node.js (nvm)"),
              QStringLiteral(":/scripts/install-node.sh"), {});
}

void MainWindow::installOpenCodeCli() {
  if (!isWslUsable()) {
    showWslNotReadyMessage();
    return;
  }
  runWslScript(tr("Install opencode CLI"),
              QStringLiteral(":/scripts/install-opencode.sh"), {});
}

void MainWindow::installCodexCli() {
  if (!isWslUsable()) {
    showWslNotReadyMessage();
    return;
  }
  runWslScript(tr("Install Codex CLI"),
              QStringLiteral(":/scripts/install-codex.sh"), {});
}

void MainWindow::installClaudeCodeCli() {
  if (!isWslUsable()) {
    showWslNotReadyMessage();
    return;
  }
  runWslScript(tr("Install Claude Code CLI"),
              QStringLiteral(":/scripts/install-claude.sh"), {});
}
#endif

void MainWindow::updateRuntimeStatusFromLog(const QString &message) {
  const int listenIndex = message.indexOf("listen=");
  if (listenIndex >= 0) {
    const QString rest = message.mid(listenIndex + 7);
    const QString listen = rest.section(' ', 0, 0);
    if (!listen.isEmpty()) {
      updateRuntimeStatus(tr("Running on %1").arg(listen));
      return;
    }
  }

  if (message.contains("starting proxy listen=")) {
    const int start = message.indexOf("starting proxy listen=");
    const QString rest = message.mid(start + 22);
    const QString listen = rest.section(' ', 0, 0);
    if (!listen.isEmpty()) {
      updateRuntimeStatus(tr("Starting %1").arg(listen));
    }
  } else if (message.contains("proxy task stopped")) {
    updateRuntimeStatus(tr("Stopped"));
  } else if (message.contains("proxy task failed")) {
    updateRuntimeStatus(tr("Stopped with error"));
  }
}

void MainWindow::handleRustLog(const char *message, void *userData) {
  auto *window = static_cast<MainWindow *>(userData);
  if (window == nullptr || message == nullptr) {
    return;
  }

  const QString copied = QString::fromUtf8(message);
  QMetaObject::invokeMethod(window, "appendLog", Qt::QueuedConnection,
                            Q_ARG(QString, copied));
}
