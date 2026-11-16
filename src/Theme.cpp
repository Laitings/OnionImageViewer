#include "Theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QPixmapCache>
#include <QSettings>
#include <QStyleHints>

namespace {

const QString SettingsTheme = QStringLiteral("appearance/theme");

Theme::Id g_current = Theme::Id::System;

// The handful of colours a theme is made from; the rest of the palette is derived.
struct Colors {
    QColor window;
    QColor base;
    QColor alternateBase;
    QColor button;
    QColor text;
    QColor highlight;
    QColor highlightedText;
    QColor link;
    bool dark = true;
};

QColor blend(const QColor &a, const QColor &b, double toB)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * toB),
                            float(a.greenF() + (b.greenF() - a.greenF()) * toB),
                            float(a.blueF() + (b.blueF() - a.blueF()) * toB));
}

QPalette makePalette(const Colors &c)
{
    QPalette palette;
    palette.setColor(QPalette::Window, c.window);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, c.base);
    palette.setColor(QPalette::AlternateBase, c.alternateBase);
    palette.setColor(QPalette::ToolTipBase, c.dark ? c.text : QColor(255, 255, 220));
    palette.setColor(QPalette::ToolTipText, c.dark ? c.base : QColor(20, 20, 20));
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::Button, c.button);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::BrightText, c.dark ? QColor(255, 255, 255) : QColor(0, 0, 0));
    palette.setColor(QPalette::Highlight, c.highlight);
    palette.setColor(QPalette::HighlightedText, c.highlightedText);
    palette.setColor(QPalette::PlaceholderText, blend(c.text, c.base, 0.45));
    palette.setColor(QPalette::Link, c.link);
    // Greyed-out items sit between the text and the background.
    const QColor muted = blend(c.text, c.window, 0.55);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, muted);
    }
    return palette;
}

Colors colorsFor(Theme::Id id)
{
    switch (id) {
    case Theme::Id::Light:
        return {QColor(240, 240, 240), QColor(255, 255, 255), QColor(245, 245, 245), QColor(228, 228, 228),
                QColor(25, 25, 25), QColor(0, 120, 215), QColor(255, 255, 255), QColor(0, 102, 204), false};
    case Theme::Id::Blue:
        return {QColor(22, 30, 46), QColor(16, 22, 36), QColor(28, 38, 58), QColor(30, 42, 66),
                QColor(226, 232, 244), QColor(58, 134, 255), QColor(255, 255, 255), QColor(110, 170, 255), true};
    case Theme::Id::Green:
        return {QColor(24, 38, 30), QColor(17, 28, 22), QColor(30, 46, 36), QColor(32, 52, 40),
                QColor(226, 240, 230), QColor(46, 160, 90), QColor(255, 255, 255), QColor(110, 210, 150), true};
    case Theme::Id::Red:
        return {QColor(42, 24, 26), QColor(32, 18, 20), QColor(52, 30, 32), QColor(60, 32, 36),
                QColor(244, 228, 228), QColor(200, 60, 60), QColor(255, 255, 255), QColor(255, 130, 130), true};
    case Theme::Id::Purple:
        return {QColor(36, 28, 48), QColor(27, 20, 38), QColor(46, 36, 60), QColor(52, 40, 70),
                QColor(236, 230, 246), QColor(150, 90, 220), QColor(255, 255, 255), QColor(190, 150, 255), true};
    case Theme::Id::Jamaica:
        // Black, green and gold.
        return {QColor(16, 16, 12), QColor(8, 8, 6), QColor(26, 28, 16), QColor(0, 90, 38),
                QColor(250, 214, 60), QColor(254, 209, 0), QColor(10, 10, 8), QColor(80, 200, 110), true};
    case Theme::Id::System:
    case Theme::Id::Dark:
        break;
    }
    return {QColor(37, 37, 38), QColor(30, 30, 30), QColor(45, 45, 48), QColor(45, 45, 48),
            QColor(230, 230, 230), QColor(0, 122, 204), QColor(255, 255, 255), QColor(86, 156, 214), true};
}

// What "System" means right now.
Theme::Id resolvedSystem()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light ? Theme::Id::Light
                                                                                  : Theme::Id::Dark;
}

} // namespace

namespace Theme {

QList<Id> all()
{
    return {Id::System, Id::Light, Id::Dark, Id::Blue, Id::Green, Id::Red, Id::Purple, Id::Jamaica};
}

QString displayName(Id id)
{
    switch (id) {
    case Id::System: return QStringLiteral("Windows setting (light or dark)");
    case Id::Light: return QStringLiteral("Light");
    case Id::Dark: return QStringLiteral("Dark");
    case Id::Blue: return QStringLiteral("Dark blue");
    case Id::Green: return QStringLiteral("Dark green");
    case Id::Red: return QStringLiteral("Dark red");
    case Id::Purple: return QStringLiteral("Dark purple");
    case Id::Jamaica: return QStringLiteral("Jamaican man");
    }
    return QString();
}

QString key(Id id)
{
    switch (id) {
    case Id::System: return QStringLiteral("system");
    case Id::Light: return QStringLiteral("light");
    case Id::Dark: return QStringLiteral("dark");
    case Id::Blue: return QStringLiteral("blue");
    case Id::Green: return QStringLiteral("green");
    case Id::Red: return QStringLiteral("red");
    case Id::Purple: return QStringLiteral("purple");
    case Id::Jamaica: return QStringLiteral("jamaica");
    }
    return QStringLiteral("system");
}

Id fromKey(const QString &name)
{
    for (Id id : all()) {
        if (key(id) == name) {
            return id;
        }
    }
    return Id::System;
}

Id saved()
{
    return fromKey(QSettings().value(SettingsTheme).toString());
}

void save(Id id)
{
    QSettings().setValue(SettingsTheme, key(id));
}

void apply(Id id)
{
    g_current = id;
    const Id shown = id == Id::System ? resolvedSystem() : id;
    const Colors colors = colorsFor(shown);

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Lets the Windows title bar and other native parts match; Unknown hands control back to Windows.
    QGuiApplication::styleHints()->setColorScheme(id == Id::System ? Qt::ColorScheme::Unknown
                                                  : colors.dark   ? Qt::ColorScheme::Dark
                                                                  : Qt::ColorScheme::Light);
#endif
    qApp->setStyle(QStringLiteral("Fusion"));
    qApp->setPalette(makePalette(colors));
    // Toolbar and menu icons are painted from the palette; drop the ones drawn in the old colours.
    QPixmapCache::clear();
}

void watchSystem()
{
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, [] {
        if (g_current == Id::System) {
            apply(Id::System);
        }
    });
}

} // namespace Theme
