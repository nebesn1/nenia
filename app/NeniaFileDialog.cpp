#include "NeniaFileDialog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QUrl>
#include <QUuid>

#ifdef NENIA_HAS_XDG_PORTAL
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QEventLoop>
#include <QVariantMap>
#endif

#ifdef NENIA_HAS_XDG_PORTAL
namespace nenia::app {

QDBusArgument &operator<<(QDBusArgument &argument,
                          const NeniaPortalNameFilterPattern &pattern)
{
    argument.beginStructure();
    argument << pattern.type << pattern.pattern;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument,
                                NeniaPortalNameFilterPattern &pattern)
{
    argument.beginStructure();
    argument >> pattern.type >> pattern.pattern;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument,
                          const NeniaPortalNameFilter &filter)
{
    argument.beginStructure();
    argument << filter.label << filter.patterns;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument,
                                NeniaPortalNameFilter &filter)
{
    argument.beginStructure();
    argument >> filter.label >> filter.patterns;
    argument.endStructure();
    return argument;
}

} // namespace nenia::app
#endif

namespace nenia::app {
namespace {

QString normalizedInitialDirectory(const QString &directory)
{
    if (!directory.isEmpty() && QFileInfo(directory).isDir()) {
        return QFileInfo(directory).absoluteFilePath();
    }
    const QString musicDirectory =
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (!musicDirectory.isEmpty() && QFileInfo(musicDirectory).isDir()) {
        return QFileInfo(musicDirectory).absoluteFilePath();
    }
    return QDir::currentPath();
}

#ifdef NENIA_HAS_XDG_PORTAL
class PortalResponseReceiver final : public QObject {
    Q_OBJECT

public:
    bool received = false;
    quint32 responseCode = 2;
    QVariantMap results;

public slots:
    void handleResponse(quint32 code, const QVariantMap &responseResults)
    {
        received = true;
        responseCode = code;
        results = responseResults;
        emit responseReady();
    }

signals:
    void responseReady();
};

QString portalRequestPath(const QString &token, const QString &serviceName)
{
    QString senderPath = serviceName;
    senderPath.replace(QLatin1Char(':'), QLatin1Char('_'));
    senderPath.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
        .arg(senderPath, token);
}

struct PortalOutcome {
    bool handled = false;
    bool accepted = false;
    QStringList paths;
};

bool xdgDesktopPortalAvailable()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || bus.interface() == nullptr) {
        return false;
    }
    const QDBusReply<bool> registered =
        bus.interface()->isServiceRegistered(QStringLiteral("org.freedesktop.portal.Desktop"));
    return registered.isValid() && registered.value();
}

