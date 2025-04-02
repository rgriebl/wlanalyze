// Copyright (C) 2025 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include <algorithm>
#include <format>

#include <QIODevice>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QColor>
#include <QtConcurrent/QtConcurrentFilter>

#include "waylanddebug.h"

using namespace Qt::StringLiterals;

namespace WaylandDebug {

ObjectRef::ObjectRef(const QString &class_, uint instance, uint generation)
    : m_class(class_)
    , m_instance(instance)
    , m_generation(generation)
{ }


Model::~Model()
{
    qDeleteAll(m_messages);
}

void Model::sort(int column, Qt::SortOrder order)
{
    emit layoutAboutToBeChanged({ }, VerticalSortHint);
    const QModelIndexList before = persistentIndexList();

    m_sorted = m_messages;
    if ((column >= 0) && (column < Count)) {
        std::sort(m_sorted.begin(), m_sorted.end(), [this, column, order](const Message *m1, const Message *m2) {
            if (order == Qt::DescendingOrder)
                std::swap(m1, m2);

            switch (column) {
            case Time: return (m1->m_time < m2->m_time); break;
            case Direction: return (m1->m_direction < m2->m_direction); break;
            case Object: return (m1->m_object <=> m2->m_object) < 0; break;
            case Method: return m1->m_method.compare(m2->m_method) < 0; break;
            case Arguments: return (m1->m_arguments < m2->m_arguments); break;
            case TimeDelta: return m_filteredTimeDeltas.at(m_filteredIndex.value(m1))
                     < m_filteredTimeDeltas.at(m_filteredIndex.value(m2)); break;
            default:   Q_ASSERT(false); break;
            }
        });
    }
    
    // we were filtered before, but we don't want to re-filter: the solution is to
    // keep the old filtered lots, but use the order from m_sorted
    if (m_filter) {
        m_filtered = QtConcurrent::blockingFiltered(m_sorted, [this](const Message *m) {
            return m_filteredIndex.contains(m);
        });
    } else {
        m_filtered = m_sorted;
    }

    rebuildFilteredIndex();
    recalculateTimeDelta();
    
    QModelIndexList after;
    after.reserve(before.size());
    for (const QModelIndex &idx : before)
        after.append(index(message(idx), idx.column()));
    changePersistentIndexList(before, after);
    emit layoutChanged({ }, VerticalSortHint);
}

int Model::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_filtered.size();
}

int Model::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(Count);
}

QVariant Model::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    auto timeDeltaPercent = [this](qint64 td) {
        qint64 diff = std::abs(td) - m_medianTimeDelta;
        if (diff < 0) {
            // scale log between smallest and median
            return 0.5 * std::log(double(-diff + 1))
                   / std::log(double(m_medianTimeDelta - m_smallestTimeDelta + 1));
        } else if (diff > 0) {
            // scale log between median and biggest
            return 0.5 + 0.5 * std::log(double(diff + 1))
                             / std::log(double(m_biggestTimeDelta - m_medianTimeDelta + 1));
        } else {
            return 0.5;
        }
    };

    auto formatTime = [](qint64 t) {
        return u"%1.%2'%3"_s
            .arg(t / 1000 / 1000)
            .arg(t / 1000 % 1000, 3, 10, QChar('0'))
            .arg(t % 1000, 3, 10, QChar('0'));
    };

    const Message *m = m_filtered.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case Time:       return formatTime(m->m_time);
        case Direction: {
            switch (m->m_direction) {
            case Direction::Any:            return tr("Any");
            case Direction::ToCompositor:   return tr("To Compositor");
            case Direction::FromCompositor: return tr("From Compositor");
            default: return QVariant();
            }
        }
        case Object:     return u"%1@%2 [%3]"_s.arg(m->m_object.m_class).arg(m->m_object.m_instance).arg(m->m_object.m_generation);
        case Method:     return m->m_method;
        case Arguments:  return m->m_arguments.join(u", ");
        case TimeDelta:  return formatTime(m_filteredTimeDeltas.at(index.row()));
        default:         return QVariant();
        }
    } else if (role == BackgroundTintRole) {
        switch (index.column()) {
        case Direction:
            if (m->m_direction == Direction::ToCompositor)
                return QColor(0, 255, 0, 128);
            else if (m->m_direction == Direction::FromCompositor)
                return QColor(0, 0, 255, 128);
            break;
        case TimeDelta: {
            auto tdp = timeDeltaPercent(m_filteredTimeDeltas.at(index.row()));
            // 0: green -> 0.5: yellow -> 1: red
            return QColor::fromHsvF(0.33f - 0.33f * tdp, 1.f, 1.f, 0.5f);
        }
        }
    } else if (role == BackgroundTintWidthRole) {
        switch (index.column()) {
        case TimeDelta: {
            return timeDeltaPercent(m_filteredTimeDeltas.at(index.row()));
            break;
        }
        }
    }
    return { };
}

