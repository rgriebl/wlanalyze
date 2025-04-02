// Copyright (C) 2025 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QMainWindow>

QT_FORWARD_DECLARE_CLASS(QTableView)

namespace Ui {
class Filter;
}

namespace WaylandDebug {
class Model;
class Filter;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openFile(const QString &fileName);

private:
    void connectFilter();
    void reFilter();
    void clearFilter();
    void setFilter(WaylandDebug::Filter *filter);

    QTableView *m_table;
    std::unique_ptr<WaylandDebug::Model> m_model;
    std::unique_ptr<Ui::Filter> m_filter;
    bool m_resettingFilter = false;
};