PortalOutcome chooseWithPortal(QWidget *parent,
                               NeniaFileDialogMode mode,
                               const QString &title,
                               const QString &initialDirectory,
                               const QString &filterText)
{
    Q_UNUSED(parent);
    PortalOutcome outcome;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return outcome;
    }

    registerNeniaPortalDBusTypes();

    const QString token = QStringLiteral("nenia_%1")
                              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)
                                       .remove(QLatin1Char('-')));
    PortalResponseReceiver receiver;
    const QString service = QStringLiteral("org.freedesktop.portal.Desktop");
    const QString interface = QStringLiteral("org.freedesktop.portal.Request");
    QString requestPath = portalRequestPath(token, bus.baseService());
    if (!bus.connect(service, requestPath, interface, QStringLiteral("Response"),
                     &receiver, SLOT(handleResponse(uint,QVariantMap)))) {
        return outcome;
    }

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);
    options.insert(QStringLiteral("modal"), true);
    if (mode == NeniaFileDialogMode::OpenFiles) {
        options.insert(QStringLiteral("multiple"), true);
    }
    if (mode == NeniaFileDialogMode::OpenDirectory) {
        options.insert(QStringLiteral("directory"), true);
        options.insert(QStringLiteral("accept_label"),
                       QCoreApplication::translate("NeniaFileDialog", "Select Folder"));
    }
    const QString startingDirectory = normalizedInitialDirectory(initialDirectory);
    QByteArray encodedFolder = QFile::encodeName(startingDirectory);
    encodedFolder.append('\0');
    options.insert(QStringLiteral("current_folder"), encodedFolder);
    const QList<NeniaPortalNameFilter> filters = parseNeniaPortalNameFilters(filterText);
    if (!filters.isEmpty()) {
        options.insert(QStringLiteral("filters"), QVariant::fromValue(filters));
        options.insert(QStringLiteral("current_filter"),
                       QVariant::fromValue(filters.constFirst()));
    }

    QDBusMessage request = QDBusMessage::createMethodCall(
        service,
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.FileChooser"),
        QStringLiteral("OpenFile"));
    request << QString() << title << options;
    const QDBusMessage reply = bus.call(request);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qWarning().noquote()
                << "Nenia: XDG FileChooser request failed; using editable Qt fallback:"
                << reply.errorName() << reply.errorMessage();
        }
        bus.disconnect(service, requestPath, interface, QStringLiteral("Response"),
                       &receiver, SLOT(handleResponse(uint,QVariantMap)));
        return outcome;
    }

    const QDBusObjectPath returnedPath = qvariant_cast<QDBusObjectPath>(reply.arguments().constFirst());
    if (!returnedPath.path().isEmpty() && returnedPath.path() != requestPath) {
        bus.disconnect(service, requestPath, interface, QStringLiteral("Response"),
                       &receiver, SLOT(handleResponse(uint,QVariantMap)));
        requestPath = returnedPath.path();
        if (!bus.connect(service, requestPath, interface, QStringLiteral("Response"),
                         &receiver, SLOT(handleResponse(uint,QVariantMap)))) {
            return outcome;
        }
    }

    outcome.handled = true;
    if (!receiver.received) {
        QEventLoop responseLoop;
        QObject::connect(&receiver, &PortalResponseReceiver::responseReady,
                         &responseLoop, &QEventLoop::quit);
        if (!receiver.received) {
            responseLoop.exec();
        }
    }
    bus.disconnect(service, requestPath, interface, QStringLiteral("Response"),
                   &receiver, SLOT(handleResponse(uint,QVariantMap)));

    if (receiver.responseCode != 0U) {
        return outcome;
    }
    outcome.accepted = true;
    const QStringList uris = receiver.results.value(QStringLiteral("uris")).toStringList();
    for (const QString &uriText : uris) {
        const QUrl url(uriText);
        if (!url.isLocalFile() || (!url.host().isEmpty()
                                   && url.host() != QStringLiteral("localhost"))) {
            qWarning("Nenia file dialog portal returned a non-local URI; it was ignored.");
            outcome.paths.clear();
            return outcome;
        }
        outcome.paths.append(url.toLocalFile());
    }
    return outcome;
}

#endif // NENIA_HAS_XDG_PORTAL

QStringList runQtFallback(QWidget *parent,
                          NeniaFileDialogMode mode,
                          const QString &title,
                          const QString &initialDirectory,
                          const QString &nameFilters)
{
    NeniaLocationDialog dialog(parent, mode, title, initialDirectory, nameFilters);
    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    return dialog.selectedPaths();
}

QStringList runFileDialog(QWidget *parent,
                          NeniaFileDialogMode mode,
                          const QString &title,
                          const QString &initialDirectory,
                          const QString &nameFilters)
{
#ifdef NENIA_HAS_XDG_PORTAL
    if (selectNeniaFileDialogBackend(xdgDesktopPortalAvailable())
        == NeniaFileDialogBackend::XdgDesktopPortal) {
        const PortalOutcome portal = chooseWithPortal(
            parent, mode, title, initialDirectory, nameFilters);
        if (portal.handled) {
            return portal.accepted ? portal.paths : QStringList{};
        }
    }
#endif
    return runQtFallback(parent, mode, title, initialDirectory, nameFilters);
}

} // namespace

#ifdef NENIA_HAS_XDG_PORTAL
void registerNeniaPortalDBusTypes()
{
    qDBusRegisterMetaType<NeniaPortalNameFilterPattern>();
    qDBusRegisterMetaType<NeniaPortalNameFilter>();
    qDBusRegisterMetaType<QList<NeniaPortalNameFilter>>();
}
#endif

