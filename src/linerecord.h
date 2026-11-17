#ifndef LINERECORD_H
#define LINERECORD_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

/// Which way a record travelled.
///
/// SYS covers everything UartX itself has to say about the session -- connect,
/// disconnect, reconnect, log trouble -- so those events sit in the same
/// numbered sequence as the data they interrupted.
enum class Direction : quint8 { Rx, Tx, Sys };

/// Flags carried by a record. They describe how the bytes were cut out of the
/// stream, never what they mean.
namespace LineFlag {
enum : quint8 {
    /// Forced out by the quiet timer before a terminator arrived. The rest of
    /// the line, if any ever comes, follows as a Continuation record.
    Partial      = 0x01,
    /// The remainder of a line that was previously flushed as Partial.
    Continuation = 0x02,
    /// Cut at the maximum line length because no terminator arrived in time.
    /// Binary streams and runaway output produce these.
    Split        = 0x04,
    /// Transmitted keystrokes typed straight into the terminal, collected up
    /// to Enter. Not drawn again: local echo already showed them.
    Typed        = 0x08,
};
}

/// One unit of the session, in the form every consumer shares: the log, the
/// recent-lines ring, the matcher, and (later) the agent API.
///
/// `seq` is the only stable way to refer to a record. It is per session, never
/// reset -- not by Clear, not by a display-mode change, not by a reconnect --
/// and it is assigned on the pipeline thread in arrival order.
///
/// The bytes are kept exactly as they arrived. Decoding is a view of them, so
/// a change of character encoding never needs the data again.
struct LineRecord
{
    quint64    seq    = 0;
    qint64     wallMs = 0;     ///< host receive time, ms since the epoch -- display only
    qint64     monoNs = 0;     ///< steady clock at receive; ordering and intervals use this
    Direction  dir    = Direction::Rx;
    quint8     flags  = 0;
    /// Who produced it: "device", "user", "agent:<id>" or "system".
    QString    origin;
    QByteArray bytes;          ///< RX/TX payload, or the UTF-8 text of a SYS message

    bool has(quint8 flag) const { return (flags & flag) != 0; }
};

namespace Origin {
inline const QString Device = QStringLiteral("device");
inline const QString User   = QStringLiteral("user");
inline const QString System = QStringLiteral("system");
}

#endif // LINERECORD_H
