// Copyright (C) 2025 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <memory>

#include <QAbstractTableModel>
#include <QList>
#include <QString>

QT_FORWARD_DECLARE_CLASS(QIODevice)

namespace WaylandDebug {

enum class Direction {
    Any            = 0,
    FromCompositor = 1,
    ToCompositor   = 2,
    Unknown        = 3,
};


class ObjectRef
{
public:
    ObjectRef() = default;
    ObjectRef(const QString &class_, uint instance, uint generation = 0);

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    auto operator<=>(const ObjectRef &) const = default;
#else
    auto operator<=>(const ObjectRef &other) const {
        auto class_compare = m_class.compare(other.m_class);

        if (class_compare != 0)
            return class_compare <=> 0;

        if (m_instance == other.m_instance)
            return m_generation <=> other.m_generation;

        return m_instance <=> other.m_instance;
    }
#endif

    QString m_class;
    uint m_instance = 0;
    uint m_generation = 0;
};

class Message {
public:
    Message() = default;

    Direction m_direction = Direction::Unknown;
    QString m_connection;
    QString m_queue;
    quint64 m_time = 0;
    ObjectRef m_object;
    QString m_method;
    QStringList m_arguments;
    QList<ObjectRef> m_created;
    QList<ObjectRef> m_destroyed;
};

class ObjectRegistry
{
public:
    ObjectRef create(const QString &class_, uint instance);
    ObjectRef destroy(uint instance);
    ObjectRef destroyIfExists(uint instance);
    ObjectRef resolve(const QString &class_, uint instance);

private:
    qsizetype findInstance(uint instance);

    QList<ObjectRef> m_objects;
    QHash<std::pair<QString, uint>, uint> m_generations;
    QList<ObjectRef> m_graveyard;
};



class Filter
{
public:
    bool isEmpty() const;
    bool match(const Message *m) const;

    Direction m_directionMatch = Direction::Any;
    quint64 m_timeMin;
    quint64 m_timeMax;
    QStringList m_connectionMatch;
    QStringList m_queueMatch;
    QStringList m_classMatch;
    QList<uint> m_instanceMatch;
    QStringList m_methodMatch;
    QStringList m_argumentMatch;

    QStringList m_destroyClassMatch;
    QStringList m_createClassMatch;
};

class Model : public QAbstractTableModel {
public:
    Model() = default;
    ~Model() override;

    enum Column {
        Time,
        Connection,
        Queue,
        Direction,
        Object,
        Method,
        Arguments,
        TimeDelta,

        Count
    };

    enum ModelRole {
        BackgroundTintRole = Qt::UserRole,
        BackgroundTintWidthRole
    };

    void setFilter(Filter *filter);
    void sort(int column, Qt::SortOrder order) override;

    int rowCount(const QModelIndex &parent) const override;
    int columnCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    const Message *message(const QModelIndex &index) const;
    QModelIndex index(int row, int column, const QModelIndex &parent = { }) const override;
    QModelIndex index(const Message *message, int column) const;

private:
    void init();
    void recalculateTimeDelta();
    void rebuildFilteredIndex();

    QList<Message *> m_messages;
    QList<Message *> m_sorted;
    QList<Message *> m_filtered;
    mutable QHash<const Message *, int> m_filteredIndex;

    QList<qint64> m_filteredTimeDeltas;
    quint64 m_smallestTimeDelta = 0;
    quint64 m_medianTimeDelta = 0;
    quint64 m_biggestTimeDelta = 0;

    std::unique_ptr<Filter> m_filter;

    friend class Parser;
};

class Parser
{
public:
    Parser(const QString &fileName);
    Parser(QIODevice *device);
    ~Parser();

    Model *parse();

private:
    Message *parseLine(const QString &line);

    QIODevice *m_device = nullptr;
    bool m_ownsDevice = false;
    QHash<QString, ObjectRegistry> m_connectionRegistry;
};

} // namespace WaylandDebug
