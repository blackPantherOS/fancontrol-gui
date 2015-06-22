/*
 * Copyright (C) 2015  Malte Veerman <maldela@halloarsch.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "loader.h"

#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QDebug>

#include <KF5/KAuth/kauthexecutejob.h>

using namespace KAuth;

#define HWMON_PATH "/sys/class/hwmon"

Loader::Loader(QObject *parent) : QObject(parent)
{
    m_configUrl = QUrl::fromLocalFile("/etc/fancontrol");
    m_interval = 10;
    m_error = "Success";
    parseHwmons();
    open(m_configUrl);
    m_timer.setSingleShot(false);
    m_timer.start(1);
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(updateSensors()));
}

void Loader::parseHwmons()
{
    foreach (Hwmon *hwmon, m_hwmons)
    {
        hwmon->deleteLater();
    }
    m_hwmons.clear();

    QDir hwmonDir(HWMON_PATH);
    QStringList list;
    if (hwmonDir.isReadable())
    {
        list = hwmonDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
    }
    else
    {
        qDebug() << HWMON_PATH << " is not readable!";
        return;
    }

    foreach (QString hwmonPath, list)
    {
        Hwmon *hwmon = new Hwmon(QFile::symLinkTarget(hwmonDir.absoluteFilePath(hwmonPath)), this);
        connect(hwmon, SIGNAL(configUpdateNeeded()), this, SLOT(createConfigFile()));
        connect(this, SIGNAL(sensorsUpdateNeeded()), hwmon, SLOT(updateSensors()));
        m_hwmons << hwmon;
    }
    emit hwmonsChanged();
}

void Loader::open(const QUrl &url)
{
    QString fileName = url.toLocalFile();
    QFile file(fileName);
    QTextStream stream;
    QString string;
    QStringList lines;

    if (file.open(QFile::ReadOnly | QFile::Text))
    {
        stream.setDevice(&file);
        m_error = "Success";
        emit errorChanged();
    }
    else if (file.exists())
    {
        Action action("fancontrol.gui.helper.action");
        action.setHelperId("fancontrol.gui.helper");
        QVariantMap map;
        map["action"] = "read";
        map["filename"] = fileName;
        action.setArguments(map);
        ExecuteJob *reply = action.execute();
        if (!reply->exec())
        {
            m_error = reply->errorString();
            emit errorChanged();
            return;
        }
        else
        {
            m_error = "Success";
            emit errorChanged();
            string = reply->data()["content"].toString();
            stream.setString(&string);
        }
    }
    m_configFile = stream.readAll();
    emit configFileChanged();
    m_configUrl = url;
    emit configUrlChanged();

    stream.seek(0);

    foreach (Hwmon *hwmon, m_hwmons)
    {
        foreach (QObject *pwmFan, hwmon->pwmFans())
        {
            qobject_cast<PwmFan *>(pwmFan)->reset();
        }
    }
    
    do
    {
        QString line(stream.readLine());
        if (line.startsWith('#')) continue;
        int offset = line.indexOf('#');
        if (offset != -1) line.truncate(offset-1);
        line = line.simplified();
        lines << line;
    }
    while(!stream.atEnd());

    foreach (QString line, lines)
    {
        if (line.startsWith("INTERVAL="))
        {
            setInterval(line.remove("INTERVAL=").toInt());
        }
        else if (line.startsWith("FCTEMPS="))
        {
            QStringList fctemps = line.split(' ');
            if (!fctemps.isEmpty())
                fctemps.first().remove("FCTEMPS=");
            foreach (QString fctemp, fctemps)
            {
                QString pwm = fctemp.split('=').at(0);
                QString temp = fctemp.split('=').at(1);
                int pwmSensorIndex = getSensorNumber(pwm);
                int tempSensorIndex = getSensorNumber(temp);
                Hwmon *pwmHwmon = m_hwmons.value(getHwmonNumber(pwm), nullptr);

                if (pwmHwmon)
                {
                    Hwmon *tempHwmon = m_hwmons.value(getHwmonNumber(temp), nullptr);
                    PwmFan *pwmPointer = pwmHwmon->pwmFan(pwmSensorIndex);
                    if (tempHwmon)
                    {
                        Temp *tempPointer = tempHwmon->temp(tempSensorIndex);

                        if (pwmPointer)
                        {
                            pwmPointer->setTemp(tempPointer);
                            pwmPointer->setMinPwm(0);
                        }
                    }
                    else if (pwmPointer)
                        pwmPointer->setTemp(nullptr);
                }
            }
        }
        else if (line.startsWith("MINTEMP="))
        {
            QStringList mintemps = line.split(' ');
            if (!mintemps.isEmpty())
                mintemps.first().remove("MINTEMP=");
            foreach (QString mintemp, mintemps)
            {
                QString pwm = mintemp.split('=').at(0);
                int value = mintemp.split('=').at(1).toInt();
                int pwmHwmon = getHwmonNumber(pwm);
                int pwmSensor = getSensorNumber(pwm);
                PwmFan *pwmPointer = m_hwmons.value(pwmHwmon, nullptr)->pwmFan(pwmSensor);
                if (pwmPointer)
                    pwmPointer->setMinTemp(value);
            }
        }
        else if (line.startsWith("MAXTEMP="))
        {
            QStringList maxtemps = line.split(' ');
            if (!maxtemps.isEmpty())
                maxtemps.first().remove("MAXTEMP=");
            foreach (QString maxtemp, maxtemps)
            {
                QString pwm = maxtemp.split('=').at(0);
                int value = maxtemp.split('=').at(1).toInt();
                int pwmHwmon = getHwmonNumber(pwm);
                int pwmSensor = getSensorNumber(pwm);
                PwmFan *pwmPointer = m_hwmons.value(pwmHwmon, nullptr)->pwmFan(pwmSensor);
                if (pwmPointer)
                    pwmPointer->setMaxTemp(value);
            }
        }
        else if (line.startsWith("MINSTART="))
        {
            QStringList minstarts = line.split(' ');
            if (!minstarts.isEmpty())
                minstarts.first().remove("MINSTART=");
            foreach (QString minstart, minstarts)
            {
                QString pwm = minstart.split('=').at(0);
                int value = minstart.split('=').at(1).toInt();
                int pwmHwmon = getHwmonNumber(pwm);
                int pwmSensor = getSensorNumber(pwm);
                PwmFan *pwmPointer = m_hwmons.value(pwmHwmon, nullptr)->pwmFan(pwmSensor);
                if (pwmPointer)
                    pwmPointer->setMinStart(value);
            }
        }
        else if (line.startsWith("MINSTOP="))
        {
            QStringList minstops = line.split(' ');
            if (!minstops.isEmpty())
                minstops.first().remove("MINSTOP=");
            foreach (QString minstop, minstops)
            {
                QString pwm = minstop.split('=').at(0);
                int value = minstop.split('=').at(1).toInt();
                int pwmHwmon = getHwmonNumber(pwm);
                int pwmSensor = getSensorNumber(pwm);
                PwmFan *pwmPointer = m_hwmons.value(pwmHwmon, nullptr)->pwmFan(pwmSensor);
                if (pwmPointer)
                    pwmPointer->setMinStop(value);
            }
        }
        else if (line.startsWith("MINPWM="))
        {
            QStringList minpwms = line.split(' ');
            if (!minpwms.isEmpty())
                minpwms.first().remove("MINPWM=");
            foreach (QString minpwm, minpwms)
            {
                QString pwm = minpwm.split('=').at(0);
                int value = minpwm.split('=').at(1).toInt();
                int pwmHwmon = getHwmonNumber(pwm);
                int pwmSensor = getSensorNumber(pwm);
                PwmFan *pwmPointer = m_hwmons.value(pwmHwmon, nullptr)->pwmFan(pwmSensor);
                if (pwmPointer)
                    pwmPointer->setMinPwm(value);
            }
        }
        else if (line.startsWith("MAXPWM="))
        {
            QStringList maxpwms = line.split(' ');
            if (!maxpwms.isEmpty())
                maxpwms.first().remove("MAXPWM=");
            foreach (QString maxpwm, maxpwms)
            {
                QString pwm = maxpwm.split('=').at(0);
                int value = maxpwm.split('=').at(1).toInt();
                int pwmHwmon = getHwmonNumber(pwm);
                int pwmSensor = getSensorNumber(pwm);
                PwmFan *pwmPointer = m_hwmons.value(pwmHwmon, nullptr)->pwmFan(pwmSensor);
                if (pwmPointer)
                    pwmPointer->setMaxPwm(value);
            }
        }
    }
}

void Loader::save(const QUrl &url)
{  
    QString fileName = url.isEmpty() ? m_configUrl.toLocalFile() : url.toLocalFile();
    QFile file(fileName);
    
    if (file.open(QFile::WriteOnly | QFile::Text))
    {
        QTextStream stream(&file);
        stream << m_configFile;
        qDebug() << m_configFile;
    }
    else
    {
        Action action("fancontrol.gui.helper.action");
        action.setHelperId("fancontrol.gui.helper");
        QVariantMap map;
        map["action"] = "write";
        map["content"] = m_configFile;

        map["filename"] = fileName;
        action.setArguments(map);
        ExecuteJob *reply = action.execute();
        if (!reply->exec())
        {
            m_error = reply->errorString();
            emit errorChanged();
        }
    }
}

void Loader::createConfigFile()
{
    QList<Hwmon *> usedHwmons;
    QList<PwmFan *> usedFans;
    foreach (Hwmon *hwmon, m_hwmons)
    {
        if (hwmon->pwmFans().size() > 0)
            usedHwmons << hwmon;
        foreach (QObject *fan, hwmon->pwmFans())
        {
            PwmFan *pwmFan = qobject_cast<PwmFan *>(fan);
            if (pwmFan->active() && pwmFan->hasTemp() && pwmFan->temp())
            {
                usedFans << pwmFan;
                if (!usedHwmons.contains(pwmFan->temp()->parent()))
                    usedHwmons << pwmFan->temp()->parent();
            }
        }
    }

    m_configFile = "INTERVAL=" + QString::number(m_interval) + "\n";

    m_configFile += "DEVPATH=";
    foreach (Hwmon *hwmon, usedHwmons)
    {
        QString sanitizedPath = hwmon->path();
        sanitizedPath.remove(QRegExp("^/sys/"));
        sanitizedPath.remove(QRegExp("/hwmon/hwmon\\d\\s*$"));
        m_configFile += "hwmon" + QString::number(hwmon->index()) + "=" + sanitizedPath + " ";
    }
    m_configFile += "\n";

    m_configFile += "DEVNAME=";
    foreach (Hwmon *hwmon, usedHwmons)
    {
        m_configFile += "hwmon" + QString::number(hwmon->index()) + "=" + hwmon->name().split('.').first() + " ";
    }
    m_configFile += "\n";

    m_configFile += "FCTEMPS=";
    foreach (PwmFan *pwmFan, usedFans)
    {
        m_configFile += "hwmon" + QString::number(pwmFan->parent()->index()) + "/";
        m_configFile += "pwm" + QString::number(pwmFan->index()) + "=";
        m_configFile += "hwmon" + QString::number(pwmFan->temp()->parent()->index()) + "/";
        m_configFile += "temp" + QString::number(pwmFan->temp()->index()) + "_input ";
    }
    m_configFile += "\n";

    m_configFile += "FCFANS=";
    foreach (PwmFan *pwmFan, usedFans)
    {
        m_configFile += "hwmon" + QString::number(pwmFan->parent()->index()) + "/";
        m_configFile += "pwm" + QString::number(pwmFan->index()) + "=";
        m_configFile += "hwmon" + QString::number(pwmFan->parent()->index()) + "/";
        m_configFile += "fan" + QString::number(pwmFan->index()) + "_input ";
    }
    m_configFile += "\n";

    m_configFile += "MINTEMP=";
    foreach (PwmFan *pwmFan, usedFans)
    {
        m_configFile += "hwmon" + QString::number(pwmFan->parent()->index()) + "/";
        m_configFile += "pwm" + QString::number(pwmFan->index()) + "=";
        m_configFile += QString::number(pwmFan->minTemp()) + " ";
    }
    m_configFile += "\n";

    m_configFile += "MAXTEMP=";
    foreach (PwmFan *pwmFan, usedFans)
    {
        m_configFile += "hwmon" + QString::number(pwmFan->parent()->index()) + "/";
        m_configFile += "pwm" + QString::number(pwmFan->index()) + "=";
        m_configFile += QString::number(pwmFan->maxTemp()) + " ";
    }
    m_configFile += "\n";

    m_configFile += "MINSTART=";
    foreach (PwmFan *pwmFan, usedFans)
    {
        m_configFile += "hwmon" + QString::number(pwmFan->parent()->index()) + "/";
        m_configFile += "pwm" + QString::number(pwmFan->index()) + "=";
        m_configFile += QString::number(pwmFan->minStart()) + " ";
    }
    m_configFile += "\n";

    m_configFile += "MINSTOP=";
    foreach (PwmFan *pwmFan, usedFans)
    {
        m_configFile += "hwmon" + QString::number(pwmFan->parent()->index()) + "/";
        m_configFile += "pwm" + QString::number(pwmFan->index()) + "=";
        m_configFile += QString::number(pwmFan->minStop()) + " ";
    }
    m_configFile += "\n";

    m_configFile += "MINPWM=";
    foreach (PwmFan *pwmFan, usedFans)
    {
        m_configFile += "hwmon" + QString::number(pwmFan->parent()->index()) + "/";
        m_configFile += "pwm" + QString::number(pwmFan->index()) + "=";
        m_configFile += QString::number(pwmFan->minPwm()) + " ";
    }
    m_configFile += "\n";

    m_configFile += "MAXPWM=";
    foreach (PwmFan *pwmFan, usedFans)
    {
        m_configFile += "hwmon" + QString::number(pwmFan->parent()->index()) + "/";
        m_configFile += "pwm" + QString::number(pwmFan->index()) + "=";
        m_configFile += QString::number(pwmFan->maxPwm()) + " ";
    }
    m_configFile += "\n";

    emit configFileChanged();
}

void Loader::testFans()
{
    for (int i=0; i<m_hwmons.size(); i++)
    {
        m_hwmons.at(i)->testFans();
    }
}

QList<QObject *> Loader::hwmons() const
{
    QList<QObject *> list;
    foreach (Hwmon *hwmon, m_hwmons)
    {
        list << qobject_cast<QObject *>(hwmon);
    }
    return list;
}
