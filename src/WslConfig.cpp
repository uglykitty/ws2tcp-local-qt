#include "WslConfig.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

namespace {

QString wslConfigPath() {
  return QDir::homePath() + "/.wslconfig";
}

bool isSectionHeader(const QString &line, QString *sectionName) {
  static const QRegularExpression pattern(R"(^\s*\[([^\]]+)\]\s*$)");
  const QRegularExpressionMatch match = pattern.match(line);
  if (!match.hasMatch()) {
    return false;
  }
  if (sectionName != nullptr) {
    *sectionName = match.captured(1).trimmed();
  }
  return true;
}

bool isNetworkingModeLine(const QString &line) {
  static const QRegularExpression pattern(
      R"(^\s*networkingMode\s*=)", QRegularExpression::CaseInsensitiveOption);
  return pattern.match(line).hasMatch();
}

}  // namespace

bool WslConfig::enableMirroredNetworking(QString *error) {
  const QString path = wslConfigPath();

  QStringList lines;
  QString lineEnding = "\n";

  QFile file(path);
  if (file.exists()) {
    // Opened without QIODevice::Text: that flag makes QFile translate
    // line endings on read/write, which would defeat the manual CRLF
    // detection and re-emission done below.
    if (!file.open(QIODevice::ReadOnly)) {
      if (error != nullptr) {
        *error =
            QString("Failed to open %1: %2").arg(path, file.errorString());
      }
      return false;
    }
    const QByteArray raw = file.readAll();
    file.close();
    if (raw.contains("\r\n")) {
      lineEnding = "\r\n";
    }
    for (const QByteArray &rawLine : raw.split('\n')) {
      QString line = QString::fromUtf8(rawLine);
      if (line.endsWith('\r')) {
        line.chop(1);
      }
      lines.append(line);
    }
    // split() on a file ending in a newline leaves one trailing empty
    // element; drop it so we do not accumulate a blank line on every save.
    if (!lines.isEmpty() && lines.last().isEmpty()) {
      lines.removeLast();
    }
  }

  bool inWsl2Section = false;
  bool sectionFound = false;
  bool keyUpdated = false;
  int wsl2SectionEndIndex = -1;

  for (int i = 0; i < lines.size(); ++i) {
    QString sectionName;
    if (isSectionHeader(lines[i], &sectionName)) {
      if (inWsl2Section) {
        wsl2SectionEndIndex = i;
      }
      inWsl2Section = sectionName.compare("wsl2", Qt::CaseInsensitive) == 0;
      if (inWsl2Section) {
        sectionFound = true;
      }
      continue;
    }
    if (inWsl2Section && isNetworkingModeLine(lines[i])) {
      lines[i] = "networkingMode=mirrored";
      keyUpdated = true;
    }
  }
  if (inWsl2Section) {
    wsl2SectionEndIndex = lines.size();
  }

  if (sectionFound && !keyUpdated) {
    lines.insert(wsl2SectionEndIndex, "networkingMode=mirrored");
  } else if (!sectionFound) {
    if (!lines.isEmpty() && !lines.last().trimmed().isEmpty()) {
      lines.append(QString());
    }
    lines.append("[wsl2]");
    lines.append("networkingMode=mirrored");
  }

  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error != nullptr) {
      *error = QString("Failed to write %1: %2").arg(path, file.errorString());
    }
    return false;
  }
  QTextStream out(&file);
  for (const QString &line : lines) {
    out << line << lineEnding;
  }
  file.close();
  return true;
}