NeniaFileDialogBackend selectNeniaFileDialogBackend(bool portalServiceAvailable)
{
    return portalServiceAvailable ? NeniaFileDialogBackend::XdgDesktopPortal
                                  : NeniaFileDialogBackend::EditableQtFallback;
}

QList<NeniaPortalNameFilter> parseNeniaPortalNameFilters(const QString &nameFilters)
{
    QList<NeniaPortalNameFilter> result;
    for (const QString &filter : parseNeniaNameFilters(nameFilters)) {
        const qsizetype open = filter.indexOf(QLatin1Char('('));
        const qsizetype close = filter.lastIndexOf(QLatin1Char(')'));
        if (open < 0 || close <= open) {
            continue;
        }

        NeniaPortalNameFilter converted;
        converted.label = filter.left(open).trimmed();
        const QStringList patterns = filter.mid(open + 1, close - open - 1)
                                         .split(QRegularExpression(QStringLiteral("\\s+")),
                                                Qt::SkipEmptyParts);
        for (const QString &pattern : patterns) {
            QString insensitivePattern;
            for (const QChar character : pattern) {
                if (character.isLetter() && character.toLower() != character.toUpper()) {
                    insensitivePattern += QLatin1Char('[');
                    insensitivePattern += character.toLower();
                    insensitivePattern += character.toUpper();
                    insensitivePattern += QLatin1Char(']');
                } else {
                    insensitivePattern += character;
                }
            }
            converted.patterns.append(NeniaPortalNameFilterPattern{0U,
                                                                     insensitivePattern});
        }
        if (!converted.patterns.isEmpty()) {
            result.append(converted);
        }
    }
    return result;
}

NeniaLocationDialog::NeniaLocationDialog(QWidget *parent,
                                         NeniaFileDialogMode mode,
                                         const QString &title,
                                         const QString &initialDirectory,
                                         const QString &nameFilters)
    : QDialog(parent),
      m_mode(mode),
      m_locationEditor(new QLineEdit(this)),
      m_locationError(new QLabel(this)),
      m_picker(new QFileDialog(this,
                               title,
                               normalizedInitialDirectory(initialDirectory),
                               nameFilters))
{
    setWindowTitle(title);
    auto *layout = new QVBoxLayout(this);
    auto *locationLabel = new QLabel(tr("Location:"), this);
    layout->addWidget(locationLabel);

    m_locationEditor->setObjectName(QStringLiteral("neniaLocationEditor"));
    m_locationEditor->setClearButtonEnabled(false);
    m_locationEditor->setText(m_picker->directory().absolutePath());
    m_locationEditor->setToolTip(tr("Enter or paste a local folder or file path, then press Enter to navigate."));
    layout->addWidget(m_locationEditor);

    m_locationError->setObjectName(QStringLiteral("neniaLocationError"));
    m_locationError->setStyleSheet(QStringLiteral("color: palette(highlight);"));
    m_locationError->setWordWrap(true);
    m_locationError->hide();
    layout->addWidget(m_locationError);

    m_picker->setOption(QFileDialog::DontUseNativeDialog, true);
    m_picker->setWindowFlags(Qt::Widget);
    m_picker->setParent(this, Qt::Widget);
    m_picker->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_picker->setAcceptMode(QFileDialog::AcceptOpen);
    if (mode == NeniaFileDialogMode::OpenFiles) {
        m_picker->setFileMode(QFileDialog::ExistingFiles);
    } else if (mode == NeniaFileDialogMode::OpenDirectory) {
        m_picker->setFileMode(QFileDialog::Directory);
        m_picker->setOption(QFileDialog::ShowDirsOnly, true);
        m_picker->setLabelText(QFileDialog::Accept, tr("Select Folder"));
    } else {
        m_picker->setFileMode(QFileDialog::ExistingFile);
    }
    if (!nameFilters.isEmpty()) {
        m_picker->setNameFilters(parseNeniaNameFilters(nameFilters));
    }
    layout->addWidget(m_picker, 1);
    resize(760, 540);

    connect(m_locationEditor, &QLineEdit::returnPressed, this, [this] {
        QString error;
        if (!applyLocationText(m_locationEditor->text(), &error)) {
            m_locationError->setText(error);
            m_locationError->show();
        } else {
            m_locationError->clear();
            m_locationError->hide();
        }
    });
    connect(m_picker, &QFileDialog::directoryEntered,
            this, [this](const QString &directory) {
        m_locationEditor->setText(QFileInfo(directory).absoluteFilePath());
        m_locationError->clear();
        m_locationError->hide();
    });
    connect(m_picker, &QFileDialog::accepted, this, &QDialog::accept);
    connect(m_picker, &QFileDialog::rejected, this, &QDialog::reject);
}

