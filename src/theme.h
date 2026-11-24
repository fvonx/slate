#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

// Follows the Omarchy theme live: its whole palette, whether it is light or
// dark, Hyprland's window rounding as radius, and Hyprland's touchpad scroll
// factor.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(bool light READ light NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    // How much Hyprland scales touchpad scrolling (Omarchy sets 0.4, so text
    // scrolls at a reading pace). A canvas undoes it, so the board follows
    // the fingers. 1 without Hyprland.
    Q_PROPERTY(double scrollFactor READ scrollFactor NOTIFY changed)

public:
    // Tests pass a temp directory, a fake hyprctl, and their own event socket.
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {},
                   const QString &hyprlandEvents = {});
    ~Theme() override;

    QColor accent() const { return color("accent"); }
    QColor accentForeground() const;
    QColor background() const { return color("background"); }
    QColor foreground() const { return color("foreground"); }
    QVariantMap colors() const;
    bool light() const { return m_light; }
    int radius() const { return m_radius; }
    double scrollFactor() const { return m_scrollFactor; }

    struct Palette {
        QHash<QString, QColor> colors;
        bool light = false;
    };
    static Palette readThemeColors(const QString &path);

signals:
    void changed();

private:
    QColor color(const QString &key) const;
    void reload();
    void refreshHyprland();

    QString m_directory;
    QHash<QString, QColor> m_colors;
    bool m_light = false;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    double m_scrollFactor = 1;
    bool m_roundingAgain = false;
    QProcess m_roundingProcess;
    QTimer m_roundingTimeout;
    QLocalSocket m_events;
    QTimer m_reconnect;
};
