#include "pimio/settings/settings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace pimio::settings {

namespace {

// Configuration keys. These are part of the file format: renaming one makes
// an existing configuration fall back to the default for that setting.
constexpr auto kTileSizeKey = "view/tileSize";
constexpr auto kSortKeyKey = "view/sortKey";
constexpr auto kSortDescendingKey = "view/sortDescending";
constexpr auto kScrollSpeedKey = "input/scrollSpeed";
constexpr auto kScrollAccelerationKey = "input/scrollAcceleration";
constexpr auto kKeyRepeatAccelerationKey = "input/keyRepeatAcceleration";

constexpr int kDefaultTileSize = 176;
constexpr int kMinimumTileSize = 96;
// The largest tile the grid offers. Bounded by the largest thumbnail tier the
// renderer produces (see browser::MediaLibraryModel::thumbnailSizeForTile and
// docs/decisions/0003-settings-and-view-controls.md): beyond this a tile would
// be filled by upscaling an image that was never rendered that large.
constexpr int kMaximumTileSize = 256;

constexpr qreal kDefaultScrollSpeed = 2.0;
constexpr qreal kMinimumScrollSpeed = 0.25;
constexpr qreal kMaximumScrollSpeed = 8.0;

constexpr bool kDefaultSortDescending = false;
constexpr bool kDefaultScrollAcceleration = true;
constexpr bool kDefaultKeyRepeatAcceleration = true;
constexpr bool kDefaultShowTileDiagnostics = false;

int clampTileSize(int size)
{
    return std::clamp(size, kMinimumTileSize, kMaximumTileSize);
}

qreal clampScrollSpeed(qreal speed)
{
    if (!std::isfinite(speed)) {
        return kDefaultScrollSpeed;
    }
    return std::clamp(speed, kMinimumScrollSpeed, kMaximumScrollSpeed);
}

/// Reads a bool that may have been hand-edited into any of QSettings' accepted
/// spellings, falling back to \a fallback for anything else.
bool readBool(const QSettings &store, const char *key, bool fallback)
{
    const QVariant value = store.value(QLatin1String(key));
    if (!value.isValid()) {
        return fallback;
    }
    const QString text = value.toString().trimmed().toLower();
    if (text == QLatin1String("true") || text == QLatin1String("1")) {
        return true;
    }
    if (text == QLatin1String("false") || text == QLatin1String("0")) {
        return false;
    }
    return fallback;
}

int readInt(const QSettings &store, const char *key, int fallback)
{
    bool ok = false;
    const int value = store.value(QLatin1String(key)).toString().toInt(&ok);
    return ok ? value : fallback;
}

qreal readReal(const QSettings &store, const char *key, qreal fallback)
{
    bool ok = false;
    const double value = store.value(QLatin1String(key)).toString().toDouble(&ok);
    return ok ? value : fallback;
}

} // namespace

QString toString(SortKey key)
{
    switch (key) {
    case SortKey::CaptureTime:
        return QStringLiteral("captureTime");
    case SortKey::FileName:
        return QStringLiteral("fileName");
    case SortKey::FileDate:
        return QStringLiteral("fileDate");
    case SortKey::FileType:
        return QStringLiteral("fileType");
    case SortKey::FileSize:
        return QStringLiteral("fileSize");
    }
    return QStringLiteral("captureTime");
}

std::optional<SortKey> sortKeyFromString(const QString &text)
{
    for (const SortKey key : allSortKeys()) {
        if (toString(key) == text) {
            return key;
        }
    }
    return std::nullopt;
}

QList<SortKey> allSortKeys()
{
    return {
        SortKey::CaptureTime, SortKey::FileName, SortKey::FileDate,
        SortKey::FileType,    SortKey::FileSize,
    };
}

QString sortKeyLabel(SortKey key)
{
    switch (key) {
    case SortKey::CaptureTime:
        return QObject::tr("Date taken");
    case SortKey::FileName:
        return QObject::tr("File name");
    case SortKey::FileDate:
        return QObject::tr("File date");
    case SortKey::FileType:
        return QObject::tr("File type");
    case SortKey::FileSize:
        return QObject::tr("File size");
    }
    return QObject::tr("Date taken");
}

