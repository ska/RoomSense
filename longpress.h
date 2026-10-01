#ifndef LONGPRESS_H
#define LONGPRESS_H

#include <QObject>
#include <QTimer>

/*
 * Long press on a widget (mouse, or touch synthesized as mouse on the
 * panel): longPressed() after holdMs without releasing. Installed as an
 * event filter on the watched widget.
 */
class LongPress : public QObject
{
    Q_OBJECT
public:
    LongPress(QWidget *watched, int holdMs = 1000);

signals:
    void longPressed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QTimer m_timer;
};

#endif // LONGPRESS_H
