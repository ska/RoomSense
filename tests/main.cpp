#include <QApplication>
#include <QTest>
#include <cstdio>

int runBleParseTests(int argc, char *argv[]);
int runConfigTests(int argc, char *argv[]);
int runFakeScenarioTests(int argc, char *argv[]);
int runMqttTests(int argc, char *argv[]);
int runLoggingTests(int argc, char *argv[]);
int runSingleInstanceTests(int argc, char *argv[]);
int runWatchDogTests(int argc, char *argv[]);
int runZoneControlTests(int argc, char *argv[]);
int runZoneViewTests(int argc, char *argv[]);
int runDiagViewTests(int argc, char *argv[]);
int runDiagPageTests(int argc, char *argv[]);
int runMainPageTests(int argc, char *argv[]);
int runMainWindowTests(int argc, char *argv[]);
int runScreenSaverTests(int argc, char *argv[]);
int runWeatherTests(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    /* widget tests without a display */
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    int failed = 0;
    failed += runBleParseTests(argc, argv);
    failed += runConfigTests(argc, argv);
    failed += runFakeScenarioTests(argc, argv);
    failed += runMqttTests(argc, argv);
    failed += runSingleInstanceTests(argc, argv);
    failed += runWatchDogTests(argc, argv);
    failed += runZoneControlTests(argc, argv);
    failed += runZoneViewTests(argc, argv);
    failed += runDiagViewTests(argc, argv);
    failed += runDiagPageTests(argc, argv);
    failed += runMainPageTests(argc, argv);
    failed += runMainWindowTests(argc, argv);
    failed += runScreenSaverTests(argc, argv);
    failed += runWeatherTests(argc, argv);
    /* Last: installs the file log handler for the rest of the process */
    failed += runLoggingTests(argc, argv);

    fprintf(stderr, "\n%s: %d failed test function(s)\n", failed ? "FAIL" : "OK", failed);
    return failed ? 1 : 0;
}
