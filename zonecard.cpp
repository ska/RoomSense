#include "zonecard.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVariant>
#include <QVBoxLayout>

void setStyleProperty(QWidget *w, const char *name, const QString &value)
{
    if(w->property(name).toString() == value)
        return;
    w->setProperty(name, value);
    w->style()->unpolish(w);
    w->style()->polish(w);
}

static QLabel *label(const char *objectName, QWidget *parent)
{
    QLabel *l = new QLabel(parent);
    l->setObjectName(objectName);
    return l;
}

ZoneCard::ZoneCard(const QString &name, QWidget *parent) : QFrame(parent)
{
    setObjectName("zoneCard");

    m_name = label("zoneName", this);
    m_name->setText(name);
    m_heat = label("heatLabel", this);
    m_temp = label("tempLabel", this);
    m_setPoint = label("setPointValue", this);
    m_setPoint->setAlignment(Qt::AlignCenter);
    m_setPointLabel = label("setPointLabel", this);
    m_setPointLabel->setAlignment(Qt::AlignCenter);
    m_hum = label("infoLabel", this);
    m_batt = label("battLabel", this);
    m_status = label("statusLabel", this);
    m_status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_minus = new QPushButton(QString::fromUtf8("−"), this);
    m_plus = new QPushButton("+", this);
    for(QPushButton *b : { m_minus, m_plus })
    {
        b->setObjectName("stepButton");
        b->setFixedSize(64, 64);
        b->setFocusPolicy(Qt::NoFocus);
    }
    m_minus->setAccessibleName("Lower the setpoint");
    m_plus->setAccessibleName("Raise the setpoint");
    connect(m_minus, &QPushButton::clicked, this, &ZoneCard::minusClicked);
    connect(m_plus, &QPushButton::clicked, this, &ZoneCard::plusClicked);

    QHBoxLayout *top = new QHBoxLayout;
    top->addWidget(m_name);
    top->addStretch();
    top->addWidget(m_heat);

    QVBoxLayout *sp = new QVBoxLayout;
    sp->setSpacing(0);
    sp->addWidget(m_setPoint);
    sp->addWidget(m_setPointLabel);
    QWidget *spBox = new QWidget(this);
    spBox->setLayout(sp);
    spBox->setFixedWidth(120);          /* "windows open" under the value */

    QHBoxLayout *middle = new QHBoxLayout;
    middle->addWidget(m_temp);
    middle->addStretch();
    middle->addWidget(m_minus);
    middle->addWidget(spBox);
    middle->addWidget(m_plus);

    QHBoxLayout *bottom = new QHBoxLayout;
    bottom->setSpacing(18);
    bottom->addWidget(m_hum);
    bottom->addWidget(m_batt);
    bottom->addStretch();
    bottom->addWidget(m_status);

    QVBoxLayout *v = new QVBoxLayout(this);
    v->setContentsMargins(18, 10, 18, 10);
    v->setSpacing(6);
    v->addLayout(top);
    v->addLayout(middle);
    v->addLayout(bottom);
}

void ZoneCard::display(const ZoneView::view_t &v, bool canChange)
{
    static const char *levels[] = { "ok", "stale", "lost", "never" };
    static const char *heats[] = { "unknown", "on", "idle", "nosensor" };
    static const char *setPoints[] = { "confirmed", "pending", "lastknown", "none", "nozone", "inmode", "inboost" };

    m_temp->setText(v.tempText);
    m_heat->setText(v.heatText);
    m_setPoint->setText(v.setPointText);
    m_setPointLabel->setText(v.setPointLabel);
    m_hum->setText("Hum " + v.humText);
    m_batt->setText("Batt " + v.battText);
    m_status->setText(v.statusText);
    m_minus->setEnabled(canChange);
    m_plus->setEnabled(canChange);

    setStyleProperty(this, "level", levels[v.level]);
    setStyleProperty(m_status, "level", levels[v.level]);
    setStyleProperty(m_temp, "level", levels[v.level]);
    setStyleProperty(m_heat, "heat", heats[v.heat]);
    setStyleProperty(m_setPoint, "setpoint", setPoints[v.setPoint]);
    setStyleProperty(m_setPointLabel, "setpoint", setPoints[v.setPoint]);
    setStyleProperty(m_batt, "low", v.battLow ? "true" : "false");
}
