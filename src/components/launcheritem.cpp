
// This file is part of lipstick, a QML desktop library
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License version 2.1 as published by the Free Software Foundation
// and appearing in the file LICENSE.LGPL included in the packaging
// of this file.
//
// This code is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Lesser General Public License for more details.
//
// Copyright (c) 2011, Robin Burchell
// Copyright (c) 2012, Timur Kristóf <venemo@fedoraproject.org>

#include <QProcess>
#include <QFile>
#include <QDesktopServices>
#include <QDir>
#include <QSettings>
#include <QTimerEvent>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QUrl>

#include <mdesktopentry.h>
#include <mremoteaction.h>

#include "launcheritem.h"
#include "launchermodel.h"

#ifdef HAVE_CONTENTACTION
#include <contentaction.h>
#else
#undef signals
#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib.h>
#endif

#include <QDebug>

LauncherItem::LauncherItem(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_isLaunching(false)
    , m_isUpdating(false)
    , m_isTemporary(false)
    , m_packageName("")
    , m_updatingProgress(-1)
    , m_customTitle("")
    , m_customIconFilename("")
    , m_serial(0)
    , m_isBlacklisted(false)
{
    if (!filePath.isEmpty()) {
        setFilePath(filePath);
    }

    // TODO: match the PID of the window thumbnails with the launcher processes
    // Launching animation will be disabled if the window of the launched app shows up
}

LauncherItem::LauncherItem(const QString &packageName, const QString &label,
        const QString &iconPath, const QString &desktopFile, QObject *parent)
    : QObject(parent)
    , m_isLaunching(false)
    , m_isUpdating(false)
    , m_isTemporary(false)
    , m_packageName(packageName)
    , m_updatingProgress(-1)
    , m_customTitle(label)
    , m_customIconFilename(iconPath)
    , m_serial(0)
    , m_isBlacklisted(false)
    , m_mimeTypesPopulated(false)
{
    if (!desktopFile.isEmpty()) {
        setFilePath(desktopFile);
    }
}

LauncherItem::~LauncherItem()
{
}

LauncherModel::ItemType LauncherItem::type() const
{
    return LauncherModel::Application;
}

void LauncherItem::setFilePath(const QString &filePath)
{
    if (!filePath.isEmpty() && QFile::exists(filePath)) {
        m_desktopEntry = QSharedPointer<MDesktopEntry>(new MDesktopEntry(filePath));

        const QString organisation = m_desktopEntry->value(QStringLiteral("X-Sailjail"), QStringLiteral("OrganizationName"));
        const QString application = m_desktopEntry->value(QStringLiteral("X-Sailjail"), QStringLiteral("ApplicationName"));

        if (!organisation.isEmpty() && !application.isEmpty()) {
            m_serviceName = organisation + QLatin1Char('.') + application;
        } else {
            const int slash = filePath.lastIndexOf(QLatin1Char('/')) + 1;
            const int period = filePath.indexOf(QLatin1Char('.'), slash);

            m_serviceName = period > 0 && period < filePath.count() - 8 // strlen(".desktop")
                    ? filePath.mid(slash, filePath.count() - slash - 8)
                    : QString();
        }
    } else {
        m_serviceName = QString();
        m_desktopEntry.clear();
    }

    emit this->itemChanged();
}

QString LauncherItem::filePath() const
{
    return !m_desktopEntry.isNull() ? m_desktopEntry->fileName() : QString();
}

QString LauncherItem::fileID() const
{
    if (m_desktopEntry.isNull()) {
        return QString();
    }

    // Retrieve the file ID according to
    // http://standards.freedesktop.org/desktop-entry-spec/latest/ape.html
    QRegularExpression re(".*applications/(.*.desktop)");
    QRegularExpressionMatch match = re.match(m_desktopEntry->fileName());
    if (!match.hasMatch()) {
        return filename();
    }

    QString id = match.captured(1);
    id.replace('/', '-');
    return id;
}

QString LauncherItem::filename() const
{
    QString filename = filePath();
    int sep = filename.lastIndexOf('/');
    if (sep == -1)
        return QString();

    return filename.mid(sep+1);
}

QString LauncherItem::exec() const
{
    return !m_desktopEntry.isNull() ? m_desktopEntry->exec() : QString();
}

bool LauncherItem::dBusActivated() const
{
    return !m_desktopEntry.isNull() && (!m_desktopEntry->xMaemoService().isEmpty()
            || m_desktopEntry->value(QStringLiteral("Desktop Entry/DBusActivatable")) == QLatin1String("true"));
}

MRemoteAction LauncherItem::remoteAction(const QStringList &arguments) const
{
    if (m_desktopEntry) {
        const QString service = m_desktopEntry->xMaemoService();
        const QString path = m_desktopEntry->value(QStringLiteral("Desktop Entry/X-Maemo-Object-Path"));
        const QString method = m_desktopEntry->value(QStringLiteral("Desktop Entry/X-Maemo-Method"));

        const int period = method.lastIndexOf(QLatin1Char('.'));

        if (!service.isEmpty() && !method.isEmpty() && period != -1) {
            return MRemoteAction(
                        service,
                        path.isEmpty() ? QStringLiteral("/") : path,
                        method.left(period),
                        method.mid(period + 1),
                        { QVariant::fromValue(arguments) });
        } else if (!m_serviceName.isEmpty()
                && m_desktopEntry->value(QStringLiteral("Desktop Entry/DBusActivatable")) == QLatin1String("true")) {
            const QString path = QLatin1Char('/') + QString(m_serviceName).replace(QLatin1Char('.'), QLatin1Char('/'));
            const QString interface = QStringLiteral("org.freedesktop.Application");

            if (arguments.isEmpty()) {
                return MRemoteAction(m_serviceName, path, interface, QStringLiteral("Activate"));
            } else {
                return MRemoteAction(
                            m_serviceName,
                            path,
                            interface,
                            QStringLiteral("Open"),
                            { QVariant::fromValue(arguments) });
            }
        }
    }

    return MRemoteAction();
}

QString LauncherItem::title() const
{
    if (m_isTemporary) {
        return m_customTitle;
    }

    return !m_desktopEntry.isNull() ? m_desktopEntry->name() : QString();
}

QString LauncherItem::entryType() const
{
    return !m_desktopEntry.isNull() ? m_desktopEntry->type() : QString();
}

QString LauncherItem::iconId() const
{
    if (!m_customIconFilename.isEmpty()) {
        return QString("%1#serial=%2").arg(m_customIconFilename).arg(m_serial);
    }

    return getOriginalIconId();
}

QStringList LauncherItem::desktopCategories() const
{
    return !m_desktopEntry.isNull() ? m_desktopEntry->categories() : QStringList();
}

QStringList LauncherItem::mimeType() const
{
    return !m_desktopEntry.isNull() ? m_desktopEntry->mimeType() : QStringList();
}

QString LauncherItem::titleUnlocalized() const
{
    if (m_isTemporary) {
        return m_customTitle;
    }

    return !m_desktopEntry.isNull() ? m_desktopEntry->nameUnlocalized() : QString();
}

bool LauncherItem::shouldDisplay() const
{
    return !m_desktopEntry.isNull() ? !m_desktopEntry->noDisplay() : m_isTemporary;
}

bool LauncherItem::isSandboxed() const
{
    return !m_desktopEntry.isNull() ? m_desktopEntry->isSandboxed() : false;
}

bool LauncherItem::isValid() const
{
    return !m_desktopEntry.isNull() ? m_desktopEntry->isValid() : m_isTemporary;
}

bool LauncherItem::isLaunching() const
{
    return m_isLaunching;
}

void LauncherItem::setIsLaunching(bool isLaunching)
{
    if (isLaunching) {
        // This is a failsafe to allow launching again after 5 seconds in case the application crashes on startup and no window is ever created
        m_launchingTimeout.start(5000, this);
    } else {
        m_launchingTimeout.stop();
    }
    if (m_isLaunching != isLaunching) {
        m_isLaunching = isLaunching;
        emit this->isLaunchingChanged();
    }
}

void LauncherItem::setIsUpdating(bool isUpdating)
{
    if (m_isUpdating != isUpdating) {
        m_isUpdating = isUpdating;
        emit isUpdatingChanged();
    }
}

void LauncherItem::setIsTemporary(bool isTemporary)
{
    if (m_isTemporary != isTemporary) {
        m_isTemporary = isTemporary;
        emit isTemporaryChanged();
    }
}


QString LauncherItem::dBusServiceName() const
{
    return m_serviceName;
}

void LauncherItem::launchApplication()
{
    launchWithArguments(QStringList());
}

#if !defined(HAVE_CONTENTACTION)
static void setupProcessIds(gpointer)
{
    const gid_t gid = getgid();
    const uid_t uid = getuid();

    if (setregid(gid, gid) < 0 || setreuid(uid, uid) < 0) {
        ::_exit(EXIT_FAILURE);
    }
}
#endif

void LauncherItem::launchWithArguments(const QStringList &arguments)
{
    if (m_isUpdating) {
        LauncherModel *model = static_cast<LauncherModel *>(parent());
        model->requestLaunch(m_packageName);
        return;
    }

    if (m_desktopEntry.isNull())
        return;

    if (m_desktopEntry->type() == QLatin1String("Link")) {
        QString url = m_desktopEntry->url();

        if (!url.isEmpty()) {
            QDesktopServices::openUrl(QUrl(QStringLiteral("file:///")).resolved(url));
        }
        return;
    }

#if defined(HAVE_CONTENTACTION)
    LAUNCHER_DEBUG("launching content action for" << m_desktopEntry->name());
    ContentAction::Action action = ContentAction::Action::launcherAction(m_desktopEntry, arguments);
    action.trigger();
#else
    LAUNCHER_DEBUG("launching exec line for" << m_desktopEntry->name());

    if (GDesktopAppInfo *appInfo = g_desktop_app_info_new_from_filename(
                m_desktopEntry->fileName().toUtf8().constData())) {
        GError *error = 0;
        GList *uris = NULL;

        for (const QString &argument : arguments) {
            uris = g_list_append(uris, g_strdup(argument.toUtf8().constData()));
        }

        g_desktop_app_info_launch_uris_as_manager(
                    appInfo,
                    uris,
                    NULL,
                    G_SPAWN_SEARCH_PATH,
                    setupProcessIds,
                    NULL,
                    NULL,
                    NULL,
                    &error);
        if (error != NULL) {
            qWarning() << "Failed to execute" << filename() << error->message;
            g_error_free(error);
        }
        g_list_foreach(uris, (GFunc)g_free, NULL);
        g_list_free(uris);

        g_object_unref(appInfo);
    }
#endif

    setIsLaunching(true);
}

bool LauncherItem::isStillValid()
{
    if (m_isTemporary) {
        return true;
    }

    // Force a reload of _desktopEntry
    setFilePath(filePath());
    return isValid();
}

QString LauncherItem::getOriginalIconId() const
{
    return !m_desktopEntry.isNull() ? m_desktopEntry->icon() : QString();
}

void LauncherItem::setIconFilename(const QString &path)
{
    m_customIconFilename = path;
    if (!path.isEmpty()) {
        m_serial++;
    }
    emit itemChanged();
}

QString LauncherItem::iconFilename() const
{
    return m_customIconFilename;
}

void LauncherItem::setPackageName(QString packageName)
{
    if (m_packageName != packageName) {
        m_packageName = packageName;
        emit packageNameChanged();
    }
}

void LauncherItem::setUpdatingProgress(int updatingProgress)
{
    if (m_updatingProgress != updatingProgress) {
        m_updatingProgress = updatingProgress;
        emit updatingProgressChanged();
    }
}

bool LauncherItem::isBlacklisted() const
{
    return m_isBlacklisted;
}

void LauncherItem::setIsBlacklisted(bool isBlacklisted) {
    if (m_isBlacklisted != isBlacklisted) {
        m_isBlacklisted = isBlacklisted;
        emit isBlacklistedChanged();
    }
}

void LauncherItem::setCustomTitle(QString customTitle)
{
    if (m_customTitle != customTitle) {
        m_customTitle = customTitle;
        emit itemChanged();
    }
}

QString LauncherItem::readValue(const QString &key) const
{
    if (m_desktopEntry.isNull())
        return QString();

    return m_desktopEntry->value("Desktop Entry", key);
}

bool LauncherItem::canOpenMimeType(const QString &mimeType)
{
    if (!m_mimeTypesPopulated && m_desktopEntry) {
        m_mimeTypesPopulated = true;

        for (const QString &mimeType : m_desktopEntry->mimeType()) {
            m_mimeTypes.append(QRegExp(mimeType, Qt::CaseInsensitive, QRegExp::Wildcard));
        }
    }

    for (const QRegExp &candidate : m_mimeTypes) {
        if (candidate.exactMatch(mimeType)) {
            return true;
        }
    }

    return false;
}


void LauncherItem::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_launchingTimeout.timerId()) {
        setIsLaunching(false);
    } else {
        QObject::timerEvent(event);
    }
}
