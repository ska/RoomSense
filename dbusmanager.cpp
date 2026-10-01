#include "dbusmanager.h"

DbusManager::DbusManager(QObject *parent) : QObject(parent)
{
}
/*
 * dbus-send --session --print-reply --dest=org.roomsense.RoomSense "/" org.roomsense.RoomSense.GuiInterface.show
 */
void DbusManager::show()
{
    emit dbusGuiShow();
}
/*
 * dbus-send --session --print-reply --dest=org.roomsense.RoomSense "/" org.roomsense.RoomSense.GuiInterface.hide
 */
void DbusManager::hide()
{
    emit dbusGuiHide();
}
