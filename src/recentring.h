#ifndef RECENTRING_H
#define RECENTRING_H

#include "linerecord.h"

#include <QMutex>
#include <QVector>

/// The most recent records of a session, in memory, for anything that needs
/// to look back without touching the disk: rebuilding a view, backfilling a
/// new filter, and (from Phase 2) answering bounded reads from an agent.
///
/// Written by the pipeline thread, readable from any thread. The lock is held
/// only for the copy, never across any work on the records.
class RecentRing
{
public:
    explicit RecentRing(int capacity = 50000)
        : m_capacity(qMax(1, capacity))
    {
        m_items.reserve(qMin(m_capacity, 4096));
    }

    void append(const LineRecord &record)
    {
        QMutexLocker lock(&m_mutex);
        if (m_items.size() < m_capacity) {
            m_items.append(record);
        } else {
            m_items[m_head] = record;
            m_head = (m_head + 1) % m_capacity;
        }
    }

    int size() const
    {
        QMutexLocker lock(&m_mutex);
        return m_items.size();
    }

    int capacity() const { return m_capacity; }

    /// Up to `maxCount` records with seq >= fromSeq, oldest first.
    ///
    /// Seq numbers within a session are consecutive, so the start is found by
    /// arithmetic rather than a scan: a poll for "anything new" costs the same
    /// whether the ring holds ten records or fifty thousand.
    QVector<LineRecord> since(quint64 fromSeq, int maxCount) const
    {
        QMutexLocker lock(&m_mutex);
        QVector<LineRecord> out;
        const int n = m_items.size();
        if (n == 0 || maxCount <= 0)
            return out;
        const quint64 oldest = m_items.at(m_head % n).seq;
        int start = 0;
        if (fromSeq > oldest)
            start = int(qMin<quint64>(fromSeq - oldest, quint64(n)));
        out.reserve(qMin(maxCount, n - start));
        for (int i = start; i < n && out.size() < maxCount; ++i)
            out.append(m_items.at((m_head + i) % n));
        return out;
    }

    /// Highest seq held, or 0 when empty.
    quint64 lastSeq() const
    {
        QMutexLocker lock(&m_mutex);
        const int n = m_items.size();
        return n ? m_items.at((m_head + n - 1) % n).seq : 0;
    }

    /// Lowest seq held, or 0 when empty.
    quint64 firstSeq() const
    {
        QMutexLocker lock(&m_mutex);
        const int n = m_items.size();
        return n ? m_items.at(m_head % n).seq : 0;
    }

    /// The newest `count` records, oldest first.
    QVector<LineRecord> tail(int count) const
    {
        QMutexLocker lock(&m_mutex);
        QVector<LineRecord> out;
        const int n = m_items.size();
        const int take = qMin(count, n);
        out.reserve(take);
        for (int i = n - take; i < n; ++i)
            out.append(m_items.at((m_head + i) % n));
        return out;
    }

    void clear()
    {
        QMutexLocker lock(&m_mutex);
        m_items.clear();
        m_head = 0;
    }

private:
    mutable QMutex      m_mutex;
    QVector<LineRecord> m_items;
    int                 m_head = 0;     ///< index of the oldest item once full
    int                 m_capacity;
};

#endif // RECENTRING_H
