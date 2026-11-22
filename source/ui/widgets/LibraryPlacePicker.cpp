#include "LibraryPlacePicker.h"

#include "../ThemeColors.h"
#include "../../core/LibraryTree.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QPainter>
#include <QSettings>
#include <functional>

namespace {

bool isDark()
{
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

QString clean(const QString& path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(path));
}

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

bool samePath(const QString& a, const QString& b)
{
    return clean(a).compare(clean(b), kPathCase) == 0;
}

QStringList knownNotebooks()
{
    QStringList out;
    for (const QString& path : QSettings("SpeedyNote", "App").value("library/notebooks").toStringList()) {
        if (QFileInfo(path).isDir()) out << clean(path);
    }
    return out;
}

QIcon swatch(const QColor& color)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);  // no colour: still keeps the names aligned
    if (color.isValid()) {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(pm.rect()), 3, 3);
    }
    return QIcon(pm);
}

QString iconPath(const char* name)
{
    return QStringLiteral(":/resources/icons/%1%2.png")
        .arg(QLatin1String(name), isDark() ? QStringLiteral("_reversed") : QString());
}

} // namespace

LibraryPlacePicker::LibraryPlacePicker(QWidget* parent)
    : QPushButton(parent)
{
    setObjectName(QStringLiteral("LibraryPlacePicker"));
    setIcon(QIcon(iconPath("folder")));
    setIconSize(QSize(16, 16));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(this, &QPushButton::clicked, this, [this]() {
        const QString chosen = choose(this, mapToGlobal(QPoint(0, height())), m_place);
        if (!chosen.isEmpty() && !samePath(chosen, m_place)) {
            setPlace(chosen);
            emit placeChanged(m_place);
        }
    });
}

void LibraryPlacePicker::setPlace(const QString& folder)
{
    m_place = clean(folder);
    setToolTip(QDir::toNativeSeparators(m_place));
    refreshText();
}

QString LibraryPlacePicker::describe(const QString& folder)
{
    const QString path = clean(folder);
    for (const QString& notebook : knownNotebooks()) {
        if (!samePath(path, notebook) && !path.startsWith(notebook + QLatin1Char('/'), kPathCase))
            continue;
        QStringList parts{QFileInfo(notebook).fileName()};
        const QString rest = path.mid(notebook.size());
        for (const QString& part : rest.split(QLatin1Char('/'), Qt::SkipEmptyParts))
            parts << LibraryTree::displayName(part);
        return parts.join(QStringLiteral(" › "));
    }
    return QDir::toNativeSeparators(path);
}

QString LibraryPlacePicker::choose(QWidget* parent, const QPoint& globalPos, const QString& current)
{
    const bool dark = isDark();
    QMenu menu(parent);
    ThemeColors::styleMenu(&menu, dark);
    bool inLibrary = false;

    auto addPlace = [&](QMenu* m, const QString& path, const QString& text) {
        QAction* action = m->addAction(swatch(LibraryTree::color(path)), text);
        action->setData(path);
        action->setCheckable(true);
        if (samePath(path, current)) {
            action->setChecked(true);
            inLibrary = true;
        }
    };
    // A folder with sub-sections opens a sub-menu that starts with the folder itself.
    std::function<void(QMenu*, const QString&)> addFolder = [&](QMenu* m, const QString& dir) {
        const QStringList sections = LibraryTree::sections(dir);
        const QString name = LibraryTree::displayName(dir);
        if (sections.isEmpty()) {
            addPlace(m, dir, name);
            return;
        }
        QMenu* sub = m->addMenu(swatch(LibraryTree::color(dir)), name);
        ThemeColors::styleMenu(sub, dark);
        addPlace(sub, dir, QObject::tr("%1 (top level)").arg(name));
        sub->addSeparator();
        for (const QString& section : sections) addFolder(sub, section);
    };

    const QStringList notebooks = knownNotebooks();
    for (const QString& notebook : notebooks) addFolder(&menu, notebook);
    if (notebooks.isEmpty()) {
        menu.addAction(QObject::tr("No notebook in the Library yet"))->setEnabled(false);
    }
    if (!current.isEmpty() && !inLibrary) {   // the place in use, outside the Library
        menu.addSeparator();
        addPlace(&menu, clean(current), QDir::toNativeSeparators(clean(current)));
    }
    QAction* other = nullptr;
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    menu.addSeparator();
    other = menu.addAction(QIcon(iconPath("folder")), QObject::tr("Another folder..."));
#endif

    QAction* picked = menu.exec(globalPos);
    if (!picked) return {};
    if (picked == other) {
        return clean(QFileDialog::getExistingDirectory(parent, QObject::tr("Choose a folder for the note"),
                                                      current, QFileDialog::ShowDirsOnly));
    }
    return picked->data().toString();
}

QSize LibraryPlacePicker::sizeHint() const
{
    // The text is elided to the width given, so the hint must not follow it.
    return QSize(320, QPushButton::sizeHint().height());
}

void LibraryPlacePicker::resizeEvent(QResizeEvent* event)
{
    QPushButton::resizeEvent(event);
    refreshText();
}

void LibraryPlacePicker::paintEvent(QPaintEvent* event)
{
    QPushButton::paintEvent(event);
    QPainter p(this);
    const QPixmap arrow = QIcon(iconPath("down_arrow")).pixmap(QSize(12, 12));
    p.drawPixmap(QPoint(width() - 24, (height() - 12) / 2), arrow);
}

void LibraryPlacePicker::refreshText()
{
    const int room = width() - iconSize().width() - 56;  // icon, paddings and the arrow
    setText(fontMetrics().elidedText(describe(m_place), Qt::ElideMiddle, qMax(room, 40)));
}
