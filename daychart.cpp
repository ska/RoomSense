#include "daychart.h"
#include <QPainter>
#include <algorithm>

static const int DAY_MIN = 24 * 60;

DayChart::DayChart(QWidget *parent) : QWidget(parent)
{
    setObjectName("dayChart");
    setAttribute(Qt::WA_StyledBackground, false);
}

void DayChart::setTemps(const QVector<double> &temps, bool on)
{
    m_temps = temps;
    m_on = on;
    update();
}

void DayChart::setNow(int minute)
{
    m_now = minute;
    update();
}

void DayChart::setShowValues(bool show)
{
    m_values = show;
    update();
}

void DayChart::setSelection(int from, int to)
{
    m_selFrom = from;
    m_selTo = to;
    update();
}

QColor DayChart::color(double temp) const
{
    if(!m_on)
        return m_off;
    if(temp >= 20)
        return m_warm;
    if(temp >= 18)
        return m_mild;
    return m_cool;
}

void DayChart::paintEvent(QPaintEvent *)
{
    if(m_temps.isEmpty())
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    /* scale: 12..24 degC, wider when the values need it */
    const auto mm = std::minmax_element(m_temps.cbegin(), m_temps.cend());
    const double lo = qMin(12.0, *mm.first - 1);
    const double hi = qMax(24.0, *mm.second);
    const double w = width(), h = height();
    const int n = m_temps.size();
    const double stepMin = double(DAY_MIN) / n;

    QFont f = font();
    f.setBold(true);
    f.setPixelSize(qBound(11, int(h / 9), 18));
    p.setFont(f);

    /* runs of equal values */
    for(int i = 0; i < n; )
    {
        int j = i;
        while(j < n && m_temps.at(j) == m_temps.at(i))
            j++;
        const double x0 = w * i / n, x1 = w * j / n;
        const double t = m_temps.at(i);
        const double bh = qMax(4.0, h * (t - lo) / (hi - lo));
        QRectF r(x0 + 1, h - bh, x1 - x0 - 2, bh);

        const int from = int(i * stepMin), to = int(j * stepMin);
        const bool selected = m_selFrom >= 0 && from < m_selTo && to > m_selFrom;
        QColor c = color(t);
        if(m_selFrom >= 0 && !selected)
            c.setAlphaF(0.4);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(r, 3, 3);
        if(selected)
        {
            p.setPen(QPen(m_line, 3));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(1.5, 1.5, -1.5, 0), 3, 3);
        }
        if(m_values && r.width() > 44 && r.height() > f.pixelSize() + 8)
        {
            p.setPen(m_label);
            p.drawText(r.adjusted(0, 4, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
                       QString::number(t, 'f', t == int(t) ? 0 : 1));
        }
        i = j;
    }

    if(m_now >= 0 && m_now < DAY_MIN)
    {
        const double x = w * m_now / DAY_MIN;
        p.setPen(QPen(m_line, 2));
        p.drawLine(QPointF(x, 0), QPointF(x, h));
    }
}
