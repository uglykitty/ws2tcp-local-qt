#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolButton>

#include "ws2tcp_local_ffi.h"

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

 private slots:
  void startProxy();
  void stopProxy();
  void updateProxyMode(const QString &mode);
  void refreshStatus();
  void appendLog(QString message);
  void showSettingsDialog();
  void showAboutDialog();
  void toggleWindowVisibility();
  void quitFromTray();
  void handleTrayActivation(QSystemTrayIcon::ActivationReason reason);
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  void setSystemProxyEnabled(bool enabled);
#endif

 protected:
  void closeEvent(QCloseEvent *event) override;

 private:
  QByteArray buildConfigJson() const;
  void setupTrayIcon();
  void updateTrayActions();
  void updateConfigurationInputs(bool running);
  void loadUserSettings();
  void saveUserSettings() const;
  void appendError(const QString &prefix);
  void showError(const QString &message);
  void updateRuntimeStatus(const QString &message);
  void updateRuntimeStatusFromLog(const QString &message);
  static void handleRustLog(const char *message, void *userData);

  Ws2TcpHandle *handle_ = nullptr;
  QLineEdit *listenEdit_ = nullptr;
  QLineEdit *gatewayEdit_ = nullptr;
  QLineEdit *usernameEdit_ = nullptr;
  QLineEdit *passwordEdit_ = nullptr;
  QToolButton *passwordVisibilityButton_ = nullptr;
  QLineEdit *customRulesEdit_ = nullptr;
  QToolButton *customRulesBrowseButton_ = nullptr;
  QComboBox *proxyModeCombo_ = nullptr;
  QAction *startAction_ = nullptr;
  QAction *stopAction_ = nullptr;
  QAction *settingsAction_ = nullptr;
  QPlainTextEdit *logView_ = nullptr;
  QTimer *statusTimer_ = nullptr;
  QSystemTrayIcon *trayIcon_ = nullptr;
  QMenu *trayMenu_ = nullptr;
  QAction *showHideAction_ = nullptr;
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  QCheckBox *systemProxyCheck_ = nullptr;
  QAction *traySystemProxyAction_ = nullptr;
  bool systemProxyActive_ = false;
#endif
  QAction *quitAction_ = nullptr;
  bool wasRunning_ = false;
  bool allowClose_ = false;
  int bufferSize_ = 16 * 1024;
  int refreshIntervalSeconds_ = 60;
  bool verifyCertificate_ = false;
  QString closeBehavior_ = "ask";
  QString sessionCloseBehavior_;
  QString runtimeStatus_;
};

#endif
