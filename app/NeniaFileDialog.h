#pragma once

#include <QDialog>
#include <QFileDialog>
#include <QList>
#include <QStringList>

class QLabel;
class QLineEdit;
class QWidget;

namespace nenia::app {

enum class NeniaFileDialogMode {
    OpenFile,
    OpenFiles,
    OpenDirectory,
};

enum class NeniaFileDialogBackend {
    XdgDesktopPortal,
    EditableQtFallback,
};

struct NeniaPortalNameFilterPattern {
    quint32 type = 0;
    QString pattern;
};

struct NeniaPortalNameFilter {
    QString label;
    QList<NeniaPortalNameFilterPattern> patterns;
};

NeniaFileDialogBackend selectNeniaFileDialogBackend(bool portalServiceAvailable);

class NeniaLocationDialog final : public QDialog {
    Q_OBJECT

public:
    NeniaLocationDialog(QWidget *parent,
                        NeniaFileDialogMode mode,
                        const QString &title,
                        const QString &initialDirectory,
                        const QString &nameFilters = {});

    bool applyLocationText(const QString &text, QString *errorMessage = nullptr);
    QStringList selectedPaths() const;
    QString currentDirectory() const;
    QStringList nameFilters() const;
    QFileDialog::FileMode fileMode() const;

    QLineEdit *locationEditor() const;
    QFileDialog *picker() const;

private:
    NeniaFileDialogMode m_mode;
    QLineEdit *m_locationEditor = nullptr;
    QLabel *m_locationError = nullptr;
    QFileDialog *m_picker = nullptr;
};

QStringList parseNeniaNameFilters(const QString &nameFilters);
QList<NeniaPortalNameFilter> parseNeniaPortalNameFilters(const QString &nameFilters);

#ifdef NENIA_HAS_XDG_PORTAL
void registerNeniaPortalDBusTypes();
#endif

QStringList getNeniaOpenFileNames(QWidget *parent,
                                  const QString &title,
                                  const QString &initialDirectory,
                                  const QString &nameFilters);

QString getNeniaOpenFileName(QWidget *parent,
                             const QString &title,
                             const QString &initialDirectory,
                             const QString &nameFilters);

QString getNeniaExistingDirectory(QWidget *parent,
                                  const QString &title,
                                  const QString &initialDirectory);

} // namespace nenia::app

Q_DECLARE_METATYPE(nenia::app::NeniaPortalNameFilterPattern)
Q_DECLARE_METATYPE(nenia::app::NeniaPortalNameFilter)
