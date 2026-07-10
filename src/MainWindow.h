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
#include <QPushButton>
#include <QSpinBox>
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
  QSpinBox *bufferSizeSpin_ = nullptr;
  QSpinBox *refreshIntervalSpin_ = nullptr;
  QComboBox *proxyModeCombo_ = nullptr;
  QComboBox *closeBehaviorCombo_ = nullptr;
  QCheckBox *verifyCertificateCheck_ = nullptr;
  QPushButton *startButton_ = nullptr;
  QPushButton *stopButton_ = nullptr;
  QPlainTextEdit *logView_ = nullptr;
  QTimer *statusTimer_ = nullptr;
  QSystemTrayIcon *trayIcon_ = nullptr;
  QMenu *trayMenu_ = nullptr;
  QAction *showHideAction_ = nullptr;
  QAction *trayStartAction_ = nullptr;
  QAction *trayStopAction_ = nullptr;
#ifdef WS2TCP_SYSTEM_PROXY_AVAILABLE
  QCheckBox *systemProxyCheck_ = nullptr;
  QAction *traySystemProxyAction_ = nullptr;
  bool systemProxyActive_ = false;
#endif
  QAction *quitAction_ = nullptr;
  bool wasRunning_ = false;
  bool allowClose_ = false;
  QString sessionCloseBehavior_;
  QString runtimeStatus_;
};

#endif
