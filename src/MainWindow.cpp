#include "MainWindow.h"

#include <QByteArray>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
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
#include <QStyle>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>

#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
#include "SystemProxy.h"
#endif

namespace {

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

  listenEdit_ = new QLineEdit("127.0.0.1:3128", this);
  gatewayEdit_ =
      new QLineEdit("wss://www.wangguofang.net/websocat", this);
  usernameEdit_ = new QLineEdit(this);
  passwordEdit_ = new QLineEdit(this);
  passwordEdit_->setEchoMode(QLineEdit::Password);
  passwordVisibilityButton_ = new QToolButton(this);
  passwordVisibilityButton_->setText("Show");
  passwordVisibilityButton_->setCheckable(true);
  passwordVisibilityButton_->setToolTip("Show password");

  auto *passwordRow = new QWidget(this);
  auto *passwordLayout = new QHBoxLayout(passwordRow);
  passwordLayout->setContentsMargins(0, 0, 0, 0);
  passwordLayout->addWidget(passwordEdit_);
  passwordLayout->addWidget(passwordVisibilityButton_);
  customRulesEdit_ = new QLineEdit(this);
  customRulesBrowseButton_ = new QToolButton(this);
  customRulesBrowseButton_->setText("Browse...");
  customRulesBrowseButton_->setToolTip("Select custom rules file");

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

  form->addRow("Listen", listenEdit_);
  form->addRow("Gateway", gatewayEdit_);
  form->addRow("Username", usernameEdit_);
  form->addRow("Password", passwordRow);
  form->addRow("Custom rules", customRulesRow);
  form->addRow("Proxy mode", proxyModeCombo_);
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  form->addRow("Set system proxy", systemProxyCheck_);
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
      style()->standardIcon(QStyle::SP_MediaPlay), "&Start", this);
  startAction_->setToolTip("Start the proxy");
  stopAction_ = new QAction(
      style()->standardIcon(QStyle::SP_MediaStop), "S&top", this);
  stopAction_->setToolTip("Stop the proxy");
  settingsAction_ = new QAction(settingsIcon(palette()), "&Settings", this);
  settingsAction_->setToolTip("Open settings");

  auto *proxyToolBar = addToolBar("Proxy");
  proxyToolBar->setObjectName("proxyToolBar");
  proxyToolBar->addAction(startAction_);
  proxyToolBar->addAction(stopAction_);
  proxyToolBar->addSeparator();
  proxyToolBar->addAction(settingsAction_);

  auto *proxyMenu = menuBar()->addMenu("&Proxy");
  proxyMenu->addAction(startAction_);
  proxyMenu->addAction(stopAction_);
  proxyMenu->addSeparator();
  auto *exitAction = proxyMenu->addAction("E&xit");
  exitAction->setShortcut(QKeySequence::Quit);
  connect(exitAction, &QAction::triggered, this, &MainWindow::quitFromTray);

  auto *helpMenu = menuBar()->addMenu("&Help");
  auto *aboutAction = helpMenu->addAction("&About ws2tcp-local");
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
            passwordVisibilityButton_->setText(visible ? "Hide" : "Show");
            passwordVisibilityButton_->setToolTip(visible ? "Hide password"
                                                          : "Show password");
          });
  connect(customRulesBrowseButton_, &QToolButton::clicked, this, [this]() {
    QString initialPath = customRulesEdit_->text().trimmed();
    if (!initialPath.isEmpty() && QFileInfo(initialPath).isFile()) {
      initialPath = QFileInfo(initialPath).absolutePath();
    }
    const QString filePath = QFileDialog::getOpenFileName(
        this, "Select custom rules file", initialPath, "All files (*)");
    if (!filePath.isEmpty()) {
      customRulesEdit_->setText(filePath);
    }
  });
  const int logRc =
      ws2tcp_set_log_callback(&MainWindow::handleRustLog, this,
                              "ws2tcp_local=info,ws2tcp_local_ffi=info");
  if (logRc != WS2TCP_OK) {
    logView_->appendPlainText("Failed to initialize Rust log callback");
  }

  statusTimer_ = new QTimer(this);
  statusTimer_->setInterval(1000);
  connect(statusTimer_, &QTimer::timeout, this, &MainWindow::refreshStatus);
  statusTimer_->start();

  if (handle_ == nullptr) {
    showError("Failed to create ws2tcp handle");
    startAction_->setEnabled(false);
    stopAction_->setEnabled(false);
  }

  loadUserSettings();
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
  saveUserSettings();

