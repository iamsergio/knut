#pragma once

#include "parser.h"
#include "query.h"

#include <QString>

namespace treesitter {

class Transformation
{
public:
    struct Error
    {
        QString description;
    };

    Transformation(const QString &source, Parser &&parser, Query &&query, QString transformationTarget);

    // Throws a Transformation::Error on failure
    QString run();

    int replacementsMade() const { return m_replacements; }

private:
    QString m_source;
    Parser m_parser;
    Query m_query;
    QString m_to;

    // After reaching max_replacements, stop the transformation.
    // This likely means the transformation is recursive and will never finish.
    int m_max_replacements = 100;
    int m_replacements = 0;
};

}
