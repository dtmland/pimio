#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <memory>
#include <optional>

namespace pimio::settings {

/// How a media list is ordered.
///
/// Declared here rather than in the projection so the choice can be stored,
/// restored, and shown in the UI without the settings component depending on
/// SQL. The projection maps it to an ORDER BY clause.
enum class SortKey {
    CaptureTime = 0, ///< Metadata capture time: the chronological default.
    FileName,        ///< File name, case-insensitive.
    FileDate,        ///< Filesystem last-modified time.
    FileType,        ///< File extension, then file name.
    FileSize,        ///< Size in bytes.
};

/// Stable configuration-file spelling of \a key, so reordering the enum can
/// never silently change what an already-saved file means.
QString toString(SortKey key);

/// Parses a value written by toString(). Returns nothing for anything else,
/// including a value written by a newer build.
std::optional<SortKey> sortKeyFromString(const QString &text);

/// Every sort key, in the order they are presented to the user.
QList<SortKey> allSortKeys();

/// Human-readable name of \a key, for menus and dropdowns.
QString sortKeyLabel(SortKey key);

/// User-facing settings, split into two lifetimes.
///
/// **Stored settings** are preferences: how the user wants pimio to work.
/// They are written to a per-user configuration file and restored on the next
/// launch. They are application-wide rather than per library root: a user who
/// prefers large tiles prefers them in every library, and a library folder is
/// not the place to keep the user's preferences.
///
/// **Session settings** last only as long as the process. They describe what
/// the user wants *right now*, usually while diagnosing something, and
/// deliberately return to their default on the next launch so that a testing
/// aid left switched on cannot quietly become how the application behaves.
///
/// Every setting is a Q_PROPERTY, so QML binds to it directly and C++ and
/// tests use the same accessors. Values are validated both when they are set
/// and when they are read back from the file: an out-of-range or unparsable
/// stored value falls back to the default instead of reaching the UI.
///
/// This object is not thread-safe; it is meant to live on the GUI thread.
class Settings : public QObject
{
    Q_OBJECT

    // Stored preferences.
    Q_PROPERTY(int tileSize READ tileSize WRITE setTileSize NOTIFY tileSizeChanged)
    Q_PROPERTY(int minimumTileSize READ minimumTileSize CONSTANT)
    Q_PROPERTY(int maximumTileSize READ maximumTileSize CONSTANT)
    Q_PROPERTY(int sortKey READ sortKeyValue WRITE setSortKeyValue NOTIFY sortKeyChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending
                       NOTIFY sortDescendingChanged)
    Q_PROPERTY(qreal scrollSpeed READ scrollSpeed WRITE setScrollSpeed NOTIFY scrollSpeedChanged)
    Q_PROPERTY(qreal minimumScrollSpeed READ minimumScrollSpeed CONSTANT)
    Q_PROPERTY(qreal maximumScrollSpeed READ maximumScrollSpeed CONSTANT)
    Q_PROPERTY(bool scrollAcceleration READ scrollAcceleration WRITE setScrollAcceleration
                       NOTIFY scrollAccelerationChanged)
    Q_PROPERTY(bool keyRepeatAcceleration READ keyRepeatAcceleration
                       WRITE setKeyRepeatAcceleration NOTIFY keyRepeatAccelerationChanged)
    Q_PROPERTY(int scanBatchSize READ scanBatchSize WRITE setScanBatchSize
                       NOTIFY scanBatchSizeChanged)
    Q_PROPERTY(int minimumScanBatchSize READ minimumScanBatchSize CONSTANT)
    Q_PROPERTY(int maximumScanBatchSize READ maximumScanBatchSize CONSTANT)

    // Session settings.
    Q_PROPERTY(bool showTileDiagnostics READ showTileDiagnostics WRITE setShowTileDiagnostics
                       NOTIFY showTileDiagnosticsChanged)

public:
    /// Constructs settings backed by \a filePath.
    ///
    /// An empty \a filePath uses the per-user configuration location. Tests
    /// pass an explicit path (or enable QStandardPaths test mode) so they
    /// never touch the running user's configuration. Stored values are read
    /// immediately, so the object is usable as soon as it is constructed.
    explicit Settings(const QString &filePath = {}, QObject *parent = nullptr);
    ~Settings() override;

    Settings(const Settings &) = delete;
    Settings &operator=(const Settings &) = delete;

    /// The configuration file stored settings are read from and written to.
    QString filePath() const;

    /// Default configuration file path for this user. Honours
    /// QStandardPaths::setTestModeEnabled().
    static QString defaultFilePath();

    // ---- Stored: view -----------------------------------------------------

    /// Edge length of a grid tile in device-independent pixels.
    int tileSize() const;
    void setTileSize(int size);
    static int minimumTileSize();
    static int maximumTileSize();

    SortKey sortKey() const;
    void setSortKey(SortKey key);

    /// True when the sort runs newest, largest, or last first.
    bool sortDescending() const;
    void setSortDescending(bool descending);

    // ---- Stored: input ----------------------------------------------------

    /// Multiplier applied to each wheel step. 1.0 is the platform's own step.
    qreal scrollSpeed() const;
    void setScrollSpeed(qreal speed);
    static qreal minimumScrollSpeed();
    static qreal maximumScrollSpeed();

    /// Whether a sustained wheel gesture scrolls progressively further.
    bool scrollAcceleration() const;
    void setScrollAcceleration(bool enabled);

    /// Whether a held navigation key jumps progressively further.
    bool keyRepeatAcceleration() const;
    void setKeyRepeatAcceleration(bool enabled);

    // ---- Stored: indexing -------------------------------------------------

    /// How many files an initial scan indexes before the ones it has found so
    /// far appear in the grid.
    ///
    /// This is the responsiveness/overhead dial the grid fills in from: each
    /// batch costs one durable commit and one projection write, so a small
    /// batch shows pictures sooner and spends more of the scan committing,
    /// while a large one scans faster and leaves the window emptier for
    /// longer. It is a preference rather than a tuning constant because the
    /// right answer depends on the library and the disk it is on.
    int scanBatchSize() const;
    void setScanBatchSize(int records);
    static int minimumScanBatchSize();
    static int maximumScanBatchSize();

    // ---- Session ----------------------------------------------------------

    /// Overlays each tile with its row index and thumbnail state. A testing
    /// aid, and deliberately not stored, so it is never on by surprise.
    bool showTileDiagnostics() const;
    void setShowTileDiagnostics(bool enabled);

    // ---- Lifecycle --------------------------------------------------------

    /// Restores every setting, stored and session, to its default and writes
    /// the defaults to the configuration file.
    Q_INVOKABLE void resetToDefaults();

    /// Writes pending stored values to disk. Values are written as they
    /// change; this only forces the write to complete now.
    Q_INVOKABLE void flush();

    /// Reloads stored values from the configuration file, emitting a change
    /// signal for every value that differs. Session settings are untouched.
    Q_INVOKABLE void reload();

    /// Sort keys as integers, in presentation order, for QML models.
    Q_INVOKABLE static QList<int> sortKeyValues();

    /// Human-readable name of the sort key with integer value \a key.
    Q_INVOKABLE static QString sortKeyLabelFor(int key);

    int sortKeyValue() const;
    void setSortKeyValue(int key);

signals:
    void tileSizeChanged();
    void sortKeyChanged();
    void sortDescendingChanged();
    void scrollSpeedChanged();
    void scrollAccelerationChanged();
    void keyRepeatAccelerationChanged();
    void scanBatchSizeChanged();
    void showTileDiagnosticsChanged();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace pimio::settings
