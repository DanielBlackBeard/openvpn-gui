/*
 *  OpenVPN-GUI -- A Windows GUI for OpenVPN.
 *
 *  Copyright (C) 2004 Mathias Sundman <mathias@nilings.se>
 *                2010 Heiko Hund <heikoh@users.sf.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program (see the file COPYING included with this
 *  distribution); if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <windows.h>
#include <shellapi.h>
#include <tchar.h>
#include <time.h>

#include "tray.h"
#include "main.h"
#include "options.h"
#include "openvpn.h"
#include "openvpn_config.h"
#include "openvpn-gui-res.h"
#include "localization.h"
#include "misc.h"
#include "assert.h"

/* Popup Menus */
HMENU hMenu;
HMENU *hMenuConn;
HMENU hMenuImport;
int hmenu_size = 0; /* allocated size of hMenuConn array */

HBITMAP hbmpConnecting;

NOTIFYICONDATA ni;
extern options_t o;

#define USE_NESTED_CONFIG_MENU ((o.config_menu_view == CONFIG_VIEW_AUTO && o.num_configs > 25)   \
                                 || (o.config_menu_view == CONFIG_VIEW_NESTED))


static void
DeleteMenuBitmaps(void)
{
    if (hbmpConnecting) {
        DeleteObject(hbmpConnecting);
        hbmpConnecting = NULL;
    }
}

/* Create bitmaps for menu items. Currently only the connecting checkmark.
 *
 * Make a color bitmap from the connecting icon for use as a checkmark
 * for indicating the connecting state. We do this by replacing
 * the icon colour bitmap pixels in the background region with the menu
 * color (COLOR_MENU)
 */
static void
CreateMenuBitmaps(void)
{

    DeleteMenuBitmaps();

    int cx = GetSystemMetrics(SM_CXMENUCHECK);
    int cy = GetSystemMetrics(SM_CYMENUCHECK);
    HICON icon = LoadLocalizedIconEx(ID_ICO_CONNECTING, cx, cy);
    ICONINFO iconinfo;

    if (!icon || !GetIconInfo(icon, &iconinfo))
    {
        MsgToEventLog(EVENTLOG_ERROR_TYPE, L"Error loading ID_ICO_CONNECTING.");
        return;
    }

    /* Create two DCs for drawing/accessing the images in memory */
    HDC maskDC = CreateCompatibleDC(NULL), imgDC = CreateCompatibleDC(NULL);
    if (!maskDC || !imgDC)
    {
        DeleteObject(iconinfo.hbmMask);
        DeleteObject(iconinfo.hbmColor);
        if (maskDC) DeleteDC(maskDC);
        if (imgDC) DeleteDC(imgDC);
        MsgToEventLog(EVENTLOG_ERROR_TYPE, L"Error creating DCs for drawing");
        return;
    }

    /* Load the image and mask bitmaps into the DCs saving the default one's */
    HBITMAP def1 = (HBITMAP) SelectObject(imgDC, iconinfo.hbmColor);
    HBITMAP def2 = (HBITMAP) SelectObject(maskDC, iconinfo.hbmMask);

    /* White mask pixels mark the background region */
    COLORREF ref = RGB(255, 255, 255);
    COLORREF bg = GetSysColor(COLOR_MENU);

    for (int x = 0; x < cx; x++) {
        for (int y = 0; y < cy; y++) {
            if (GetPixel(maskDC, x, y) == ref)
                SetPixel(imgDC, x, y, bg);
        }
    }

    /* Save the result and restore the default bitmaps back in the DC */
    hbmpConnecting = (HBITMAP) SelectObject(imgDC, def1);
    SelectObject(maskDC, def2);

    /* We don't need the mask bitmap -- free it */
    DeleteObject(iconinfo.hbmMask);

    DeleteDC(imgDC);
    DeleteDC(maskDC);
}

/*
 * Reallocate hMenuConn as required. In case of not enough memory,
 * o.num_config is reset to max space available value so that the
 * program can continue.
 */
