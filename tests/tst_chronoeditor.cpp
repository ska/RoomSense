#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include "chronoeditor.h"
#include "daychart.h"

static const char *PROGRAM =
    R"({"edited":true,"enabled":true,"holiday":[{"at":"08:00","temp":20.5},{"at":"23:00","temp":17}],)"
    R"("holiday_days":[6,7],"weekday":[{"at":"06:30","temp":20.5},{"at":"08:00","temp":18},)"
    R"({"at":"17:00","temp":20.5},{"at":"22:30","temp":17}]})";

class TstChronoEditor : public QObject
{
    Q_OBJECT

private:
    QVector<sensor_t>   m_sensors;
    ZoneControl        *m_control = nullptr;
    ChronoEditor       *m_editor = nullptr;

    QPushButton *button(const QString &text) const
    {
        for(QPushButton *b : m_editor->findChildren<QPushButton *>())
            if(b->text() == text || b->accessibleName() == text)
                return b;
        return nullptr;
    }
    void click(const QString &text) const
    {
        QPushButton *b = button(text);
        QVERIFY2(b, qPrintable(text));
        QVERIFY2(b->isEnabled(), qPrintable(text));
        QTest::mouseClick(b, Qt::LeftButton);
    }
    QStringList chips() const
    {
        QStringList out;
        for(QPushButton *b : m_editor->findChildren<QPushButton *>("editChip"))
            if(b->isVisible())
                out.append(b->text());
        return out;
    }
    QString weekday() const { return Chrono::toString(m_editor->program().weekday); }

private slots:
    void init()
    {
        m_sensors.clear();
        sensor_t s;
        memset(&s, 0, sizeof(s));
        qstrncpy(s.name, "Salotto", sizeof(s.name));
        qstrncpy(s.zone, "salotto", sizeof(s.zone));
        m_sensors.append(s);
        m_control = new ZoneControl({ "salotto" }, 50, 300);
        m_control->setConnected(true);
        m_control->onStatus("online");
        m_control->onChronoState("salotto", R"({"chrono":"on"})");
        m_control->onChronoProgram("salotto", PROGRAM);
        m_editor = new ChronoEditor(m_sensors, m_control);
        m_editor->resize(600, 1024);
        m_editor->show();
        m_editor->edit(0);
    }

    void cleanup()
    {
        delete m_editor;
        delete m_control;
    }

    void opens()
    {
        QCOMPARE(m_editor->findChild<QLabel *>("chronoTitle")->text(), QString("Salotto"));
        QVERIFY(!m_editor->holidayShown());
        QCOMPARE(chips(), QStringList({ "06:30", "08:00", "17:00", "22:30" }));
        QCOMPARE(m_editor->selected(), 0);
        QCOMPARE(m_editor->findChildren<QLabel *>("editValue").at(0)->text(), QString("06:30"));
        QCOMPARE(m_editor->findChildren<QLabel *>("editValue").at(1)->text(), QString::fromUtf8("20.5°"));
        QCOMPARE(m_editor->findChild<DayChart *>()->temps().size(), 96);
        QVERIFY(button("Holiday SAT SUN"));
        QVERIFY(!button(QString::fromUtf8("‹ Prev"))->isEnabled());
    }

    void editSlot()
    {
        click("Later");
        click("Higher");
        QCOMPARE(weekday(), QString("06:45=21.0, 08:00=18.0, 17:00=20.5, 22:30=17.0"));
        click(QString::fromUtf8("Next ›"));
        QCOMPARE(m_editor->selected(), 1);
        click("Lower");
        click("Earlier");
        QCOMPARE(weekday(), QString("06:45=21.0, 07:45=17.5, 17:00=20.5, 22:30=17.0"));
        QTest::mouseClick(m_editor->findChildren<QPushButton *>("editChip").at(3), Qt::LeftButton);
        QCOMPARE(m_editor->selected(), 3);
        QVERIFY(!button(QString::fromUtf8("Next ›"))->isEnabled());
        /* not past midnight */
        for(int i = 0; i < 10; i++)
            QTest::mouseClick(button("Later"), Qt::LeftButton);
        QCOMPARE(m_editor->program().weekday.last().at, QTime(23, 45));
    }

