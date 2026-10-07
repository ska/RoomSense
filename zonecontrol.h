#ifndef ZONECONTROL_H
#define ZONECONTROL_H

#include <QObject>
#include <QStringList>
#include "chrono.h"
#include <QTimer>
#include <QVector>

/*
 * Setpoints of the zones as AcquaThermoNet publishes them, and the setpoint
 * commands of the RoomSense panel (HA_Hmi_Relay
 * docs/SENSOR_GATEWAY_INTERFACE.md §7). No MQTT here: Mqtt feeds the
 * controller topics in and publishes what sendSetPoint asks.
 *
 * AcquaThermoNet owns the setpoints: the value shown is its retained
 * state_temp. A change from the panel is pending until a state_temp
 * arrives after the command (the confirmation), or until it times out.
 * Presses are collected: one command per zone after commandDelayMs of
 * quiet. No command while the controller is not reachable (status not
 * online, broker disconnected, or no state_temp received for the zone).
 *
 * House mode (§8): the mode of the whole house as AcquaThermoNet publishes
 * it (retained mode/state), requested from the panel with requestMode().
 * The request is pending until a mode/state arrives after it, or until it
 * times out. While a mode is active the zone setpoints cannot be changed.
 *
 * Chrono thermostat (§9): the state (<zone>/chrono) and the program
 * (<zone>/chrono/profile) of each zone as AcquaThermoNet publishes them.
 * A change from the panel (on/off, a new program, a reset) is "saving"
 * until the controller answers: the next chrono state for on/off, the
 * next program for the others (also when refused), or a timeout. The
 * result is told once (chronoResult). The program version grows at every
 * program received: an editor compares it to warn about a change made
 * elsewhere meanwhile. House modes do not block the chrono.
 *
 * Zones by index: the sensor index, one sensor per zone.
 */
class ZoneControl : public QObject
{
    Q_OBJECT

public:
    /* Mirror of AcquaThermoNet climatezones.h: TEMP_STEP, TEMP_MIN, TEMP_MAX */
    static constexpr double STEP = 0.5;
    static constexpr double MIN = 5;
    static constexpr double MAX = 25;

    enum HouseMode { Normal, Window, Away, Boost };
    /* Answer to a chrono change from the panel */
    enum ChronoResult { ChronoSaved, ChronoRefused, ChronoNotConfirmed };
    Q_ENUM(ChronoResult)

    typedef struct {
        bool        hasState = false;       /*a <zone>/chrono was received*/
        ChronoState state;
        bool        hasProgram = false;     /*a <zone>/chrono/profile was received*/
        ChronoConfig program;
        int         programVersion = 0;     /*programs received*/
        bool        saving = false;         /*changed on the panel, no answer yet*/
    } chrono_t;

    typedef struct {
        bool    hasSetPoint = false;    /*a state_temp was received*/
        double  setPoint = 0;           /*the confirmed value (state_temp)*/
        bool    hasHeat = false;        /*a state_mode was received*/
        bool    heat = false;           /*state_mode heat*/
        bool    pending = false;        /*changed on the panel, not confirmed yet*/
        double  pendingValue = 0;       /*the value to show while pending*/
    } zone_t;

    /* zones: AcquaThermoNet zone names by index, empty = no zone */
    explicit ZoneControl(const QStringList &zones, int commandDelayMs = 1000,
                         int confirmTimeoutMs = 5000, QObject *parent = nullptr);

    int count() const { return m_zones.size(); }
    int indexOf(const QString &zone) const;
    const zone_t &zone(int index) const { return m_state.at(index); }

    bool controllerOnline() const { return m_connected && m_statusOnline; }
    /* A setpoint command for the zone is possible now (not during a house mode) */
    bool canChange(int index) const;

    /* House mode: the confirmed one (Normal until a mode/state arrives) */
    HouseMode houseMode() const { return m_mode; }
    bool hasHouseMode() const { return m_hasMode; }
    /* Window and boost: seconds left, counted down from the last mode/state */
    int remainingS() const;
    /* Requested on the panel, not confirmed yet */
    bool modePending() const { return m_modePending; }
    HouseMode pendingMode() const { return m_pendingMode; }
    /* A mode command is possible now */
    bool canChangeMode() const;
    /* Panel: ask AcquaThermoNet for mode; false if not possible now */
    bool requestMode(HouseMode mode);