void
AllocateConnectionMenu()
{
    if (hmenu_size >= o.num_configs) return;
    HMENU *tmp  = (HMENU *) realloc(hMenuConn, sizeof(HMENU)*(o.num_configs + 50));
    if (tmp) {
        hmenu_size = o.num_configs + 50;
        hMenuConn = tmp;
    }
    else {
        o.num_configs = hmenu_size;
        MsgToEventLog(EVENTLOG_ERROR_TYPE, L"Allocation of hMenuConn failed. Ignoring configs beyond index = %d", o.num_configs);
    }
    return;
}

/* Create popup menus */
void
CreatePopupMenus()
{
    /* We use groups[0].menu as the root menu, so,
     * even if num_configs = 0, we want num_groups > 0.
     * This is guaranteed as the root node is always defined.
     */
    assert(o.num_groups > 0);

    AllocateConnectionMenu();

    CreateMenuBitmaps();
    MENUINFO minfo = {.cbSize = sizeof(MENUINFO)};

    for (int i = 0; i < o.num_configs; i++)
    {
        hMenuConn[i] = CreatePopupMenu();
        /* Save the connection index in the menu.*/
        minfo.fMask = MIM_MENUDATA;
        minfo.dwMenuData = i;
        SetMenuInfo(hMenuConn[i], &minfo);
    }
    for (int i = 0; i < o.num_groups; i++)
    {
        if (!o.groups[i].active)
            continue;
        o.groups[i].menu = CreatePopupMenu();
        o.groups[i].children = 0; /* we have to recount this when assigning menu position index */
    }

    hMenu = o.groups[0].menu; /* the first group menu is also the root menu */

    /* Set notify by position style for the top menu - gets automatically applied to sub-menus */
    minfo.fMask = MIM_STYLE;
    GetMenuInfo(hMenu, &minfo);
    minfo.dwStyle |= MNS_NOTIFYBYPOS;
    SetMenuInfo(hMenu, &minfo);

    if (o.num_configs == 1) {
        /* Create Main menu with actions */
        AppendMenu(hMenu, MF_STRING, IDM_CONNECTMENU, LoadLocalizedString(IDS_MENU_CONNECT));
        AppendMenu(hMenu, MF_STRING, IDM_DISCONNECTMENU, LoadLocalizedString(IDS_MENU_DISCONNECT));
        AppendMenu(hMenu, MF_STRING, IDM_RECONNECTMENU, LoadLocalizedString(IDS_MENU_RECONNECT));
        AppendMenu(hMenu, MF_STRING, IDM_STATUSMENU, LoadLocalizedString(IDS_MENU_STATUS));
        AppendMenu(hMenu, MF_SEPARATOR, 0, 0);

        AppendMenu(hMenu, MF_STRING, IDM_VIEWLOGMENU, LoadLocalizedString(IDS_MENU_VIEWLOG));

        AppendMenu(hMenu, MF_STRING, IDM_EDITMENU, LoadLocalizedString(IDS_MENU_EDITCONFIG));
        AppendMenu(hMenu, MF_STRING, IDM_CLEARPASSMENU, LoadLocalizedString(IDS_MENU_CLEARPASS));

#ifndef DISABLE_CHANGE_PASSWORD
        if (o.conn[0].flags & FLAG_ALLOW_CHANGE_PASSPHRASE)
            AppendMenu(hMenu, MF_STRING, IDM_PASSPHRASEMENU, LoadLocalizedString(IDS_MENU_PASSPHRASE));
#endif

        AppendMenu(hMenu, MF_SEPARATOR, 0, 0);

        hMenuImport = CreatePopupMenu();
        AppendMenu(hMenu, MF_POPUP, (UINT_PTR) hMenuImport, LoadLocalizedString(IDS_MENU_IMPORT));
        AppendMenu(hMenuImport, MF_STRING, IDM_IMPORT_FILE, LoadLocalizedString(IDS_MENU_IMPORT_FILE));
        AppendMenu(hMenuImport, MF_STRING, IDM_IMPORT_AS, LoadLocalizedString(IDS_MENU_IMPORT_AS));
        AppendMenu(hMenuImport, MF_STRING, IDM_IMPORT_URL, LoadLocalizedString(IDS_MENU_IMPORT_URL));

        AppendMenu(hMenu, MF_STRING ,IDM_SETTINGS, LoadLocalizedString(IDS_MENU_SETTINGS));
        AppendMenu(hMenu, MF_STRING ,IDM_CLOSE, LoadLocalizedString(IDS_MENU_CLOSE));

        SetMenuStatusById(0,  o.conn[0].state);
    }
    else {
        /* construct the submenu tree first */
        if (USE_NESTED_CONFIG_MENU)
        {
            /* i = 0 is the root menu and has no parent */
            for (int i = 1; i < o.num_groups; i++)
            {
                config_group_t *this = &o.groups[i];
                config_group_t *parent = PARENT_GROUP(this);

                if (!this->active || !parent)
                    continue;
                AppendMenu(parent->menu, MF_POPUP, (UINT_PTR) this->menu, this->name);
                this->pos = parent->children++;

                PrintDebug(L"Submenu %d named %ls added to parent %ls with position %d",
                        i, this->name, parent->name, this->pos);
            }
        }

        /* add config file (connection) entries */
        for (int i = 0; i < o.num_configs; i++)
        {
            connection_t *c = &o.conn[i];
            config_group_t *parent = &o.groups[0]; /* by default config is added to the root */

            if (USE_NESTED_CONFIG_MENU)
            {
                parent = CONFIG_GROUP(c);
            }
            assert(parent);

            /* Add config to the current sub menu */
            AppendMenu(parent->menu, MF_POPUP, (UINT_PTR) hMenuConn[i], c->config_name);
            c->pos = parent->children++;

            PrintDebug(L"Config %d named %ls added to submenu %ls with position %d",
                        i, c->config_name, parent->name, c->pos);
        }

        if (o.num_configs > 0)
            AppendMenu(hMenu, MF_SEPARATOR, 0, 0);

        hMenuImport = CreatePopupMenu();
        AppendMenu(hMenu, MF_POPUP, (UINT_PTR) hMenuImport, LoadLocalizedString(IDS_MENU_IMPORT));
        AppendMenu(hMenuImport, MF_STRING, IDM_IMPORT_FILE, LoadLocalizedString(IDS_MENU_IMPORT_FILE));
        AppendMenu(hMenuImport, MF_STRING, IDM_IMPORT_AS, LoadLocalizedString(IDS_MENU_IMPORT_AS));
        AppendMenu(hMenuImport, MF_STRING, IDM_IMPORT_URL, LoadLocalizedString(IDS_MENU_IMPORT_URL));

        AppendMenu(hMenu, MF_STRING, IDM_SETTINGS, LoadLocalizedString(IDS_MENU_SETTINGS));
        AppendMenu(hMenu, MF_STRING, IDM_CLOSE, LoadLocalizedString(IDS_MENU_CLOSE));

        /* Create popup menus for every connection */
        for (int i = 0; i < o.num_configs; i++) {
            AppendMenu(hMenuConn[i], MF_STRING, IDM_CONNECTMENU, LoadLocalizedString(IDS_MENU_CONNECT));
            AppendMenu(hMenuConn[i], MF_STRING, IDM_DISCONNECTMENU, LoadLocalizedString(IDS_MENU_DISCONNECT));
            AppendMenu(hMenuConn[i], MF_STRING, IDM_RECONNECTMENU, LoadLocalizedString(IDS_MENU_RECONNECT));
            AppendMenu(hMenuConn[i], MF_STRING, IDM_STATUSMENU, LoadLocalizedString(IDS_MENU_STATUS));
            AppendMenu(hMenuConn[i], MF_SEPARATOR, 0, 0);

            AppendMenu(hMenuConn[i], MF_STRING, IDM_VIEWLOGMENU, LoadLocalizedString(IDS_MENU_VIEWLOG));

            AppendMenu(hMenuConn[i], MF_STRING, IDM_EDITMENU, LoadLocalizedString(IDS_MENU_EDITCONFIG));
            AppendMenu(hMenuConn[i], MF_STRING, IDM_CLEARPASSMENU, LoadLocalizedString(IDS_MENU_CLEARPASS));

#ifndef DISABLE_CHANGE_PASSWORD
            if (o.conn[i].flags & FLAG_ALLOW_CHANGE_PASSPHRASE)
                AppendMenu(hMenuConn[i], MF_STRING, IDM_PASSPHRASEMENU, LoadLocalizedString(IDS_MENU_PASSPHRASE));
#endif

            SetMenuStatusById(i, o.conn[i].state);
        }
    }
}


