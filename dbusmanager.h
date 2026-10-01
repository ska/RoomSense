#ifndef DBUSMANAGER_H
#define DBUSMANAGER_H
#include "common.h"

class DbusManager : public QObject
{
    Q_OBJECT
public:
    explicit DbusManager(QObject *parent = nullptr);

signals:
    void dbusGuiShow(void);
    void dbusGuiHide(void);

public Q_SLOTS:
    void show();
    void hide();

};

#endif // DBUSMANAGER_H