#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  SystemProxy::disable(nullptr);
#endif

  if (handle_ != nullptr) {
    ws2tcp_stop(handle_);
    ws2tcp_handle_free(handle_);
  }
}

void MainWindow::startProxy() {
  if (handle_ == nullptr) {
    showError("Failed to create ws2tcp handle");
    return;
  }
  if (gatewayEdit_->text().trimmed().isEmpty()) {
    showError("Gateway is required");
    return;
  }

  const QByteArray config = buildConfigJson();
  const int rc = ws2tcp_start(handle_, config.constData());
  if (rc == WS2TCP_OK) {
    saveUserSettings();
    updateConfigurationInputs(true);
    updateRuntimeStatus("Starting " + listenEdit_->text().trimmed());
    logView_->appendPlainText("Started");
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
    if (systemProxyCheck_->isChecked()) {
      setSystemProxyEnabled(true);
    }
#endif
  } else {
    appendError("Start failed");
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
    updateRuntimeStatus("Stopped");
    logView_->appendPlainText("Stopped");
  } else {
    appendError("Stop failed");
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
    logView_->appendPlainText("Proxy mode changed to " + mode);
  } else {
    appendError("Failed to change proxy mode");
  }
}

void MainWindow::refreshStatus() {
  if (handle_ == nullptr) {
    updateRuntimeStatus("Unavailable");
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
      logView_->appendPlainText("Proxy stopped with error: " + error);
      updateRuntimeStatus("Stopped with error");
    } else {
      updateRuntimeStatus("Stopped");
    }
  }
  wasRunning_ = running;
}

void MainWindow::appendLog(QString message) {
  logView_->appendPlainText(message);
  updateRuntimeStatusFromLog(message);
}

void MainWindow::showSettingsDialog() {
  QDialog dialog(this);
  dialog.setWindowTitle("Settings");

  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout();
  auto *bufferSizeSpin = new QSpinBox(&dialog);
  bufferSizeSpin->setRange(1, 1024 * 1024);
  bufferSizeSpin->setValue(bufferSize_);
  const bool running =
      handle_ != nullptr &&
      ws2tcp_status(handle_) == WS2TCP_STATUS_RUNNING;
  bufferSizeSpin->setEnabled(!running);
  form->addRow("Buffer size", bufferSizeSpin);

  auto *refreshIntervalSpin = new QSpinBox(&dialog);
  refreshIntervalSpin->setRange(1, 24 * 60 * 60);
  refreshIntervalSpin->setValue(refreshIntervalSeconds_);
  refreshIntervalSpin->setEnabled(!running);
  form->addRow("Rule refresh seconds", refreshIntervalSpin);

  auto *verifyCertificateCheck = new QCheckBox(&dialog);
  verifyCertificateCheck->setChecked(verifyCertificate_);
  verifyCertificateCheck->setEnabled(!running);
  form->addRow("Verify TLS certificate", verifyCertificateCheck);

  auto *closeBehaviorCombo = new QComboBox(&dialog);
  closeBehaviorCombo->addItem("Ask every time", "ask");
  closeBehaviorCombo->addItem("Minimize to tray", "tray");
  closeBehaviorCombo->addItem("Exit application", "exit");
  const int currentIndex = closeBehaviorCombo->findData(closeBehavior_);
  if (currentIndex >= 0) {
    closeBehaviorCombo->setCurrentIndex(currentIndex);
  }
  form->addRow("When closing window", closeBehaviorCombo);
  layout->addLayout(form);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() == QDialog::Accepted) {
    bufferSize_ = bufferSizeSpin->value();
    refreshIntervalSeconds_ = refreshIntervalSpin->value();
    verifyCertificate_ = verifyCertificateCheck->isChecked();
    closeBehavior_ = closeBehaviorCombo->currentData().toString();
    sessionCloseBehavior_.clear();
    saveUserSettings();
  }
}

void MainWindow::updateConfigurationInputs(bool running) {
  const bool editable = !running;
  listenEdit_->setEnabled(editable);
  gatewayEdit_->setEnabled(editable);
  usernameEdit_->setEnabled(editable);
  passwordEdit_->setEnabled(editable);
  passwordVisibilityButton_->setEnabled(editable);
  customRulesEdit_->setEnabled(editable);
  customRulesBrowseButton_->setEnabled(editable);
}