/* Destroy popup menus */
static void
DestroyPopupMenus()
{
    int i;
    for (i = 0; i < o.num_configs; i++)
        DestroyMenu(hMenuConn[i]);

    DestroyMenu(hMenuImport);
    DestroyMenu(hMenu);

    hMenuImport = NULL;
    hMenu = NULL;
}

/* Rescan config folders and recreate popup menus */
void
RecreatePopupMenus(void)
{
    DestroyPopupMenus();
    BuildFileList();
    CreatePopupMenus();
}

/*
 * Handle mouse clicks on tray icon
 */
void
OnNotifyTray(LPARAM lParam)
{
    POINT pt;

    switch (lParam) {
    case WM_RBUTTONUP:
        RecreatePopupMenus();

        GetCursorPos(&pt);
        SetForegroundWindow(o.hWnd);
        TrackPopupMenu(hMenu, TPM_RIGHTALIGN, pt.x, pt.y, 0, o.hWnd, NULL);
        PostMessage(o.hWnd, WM_NULL, 0, 0);
        break;

    case WM_LBUTTONDBLCLK:
        {
            int disconnected_conns = CountConnState(disconnected);

            RecreatePopupMenus();

            /* Start connection if only one config exist */
            if (o.num_configs == 1 && o.conn[0].state == disconnected)
                    StartOpenVPN(&o.conn[0]);
            /* show the status window of all connected/connecting profiles upto a max of 10 */
            else if (disconnected_conns < o.num_configs) {
                int i;
                int num_shown = 0;
                for (i = 0; i < o.num_configs; i++) {
                    if (o.conn[i].state != disconnected) {
                        ShowWindow(o.conn[i].hwndStatus, SW_SHOW);
                        SetForegroundWindow(o.conn[i].hwndStatus);
                        if (++num_shown >= 10) break;
                    }
                }
            }
        }
        break;

    case WM_OVPN_RESCAN:
        /* Rescan config folders and recreate popup menus */
        RecreatePopupMenus();
        break;
    }
}


