#ifndef DAYCHART_H
#define DAYCHART_H

#include <QColor>
#include <QVector>
#include <QWidget>

/*
 * The setpoints of one day as bars (one per interval of
 * Chrono::dayTemps, 24 h across), equal neighbours merged. Colors from
 * the stylesheet (qproperty-*): warm from 20 °C, mild from 18 °C, cool
 * below, all off-colored when the chrono is off. Optional: the time now
 * (a line), the setpoint written in the wide bars, a selected interval
 * (the editor) drawn with a frame, the others dimmed.
 */
class DayChart : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QColor warmColor MEMBER m_warm DESIGNABLE true)
    Q_PROPERTY(QColor mildColor MEMBER m_mild DESIGNABLE true)
    Q_PROPERTY(QColor coolColor MEMBER m_cool DESIGNABLE true)
    Q_PROPERTY(QColor offColor MEMBER m_off DESIGNABLE true)
    Q_PROPERTY(QColor lineColor MEMBER m_line DESIGNABLE true)
    Q_PROPERTY(QColor labelColor MEMBER m_label DESIGNABLE true)

public:
    explicit DayChart(QWidget *parent = nullptr);

    /* temps: from 00:00, equal intervals (96 = 15 min) */
    void setTemps(const QVector<double> &temps, bool on);
    /* minute of the day, -1: no line */
    void setNow(int minute);
    void setShowValues(bool show);
    /* the editor: minutes [from, to), from < 0: none */
    void setSelection(int from, int to);

    const QVector<double> &temps() const { return m_temps; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor color(double temp) const;

    QVector<double> m_temps;
    bool    m_on = true;
    int     m_now = -1;
    bool    m_values = false;
    int     m_selFrom = -1;
    int     m_selTo = -1;

    QColor  m_warm = QColor("#F28C28");
    QColor  m_mild = QColor("#D9A441");
    QColor  m_cool = QColor("#5B8FD6");
    QColor  m_off = QColor("#4A525B");
    QColor  m_line = QColor("#F3F4F5");
    QColor  m_label = QColor("#0E1012");
};

#endif // DAYCHART_H
