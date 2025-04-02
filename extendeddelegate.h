// Copyright (C) 2025 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QStyledItemDelegate>

class ExtendedDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    ExtendedDelegate(int tintRole, int tintWidthRole, QObject *parent);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
private:
    int m_tintRole = 0;
    int m_tintWidthRole = 0;
};