void
OnDestroyTray()
{
    DestroyMenu(hMenu);
    Shell_NotifyIcon(NIM_DELETE, &ni);
}


void
ShowTrayIcon()
{
  ni.cbSize = sizeof(ni);
  ni.uID = 0;
  ni.hWnd = o.hWnd;
  ni.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
  ni.uCallbackMessage = WM_NOTIFYICONTRAY;
  ni.hIcon = LoadLocalizedSmallIcon(ID_ICO_DISCONNECTED);
  _tcsncpy(ni.szTip, LoadLocalizedString(IDS_TIP_DEFAULT), _countof(ni.szTip));

  Shell_NotifyIcon(NIM_ADD, &ni);
}

void
SetTrayIcon(conn_state_t state)
{
    TCHAR msg[500];
    TCHAR msg_connected[100];
    TCHAR msg_connecting[100];
    int i, config = 0;
    BOOL first_conn;
    UINT icon_id;

    _tcsncpy(msg, LoadLocalizedString(IDS_TIP_DEFAULT), _countof(ni.szTip));
    _tcsncpy(msg_connected, LoadLocalizedString(IDS_TIP_CONNECTED), _countof(msg_connected));
    _tcsncpy(msg_connecting, LoadLocalizedString(IDS_TIP_CONNECTING), _countof(msg_connecting));

    first_conn = TRUE;
    for (i = 0; i < o.num_configs; i++) {
        if (o.conn[i].state == connected) {
            /* Append connection name to Icon Tip Msg */
            _tcsncat(msg, (first_conn ? msg_connected : _T(", ")), _countof(msg) - _tcslen(msg) - 1);
            _tcsncat(msg, o.conn[i].config_name, _countof(msg) - _tcslen(msg) - 1);
            first_conn = FALSE;
            config = i;
        }
    }

    first_conn = TRUE;
    for (i = 0; i < o.num_configs; i++) {
        if (o.conn[i].state == connecting || o.conn[i].state == resuming || o.conn[i].state == reconnecting) {
            /* Append connection name to Icon Tip Msg */
            _tcsncat(msg, (first_conn ? msg_connecting : _T(", ")), _countof(msg) - _tcslen(msg) - 1);
            _tcsncat(msg, o.conn[i].config_name, _countof(msg) - _tcslen(msg) - 1);
            first_conn = FALSE;
        }
    }

    if (CountConnState(connected) == 1) {
        /* Append "Connected since and assigned IP" to message */
        const connection_t *c = &o.conn[config];
        TCHAR time[50];

        LocalizedTime(o.conn[config].connected_since, time, _countof(time));
        _tcsncat(msg, LoadLocalizedString(IDS_TIP_CONNECTED_SINCE), _countof(msg) - _tcslen(msg) - 1);
        _tcsncat(msg, time, _countof(msg) - _tcslen(msg) - 1);

        /* concatenate ipv4 and ipv6 addresses into one string */
        WCHAR ip[64];
        wcs_concat2(ip, _countof(ip), c->ip, c->ipv6, L", ");
        WCHAR *assigned_ip = LoadLocalizedString(IDS_TIP_ASSIGNED_IP, ip);
        _tcsncat(msg, assigned_ip, _countof(msg) - _tcslen(msg) - 1);
    }

    icon_id = ID_ICO_CONNECTING;
    if (state == connected)
        icon_id = ID_ICO_CONNECTED;
    else if (state == disconnected)
        icon_id = ID_ICO_DISCONNECTED;

    ni.cbSize = sizeof(ni);
    ni.uID = 0;
    ni.hWnd = o.hWnd;
    ni.hIcon = LoadLocalizedSmallIcon(icon_id);
    ni.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
    ni.uCallbackMessage = WM_NOTIFYICONTRAY;
    _tcsncpy(ni.szTip, msg, _countof(ni.szTip));

    Shell_NotifyIcon(NIM_MODIFY, &ni);
}


