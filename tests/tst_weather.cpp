#include <QTest>
#include "saverview.h"
#include "weather.h"

class TstWeather : public QObject
{
    Q_OBJECT

private slots:
    void parseWttr()
    {
        weather_t w;
        QVERIFY(Weather::parseWttr(QString::fromUtf8("\"home: +12°C 70% ↓3m/s 0.0mm 1015hPa\""), w));
        QCOMPARE(w.location, QString("home"));
        QCOMPARE(w.temp, 12.0);
        QCOMPARE(w.hum, 70);
        QCOMPARE(w.ws, 3.0);
        QCOMPARE(w.rain, 0.0);
        QCOMPARE(w.press, 1015);
        QCOMPARE(w.symbol, QString());                  /* no icon from wttr.in */

        QVERIFY(Weather::parseWttr(QString::fromUtf8("\"home: -7°C 90% ↑1.5m/s 0.2mm 1022hPa\""), w));
        QCOMPARE(w.temp, -7.0);
        QCOMPARE(w.ws, 1.5);
        QVERIFY(!Weather::parseWttr("Unknown location", w));
        QVERIFY(!Weather::parseWttr("", w));
    }

    void parseMetNo()
    {
        /* Trimmed api.met.no locationforecast/2.0/compact answer */
        const QByteArray json = R"({
          "type": "Feature",
          "properties": {
            "timeseries": [
              { "time": "2026-09-29T14:00:00Z",
                "data": {
                  "instant": { "details": {
                    "air_pressure_at_sea_level": 1014.6, "air_temperature": -2.6,
                    "relative_humidity": 71.4, "wind_speed": 3.2 } },
                  "next_1_hours": { "summary": { "symbol_code": "lightrainshowers_night" },
                                    "details": { "precipitation_amount": 0.4 } },
                  "next_6_hours": { "summary": { "symbol_code": "cloudy" } } } }
            ]
          }
        })";
        weather_t w;
        QVERIFY(Weather::parseMetNo(json, "home", w));
        QCOMPARE(w.location, QString("home"));
        QCOMPARE(w.temp, -2.6);
        QCOMPARE(w.hum, 71);
        QCOMPARE(w.press, 1015);
        QCOMPARE(w.ws, 3.2);
        QCOMPARE(w.rain, 0.4);
        QCOMPARE(w.symbol, QString("lightrainshowers_night"));

        /* the last steps of the forecast have next_6_hours only */
        const QByteArray six = R"({"properties":{"timeseries":[{"data":{"instant":{"details":{
            "air_temperature":12.0,"relative_humidity":50.0,"air_pressure_at_sea_level":1010.0}},
            "next_6_hours":{"summary":{"symbol_code":"fair_day"}}}}]}})";
        QVERIFY(Weather::parseMetNo(six, "x", w));
        QCOMPARE(w.symbol, QString("fair_day"));
        QCOMPARE(w.rain, 0.0);
        QCOMPARE(w.ws, 0.0);
    }

    void parseMetNoInvalid_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("empty")          << QByteArray();
        QTest::newRow("not json")       << QByteArray("<html>403 Forbidden</html>");
        QTest::newRow("empty series")   << QByteArray(R"({"properties":{"timeseries":[]}})");
        QTest::newRow("no temperature") << QByteArray(R"({"properties":{"timeseries":[{"data":{"instant":{"details":{"relative_humidity":50.0,"air_pressure_at_sea_level":1010.0}}}}]}})");
    }

    void parseMetNoInvalid()
    {
        QFETCH(QByteArray, json);
        weather_t w;
        QVERIFY(!Weather::parseMetNo(json, "x", w));
    }

    void httpDate()
    {
        QCOMPARE(Weather::parseHttpDate("Tue, 29 Sep 2026 14:44:19 GMT"),
                 QDateTime(QDate(2026, 9, 29), QTime(14, 44, 19), Qt::UTC));
        QVERIFY(!Weather::parseHttpDate("tomorrow").isValid());
    }

    void icons_data()
    {
        QTest::addColumn<QString>("symbol");
        QTest::addColumn<QString>("icon");
        QTest::newRow("clear day")      << "clearsky_day" << "wx-sun";
        QTest::newRow("clear night")    << "clearsky_night" << "wx-moon";
        QTest::newRow("polar twilight") << "clearsky_polartwilight" << "wx-sun";
        QTest::newRow("fair")           << "fair_day" << "wx-partly-day";
        QTest::newRow("partly night")   << "partlycloudy_night" << "wx-partly-night";
        QTest::newRow("cloudy")         << "cloudy" << "wx-cloud";
        QTest::newRow("fog")            << "fog" << "wx-fog";
        QTest::newRow("rain")           << "heavyrain" << "wx-rain";
        QTest::newRow("showers")        << "lightrainshowers_day" << "wx-rain";
        QTest::newRow("sleet")          << "sleet" << "wx-rain";
        QTest::newRow("snow")           << "snowshowers_night" << "wx-snow";
        QTest::newRow("thunder")        << "rainandthunder" << "wx-thunder";
        QTest::newRow("unknown")        << "" << "";
    }

    void icons()
    {
        QFETCH(QString, symbol);
        QFETCH(QString, icon);
        QCOMPARE(SaverView::weatherIcon(symbol), icon);
        if(!icon.isEmpty())
            QVERIFY(QFile::exists(":/icons/" + icon + ".svg"));
    }

    void view()
    {
        weather_t w;
        w.temp = -2.6;
        w.hum = 71;
        w.ws = 3.2;
        w.rain = 0.4;
        w.symbol = "rain";
        SaverView::weather_view_t v = SaverView::weather(w, 60);
        QVERIFY(v.shown);
        QCOMPARE(v.icon, QString("wx-rain"));
        QCOMPARE(v.temp, QString::fromUtf8("-2.6°"));
        QCOMPARE(v.details, QString::fromUtf8("Humidity 71 % · Wind 3.2 m/s · Rain 0.4 mm"));

        w.rain = 0;
        QCOMPARE(SaverView::weather(w, 60).details, QString::fromUtf8("Humidity 71 % · Wind 3.2 m/s"));
        QVERIFY(!SaverView::weather(w, -1).shown);                  /* no data */
        QVERIFY(SaverView::weather(w, SaverView::WEATHER_MAX_AGE_S).shown);
        QVERIFY(!SaverView::weather(w, SaverView::WEATHER_MAX_AGE_S + 1).shown);   /* too old */
    }
};

int runWeatherTests(int argc, char *argv[])
{
    TstWeather t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_weather.moc"
