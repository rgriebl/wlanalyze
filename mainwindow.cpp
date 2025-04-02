// Copyright (C) 2025 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include <QMenuBar>
#include <QMenu>
#include <QTableView>
#include <QFileDialog>
#include <QDockWidget>
#include <QHeaderView>
#include <QClipboard>

#include "mainwindow.h"
#include "extendeddelegate.h"
#include "waylanddebug.h"
#include "ui_filter.h"

using namespace Qt::StringLiterals;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_table(new QTableView(this))
    , m_filter(new Ui::Filter)
{
    m_table->setCornerButtonEnabled(true);
    m_table->setShowGrid(true);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSortingEnabled(true);
    m_table->verticalHeader()->hide();
    m_table->setItemDelegate(new ExtendedDelegate(WaylandDebug::Model::BackgroundTintRole,
                                                  WaylandDebug::Model::BackgroundTintWidthRole,
                                                  m_table));
    connect(m_table, &QTableView::customContextMenuRequested, [this](const QPoint &pos) {
        if (!m_model)
            return;

        QMenu menu;
        menu.addAction(tr("Copy"), [this, pos]() {
            auto idx = m_table->indexAt(pos);
            if (idx.isValid())
                qApp->clipboard()->setText(idx.data().toString());
        });
        if (m_table->indexAt(pos).column() != WaylandDebug::Model::Column::TimeDelta) {
            menu.addSeparator();
            menu.addAction(tr("Set as Filter"), [this, pos]() {
                auto idx = m_table->indexAt(pos);
                if (idx.isValid() && m_model) {
                    const auto *m = m_model->message(idx);
                    auto f = std::make_unique<WaylandDebug::Filter>();
                    switch (idx.column()) {
                    case WaylandDebug::Model::Column::Time:
                        f->m_timeMin = f->m_timeMax = m->m_time;
                        break;
                    case WaylandDebug::Model::Column::Direction:
                        f->m_directionMatch = m->m_direction;
                        break;
                    case WaylandDebug::Model::Column::Object:
                        f->m_classMatch = { m->m_object.m_class };
                        f->m_instanceMatch = { m->m_object.m_instance };
                        break;
                    case WaylandDebug::Model::Column::Method:
                        f->m_methodMatch = { m->m_method };
                        break;
                    case WaylandDebug::Model::Column::Arguments:
                        f->m_argumentMatch = m->m_arguments;
                        break;
                    }
                    if (f->isEmpty())
                        f.reset();
                    setFilter(f.release());
                }
            });
        }
        menu.exec(m_table->viewport()->mapToGlobal(pos));
    });
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);

    setCentralWidget(m_table);

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Open..."), QKeySequence::Open, this, [this]() {
        QString file = QFileDialog::getOpenFileName(this, tr("Open Log File"));
        if (!file.isEmpty())
            openFile(file);
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit, this, [this]() { close(); });

    QDockWidget *dock = new QDockWidget(tr("Filter"), this);
    dock->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetMovable);
    QWidget *inner = new QWidget();
    m_filter->setupUi(inner);
    dock->setWidget(inner);
    addDockWidget(Qt::BottomDockWidgetArea, dock, Qt::Horizontal);

    connectFilter();
}


MainWindow::~MainWindow()
{ }

void MainWindow::openFile(const QString &fileName)
{
    WaylandDebug::Parser parser(fileName);
    auto *model = parser.parse();
    if (model) {
        m_table->setModel(model);
        m_model.reset(model);
        setWindowFilePath(fileName);
        m_table->resizeColumnsToContents();

        if (auto *hh = m_table->horizontalHeader()) {
            hh->setSectionResizeMode(WaylandDebug::Model::Time, QHeaderView::ResizeToContents);
            hh->setSectionResizeMode(WaylandDebug::Model::Direction, QHeaderView::ResizeToContents);
            hh->setSectionResizeMode(WaylandDebug::Model::Object, QHeaderView::Interactive);
            hh->setSectionResizeMode(WaylandDebug::Model::Method, QHeaderView::Interactive);
            hh->setSectionResizeMode(WaylandDebug::Model::Arguments, QHeaderView::Interactive);
            hh->setSectionResizeMode(WaylandDebug::Model::TimeDelta, QHeaderView::Stretch);
        }
    }
}