    void splitAndDelete()
    {
        click(QString::fromUtf8("Next ›"));            /* 08:00-17:00 */
        click("Split");
        QCOMPARE(weekday(), QString("06:30=20.5, 08:00=18.0, 12:30=18.0, 17:00=20.5, 22:30=17.0"));
        QCOMPARE(m_editor->selected(), 2);
        click("Delete");
        QCOMPARE(weekday(), QString("06:30=20.5, 08:00=18.0, 17:00=20.5, 22:30=17.0"));
        QCOMPARE(m_editor->selected(), 2);
        for(int i = 0; i < 4; i++)
            click("Delete");
        QVERIFY(m_editor->program().weekday.isEmpty());
        QCOMPARE(m_editor->selected(), -1);
        QVERIFY(!button("Delete")->isEnabled());
        /* on without a weekday slot: refused by the controller, not saved */
        QVERIFY(!button("Save")->isEnabled());
        QVERIFY(m_editor->findChild<QLabel *>("chronoMessage")->isVisible());
        click("ON");                                    /* off */
        QVERIFY(button("Save")->isEnabled());
        click("Add");
        QCOMPARE(weekday(), QString("08:00=20.0"));
    }

    void holiday()
    {
        click("Holiday SAT SUN");
        QVERIFY(m_editor->holidayShown());
        QCOMPARE(chips(), QStringList({ "08:00", "23:00" }));
        click("Same as weekday");
        QVERIFY(m_editor->program().holiday.isEmpty());
        QVERIFY(m_editor->findChild<QLabel *>("chronoNone")->isVisible());
        QVERIFY(!button("Same as weekday")->isEnabled());
        click("Weekday");
        click("Copy to holiday");
        QCOMPARE(m_editor->program().holiday, m_editor->program().weekday);
        QVERIFY(Chrono::toString(m_editor->program().weekday) == weekday());
    }

    void save()
    {
        QSignalSpy sent(m_control, &ZoneControl::sendChronoProgram);
        QSignalSpy done(m_editor, &ChronoEditor::done);
        click("Higher");
        click("Save");
        QCOMPARE(sent.count(), 1);
        QCOMPARE(sent.at(0).at(1).toByteArray(), Chrono::programCommand(m_editor->program()));
        QVERIFY(sent.at(0).at(1).toByteArray().contains(R"({"at":"06:30","temp":21})"));
        QCOMPARE(done.count(), 1);
        QVERIFY(m_control->chrono(0).saving);
    }

    void changedElsewhere()
    {
        QSignalSpy sent(m_control, &ZoneControl::sendChronoProgram);
        click("Higher");
        m_control->onChronoProgram("salotto", PROGRAM);         /* from another panel */
        click("Save");
        QCOMPARE(sent.count(), 0);
        QVERIFY(m_editor->findChild<QLabel *>("chronoMessage")->isVisible());
        click("Save anyway");
        QCOMPARE(sent.count(), 1);
    }

    void cancelAndReset()
    {
        QSignalSpy sent(m_control, &ZoneControl::sendChronoProgram);
        QSignalSpy done(m_editor, &ChronoEditor::done);
        click("Higher");
        click("Cancel");
        QCOMPARE(done.count(), 1);
        QCOMPARE(sent.count(), 0);
        m_editor->edit(0);                              /* a new edit starts from the program */
        QCOMPARE(m_editor->program().weekday.first().temp, 20.5);
        click("Reset to the controller's");
        QCOMPARE(sent.count(), 1);
        QCOMPARE(sent.at(0).at(1).toByteArray(), QByteArray(R"({"reset":true})"));
        QCOMPARE(done.count(), 2);
    }

    void offline()
    {
        m_control->onStatus("offline");
        QVERIFY(!button("Save")->isEnabled());
        QVERIFY(!button("Reset to the controller's")->isEnabled());
        QVERIFY(button("Higher")->isEnabled());         /* editing goes on */
    }
};

int runChronoEditorTests(int argc, char *argv[])
{
    TstChronoEditor t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_chronoeditor.moc"
