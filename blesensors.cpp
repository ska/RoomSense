#include "blesensors.h"
#include "bleparse.h"
#include <QElapsedTimer>
#include <poll.h>

#define POLL_TIMEOUT_MS     1000    /*stop() checked at least this often*/
#define NO_EVENT_REINIT_S   120     /*no advertisement at all: restart the scan*/

BleSensors::BleSensors(QVector<sensor_t> *sensors )
{
    m_stop = false;
    m_sensors = sensors;

    /* the thread matches the MACs on its own copy: m_sensors is changed
     * by the main thread */
    for ( const sensor_t &s : *m_sensors )
        m_macs.append( QString::fromLatin1(s.smac) );

    qCDebug(lcBle).noquote() << "BleSensors";

    qRegisterMetaType<sensor_t>();
    qRegisterMetaType<sensor_env_t>();

    connect( this, SIGNAL( sensorEnvValueChanged(int, unsigned, sensor_env_t)), this, SLOT(sensorValueIsChanged(int, unsigned, sensor_env_t)) );
}


struct hci_request BleSensors::ble_hci_request(uint16_t ocf, int clen, void * status, void * cparam)
{
    struct hci_request rq;
    memset(&rq, 0, sizeof(rq));
    rq.ogf = OGF_LE_CTL;
    rq.ocf = ocf;
    rq.cparam = cparam;
    rq.clen = clen;
    rq.rparam = status;
    rq.rlen = 1;
    return rq;
}

/**
 * @brief BleSensors::openScanner
 * Opens hci1 (or hci0) and starts a passive LE scan
 * @param hciDev   set to the device number used
 * @return HCI socket, -1 on failure (logged)
 */
