#ifndef UPDATEDOWNLOADDIALOG_H
#define UPDATEDOWNLOADDIALOG_H

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QNetworkAccessManager;
class QNetworkReply;
class QFile;

// Downloads an update installer in-process while always showing the raw
// download URL, so the user can fall back to copying it into another
// download tool if the built-in download is slow or blocked.
class UpdateDownloadDialog final : public QDialog {
  Q_OBJECT

 public:
  UpdateDownloadDialog(const QString &downloadUrl, const QString &version,
                        QWidget *parent = nullptr);
  ~UpdateDownloadDialog() override;

  bool installerLaunched() const { return installerLaunched_; }

 private:
  void startDownload();
  void failWithError(const QString &message);
  void cancelDownload();
  void copyUrlToClipboard();
  void runInstallerAndClose();

  QString downloadUrl_;
  QString version_;
  QString filePath_;
  bool installerLaunched_ = false;
  bool downloadComplete_ = false;

  QLabel *statusLabel_ = nullptr;
  QLineEdit *urlEdit_ = nullptr;
  QProgressBar *progressBar_ = nullptr;
  QPushButton *cancelButton_ = nullptr;
  QPushButton *runButton_ = nullptr;

  QNetworkAccessManager *manager_ = nullptr;
  QNetworkReply *reply_ = nullptr;
  QFile *file_ = nullptr;
};

#endif
