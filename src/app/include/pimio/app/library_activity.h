#pragma once

#include <QObject>
#include <QString>

namespace pimio::app {

/// What the library is doing right now, for the parts of the UI that have to
/// say so.
///
/// Indexing a large library takes long enough that a window showing nothing
/// looks like a window that has crashed. This object is the one place that
/// knows a scan is running and how much of it has been indexed, so the view
/// can show an activity indicator and a count instead of an empty grid and no
/// explanation.
///
/// It is deliberately a plain observable value holder: it starts nothing,
/// owns nothing, and is written only by LibrarySession on the GUI thread.
class LibraryActivity : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool scanning READ isScanning NOTIFY scanningChanged)
    Q_PROPERTY(int indexedCount READ indexedCount NOTIFY indexedCountChanged)

public:
    explicit LibraryActivity(QObject *parent = nullptr);
    ~LibraryActivity() override;

    /// True while at least one scan or reconcile job is running.
    bool isScanning() const;
    void setScanning(bool scanning);

    /// Number of files the running scan has indexed so far. Reset to zero
    /// when a scan starts, so it always describes the scan being shown.
    int indexedCount() const;
    void setIndexedCount(int count);

signals:
    void scanningChanged();
    void indexedCountChanged();

private:
    bool m_scanning = false;
    int m_indexedCount = 0;
};

} // namespace pimio::app