void
CheckAndSetTrayIcon()
{
    if (CountConnState(connected) != 0)
    {
        SetTrayIcon(connected);
    }
    else
    {
        if (CountConnState(connecting) != 0 || CountConnState(reconnecting) != 0
            ||  CountConnState(resuming) != 0)
            SetTrayIcon(connecting);
        else
            SetTrayIcon(disconnected);
    }
}


void
ShowTrayBalloon(TCHAR *infotitle_msg, TCHAR *info_msg)
{
    ni.cbSize = sizeof(ni);
    ni.uID = 0;
    ni.hWnd = o.hWnd;
    ni.uFlags = NIF_INFO;
    ni.uTimeout = 5000;
    ni.dwInfoFlags = NIIF_INFO;
    _tcsncpy(ni.szInfo, info_msg, _countof(ni.szInfo));
    _tcsncpy(ni.szInfoTitle, infotitle_msg, _countof(ni.szInfoTitle));

    Shell_NotifyIcon(NIM_MODIFY, &ni);
}


void
SetMenuStatus(connection_t *c, conn_state_t state)
{
    int i;

    for (i = 0; i < o.num_configs; ++i)
    {
        if (c == &o.conn[i])
            break;
    }
    SetMenuStatusById(i, state);
}

void
SetMenuStatusById(int i, conn_state_t state)
{
    connection_t *c = &o.conn[i];
    int checked = 0;

    if (state == connected || state == disconnecting) checked = 1;
    else if (state != disconnected && state != detached && state != onhold) checked = 2;

    if (o.num_configs == 1)
    {
        if (state == disconnected || state == detached)
        {
            EnableMenuItem(hMenu, IDM_CONNECTMENU, MF_ENABLED);
            EnableMenuItem(hMenu, IDM_DISCONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenu, IDM_RECONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenu, IDM_STATUSMENU, MF_GRAYED);
        }
        else if (state == connecting || state == resuming || state == connected)
        {
            EnableMenuItem(hMenu, IDM_CONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenu, IDM_DISCONNECTMENU, MF_ENABLED);
            EnableMenuItem(hMenu, IDM_RECONNECTMENU, MF_ENABLED);
            EnableMenuItem(hMenu, IDM_STATUSMENU, MF_ENABLED);
        }
        else if (state == disconnecting)
        {
            EnableMenuItem(hMenu, IDM_CONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenu, IDM_DISCONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenu, IDM_RECONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenu, IDM_STATUSMENU, MF_ENABLED);
        }
        else if (state == onhold)
        {
            EnableMenuItem(hMenu, IDM_CONNECTMENU, MF_ENABLED);
            EnableMenuItem(hMenu, IDM_DISCONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenu, IDM_RECONNECTMENU, MF_ENABLED);
            EnableMenuItem(hMenu, IDM_STATUSMENU, MF_ENABLED);
        }
        if (c->flags & (FLAG_SAVE_AUTH_PASS | FLAG_SAVE_KEY_PASS))
            EnableMenuItem(hMenu, IDM_CLEARPASSMENU, MF_ENABLED);
        else
            EnableMenuItem(hMenu, IDM_CLEARPASSMENU, MF_GRAYED);
    }
    else
    {
        config_group_t *parent = &o.groups[0];
        int pos = c->pos;

        if (USE_NESTED_CONFIG_MENU && CONFIG_GROUP(c))
            parent = CONFIG_GROUP(c);

        if (checked == 1)
        {
            /* Connected: use system-default check mark */
            SetMenuItemBitmaps(parent->menu, pos,  MF_BYPOSITION, NULL, NULL);
        }
        else if (checked == 2)
        {
            /* Connecting: use our custom check mark */
            SetMenuItemBitmaps(parent->menu, pos,  MF_BYPOSITION, NULL, hbmpConnecting);
        }
        CheckMenuItem(parent->menu, pos, MF_BYPOSITION | (checked ? MF_CHECKED : MF_UNCHECKED));

        PrintDebug(L"Setting state of config %ls checked = %d, parent %ls, pos %d",
                    c->config_name, checked, (parent->id == 0)? L"Main Menu" : L"SubMenu", pos);

        if (checked) /* also check all parent groups */
        {
            while (PARENT_GROUP(parent))
            {
                pos = parent->pos;
                parent = PARENT_GROUP(parent);
                CheckMenuItem(parent->menu, pos, MF_BYPOSITION | MF_CHECKED);
            }
        }

        if (state == disconnected || state == detached)
        {
            EnableMenuItem(hMenuConn[i], IDM_CONNECTMENU, MF_ENABLED);
            EnableMenuItem(hMenuConn[i], IDM_DISCONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenuConn[i], IDM_RECONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenuConn[i], IDM_STATUSMENU, MF_GRAYED);
        }
        else if (state == connecting || state == resuming || state == connected)
        {
            EnableMenuItem(hMenuConn[i], IDM_CONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenuConn[i], IDM_DISCONNECTMENU, MF_ENABLED);
            EnableMenuItem(hMenuConn[i], IDM_RECONNECTMENU, MF_ENABLED);
            EnableMenuItem(hMenuConn[i], IDM_STATUSMENU, MF_ENABLED);
        }
        else if (state == disconnecting)
        {
            EnableMenuItem(hMenuConn[i], IDM_CONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenuConn[i], IDM_DISCONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenuConn[i], IDM_RECONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenuConn[i], IDM_STATUSMENU, MF_ENABLED);
        }
        else if (state == onhold)
        {
            EnableMenuItem(hMenuConn[i], IDM_CONNECTMENU, MF_ENABLED);
            EnableMenuItem(hMenuConn[i], IDM_DISCONNECTMENU, MF_GRAYED);
            EnableMenuItem(hMenuConn[i], IDM_RECONNECTMENU, MF_ENABLED);
            EnableMenuItem(hMenuConn[i], IDM_STATUSMENU, MF_ENABLED);
        }
        if (c->flags & (FLAG_SAVE_AUTH_PASS | FLAG_SAVE_KEY_PASS))
            EnableMenuItem(hMenuConn[i], IDM_CLEARPASSMENU, MF_ENABLED);
        else
            EnableMenuItem(hMenuConn[i], IDM_CLEARPASSMENU, MF_GRAYED);
    }
}