    /* "normal", "window", "away", "boost" */
    static QString modeName(HouseMode mode);

    /* Panel: +/- steps of STEP; false if the zone cannot be changed now */
    bool step(int index, int steps);

    /* Panel, all the zones that can be changed now: direction < 0 aligns
     * them to the lowest setpoint, or lowers them by STEP if already
     * aligned; > 0 the same with the highest. The values shown (pending
     * ones included) count. false if no zone can be changed */
    bool stepAll(int direction);
    /* Lowest and highest setpoint shown among the zones that can be
     * changed now; false if none */
    bool range(double &low, double &high) const;

    /* "21.3" -> 21.5, clamped to MIN..MAX, as AcquaThermoNet does */
    static double normalize(double value);

    /* Chrono of the zone */
    const chrono_t &chrono(int index) const { return m_chrono.at(index); }
    /* A chrono change is possible now: controller reachable, program
     * known, nothing being saved */
    bool canChangeChrono(int index) const;
    /* Panel: chrono on or off (chrono/set); false if not possible now */
    bool setChronoOn(int index, bool on);
    /* Panel: the whole program (enabled, weekday, holiday) */
    bool saveProgram(int index, const ChronoConfig &program);
    /* Panel: back to the program of the controller configuration */
    bool resetProgram(int index);

public slots:
    /* From Mqtt */
    void setConnected(bool connected);
    void onStatus(const QByteArray &payload);
    void onStateTemp(const QString &zone, const QByteArray &payload);
    void onStateMode(const QString &zone, const QByteArray &payload);
    /* mode/state JSON: {"mode":"window","remaining_s":1740} */
    void onModeState(const QByteArray &payload);
    /* <zone>/chrono and <zone>/chrono/profile JSON */
    void onChronoState(const QString &zone, const QByteArray &payload);
    void onChronoProgram(const QString &zone, const QByteArray &payload);

signals:
    /* To Mqtt: publish AcquaThermoNet/<zone>/set_temp */
    void sendSetPoint(const QString &zone, double value);
    void zoneChanged(int index);
    void controllerOnlineChanged(bool online);
    /* To Mqtt: publish AcquaThermoNet/mode/set */
    void sendMode(const QString &mode);
    void houseModeChanged();
    /* To Mqtt: publish AcquaThermoNet/<zone>/chrono/set (on/off) */
    void sendChronoOn(const QString &zone, bool on);
    /* To Mqtt: publish AcquaThermoNet/<zone>/chrono/profile/set */
    void sendChronoProgram(const QString &zone, const QByteArray &payload);
    /* State, program or saving of the zone changed */
    void chronoChanged(int index);
    /* The answer to a chrono change from the panel, once */
    void chronoResult(int index, ZoneControl::ChronoResult result);

private:
    void cancelPending(int index);
    void setPending(int index, double value);
    double shown(int index) const;
    void commandDue(int index);
    void confirmTimeout(int index);
    void updateOnline(bool wasOnline);

    QStringList         m_zones;
    QVector<zone_t>     m_state;
    QVector<bool>       m_sent;             /*command sent, waiting for state_temp*/
    QVector<QTimer *>   m_commandTimers;
    QVector<QTimer *>   m_confirmTimers;
    bool                m_connected;
    bool                m_statusOnline;
    bool                m_hasMode;
    HouseMode           m_mode;
    int                 m_remainingS;       /*of the last mode/state*/
    qint64              m_modeAtMs;         /*MonoClock of the last mode/state*/
    bool                m_modePending;
    HouseMode           m_pendingMode;
    QTimer             *m_modeTimer;        /*mode request not confirmed*/

    void cancelAllPending();

    /* Chrono: what is being saved and how the answer is recognized */
    enum ChronoWait { WaitNone, WaitOnOff, WaitProgram, WaitReset };
    QVector<chrono_t>   m_chrono;
    QVector<ChronoWait> m_chronoWait;
    QVector<bool>       m_chronoOn;         /*WaitOnOff: requested*/
    QVector<ChronoConfig> m_chronoSent;     /*WaitProgram: sent*/
    QVector<QTimer *>   m_chronoTimers;     /*no answer*/
    bool startSaving(int index, ChronoWait wait);
    void endSaving(int index, ChronoResult result);
};

#endif // ZONECONTROL_H
