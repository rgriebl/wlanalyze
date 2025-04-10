// Copyright (C) 2004-2025 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include <memory>
#include <QByteArray>

#include "exception.h"

using namespace Qt::StringLiterals;

Exception::Exception(const QString &message)
    : QException()
    , m_errorString(message)
{ }

Exception::Exception(const char *message)
    : Exception(QString::fromLatin1(message))
{ }

Exception::Exception(const Exception &copy)
    : m_errorString(copy.m_errorString)
{ }

Exception::Exception(Exception &&move) noexcept
    : m_errorString(std::move(move.m_errorString))
{
    std::swap(m_whatBuffer, move.m_whatBuffer);
}

const char *Exception::what() const noexcept
{
    if (!m_whatBuffer)
        m_whatBuffer = std::make_unique<QByteArray>();
    *m_whatBuffer = m_errorString.toLocal8Bit();
    return m_whatBuffer->constData();
}