class Settings::Private
{
public:
    explicit Private(const QString &filePath)
        : store(filePath.isEmpty() ? Settings::defaultFilePath() : filePath,
                QSettings::IniFormat)
    {
    }

    QSettings store;

    // Stored.
    int tileSize = kDefaultTileSize;
    SortKey sortKey = SortKey::CaptureTime;
    bool sortDescending = kDefaultSortDescending;
    qreal scrollSpeed = kDefaultScrollSpeed;
    bool scrollAcceleration = kDefaultScrollAcceleration;
    bool keyRepeatAcceleration = kDefaultKeyRepeatAcceleration;

    // Session.
    bool showTileDiagnostics = kDefaultShowTileDiagnostics;
};

Settings::Settings(const QString &filePath, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(filePath))
{
    // The directory may not exist yet on a first run; QSettings creates the
    // file lazily but not the directory tree above it.
    QDir().mkpath(QFileInfo(d->store.fileName()).absolutePath());
    reload();
}

Settings::~Settings() = default;

QString Settings::filePath() const
{
    return d->store.fileName();
}

QString Settings::defaultFilePath()
{
    const QString directory =
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(directory).filePath(QStringLiteral("pimio.conf"));
}

int Settings::tileSize() const
{
    return d->tileSize;
}

void Settings::setTileSize(int size)
{
    const int clamped = clampTileSize(size);
    if (clamped == d->tileSize) {
        return;
    }
    d->tileSize = clamped;
    d->store.setValue(QLatin1String(kTileSizeKey), clamped);
    emit tileSizeChanged();
}

int Settings::minimumTileSize()
{
    return kMinimumTileSize;
}

int Settings::maximumTileSize()
{
    return kMaximumTileSize;
}

SortKey Settings::sortKey() const
{
    return d->sortKey;
}

void Settings::setSortKey(SortKey key)
{
    if (key == d->sortKey) {
        return;
    }
    d->sortKey = key;
    d->store.setValue(QLatin1String(kSortKeyKey), toString(key));
    emit sortKeyChanged();
}

int Settings::sortKeyValue() const
{
    return static_cast<int>(d->sortKey);
}

void Settings::setSortKeyValue(int key)
{
    const QList<SortKey> keys = allSortKeys();
    const auto match = std::find_if(keys.cbegin(), keys.cend(), [key](SortKey candidate) {
        return static_cast<int>(candidate) == key;
    });
    if (match == keys.cend()) {
        return;
    }
    setSortKey(*match);
}

bool Settings::sortDescending() const
{
    return d->sortDescending;
}

void Settings::setSortDescending(bool descending)
{
    if (descending == d->sortDescending) {
        return;
    }
    d->sortDescending = descending;
    d->store.setValue(QLatin1String(kSortDescendingKey), descending);
    emit sortDescendingChanged();
}

qreal Settings::scrollSpeed() const
{
    return d->scrollSpeed;
}

void Settings::setScrollSpeed(qreal speed)
{
    const qreal clamped = clampScrollSpeed(speed);
    if (qFuzzyCompare(clamped, d->scrollSpeed)) {
        return;
    }
    d->scrollSpeed = clamped;
    d->store.setValue(QLatin1String(kScrollSpeedKey), clamped);
    emit scrollSpeedChanged();
}

qreal Settings::minimumScrollSpeed()
{
    return kMinimumScrollSpeed;
}

qreal Settings::maximumScrollSpeed()
{
    return kMaximumScrollSpeed;
}

bool Settings::scrollAcceleration() const
{
    return d->scrollAcceleration;
}

void Settings::setScrollAcceleration(bool enabled)
{
    if (enabled == d->scrollAcceleration) {
        return;
    }
    d->scrollAcceleration = enabled;
    d->store.setValue(QLatin1String(kScrollAccelerationKey), enabled);
    emit scrollAccelerationChanged();
}

bool Settings::keyRepeatAcceleration() const
{
    return d->keyRepeatAcceleration;
}

