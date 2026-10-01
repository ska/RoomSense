#ifndef FAKEBROKER_H
#define FAKEBROKER_H

#include <QHash>
#include <QList>
#include <QMap>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

/*
 * Minimal MQTT 3.1.1 broker for the tests, in process: records CONNECT
 * (client id, keepalive, Will), PUBLISH and SUBSCRIBE, acknowledges QoS 1,
 * answers PINGREQ. deliver() publishes to the subscribed clients (QoS 0)
 * as another client would, keeping retained messages for later
 * subscriptions. stop() drops the clients and stops listening, listen() on
 * the same port brings it back.
 */
class FakeBroker
{
public:
    typedef struct {
        QString    clientId;
        int        keepAlive;
        QString    willTopic;
        QByteArray willMessage;
        int        willQos;
        bool       willRetain;
    } connect_t;

    typedef struct {
        QString    topic;
        QByteArray payload;
        int        qos;
        bool       retain;
    } publish_t;

    QList<connect_t> connects;
    QList<publish_t> publishes;
    QStringList      subscriptions;
    int              disconnects = 0;

    FakeBroker()
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, [this] {
            while(QTcpSocket *s = m_server.nextPendingConnection())
            {
                m_clients.append(s);
                QObject::connect(s, &QTcpSocket::readyRead, [this, s] { onReadyRead(s); });
                QObject::connect(s, &QTcpSocket::disconnected, [this, s] {
                    m_clients.removeAll(s);
                    m_buf.remove(s);
                    m_filters.remove(s);
                    s->deleteLater();
                });
            }
        });
    }

    ~FakeBroker() { stop(); }

    bool listen(quint16 port = 0)
    {
        return m_server.listen(QHostAddress::LocalHost, port);
    }

    quint16 port() const { return m_server.serverPort(); }

    void stop()
    {
        m_server.close();
        for(QTcpSocket *s : m_clients)
            s->abort();
    }

    /* As another client publishing: to the subscribed clients; retained
     * ones also to the later subscriptions */
    void deliver(const QString &topic, const QByteArray &payload, bool retain = false)
    {
        if(retain)
            m_retained.insert(topic, payload);
        for(QTcpSocket *s : m_clients)
            for(const QString &f : m_filters.value(s))
                if(match(f, topic)) { send(s, topic, payload, retain); break; }
    }

    /* Messages published on topic */
    QList<publish_t> on(const QString &topic) const
    {
        QList<publish_t> out;
        for(const publish_t &p : publishes)
            if(p.topic == topic)
                out.append(p);
        return out;
    }

private:
    static bool match(const QString &filter, const QString &topic)
    {
        const QStringList f = filter.split('/'), t = topic.split('/');
        for(int i = 0; i < f.size(); i++)
        {
            if(f.at(i) == "#")
                return true;
            if(i >= t.size() || (f.at(i) != "+" && f.at(i) != t.at(i)))
                return false;
        }
        return f.size() == t.size();
    }

    static QByteArray mstr(const QString &s)
    {
        const QByteArray b = s.toUtf8();
        return QByteArray(1, char(b.size() >> 8)) + QByteArray(1, char(b.size() & 0xFF)) + b;
    }

    static void send(QTcpSocket *s, const QString &topic, const QByteArray &payload, bool retain)
    {
        const QByteArray body = mstr(topic) + payload;
        QByteArray pkt(1, char(0x30 | (retain ? 1 : 0)));
        int n = body.size();
        do { quint8 b = n % 128; n /= 128; pkt.append(char(b | (n ? 0x80 : 0))); } while(n);
        s->write(pkt + body);
    }

    static quint16 u16(const QByteArray &b, int i)
    {
        return quint16(quint8(b.at(i)) << 8 | quint8(b.at(i + 1)));
    }

    static QByteArray str(const QByteArray &b, int &i)
    {
        const int n = u16(b, i);
        const QByteArray s = b.mid(i + 2, n);
        i += 2 + n;
        return s;
    }

    void onReadyRead(QTcpSocket *s)
    {
        QByteArray &buf = m_buf[s];
        buf.append(s->readAll());
        for(;;)
        {
            /* fixed header: type/flags, remaining length (1-4 bytes) */
            int len = 0, mult = 1, i = 1;
            bool complete = false;
            while(i < buf.size() && i <= 4)
            {
                const quint8 b = quint8(buf.at(i++));
                len += (b & 127) * mult;
                mult *= 128;
                if(!(b & 128)) { complete = true; break; }
            }
            if(!complete || buf.size() < i + len)
                return;

            const quint8 h = quint8(buf.at(0));
            const QByteArray body = buf.mid(i, len);
            buf.remove(0, i + len);
            handle(s, h, body);
        }
    }

    void handle(QTcpSocket *s, quint8 h, const QByteArray &body)
    {
        switch(h >> 4)
        {
        case 1: {                                       /* CONNECT */
            int i = 0;
            str(body, i);                               /* protocol name */
            i++;                                        /* level */
            const quint8 flags = quint8(body.at(i++));
            connect_t c;
            c.keepAlive = u16(body, i);
            i += 2;
            c.clientId = QString::fromUtf8(str(body, i));
            c.willQos = (flags >> 3) & 3;
            c.willRetain = flags & 0x20;
            if(flags & 0x04)
            {
                c.willTopic = QString::fromUtf8(str(body, i));
                c.willMessage = str(body, i);
            }
            connects.append(c);
            s->write(QByteArray::fromHex("20020000"));  /* CONNACK accepted */
            break;
        }
        case 3: {                                       /* PUBLISH */
            int i = 0;
            publish_t p;
            p.qos = (h >> 1) & 3;
            p.retain = h & 1;
            p.topic = QString::fromUtf8(str(body, i));
            if(p.qos)
            {
                s->write(QByteArray::fromHex("4002") + body.mid(i, 2));    /* PUBACK */
                i += 2;
            }
            p.payload = body.mid(i);
            publishes.append(p);
            break;
        }
        case 8: {                                       /* SUBSCRIBE */
            int i = 2;                                  /* after the packet id */
            QByteArray granted;
            while(i < body.size())
            {
                const QString f = QString::fromUtf8(str(body, i));
                i++;                                    /* requested QoS */
                subscriptions.append(f);
                m_filters[s].append(f);
                granted.append(char(0));
            }
            s->write(QByteArray(1, char(0x90)) + QByteArray(1, char(2 + granted.size())) + body.left(2) + granted);
            for(auto it = m_retained.constBegin(); it != m_retained.constEnd(); ++it)
                for(const QString &f : m_filters.value(s))
                    if(match(f, it.key())) { send(s, it.key(), it.value(), true); break; }
            break;
        }
        case 12:                                        /* PINGREQ */
            s->write(QByteArray::fromHex("d000"));
            break;
        case 14:                                        /* DISCONNECT */
            disconnects++;
            break;
        default:
            break;
        }
    }

    QTcpServer                      m_server;
    QList<QTcpSocket *>             m_clients;
    QHash<QTcpSocket *, QByteArray> m_buf;
    QHash<QTcpSocket *, QStringList> m_filters;
    QMap<QString, QByteArray>       m_retained;
};

#endif // FAKEBROKER_H