void MainWindow::showAboutDialog() {
  QMessageBox::about(
      this, "About ws2tcp-local",
      "<h3>ws2tcp-local</h3>"
      "<p>A Qt GUI for the ws2tcp-local WebSocket-to-TCP proxy.</p>"
      "<p><b>Repository:</b> "
      "<a href=\"https://github.com/uglykitty/ws2tcp-local-qt\">"
      "github.com/uglykitty/ws2tcp-local-qt</a><br>"
      "<b>Author:</b> Guofang Wang<br>"
      "<b>Email:</b> "
      "<a href=\"mailto:lazysoez@gmail.com\">"
      "lazysoez@gmail.com</a></p>");
}

void MainWindow::toggleWindowVisibility() {
  if (isVisible()) {
    hide();
  } else {
    showNormal();
    raise();
    activateWindow();
  }
  updateTrayActions();
}

void MainWindow::quitFromTray() {
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
    logView_->appendPlainText("System proxy error: " + error);
    showError("Failed to update system proxy: " + error);
  } else {
    systemProxyActive_ = enabled;
    logView_->appendPlainText(enabled ? "System proxy enabled"
                                      : "System proxy disabled");
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
    messageBox.setWindowTitle("Close ws2tcp-local");
    messageBox.setText("What should happen when the window is closed?");
    auto *minimizeButton = messageBox.addButton(
        "Minimize to Tray", QMessageBox::AcceptRole);
    auto *exitButton = messageBox.addButton(
        "Exit", QMessageBox::DestructiveRole);
    messageBox.addButton(QMessageBox::Cancel);
    auto *rememberCheck = new QCheckBox("Remember my choice", &messageBox);
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
  config["gateway"] = gatewayEdit_->text().trimmed();
  config["buffer_size"] = bufferSize_;
  config["rule_refresh_interval_secs"] = refreshIntervalSeconds_;
  config["proxy_mode"] = proxyModeCombo_->currentText();
  config["verify_server_certificate"] = verifyCertificate_;

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
    logView_->appendPlainText("System tray is not available");
    return;
  }

  trayMenu_ = new QMenu(this);
  showHideAction_ = trayMenu_->addAction("Hide window");
  trayMenu_->addAction(startAction_);
  trayMenu_->addAction(stopAction_);
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  traySystemProxyAction_ = trayMenu_->addAction("Set system proxy");
  traySystemProxyAction_->setCheckable(true);
#endif
  trayMenu_->addSeparator();
  quitAction_ = trayMenu_->addAction("Quit");

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
    showHideAction_->setText(isVisible() ? "Hide window" : "Show window");
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

  verifyCertificate_ =
      settings.value("proxy/verify_server_certificate", verifyCertificate_)
          .toBool();
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  systemProxyCheck_->setChecked(
      settings.value("proxy/set_system_proxy", false).toBool());
#endif
}

void MainWindow::saveUserSettings() const {
  QSettings settings;

  settings.setValue("proxy/listen", listenEdit_->text());
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
  settings.setValue("proxy/verify_server_certificate",
                    verifyCertificate_);
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  settings.setValue("proxy/set_system_proxy",
                    systemProxyCheck_->isChecked());
#endif
}

void MainWindow::appendError(const QString &prefix) {
  const char *error = ws2tcp_last_error(handle_);
  const QString message = prefix + ": " + QString::fromUtf8(error);
  logView_->appendPlainText(message);
  showError(message);
}

void MainWindow::showError(const QString &message) {
  updateRuntimeStatus(message);
  QMessageBox::warning(this, "ws2tcp-local", message);
}

void MainWindow::updateRuntimeStatus(const QString &message) {
  runtimeStatus_ = message;
  statusBar()->showMessage(runtimeStatus_);
}

void MainWindow::updateRuntimeStatusFromLog(const QString &message) {
  const int listenIndex = message.indexOf("listen=");
  if (listenIndex >= 0) {
    const QString rest = message.mid(listenIndex + 7);
    const QString listen = rest.section(' ', 0, 0);
    if (!listen.isEmpty()) {
      updateRuntimeStatus("Running on " + listen);
      return;
    }
  }

  if (message.contains("starting proxy listen=")) {
    const int start = message.indexOf("starting proxy listen=");
    const QString rest = message.mid(start + 22);
    const QString listen = rest.section(' ', 0, 0);
    if (!listen.isEmpty()) {
      updateRuntimeStatus("Starting " + listen);
    }
  } else if (message.contains("proxy task stopped")) {
    updateRuntimeStatus("Stopped");
  } else if (message.contains("proxy task failed")) {
    updateRuntimeStatus("Stopped with error");
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
