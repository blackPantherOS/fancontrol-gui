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

#include <QtWidgets/QApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtCore/QFileInfo>
#include <QDebug>

#include <KDeclarative/KDeclarative>
#include <KI18n/KLocalizedString>
#include <KPackage/PackageLoader>
#include <KCoreAddons/KAboutData>

#include "lib/src/guibase.h"
#include "windowconfig.h"


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    KLocalizedString::setApplicationDomain("kcm_fancontrol");

    KAboutData about(QStringLiteral("fancontrol_gui"),
                     i18n("Fancontrol-GUI"),
                     QStringLiteral("0.1"),
                     i18n("Graphical user interface for fancontrol"),
                     KAboutLicense::KAboutLicense::GPL_V2,
                     QStringLiteral("Copyright (C) 2015 Malte Veerman"),
                     QString(),
                     QStringLiteral("http://github.com/maldela/fancontrol-gui"),
                     QStringLiteral("http://github.com/maldela/fancontrol-gui/issues"));
    about.addAuthor(i18n("Malte Veerman"), i18n("Main Developer"), QStringLiteral("maldela@halloarsch.de"));
    KAboutData::setApplicationData(about);

    QScopedPointer<QQmlApplicationEngine> engine(new QQmlApplicationEngine);
    QQmlContext *context = engine->rootContext();
    
    KDeclarative::KDeclarative decl;
    decl.setDeclarativeEngine(engine.data());
    decl.setupBindings();

    Fancontrol::GUIBase base;
    base.load();
    context->setContextProperty(QStringLiteral("base"), &base);
    
    WindowConfig *windowConfig = WindowConfig::instance();
    context->setContextProperty(QStringLiteral("windowConfig"), windowConfig);

    KPackage::Package package = KPackage::PackageLoader::self()->loadPackage(QStringLiteral("KPackage/GenericQML"));
    package.setDefaultPackageRoot(QStringLiteral("/usr/local/share/kpackage/kcms"));
    package.setPath(QStringLiteral("kcm_fancontrol"));

    if (!package.isValid())
        package.setDefaultPackageRoot("/usr/share/kpackage/kcms");
    
    package.addFileDefinition("appqmlroot", "ui/Application.qml", i18n("The Application's root QML file"));
    package.setRequired("appqmlroot", true);
    
    if (package.isValid())
        engine->load(QUrl::fromLocalFile(package.filePath("appqmlroot")));

    return app.exec();
}