void Settings::setKeyRepeatAcceleration(bool enabled)
{
    if (enabled == d->keyRepeatAcceleration) {
        return;
    }
    d->keyRepeatAcceleration = enabled;
    d->store.setValue(QLatin1String(kKeyRepeatAccelerationKey), enabled);
    emit keyRepeatAccelerationChanged();
}

bool Settings::showTileDiagnostics() const
{
    return d->showTileDiagnostics;
}

void Settings::setShowTileDiagnostics(bool enabled)
{
    if (enabled == d->showTileDiagnostics) {
        return;
    }
    d->showTileDiagnostics = enabled;
    emit showTileDiagnosticsChanged();
}

void Settings::resetToDefaults()
{
    setTileSize(kDefaultTileSize);
    setSortKey(SortKey::CaptureTime);
    setSortDescending(kDefaultSortDescending);
    setScrollSpeed(kDefaultScrollSpeed);
    setScrollAcceleration(kDefaultScrollAcceleration);
    setKeyRepeatAcceleration(kDefaultKeyRepeatAcceleration);
    setShowTileDiagnostics(kDefaultShowTileDiagnostics);

    // The setters above only write the values that changed, so a file holding
    // an out-of-range value for a setting already at its default would keep
    // it. Write every stored value explicitly instead.
    d->store.setValue(QLatin1String(kTileSizeKey), d->tileSize);
    d->store.setValue(QLatin1String(kSortKeyKey), toString(d->sortKey));
    d->store.setValue(QLatin1String(kSortDescendingKey), d->sortDescending);
    d->store.setValue(QLatin1String(kScrollSpeedKey), d->scrollSpeed);
    d->store.setValue(QLatin1String(kScrollAccelerationKey), d->scrollAcceleration);
    d->store.setValue(QLatin1String(kKeyRepeatAccelerationKey), d->keyRepeatAcceleration);
    flush();
}

void Settings::flush()
{
    d->store.sync();
}

void Settings::reload()
{
    d->store.sync();

    const int tile = clampTileSize(readInt(d->store, kTileSizeKey, kDefaultTileSize));
    if (tile != d->tileSize) {
        d->tileSize = tile;
        emit tileSizeChanged();
    }

    const SortKey sort =
            sortKeyFromString(d->store.value(QLatin1String(kSortKeyKey)).toString())
                    .value_or(SortKey::CaptureTime);
    if (sort != d->sortKey) {
        d->sortKey = sort;
        emit sortKeyChanged();
    }

    const bool descending = readBool(d->store, kSortDescendingKey, kDefaultSortDescending);
    if (descending != d->sortDescending) {
        d->sortDescending = descending;
        emit sortDescendingChanged();
    }

    const qreal speed = clampScrollSpeed(readReal(d->store, kScrollSpeedKey,
                                                  kDefaultScrollSpeed));
    if (!qFuzzyCompare(speed, d->scrollSpeed)) {
        d->scrollSpeed = speed;
        emit scrollSpeedChanged();
    }

    const bool wheelAcceleration =
            readBool(d->store, kScrollAccelerationKey, kDefaultScrollAcceleration);
    if (wheelAcceleration != d->scrollAcceleration) {
        d->scrollAcceleration = wheelAcceleration;
        emit scrollAccelerationChanged();
    }

    const bool keyAcceleration =
            readBool(d->store, kKeyRepeatAccelerationKey, kDefaultKeyRepeatAcceleration);
    if (keyAcceleration != d->keyRepeatAcceleration) {
        d->keyRepeatAcceleration = keyAcceleration;
        emit keyRepeatAccelerationChanged();
    }
}

QList<int> Settings::sortKeyValues()
{
    QList<int> values;
    for (const SortKey key : allSortKeys()) {
        values.append(static_cast<int>(key));
    }
    return values;
}

QString Settings::sortKeyLabelFor(int key)
{
    for (const SortKey candidate : allSortKeys()) {
        if (static_cast<int>(candidate) == key) {
            return sortKeyLabel(candidate);
        }
    }
    return {};
}

} // namespace pimio::settings