bool NeniaLocationDialog::applyLocationText(const QString &text,
                                            QString *errorMessage)
{
    const QString pathText = text;
    if (pathText.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Enter a local file or directory path.");
        }
        return false;
    }

    const QString resolved = QDir::isAbsolutePath(pathText)
                                 ? QDir::cleanPath(pathText)
                                 : QDir(currentDirectory()).absoluteFilePath(pathText);
    const QFileInfo info(resolved);
    if (info.isDir()) {
        const QDir directory(info.absoluteFilePath());
        if (!info.isReadable() || !directory.isReadable()) {
            if (errorMessage != nullptr) {
                *errorMessage = tr("The directory exists but is not accessible.");
            }
            return false;
        }
        m_picker->setDirectory(info.absoluteFilePath());
        m_locationEditor->setText(info.absoluteFilePath());
        return true;
    }

    if (m_mode != NeniaFileDialogMode::OpenDirectory
        && info.exists() && info.isFile() && info.isReadable()) {
        m_picker->setDirectory(info.absolutePath());
        m_picker->selectFile(info.fileName());
        m_locationEditor->setText(info.absolutePath());
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = tr("The path does not name an accessible local directory%1.")
                            .arg(m_mode == NeniaFileDialogMode::OpenDirectory
                                     ? QString()
                                     : tr(" or existing file"));
    }
    return false;
}

QStringList NeniaLocationDialog::selectedPaths() const
{
    QStringList selected = m_picker->selectedFiles();
    if (m_mode == NeniaFileDialogMode::OpenDirectory
        && (selected.isEmpty() || selected.constFirst().isEmpty())) {
        selected = {m_picker->directory().absolutePath()};
    }
    for (QString &path : selected) {
        path = QFileInfo(path).absoluteFilePath();
    }
    return selected;
}

QString NeniaLocationDialog::currentDirectory() const
{
    return m_picker->directory().absolutePath();
}

QStringList NeniaLocationDialog::nameFilters() const
{
    return m_picker->nameFilters();
}

QFileDialog::FileMode NeniaLocationDialog::fileMode() const
{
    return m_picker->fileMode();
}

QLineEdit *NeniaLocationDialog::locationEditor() const
{
    return m_locationEditor;
}

QFileDialog *NeniaLocationDialog::picker() const
{
    return m_picker;
}

QStringList parseNeniaNameFilters(const QString &nameFilters)
{
    if (nameFilters.isEmpty()) {
        return {};
    }
    return nameFilters.split(QStringLiteral(";;"), Qt::SkipEmptyParts);
}

QStringList getNeniaOpenFileNames(QWidget *parent,
                                  const QString &title,
                                  const QString &initialDirectory,
                                  const QString &nameFilters)
{
    return runFileDialog(parent, NeniaFileDialogMode::OpenFiles,
                         title, initialDirectory, nameFilters);
}

QString getNeniaOpenFileName(QWidget *parent,
                             const QString &title,
                             const QString &initialDirectory,
                             const QString &nameFilters)
{
    const QStringList paths = runFileDialog(parent, NeniaFileDialogMode::OpenFile,
                                            title, initialDirectory, nameFilters);
    return paths.isEmpty() ? QString() : paths.constFirst();
}

QString getNeniaExistingDirectory(QWidget *parent,
                                  const QString &title,
                                  const QString &initialDirectory)
{
    const QStringList paths = runFileDialog(parent, NeniaFileDialogMode::OpenDirectory,
                                            title, initialDirectory, {});
    return paths.isEmpty() ? QString() : paths.constFirst();
}

} // namespace nenia::app

#ifdef NENIA_HAS_XDG_PORTAL
#include "NeniaFileDialog.moc"
#endif
