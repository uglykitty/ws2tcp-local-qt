#include "MainWindow.h"

#include <QByteArray>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStyle>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#ifdef Q_OS_WIN
#include <QProcess>
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
  return file.readAll();
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

  startAction_ = new QAction(
      style()->standardIcon(QStyle::SP_MediaPlay), tr("&Start"), this);
  startAction_->setToolTip(tr("Start the proxy"));
  stopAction_ = new QAction(
      style()->standardIcon(QStyle::SP_MediaStop), tr("S&top"), this);
  stopAction_->setToolTip(tr("Stop the proxy"));
  settingsAction_ = new QAction(settingsIcon(palette()), tr("&Settings"), this);
  settingsAction_->setToolTip(tr("Open settings"));

  auto *proxyToolBar = addToolBar(tr("Proxy"));
  proxyToolBar->setObjectName("proxyToolBar");
  proxyToolBar->addAction(startAction_);
  proxyToolBar->addAction(stopAction_);
  proxyToolBar->addSeparator();
  proxyToolBar->addAction(settingsAction_);

  auto *proxyMenu = menuBar()->addMenu(tr("&Proxy"));
  proxyMenu->addAction(startAction_);
  proxyMenu->addAction(stopAction_);
  proxyMenu->addSeparator();
  auto *exitAction = proxyMenu->addAction(tr("E&xit"));
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
  connect(installWslAction, &QAction::triggered, this,
          &MainWindow::installWsl);

  auto *wslMirroredAction =
      wslMenu_->addAction(tr("Set WSL Networking to Mirrored"));
  connect(wslMirroredAction, &QAction::triggered, this,
          &MainWindow::enableWslMirroredNetworking);

  wslMenu_->addSeparator();

  auto *installNodeAction =
      wslMenu_->addAction(tr("Install Node.js (via nvm) in WSL"));
  connect(installNodeAction, &QAction::triggered, this,
          &MainWindow::installNodeViaNvm);

  auto *installOpenCodeAction =
      wslMenu_->addAction(tr("Install opencode CLI in WSL"));
  connect(installOpenCodeAction, &QAction::triggered, this,
          &MainWindow::installOpenCodeCli);

  auto *installCodexAction =
      wslMenu_->addAction(tr("Install Codex CLI in WSL"));
  connect(installCodexAction, &QAction::triggered, this,
          &MainWindow::installCodexCli);

  auto *installClaudeAction =
      wslMenu_->addAction(tr("Install Claude Code CLI in WSL"));
  connect(installClaudeAction, &QAction::triggered, this,
          &MainWindow::installClaudeCodeCli);
#endif

  auto *helpMenu = menuBar()->addMenu(tr("&Help"));
  auto *aboutAction = helpMenu->addAction(tr("&About ws2tcp-local"));
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
  startAction_->setEnabled(!running);
  stopAction_->setEnabled(running);
  updateConfigurationInputs(running);
  updateTrayActions();

  if (wasRunning_ && !running) {
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
    setSystemProxyEnabled(false);
#endif
    const QString error =
        handle_ != nullptr ? QString::fromUtf8(ws2tcp_last_error(handle_))
                           : QString();
    if (!error.isEmpty()) {
      logMessage(tr("Proxy stopped with error: %1").arg(error));
      updateRuntimeStatus(tr("Stopped with error"));
    } else {
      updateRuntimeStatus(tr("Stopped"));
    }
  }
  wasRunning_ = running;
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

void MainWindow::quitGracefully() {
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
  applyMirroredNetworking();
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
  if (QStandardPaths::findExecutable("wsl.exe").isEmpty()) {
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
    logMessage(tr("%1: another WSL command is already running.").arg(label));
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
                QString::fromLocal8Bit(process->readAllStandardOutput());
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
            } else {
              logMessage(
                  tr("%1: failed (exit code %2).").arg(label).arg(exitCode));
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
            logMessage(tr("%1: failed to start (%2).")
                          .arg(label, process->errorString()));
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
    logMessage(tr("%1: bundled script %2 is missing or empty.")
                  .arg(label, resourcePath));
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
  runWslCommand(tr("Install WSL"), {QStringLiteral("--install")},
               QByteArray(), [this](bool success) {
                 if (!success) {
                   return;
                 }
                 applyMirroredNetworking();
               });
}

void MainWindow::installNodeViaNvm() {
  runWslScript(tr("Install Node.js (nvm)"),
              QStringLiteral(":/scripts/install-node.sh"), {});
}

void MainWindow::installOpenCodeCli() {
  runWslScript(tr("Install opencode CLI"),
              QStringLiteral(":/scripts/install-opencode.sh"), {});
}

void MainWindow::installCodexCli() {
  runWslScript(tr("Install Codex CLI"),
              QStringLiteral(":/scripts/install-codex.sh"), {});
}

void MainWindow::installClaudeCodeCli() {
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