QVariant Model::headerData(int section, Qt::Orientation orientation, int role) const
{
    if ((orientation != Qt::Horizontal) || (role != Qt::DisplayRole))
        return QVariant();
    switch (Column(section)) {
    case Time:       return tr("Time");
    case Direction:  return tr("Direction");
    case Object:     return tr("Object");
    case Method:     return tr("Method");
    case Arguments:  return tr("Arguments");
    case TimeDelta:  return tr("Time Δ");
    default: return QVariant();
    }
}

const Message *Model::message(const QModelIndex &index) const
{
    return index.isValid() ? static_cast<const Message *>(index.internalPointer()) : nullptr;
}

QModelIndex Model::index(int row, int column, const QModelIndex &parent) const
{
    if (!parent.isValid() && (row >= 0) && (column >= 0) && (row < rowCount({ })) && (column < columnCount({ })))
        return createIndex(row, column, m_filtered.at(row));
    return { };
}

QModelIndex Model::index(const Message *message, int column) const
{
    int row = m_filteredIndex.value(message, -1);
    if (row >= 0)
        return createIndex(row, column, message);
    return { };
}

void Model::init()
{
    m_sorted = m_filtered = m_messages;
    rebuildFilteredIndex();
    recalculateTimeDelta();
}


void Model::setFilter(Filter *filter)
{
    if (m_filter.get() == filter)
        return;

    beginResetModel();
    m_filter.reset(filter);
    if (!filter) {
        m_filtered = m_sorted;
    } else {
        m_filtered = QtConcurrent::blockingFiltered(m_sorted, [filter](const Message *m) {
            return filter->match(m);
        });
    }
    recalculateTimeDelta();
    rebuildFilteredIndex();
    endResetModel();
}

void Model::recalculateTimeDelta()
{
    m_filteredTimeDeltas.resize(m_filtered.size());
    m_smallestTimeDelta = 0;
    m_biggestTimeDelta = 0;
    m_medianTimeDelta = 0;

    if (m_filtered.isEmpty())
        return;

    m_smallestTimeDelta = std::numeric_limits<qint64>::max();
    qint64 last = m_filtered.constFirst()->m_time;
    for (qsizetype i = 0; i < m_filtered.size(); ++i) {
        qint64 now = qint64(m_filtered.at(i)->m_time);
        auto delta = now - last;
        m_filteredTimeDeltas[i] = delta;
        quint64 absDelta = std::abs(delta);
        if (absDelta < m_smallestTimeDelta)
            m_smallestTimeDelta = absDelta;
        if (absDelta > m_biggestTimeDelta)
            m_biggestTimeDelta = absDelta;
        last = now;
    }

    auto tdcopy = m_filteredTimeDeltas;
    std::sort(tdcopy.begin(), tdcopy.end(), [](qint64 a, qint64 b) { return std::abs(a) < std::abs(b); });
    m_medianTimeDelta = std::abs(tdcopy.at(tdcopy.size() / 2));
}

void Model::rebuildFilteredIndex()
{
    m_filteredIndex.clear();
    for (auto i = 0; i < m_filtered.size(); ++i)
        m_filteredIndex[m_filtered.at(i)] = i;
}

qsizetype ObjectRegistry::findInstance(uint instance)
{
    for (qsizetype i = 0; i < m_objects.size(); ++i) {
        if (m_objects.at(i).m_instance == instance)
            return i;
    }
    return -1;
};

