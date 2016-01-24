#include "handlerwindow.h"
#include "handlerstorage.h"
#include <nxmurl.h>
#include <utility.h>
#include <QApplication>
#include <QMessageBox>
#include <QAbstractButton>
#include <QDesktopServices>
#include <boost/scoped_ptr.hpp>
#include <QJsonDocument>
#include <QJsonObject>


#pragma comment(linker, "/manifestDependency:\"name='dlls' processorArchitecture='x86' version='1.0.0.0' type='win32' \"")


using MOBase::ToWString;


void handleLink(const QString &executable, const QString &link)
{
  ::ShellExecute(nullptr, TEXT("open"), ToWString(executable).c_str(),
                 ToWString(link).c_str(),
                 ToWString(QFileInfo(executable).absolutePath()).c_str(),
                 SW_SHOWNORMAL);
}


HandlerStorage *registerExecutable(QString handlerPath)
{
  HandlerStorage *storage = nullptr;
  if (!handlerPath.isEmpty()) {
    // a foreign nxm handler, register ourself and use that handler as an option
    storage = new HandlerStorage(QCoreApplication::applicationDirPath());
    storage->registerHandler(handlerPath, false);
    storage->registerProxy(QCoreApplication::applicationFilePath());
  } else {
    // no handler registered yet or the existing handler is invalid -> overwrite
    storage = new HandlerStorage(QCoreApplication::applicationDirPath());
    storage->registerProxy(QCoreApplication::applicationFilePath());
  }
  return storage;
}

// ensure a nxmhandler.exe is registered to handle nxm-links, then load the handler storage for that registered instance
// (even if it's different from the one actually being run)
HandlerStorage *loadStorage(bool forceReg)
{
  HandlerStorage *storage = nullptr;

  QSettings handlerReg("HKEY_CURRENT_USER\\Software\\Classes\\nxm\\", QSettings::NativeFormat);
  QString handlerPath = HandlerStorage::stripCall(handlerReg.value("shell/open/command/Default", QString()).toString());

  QSettings settings(QCoreApplication::applicationDirPath() + "/nxmhandlers.ini", QSettings::IniFormat);
  bool noRegister = settings.value("noregister", false).toBool();

  if (handlerPath.endsWith("nxmhandler.exe", Qt::CaseInsensitive) && QFile::exists(handlerPath)) {
    // already a nxmhandler.exe registered, use its configuration
    storage = new HandlerStorage(QFileInfo(handlerPath).absolutePath());
    if (forceReg && (QString::compare(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()),
                                      handlerPath,
                                      Qt::CaseInsensitive))) {
      if (QMessageBox::question(nullptr, QObject::tr("Change Handler?"),
                                QObject::tr("A nxm handler from a different Mod Organizer installation has been "
                                            "registered. Do you want to replace it? This is usually not necessary "
                                            "unless the other installation is defective."),
                                QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        storage->registerProxy(QCoreApplication::applicationFilePath());
      }
    }
  } else if (!noRegister || forceReg) {
    QMessageBox registerBox(QMessageBox::Question, QObject::tr("Register?"),
                            QObject::tr("Mod Organizer is not set up to handle nxm links. Associate it with nxm links?"),
                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Save);
    registerBox.button(QMessageBox::Save)->setText(QObject::tr("No, don't ask again"));
    switch (registerBox.exec()) {
      case QMessageBox::Yes: {
          storage = registerExecutable(handlerPath);
        } break;
      case QMessageBox::Save: {
          settings.setValue("noregister", true);
        } break;
    }
  }
  return storage;
}

static void applyChromeFix()
{
  QString dataPath = QDir::fromNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::DataLocation));
  QString fileName = QDir(dataPath + "/../google/chrome/user data/local state").canonicalPath();

  QFile chromeLocalState(fileName);

  if (!chromeLocalState.exists()) {
    // probably simply means that chrome isn't installed
    return;
  }

  if (!chromeLocalState.open(QIODevice::ReadOnly)) {
    // don't know, still no reason to report an error
    return;
  }

  QJsonParseError parseError;
  QJsonDocument document = QJsonDocument::fromJson(chromeLocalState.readAll(), &parseError);
  chromeLocalState.close();
  if (parseError.error == QJsonParseError::NoError) {
    QJsonObject docMap = document.object();
    QJsonObject handlers = docMap["protocol_handler"].toObject();
    // toObject returns empty object if the key doesn't exist. Therefore if excluded_schemes exists, protocol_handler existed as well
    if (handlers.contains("excluded_schemes")) {
      QJsonObject schemes = handlers["excluded_schemes"].toObject();
      if (schemes["nxm"].toBool(true)) {
        if (QMessageBox::question(nullptr, "Apply Chrome fix",
                                  "Chrome may not support nexus links even though the association is set up correctly. "
                                  "Do you want to apply a fix for that (You have to close chrome before pressing yes or "
                                  "this will have no effect!)?",
                                  QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
          return;
        }
        schemes["nxm"] = false;
        handlers["excluded_schemes"] = schemes;
        docMap["protocol_handler"] = handlers;
        QByteArray result = QJsonDocument(docMap).toJson();
        chromeLocalState.open(QIODevice::WriteOnly | QIODevice::Truncate);
        chromeLocalState.write(result);
        chromeLocalState.close();
        qDebug("chrome fix applied");
      }
    }
  }
}

int main(int argc, char *argv[])
{
  QApplication app(argc, argv);

  QStringList args = app.arguments();

  bool forceReg = (args.count() > 1) && args.at(1) == "forcereg";

  boost::scoped_ptr<HandlerStorage> storage(loadStorage(forceReg));
  if (storage.get() == nullptr) {
    return 0;
  }

  if (args.count() > 1) {
    if ((args.at(1) == "reg") || (args.at(1) == "forcereg")) {
      if (args.count() == 4) {
        storage->registerHandler(args.at(2).split(",", QString::SkipEmptyParts), QDir::toNativeSeparators(args.at(3)), true, forceReg);
        if (forceReg) {
          applyChromeFix();
        }
        return 0;
      } else {
        QMessageBox::critical(nullptr, QObject::tr("Error"), QObject::tr("Invalid number of parameters"));
      }
    } else if (args.at(1).startsWith("nxm://")) {
      NXMUrl url(args.at(1));
      QString executable = storage->getHandler(url.game());
      if (!executable.isEmpty()) {
        handleLink(executable, args.at(1));
        return 0;
      } else {
        QMessageBox::warning(nullptr, QObject::tr("No handler found"),
                             QObject::tr( "No application registered to handle this game.\n"
                                          "If you expected Mod Organizer to handle the link, "
                                          "you have to go to Settings->Nexus and click the \"Associate with ... links\"-button.\n"
                                          "If you have NMM installed, you can re-register it for nxm-links so it handles "
                                          "links MO doesn't."));
        return 1;
      }
    } else {
      QMessageBox::warning(nullptr, QObject::tr("Invalid Arguments"), QObject::tr("Invalid number of parameters"));
      return 1;
    }
    return 0;
  } else {
    HandlerWindow win;
    win.setHandlerStorage(storage.get());
    QSettings handlerReg("HKEY_CURRENT_USER\\Software\\Classes\\nxm\\", QSettings::NativeFormat);
    QString handlerPath = HandlerStorage::stripCall(handlerReg.value("shell/open/command/Default", QString()).toString());
    win.setPrimaryHandler(handlerPath);
    win.show();

    return app.exec();
  }
}
