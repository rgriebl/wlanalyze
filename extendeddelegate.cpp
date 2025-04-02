// Copyright (C) 2025 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include <QApplication>
#include <QPainter>

#include "extendeddelegate.h"


ExtendedDelegate::ExtendedDelegate(int tintRole, int tintWidthRole, QObject *parent = nullptr)
    : QStyledItemDelegate(parent)
    , m_tintRole(tintRole)
    , m_tintWidthRole(tintWidthRole)
{ }

void ExtendedDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_ASSERT(index.isValid());

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    auto mixColor = [](QColor c1, QColor c2, float f) {
        QColor c;
        c.setRedF(c1.redF() * (1.0 - f) + c2.redF() * f);
        c.setGreenF(c1.greenF() * (1.0 - f) + c2.greenF() * f);
        c.setBlueF(c1.blueF() * (1.0 - f) + c2.blueF() * f);
        return c;
    };

    if (m_tintRole) {
        QColor bgTint = index.data(m_tintRole).value<QColor>();
        if (bgTint.isValid()) {
            QColor base = opt.palette.color(opt.features & QStyleOptionViewItem::Alternate
                                                ? QPalette::AlternateBase : QPalette::Base);
            QColor mix = mixColor(base, bgTint, 0.1f);

            QVariant v = index.data(m_tintWidthRole);
            if (v.isValid() && v.typeId() == qMetaTypeId<double>()) {
                auto w = v.toDouble();

                QPixmap pix(opt.rect.size());
                pix.fill(base);
                QPainter pixPainter(&pix);
                pixPainter.fillRect(0, 0, w * pix.width(), pix.height(), mix);
                pixPainter.end();
                opt.backgroundBrush = pix;
            } else {
                opt.backgroundBrush = mix;
            }
        }
    }

    auto *widget = qobject_cast<QWidget *>(opt.styleObject);
    QStyle *style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
}