void MainWindow::connectFilter()
{
    connect(m_filter->direction, &QComboBox::currentIndexChanged, this, &MainWindow::reFilter);
    connect(m_filter->timeMin, &QSpinBox::valueChanged, this, &MainWindow::reFilter);
    connect(m_filter->timeMax, &QSpinBox::valueChanged, this, &MainWindow::reFilter);
    connect(m_filter->classes, &QLineEdit::textEdited, this, &MainWindow::reFilter);
    connect(m_filter->instances, &QLineEdit::textEdited, this, &MainWindow::reFilter);
    connect(m_filter->methods, &QLineEdit::textEdited, this, &MainWindow::reFilter);
    connect(m_filter->arguments, &QLineEdit::textEdited, this, &MainWindow::reFilter);
    connect(m_filter->lifetime, &QLineEdit::textEdited, this, &MainWindow::reFilter);
    connect(m_filter->clear, &QToolButton::clicked, this, &MainWindow::clearFilter);
}

void MainWindow::clearFilter()
{
    setFilter(nullptr);
}

void MainWindow::setFilter(WaylandDebug::Filter *filter)
{
    m_resettingFilter = true;
    m_filter->direction->setCurrentIndex(filter ? int(filter->m_directionMatch) : 0);
    m_filter->timeMin->setValue(filter ? filter->m_timeMin : 0);
    m_filter->timeMax->setValue(filter ? filter->m_timeMax : 0);
    m_filter->classes->setText(filter ? filter->m_classMatch.join(u' ') : QString { });
    QStringList sl;
    if (filter) {
        for (const auto &i : std::as_const(filter->m_instanceMatch))
            sl << QString::number(i);
    }
    m_filter->instances->setText(filter ? sl.join(u' ') : QString { });
    m_filter->methods->setText(filter ? filter->m_methodMatch.join(u' ') : QString { });
    m_filter->arguments->setText(filter ? filter->m_argumentMatch.join(u' ') : QString { });
    m_filter->lifetime->setText(filter ? (filter->m_createClassMatch + filter->m_destroyClassMatch).join(u' ') : QString { });
    m_resettingFilter = false;
    m_model->setFilter(filter);
    reFilter();
}

void MainWindow::reFilter()
{
    if (!m_model)
        return;
    if (m_resettingFilter)
        return;

    auto f = std::make_unique<WaylandDebug::Filter>();

    int directionIndex = m_filter->direction->currentIndex();
    if ((directionIndex >= 0) && (directionIndex < 3))
        f->m_directionMatch = WaylandDebug::Direction(directionIndex);
    f->m_classMatch = m_filter->classes->text().simplified().split(u" "_s, Qt::SkipEmptyParts);
    const auto iids = m_filter->instances->text().simplified().split(u" "_s, Qt::SkipEmptyParts);
    for (const auto &iid : iids)
        f->m_instanceMatch.append(iid.toUInt());
    f->m_methodMatch = m_filter->methods->text().simplified().split(u" "_s, Qt::SkipEmptyParts);
    f->m_argumentMatch = m_filter->arguments->text().simplified().split(u" "_s, Qt::SkipEmptyParts);
    f->m_timeMin = m_filter->timeMin->value();
    f->m_timeMax = m_filter->timeMax->value();
    f->m_createClassMatch = f->m_destroyClassMatch = m_filter->lifetime->text().simplified().split(u" "_s, Qt::SkipEmptyParts);


    if (f->isEmpty())
        f.reset();
    m_model->setFilter(f.release());
}
