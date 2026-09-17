#include "UpdateDownloadDialog.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString suggestedFileName(const QUrl &url) {
  const QString name = QFileInfo(url.path()).fileName();
  return name.isEmpty() ? QStringLiteral("ws2tcp-local-update") : name;
}

QString formatMegabytes(qint64 bytes) {
  return QString::number(bytes / (1024.0 * 1024.0), 'f', 1);
}

}  // namespace

UpdateDownloadDialog::UpdateDownloadDialog(const QString &downloadUrl,
                                            const QString &version,
                                            QWidget *parent)
    : QDialog(parent), downloadUrl_(downloadUrl), version_(version) {
  setWindowTitle(tr("Downloading Update"));
  setModal(true);
  resize(480, sizeHint().height());

  auto *layout = new QVBoxLayout(this);

  statusLabel_ =
      new QLabel(tr("Downloading version %1...").arg(version_), this);
  statusLabel_->setWordWrap(true);
  layout->addWidget(statusLabel_);

  auto *urlHint = new QLabel(
      tr("Download URL (you can copy it and use another download tool "
         "instead):"),
      this);
  urlHint->setWordWrap(true);
  layout->addWidget(urlHint);

  auto *urlLayout = new QHBoxLayout();
  urlEdit_ = new QLineEdit(downloadUrl_, this);
  urlEdit_->setReadOnly(true);
  urlEdit_->setCursorPosition(0);
  auto *copyButton = new QPushButton(tr("Copy"), this);
  connect(copyButton, &QPushButton::clicked, this,
          &UpdateDownloadDialog::copyUrlToClipboard);
  urlLayout->addWidget(urlEdit_);
  urlLayout->addWidget(copyButton);
  layout->addLayout(urlLayout);

  progressBar_ = new QProgressBar(this);
  progressBar_->setRange(0, 0);
  layout->addWidget(progressBar_);

  auto *buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();
  cancelButton_ = new QPushButton(tr("Cancel"), this);
  connect(cancelButton_, &QPushButton::clicked, this,
          &UpdateDownloadDialog::cancelDownload);
  buttonLayout->addWidget(cancelButton_);
  runButton_ = new QPushButton(tr("Run Installer"), this);
  runButton_->setDefault(true);
  runButton_->setVisible(false);
  connect(runButton_, &QPushButton::clicked, this,
          &UpdateDownloadDialog::runInstallerAndClose);
  buttonLayout->addWidget(runButton_);
  layout->addLayout(buttonLayout);

  startDownload();
}

UpdateDownloadDialog::~UpdateDownloadDialog() {
  if (reply_) {
    reply_->disconnect(this);
    reply_->abort();
  }
  if (file_) {
    if (file_->isOpen()) {
      file_->close();
    }
    if (!downloadComplete_) {
      file_->remove();
    }
  }
}

void UpdateDownloadDialog::startDownload() {
  const QUrl url(downloadUrl_);
  const QString dir =
      QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
      QStringLiteral("/ws2tcp-local-updates");
  QDir().mkpath(dir);
  filePath_ = dir + QLatin1Char('/') + suggestedFileName(url);

  file_ = new QFile(filePath_, this);
  if (!file_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    failWithError(
        tr("Failed to create local file: %1").arg(file_->errorString()));
    return;
  }

  manager_ = new QNetworkAccessManager(this);
  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                        QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                     QStringLiteral("ws2tcp-local-qt/%1")
                         .arg(QCoreApplication::applicationVersion()));
  reply_ = manager_->get(request);
  connect(reply_, &QNetworkReply::downloadProgress, this,
          [this](qint64 bytesReceived, qint64 bytesTotal) {
            if (bytesTotal > 0) {
              progressBar_->setRange(0, 100);
              progressBar_->setValue(
                  static_cast<int>(bytesReceived * 100 / bytesTotal));
              statusLabel_->setText(tr("Downloading version %1... (%2 / %3 "
                                        "MB)")
                                         .arg(version_)
                                         .arg(formatMegabytes(bytesReceived))
                                         .arg(formatMegabytes(bytesTotal)));
            } else {
              statusLabel_->setText(
                  tr("Downloading version %1... (%2 MB)")
                      .arg(version_)
                      .arg(formatMegabytes(bytesReceived)));
            }
          });
  connect(reply_, &QNetworkReply::readyRead, this, [this]() {
    if (file_) {
      file_->write(reply_->readAll());
    }
  });
  connect(reply_, &QNetworkReply::finished, this, [this]() {
    QNetworkReply *finishedReply = reply_;
    reply_ = nullptr;
    finishedReply->deleteLater();

    const QNetworkReply::NetworkError error = finishedReply->error();
    const QString errorString = finishedReply->errorString();

    if (file_) {
      file_->close();
    }

    if (error == QNetworkReply::OperationCanceledError) {
      if (file_) {
        file_->remove();
      }
      reject();
      return;
    }
    if (error != QNetworkReply::NoError) {
      if (file_) {
        file_->remove();
      }
      failWithError(tr("Download failed: %1").arg(errorString));
      return;
    }

    downloadComplete_ = true;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(100);
    statusLabel_->setText(tr("Download complete:\n%1").arg(filePath_));
    cancelButton_->setText(tr("Close"));
    runButton_->setVisible(true);
  });
}

void UpdateDownloadDialog::cancelDownload() {
  if (reply_) {
    reply_->abort();
    return;
  }
  reject();
}

void UpdateDownloadDialog::copyUrlToClipboard() {
  QGuiApplication::clipboard()->setText(downloadUrl_);
}

void UpdateDownloadDialog::failWithError(const QString &message) {
  QMessageBox::warning(this, tr("Download Update"), message);
  reject();
}

void UpdateDownloadDialog::runInstallerAndClose() {
#ifdef Q_OS_WIN
  const bool started = QProcess::startDetached(filePath_, {});
#else
  const bool started =
      QDesktopServices::openUrl(QUrl::fromLocalFile(filePath_));
#endif
  if (!started) {
    QMessageBox::warning(
        this, tr("Run Installer"),
        tr("Failed to launch the installer. You can run it manually "
           "from:\n%1")
            .arg(filePath_));
    return;
  }
  installerLaunched_ = true;
  accept();
}
