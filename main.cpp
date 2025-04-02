// Copyright (C) 2025 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include "mainwindow.h"

#include <QTimer>
#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    QCoreApplication::setApplicationName(u"WLAnalyze"_s);
    QCoreApplication::setApplicationVersion(u"0.1"_s);

    QCommandLineParser clp;
    clp.addHelpOption();
    clp.addPositionalArgument(u"logfile"_s, u"The path to the logfile"_s);

    QApplication a(argc, argv);
    clp.process(a);
    MainWindow w;

    const QStringList logfiles = clp.positionalArguments();
    QTimer::singleShot(0, &w, [&w, logfiles] {
        for (const auto &logfile : logfiles)
            w.openFile(logfile);
    });

    w.show();
    return a.exec();
}