int BleSensors::openScanner(int &hciDev)
{
    int ret, status;

    /*
     * Open HCI
     * * * * * * * * * * * * * * * */
    hciDev = 1;
    int device = hci_open_dev(1);
    if ( device < 0 ) {
        hciDev = 0;
        device = hci_open_dev(0);
        if (device >= 0) {
            qCInfo(lcBle).noquote() << "Using hci0";
        }
    }
    else {
        qCInfo(lcBle).noquote() << "Using hci1";
    }

    /*
     * Check HCI
     * * * * * * * * * * * * * * * */
    if ( device < 0 ) {
        qCWarning(lcBle).noquote() << QString("Failed to open HCI device: %1").arg(strerror(errno));
        return -1;
    }

    /*
     * Set BLE scan parameters.
     * * * * * * * * * * * * * * * */
    le_set_scan_parameters_cp scan_params_cp;
    memset(&scan_params_cp, 0, sizeof(scan_params_cp));
    scan_params_cp.type 			= 0x00;
    scan_params_cp.interval 		= htobs(0x0010);
    scan_params_cp.window 			= htobs(0x0010);
    scan_params_cp.own_bdaddr_type 	= 0x00; // Public Device Address (default).
    scan_params_cp.filter 			= 0x00; // Accept all.

    struct hci_request scan_params_rq = ble_hci_request(OCF_LE_SET_SCAN_PARAMETERS,
                                                        LE_SET_SCAN_PARAMETERS_CP_SIZE,
                                                        &status, &scan_params_cp);
    ret = hci_send_req(device, &scan_params_rq, 1000);
    if ( ret < 0 ) {
        qCWarning(lcBle).noquote() << QString("Failed to set scan parameters: %1").arg(strerror(errno));
        hci_close_dev(device);
        return -1;
    }

    /*
     * Set BLE events report mask.
     * * * * * * * * * * * * * * * */
    le_set_event_mask_cp event_mask_cp;
    memset(&event_mask_cp, 0, sizeof(le_set_event_mask_cp));
    int i = 0;
    for ( i = 0 ; i < 8 ; i++ )
        event_mask_cp.mask[i] = 0xFF;

    struct hci_request set_mask_rq = ble_hci_request(OCF_LE_SET_EVENT_MASK,
                                                     LE_SET_EVENT_MASK_CP_SIZE,
                                                     &status, &event_mask_cp);
    ret = hci_send_req(device, &set_mask_rq, 1000);
    if ( ret < 0 ) {
        qCWarning(lcBle).noquote() << QString("Failed to set event mask: %1").arg(strerror(errno));
        hci_close_dev(device);
        return -1;
    }

    /*
     * Enable scanning.
     * * * * * * * * * * * * * * * */
    le_set_scan_enable_cp scan_cp;
    memset(&scan_cp, 0, sizeof(scan_cp));
    scan_cp.enable 		= 0x01;	// Enable flag.
    scan_cp.filter_dup 	= 0x00; // Filtering disabled.

    struct hci_request enable_adv_rq = ble_hci_request(OCF_LE_SET_SCAN_ENABLE, LE_SET_SCAN_ENABLE_CP_SIZE, &status, &scan_cp);

    ret = hci_send_req(device, &enable_adv_rq, 1000);
    if ( ret < 0 ) {
        qCWarning(lcBle).noquote() << QString("Failed to enable scan: %1").arg(strerror(errno));
        hci_close_dev(device);
        return -1;
    }

    /*
     * Get Results.
     * * * * * * * * * * * * * * * */
    struct hci_filter nf;
    hci_filter_clear(&nf);
    hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
    hci_filter_set_event(EVT_LE_META_EVENT, &nf);
    if ( setsockopt(device, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0 ) {
        qCWarning(lcBle).noquote() << QString("Could not set socket options: %1").arg(strerror(errno));
        hci_close_dev(device);
        return -1;
    }

    qCInfo(lcBle).noquote() << "BLE scan started";
    return device;
}

/**
 * @brief BleSensors::readLoop
 * Returns on a read error, when no advertisement arrives for
 * NO_EVENT_REINIT_S (adapter stuck) or on stop()
 * @param device HCI socket from openScanner()
 */
void BleSensors::readLoop(int device)
{
    uint8_t buf[HCI_MAX_EVENT_SIZE];
    QElapsedTimer lastEvent;
    lastEvent.start();

    while ( !m_stop )
    {
        m_heartbeat.beat();
        struct pollfd pfd = { device, POLLIN, 0 };
        const int ready = poll(&pfd, 1, POLL_TIMEOUT_MS);
        if ( ready < 0 && errno != EINTR ) {
            qCWarning(lcBle).noquote() << QString("BLE poll failed: %1").arg(strerror(errno));
            return;
        }
        if ( ready <= 0 ) {
            if ( lastEvent.elapsed() > NO_EVENT_REINIT_S * 1000 ) {
                qCWarning(lcBle).noquote() << QString("No BLE advertisement for %1 s, restarting the scan").arg(NO_EVENT_REINIT_S);
                return;
            }
            continue;
        }
        if ( pfd.revents & (POLLERR | POLLHUP | POLLNVAL) ) {
            qCWarning(lcBle).noquote() << QString("BLE socket error (revents %1)").arg(pfd.revents);
            return;
        }

        const int len = read(device, buf, sizeof(buf));
        if ( len < 0 ) {
            if ( errno == EINTR || errno == EAGAIN )
                continue;
            qCWarning(lcBle).noquote() << QString("BLE read failed: %1").arg(strerror(errno));
            return;
        }
        if ( len == 0 ) {
            qCWarning(lcBle).noquote() << "BLE socket closed";
            return;
        }
        lastEvent.restart();
        m_status.advertisement();

        const QVector<BleParse::reading_t> readings = BleParse::advertisingReports(buf, len, (unsigned)time(NULL));
        for ( const BleParse::reading_t &r : readings )
        {
            const int sens = m_macs.indexOf(r.mac);
            if ( sens >= 0 )
                emit sensorEnvValueChanged( sens, r.env.ldt, r.env );
        }
    }
}

/**
 * @brief BleSensors::waitRetry
 * Backoff 1, 2, 5, 10, then 30 s, interrupted by stop()
 * @param attempt 0 for the first retry
 */
void BleSensors::waitRetry(int attempt)
{
    static const int backoff[] = { 1, 2, 5, 10, 30 };
    static const int backoffLen = sizeof(backoff) / sizeof(backoff[0]);

    const int delay = backoff[qMin(attempt, backoffLen - 1)];
    qCInfo(lcBle).noquote() << QString("BLE scan: retry in %1 s").arg(delay);
    for ( int i = 0; i < delay * 10 && !m_stop; i++ )
    {
        m_heartbeat.beat();
        msleep(100);
    }
}

void BleSensors::run()
{
    int attempt = 0;
    while ( !m_stop )
    {
        m_heartbeat.beat();
        int hciDev = -1;
        const int device = openScanner(hciDev);
        if ( device >= 0 )
        {
            attempt = 0;
            m_status.set(BleStatus::Scanning, hciDev);
            readLoop(device);
            hci_close_dev(device);
        }
        m_status.set(BleStatus::Retrying);
        if ( !m_stop )
            waitRetry(attempt++);
    }
}

void BleSensors::sensorValueIsChanged( int index, unsigned time, sensor_env_t data)
{
    (void) time;

    /* every reading: debug only (rs.ble.debug=true) */
    qCDebug(lcBle).noquote() << QString("Sensor %1 %2 %3: RSSI %4 | PC %5 | Temp %6 | Hum %7% | Batt %8% %9mV")
        .arg(m_sensors->at(index).id).arg(m_sensors->at(index).name).arg(m_sensors->at(index).smac)
        .arg(data.rssi).arg(data.pc).arg(data.temp / 10.0).arg(data.hum).arg(data.batt).arg(data.battmv);

    sensor_t tmp;
    tmp = m_sensors->at(index);
    tmp.se = data;
    m_sensors->replace(index, tmp);
}

void BleSensors::stop()
{
    m_stop = true;
}