ObjectRef ObjectRegistry::resolve(const QString &class_, uint instance)
{
    auto idx = findInstance(instance);
    ObjectRef o;
    if (idx < 0) {
        bool found = false;
        if (!class_.isEmpty()) {
            for (qsizetype i = m_graveyard.size() - 1; i >= 0; --i) {
                if ((m_graveyard.at(i).m_instance == instance) && (m_graveyard.at(i).m_class == class_)) {
                    o = m_graveyard.at(i);
                    qWarning().nospace() << "Found object " << o.m_class << "@" << o.m_instance << " in the graveyard";
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            throw std::logic_error(std::format("resolve failed to find an instance of {}@{}",
                                               qPrintable(class_), instance));
        }
    } else {
        o = m_objects.at(idx);
    }
    if (!class_.isEmpty() && (o.m_class != class_))
        throw std::logic_error(std::format("resolve found an instance ({}), but it is the wrong class ({})",
                                           qPrintable(o.m_class), qPrintable(class_)));
    return o;
}

ObjectRef ObjectRegistry::create(const QString &class_, uint instance)
{
    auto idx = findInstance(instance);
    if (idx >= 0) {
        throw std::logic_error(std::format("trying to create an already existing object: {}@{}",
                                           qPrintable(class_), instance));
    }
    uint generation = 1; 
    auto genKey = std::make_pair(class_, instance);
    auto genIt = m_generations.find(genKey);
    if (genIt != m_generations.end())
        generation = ++genIt.value();
    else
        m_generations.insert(genKey, generation);
       
    ObjectRef o(class_, instance, generation);
    m_objects << o;
    return o;
}

ObjectRef ObjectRegistry::destroy(uint instance)
{
    auto idx = findInstance(instance);
    if (idx < 0)
        throw std::logic_error(std::format("destroy for unknown object @{}", instance));
    auto o = m_objects.takeAt(idx);
    m_graveyard << o;
    return o;
}

ObjectRef ObjectRegistry::destroyIfExists(uint instance)
{
    auto idx = findInstance(instance);
    if (idx >= 0)
        return m_objects.takeAt(idx);
    return { };
}

Parser::Parser(const QString &fileName)
    : m_device(new QFile(fileName))
    , m_ownsDevice(true)
{
    m_device->open(QIODevice::ReadOnly);
}

Parser::Parser(QIODevice *device)
    : m_device(device)
{
}

Parser::~Parser()
{
    if (m_ownsDevice)
        delete m_device;
};

Model *Parser::parse()
{
    m_registry = { };
    m_registry.create("wl_display", 1);

    uint lineNumber = 0;
    try {
        if (!m_device || !m_device->isReadable())
            throw std::logic_error("device is not readable");

        auto model = std::make_unique<Model>();

        QTextStream ts(m_device);
        QString line;
        while (ts.readLineInto(&line)) {
            ++lineNumber;
            if (auto *m = parseLine(line))
                model->m_messages << m;
        }

        model->init();
        return model.release();
    } catch (const std::exception &e) {
        qWarning().nospace() << "Failed to parse log at line " << lineNumber << ": " << e.what();
        return nullptr;
    }
};


Message *Parser::parseLine(const QString &line)
{
    if (!line.startsWith(u'[') || !line.endsWith(u')'))
        return nullptr;
    
    static QRegularExpression re(uR"(^\[(\d+)\.(\d+)\](  ->)? (\w+)@(\d+)\.(\w+)\((.*)\)$)"_s);
    
    auto match = re.match(line);
    if (!match.hasMatch())
        return nullptr;
    if (match.lastCapturedIndex() != 7)
        return nullptr;

    auto m = std::make_unique<Message>();
    m->m_direction = match.hasCaptured(3) ? Direction::ToCompositor : Direction::FromCompositor;
    m->m_time = match.capturedView(1).toULongLong() * 1000 + match.capturedView(2).toULongLong();
    m->m_object = m_registry.resolve(match.captured(4), match.captured(5).toUInt());
    m->m_method = match.captured(6);
    m->m_arguments = match.captured(7).split(u", "_s);
    for (const auto &arg : std::as_const(m->m_arguments)) {
        if (arg.startsWith(u"new id ")) {
            auto p = arg.indexOf(u'@');
            if (p > 0) {
                QString class_ = arg.mid(7, p - 7);
                uint instance = arg.mid(p + 1).toUInt();

                // special case: registry binds
                if ((m->m_direction == Direction::ToCompositor)
                    && (m->m_object.m_class == u"wl_registry")
                    && (m->m_method == u"bind")
                    && (m->m_arguments.size() == 4)
                    && (class_ == u"[unknown]")) {
                    class_ = m->m_arguments.at(1).mid(1).chopped(1); // remove quotes
                }

                if (instance >= 0xff000000) // server side, but there are no delete_id calls
                    m_registry.destroyIfExists(instance);

                m->m_created << m_registry.create(class_, instance);
            }
        }
    }
    if ((m->m_method == u"delete_id") && (m->m_arguments.size() == 1)) {
        uint id = m->m_arguments.constFirst().toUInt();
        if (id)
            m->m_destroyed << m_registry.destroy(id);
    }
    return m.release();
};


bool Filter::match(const Message *m) const
{
    if (!m)
        return false;
        
    if ((m_directionMatch == Direction::FromCompositor) || (m_directionMatch == Direction::ToCompositor)) {
        if (m_directionMatch != m->m_direction)
            return false;
    }
    if (m_timeMin || m_timeMax) {
        if ((m_timeMin && (m->m_time < m_timeMin)) || (m_timeMax && (m->m_time > m_timeMax)))
            return false;
    }
    if (!m_classMatch.isEmpty()) {
        if (!m_classMatch.contains(m->m_object.m_class))
            return false;
    }
    if (!m_instanceMatch.isEmpty()) {
        if (!m_instanceMatch.contains(m->m_object.m_instance))
            return false;
    }
    if (!m_methodMatch.isEmpty()) {
        if (!m_methodMatch.contains(m->m_method))
            return false;
    }
    if (!m_argumentMatch.isEmpty()) {
        bool found = false;
        for (const auto &arg : m->m_arguments) {
            if (m_argumentMatch.contains(arg)) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    if (!m_createClassMatch.isEmpty()) {
        bool found = false;
        for (const auto &o : m->m_created) {
            if (m_createClassMatch.contains(o.m_class)) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    if (!m_destroyClassMatch.isEmpty()) {
        bool found = false;
        for (const auto &o : m->m_destroyed) {
            if (m_destroyClassMatch.contains(o.m_class )) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
        
    return true;
}

bool Filter::isEmpty() const
{
    return (m_directionMatch == Direction::Any)
           && !m_timeMin
           && !m_timeMax
           && m_classMatch.isEmpty()
           && m_instanceMatch.isEmpty()
           && m_methodMatch.isEmpty()
           && m_argumentMatch.isEmpty()
           && m_createClassMatch.isEmpty()
           && m_destroyClassMatch.isEmpty();
}

} // namespace WaylandDebug
