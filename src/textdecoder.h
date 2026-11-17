#ifndef TEXTDECODER_H
#define TEXTDECODER_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <memory>

class QStringDecoder;

/// Turns a record's bytes into text for the screen and for matching.
///
/// Decoding is a view: the bytes are stored untouched, so choosing another
/// encoding never loses anything. The default, Auto, assumes nothing about
/// the device -- valid UTF-8 (which includes plain ASCII) is shown as text,
/// and any byte that is not part of a valid sequence is shown as ‹xNN› in
/// place, so binary noise and a wrong baud rate are visible as what they are.
class TextDecoder
{
public:
    TextDecoder();
    ~TextDecoder();
    TextDecoder(const TextDecoder &other);
    TextDecoder &operator=(const TextDecoder &other);

    /// Every encoding this build can decode, Auto first. Legacy multi-byte
    /// codecs (GBK, Shift-JIS...) appear only where Qt was built with support
    /// for them, which differs per platform.
    static QStringList available();

    /// Selects an encoding by name. An unknown or unavailable name falls back
    /// to Auto and returns false.
    bool setEncoding(const QString &name);
    QString encoding() const { return m_name; }

    QString decode(const QByteArray &bytes) const;

    /// The bytes to transmit for text typed by the user, in the same encoding
    /// the session reads with, so what is typed round-trips. Auto and UTF-8
    /// send UTF-8.
    QByteArray encode(const QString &text) const;

    /// The one-line, tab-free, plain-UTF-8 form written to lines.log: valid
    /// printable UTF-8 is kept, a backslash is doubled, CR LF TAB become \r \n
    /// \t, and every other control or invalid byte becomes \xNN. Independent
    /// of the display encoding, so lines.log reads the same whatever the user
    /// picked, and grep works on it.
    static QString escape(const QByteArray &bytes);

    /// Auto decoding as a free function, for callers without a decoder.
    static QString decodeAuto(const QByteArray &bytes);

private:
    enum class Kind { Auto, Utf8, Latin1, Windows1252, Named };

    Kind    m_kind = Kind::Auto;
    QString m_name = QStringLiteral("Auto");
    std::unique_ptr<QStringDecoder> m_named;   ///< Kind::Named only
};

#endif // TEXTDECODER_H
